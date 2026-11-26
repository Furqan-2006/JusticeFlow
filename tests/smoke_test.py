#!/usr/bin/env python3
"""End-to-end smoke test: the JusticeFlow demo story, step by step.

Needs a running stack (see `make demo`): the daemon on its Unix socket and the
dashboard. Everything an officer would do goes through the gateway or the
dashboard; the database is touched directly only to (a) find test officers,
(b) move a warrant's dates into the past, which stands in for time passing,
and (c) check the audit trail from outside.

Admin database access comes from the standard PG* variables (PGHOST, PGUSER,
PGPASSWORD, PGDATABASE). Run:  python3 tests/smoke_test.py
"""
import http.cookiejar
import itertools
import json
import os
import re
import socket
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

import psycopg2

SOCKET = os.environ.get("JF_SOCKET", "/tmp/justiceflow.sock")
DASHBOARD = os.environ.get("JF_DASHBOARD_URL", "http://127.0.0.1:5000")
OFFICER_PASSWORD = os.environ.get("JF_SEED_PASSWORD", "justice123")
WAIT_SECONDS = int(os.environ.get("JF_SMOKE_WAIT", "90"))

_ids = itertools.count(1)
_passed = 0


def step(title):
    print(f"\n== {title}")


def check(condition, message):
    global _passed
    if not condition:
        print(f"   FAIL: {message}")
        sys.exit(1)
    _passed += 1
    print(f"   ok: {message}")


def gateway(command, **params):
    body = json.dumps({"request_id": next(_ids), "command": command,
                       "params": {k: str(v) for k, v in params.items()}}).encode()
    with socket.socket(socket.AF_UNIX) as s:
        s.settimeout(5)
        s.connect(SOCKET)
        s.sendall(struct.pack("<I", len(body)) + body)
        n = struct.unpack("<I", s.recv(4))[0]
        data = b""
        while len(data) < n:
            data += s.recv(n - len(data))
    reply = json.loads(data)
    return reply["status"], reply["message"], reply["data"]


def wait_for(predicate, what):
    deadline = time.time() + WAIT_SECONDS
    while time.time() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(1)
    check(False, f"timed out after {WAIT_SECONDS}s waiting for {what}")


def main():
    try:
        admin = psycopg2.connect("")  # PG* environment
    except psycopg2.Error as exc:
        sys.exit(f"Cannot reach the database as admin ({exc}). Set PGHOST/PGUSER/PGPASSWORD/PGDATABASE.")
    admin.autocommit = True
    cur = admin.cursor()

    step("1. The gateway is up and data is not served anonymously")
    status, _, data = gateway("PING")
    check(status == 0 and data.get("pong"), "PING answers")
    status, msg, _ = gateway("GET_CASE_LIST")
    check(status != 0, f"anonymous case list is refused ({msg})")

    step("2. An officer signs in")
    cur.execute("""SELECT o.cnic, o.officer_id, o.station_id FROM officers o
                   JOIN cases c ON c.station_id = o.station_id AND c.case_status <> 'CLOSED'
                   WHERE o.status = 'ACTIVE' AND o.current_rank = 'INSPECTOR'
                   GROUP BY o.cnic, o.officer_id, o.station_id ORDER BY o.officer_id LIMIT 1""")
    cnic, officer_id, station_id = cur.fetchone()
    status, msg, data = gateway("LOGIN", cnic=cnic, password="not-the-password")
    check(status != 0 and msg == "Invalid credentials", "a wrong password is rejected")
    status, msg, data = gateway("LOGIN", cnic=cnic, password=OFFICER_PASSWORD)
    check(status == 0 and data["rank"] == "INSPECTOR", f"Inspector {data['name']} ({data['belt_number']}) signs in")
    token, belt = data["token"], data["belt_number"]
    check(gateway("GET_ACTIVE_SESSIONS")[2]["active_sessions"] >= 1, "the session is counted")

    step("3. Register an FIR")
    cur.execute("SELECT cnic FROM persons ORDER BY cnic LIMIT 1")
    complainant = cur.fetchone()[0]
    status, msg, data = gateway("REGISTER_FIR", token=token, complainant_cnic=complainant, case_type="ROBBERY",
                                incident_address="Smoke Test Road, Saddar'; DROP TABLE cases;--",
                                description="Smoke test robbery", incident_date="2026-11-20T18:30:00+05:00",
                                lat="24.86", lon="67.01")
    check(status == 0, f"FIR registered ({msg})")
    case_id, fir = data["case_id"], data["fir_number"]
    check(re.fullmatch(r"FIR-\d{4}-[A-Z]+-[A-Z]+-\d{4}", fir) is not None, f"FIR number generated: {fir}")
    cur.execute("SELECT count(*) FROM cases")
    check(cur.fetchone()[0] > 0, "the cases table survived the injection attempt in the address")

    step("4. Attach evidence")
    status, msg, data = gateway("ADD_EVIDENCE", token=token, case_id=case_id, evidence_type="PHYSICAL",
                                description="Smoke test evidence", quantity=1)
    check(status == 0, f"evidence recorded as {data and data['evidence_number']}")
    evidence_number = data["evidence_number"]

    step("5. Issue a warrant")
    status, msg, data = gateway("ISSUE_WARRANT", token=token, case_id=case_id, warrant_type="ARREST",
                                issuing_court="Smoke Test Court", magistrate_name="Mr Smoke",
                                valid_until=time.strftime("%Y-%m-%d", time.localtime(time.time() + 86400)))
    check(status == 0 and data["status"] == "ISSUED", f"warrant {data and data['warrant_number']} issued")
    warrant_number = data["warrant_number"]

    step("6. The scheduler expires it once its date has passed")
    cur.execute("UPDATE warrants SET issue_date = CURRENT_DATE - 3, valid_until = CURRENT_DATE - 1 "
                "WHERE warrant_number = %s", (warrant_number,))

    def expired():
        _, _, d = gateway("GET_CASE_WARRANTS", token=token, case_id=case_id)
        return [w for w in d["warrants"] if w["warrant_number"] == warrant_number and w["status"] == "EXPIRED"]
    wait_for(expired, "the scheduler to expire the warrant")
    check(True, "warrant moved to EXPIRED by the daemon")

    step("7. The AI agents have refreshed the rankings")

    def agents_done():
        agents = gateway("GET_AGENT_STATUS")[2]["agents"]
        return all(a["last_updated"] > 0 for a in agents) and agents
    agents = wait_for(agents_done, "all three agents to finish a run")
    check(all(a["status_code"] == 0 for a in agents), "hotspot, priority and workload all report OK")
    check(len(gateway("GET_HOTSPOT_DATA", token=token)[2]["hotspots"]) > 0, "hotspot zones exist")
    check(len(gateway("GET_PRIORITY_CASES", token=token, min_priority="LOW", limit=5)[2]["cases"]) > 0,
          "priority rankings exist")
    check(len(gateway("GET_OFFICER_WORKLOAD", token=token, limit=5)[2]["officers"]) > 0, "workload data exists")

    step("8. The audit trail names the officer and cannot be changed")
    cur.execute("""SELECT table_name, action, changed_by_officer_id, changed_by_belt FROM audit.audit_log
                   WHERE changed_by_officer_id = %s ORDER BY changed_at DESC LIMIT 50""", (officer_id,))
    rows = cur.fetchall()
    tables = {r[0] for r in rows}
    check({"cases", "evidence", "warrants"} <= tables, f"cases, evidence and warrants changes are attributed to officer {officer_id}")
    check(all(r[3] == belt for r in rows), f"rows carry belt number {belt}")
    app_password = os.environ.get("JF_DB_PASS", "justice_app")
    try:
        app = psycopg2.connect(host=os.environ.get("PGHOST", "localhost"), dbname=os.environ.get("PGDATABASE", "justiceflow"),
                               user="justice_app", password=app_password)
    except psycopg2.Error as exc:
        check(False, f"could not connect as justice_app to test immutability ({exc})")
    app.autocommit = True
    acur = app.cursor()
    for sql in ("UPDATE audit.audit_log SET changed_by_user = 'x'", "DELETE FROM audit.audit_log"):
        try:
            acur.execute(sql)
            check(False, f"application role was able to run: {sql}")
        except psycopg2.Error:
            check(True, f"application role cannot run: {sql.split(' SET')[0]}")
    app.close()

    step("9. The dashboard shows it all")
    jar = http.cookiejar.CookieJar()
    web = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
    try:
        web.open(urllib.request.Request(f"{DASHBOARD}/login", data=urllib.parse.urlencode(
            {"cnic": cnic, "password": OFFICER_PASSWORD}).encode()))
        page = web.open(f"{DASHBOARD}/cases/{case_id}").read().decode()
    except (urllib.error.URLError, OSError) as exc:
        check(False, f"dashboard not reachable at {DASHBOARD} ({exc})")
    check(fir in page, "case page shows the FIR number")
    check(evidence_number in page, "case page lists the evidence")
    check(warrant_number in page and "Expired" in page, "case page shows the warrant as expired")

    print(f"\nSmoke test passed ({_passed} checks).")


if __name__ == "__main__":
    main()
