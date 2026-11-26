"""JusticeFlow dashboard.

A Flask UI. Every piece of data comes from the C++ daemon through the API
gateway (client/gateway.py); this module never touches PostgreSQL.

Officers sign in with CNIC and password. The daemon checks them and issues a
session token, which is kept in a signed, HttpOnly cookie and sent with every
gateway call, so the daemon always knows which officer is acting.
"""
import os
from datetime import datetime
from urllib.parse import urlparse

from flask import (Flask, abort, flash, g, jsonify, redirect, render_template,
                   request, session, url_for)

from client import gateway

PAGE_SIZE = 25
PRIORITIES = ["LOW", "MEDIUM", "HIGH", "CRITICAL"]


def create_app():
    app = Flask(__name__)
    # Signs the session cookie. Set JF_DASHBOARD_SECRET to keep logins across
    # restarts; without it a random key is made per start (everyone signs in again).
    app.secret_key = os.environ.get("JF_DASHBOARD_SECRET") or os.urandom(32)
    app.config.update(SESSION_COOKIE_HTTPONLY=True, SESSION_COOKIE_SAMESITE="Lax",
                      SESSION_COOKIE_NAME="jf_session")
    PUBLIC_ENDPOINTS = {"login", "healthz", "static"}
    AUTH_FAILURES = (1, 3)  # AUTH_FAILED, SESSION_EXPIRED

    # ---- helpers -----------------------------------------------------------

    def call(command, params=None):
        """Gateway call carrying the signed-in officer's token. A rejected token
        means the session ended, so remember that and send them to the login."""
        result = gateway.call(command, params, token=session.get("token"))
        if result["status"] in AUTH_FAILURES and command not in ("LOGIN", "LOGOUT"):
            g.session_lost = True
        return result

    @app.before_request
    def require_login():
        if request.endpoint in PUBLIC_ENDPOINTS or request.endpoint is None:
            return None
        if not session.get("token"):
            if request.path.startswith("/api/"):
                return jsonify({"status": "error", "message": "login required"}), 401
            return redirect(url_for("login", next=request.full_path.rstrip("?")))
        return None

    @app.after_request
    def drop_dead_session(response):
        if getattr(g, "session_lost", False):
            session.clear()
            if not request.path.startswith("/api/"):
                flash("Your session has ended. Please sign in again.")
                return redirect(url_for("login"))
        return response

    @app.context_processor
    def inject_officer():
        return {"officer": session.get("officer")}

    def fetch(command, params=None, errors=None, key=None, default=None):
        """Call the gateway; on failure record the reason and return `default`."""
        result = call(command, params)
        if not result["ok"]:
            if errors is not None:
                errors.append(f"{command}: {result['message']}")
            return default
        data = result["data"] or {}
        return data.get(key, default) if key else data

    def agent_view(agent):
        """Describe one agent slot from the shared status table."""
        never_run = str(agent.get("last_updated", "0")) in ("0", "")
        code = int(agent.get("status_code", 0) or 0)
        if never_run:
            state, label = "idle", "never run"
        elif code == 0:
            state, label = "ok", "ok"
        else:
            state, label = "error", f"error {code}"
        return {**agent, "state": state, "label": label}

    @app.template_filter("ts")
    def format_timestamp(value):
        if not value:
            return "—"
        try:
            return datetime.fromisoformat(str(value).replace("+00", "+00:00")).strftime("%Y-%m-%d %H:%M")
        except ValueError:
            return str(value)

    @app.template_filter("epoch")
    def format_epoch(value):
        try:
            seconds = int(value)
        except (TypeError, ValueError):
            return "never"
        return datetime.fromtimestamp(seconds).strftime("%Y-%m-%d %H:%M:%S") if seconds else "never"

    @app.template_filter("slug")
    def slug(value):
        return str(value or "").lower().replace("_", "-")

    @app.template_filter("pretty")
    def pretty(value):
        return str(value or "—").replace("_", " ").title()

    # ---- sign in / out ---------------------------------------------------------

    def safe_next(target):
        """Only follow redirects that stay on this site."""
        if target and not urlparse(target).netloc and target.startswith("/") and not target.startswith("//"):
            return target
        return url_for("dashboard")

    @app.route("/login", methods=["GET", "POST"])
    def login():
        error = None
        if request.method == "POST":
            result = gateway.call("LOGIN", {"cnic": request.form.get("cnic", "").strip(),
                                            "password": request.form.get("password", "")})
            if result["ok"]:
                data = result["data"]
                session.clear()
                session["token"] = data["token"]
                session["officer"] = {k: data.get(k) for k in ("name", "belt_number", "rank")}
                return redirect(safe_next(request.args.get("next")))
            error = result["message"] if result["status"] in (-1, 12) else "Invalid CNIC or password."
        return render_template("login.html", error=error), (401 if error else 200)

    @app.route("/logout", methods=["POST"])
    def logout():
        gateway.call("LOGOUT", token=session.get("token"))
        session.clear()
        return redirect(url_for("login"))

    # ---- pages ---------------------------------------------------------------

    @app.route("/")
    def dashboard():
        errors = []
        ping = fetch("PING", errors=errors, default={})
        sessions = fetch("GET_ACTIVE_SESSIONS", errors=errors, key="active_sessions", default="—")
        agents = [agent_view(a) for a in fetch("GET_AGENT_STATUS", errors=errors, key="agents", default=[])]
        recent = fetch("GET_CASE_LIST", {"limit": 5}, errors, "cases", [])
        busiest = fetch("GET_OFFICER_WORKLOAD", {"limit": 5}, errors, "officers", [])
        return render_template(
            "dashboard.html", errors=errors, ping=ping, sessions=sessions,
            agents=agents, recent=recent, busiest=busiest,
        )

    @app.route("/cases")
    def cases():
        errors = []
        try:
            page = max(1, int(request.args.get("page", 1)))
        except ValueError:
            page = 1
        # Ask for one extra row to learn whether a next page exists.
        rows = fetch("GET_CASE_LIST", {"limit": PAGE_SIZE + 1, "offset": (page - 1) * PAGE_SIZE},
                     errors, "cases", [])
        return render_template("cases.html", errors=errors, cases=rows[:PAGE_SIZE],
                               page=page, has_next=len(rows) > PAGE_SIZE)

    @app.route("/cases/<int:case_id>")
    def case_detail(case_id):
        result = call("GET_CASE_BY_ID", {"case_id": case_id})
        if not result["ok"] and result["status"] not in (-1, *AUTH_FAILURES):
            abort(404)  # gateway answered: no such case
        errors = [] if result["ok"] else [result["message"]]
        return render_template("case_detail.html", errors=errors, case=result["data"])

    @app.route("/analytics")
    def analytics():
        errors = []
        minimum = request.args.get("min_priority", "MEDIUM").upper()
        if minimum not in PRIORITIES:
            minimum = "MEDIUM"
        hotspots = fetch("GET_HOTSPOT_DATA", {"limit": 10}, errors, "hotspots", [])
        priority = fetch("GET_PRIORITY_CASES", {"min_priority": minimum, "limit": 25}, errors, "cases", [])
        return render_template("analytics.html", errors=errors, hotspots=hotspots,
                               priority=priority, minimum=minimum, priorities=PRIORITIES)

    @app.route("/workload")
    def workload():
        errors = []
        officers = fetch("GET_OFFICER_WORKLOAD", {"limit": 50}, errors, "officers", [])
        busiest = max((int(o.get("active_cases", 0) or 0) for o in officers), default=1) or 1
        return render_template("workload.html", errors=errors, officers=officers, busiest=busiest)

    @app.route("/os_metrics")
    def os_metrics_page():
        return render_template("os_metrics.html")

    @app.route("/ai_status")
    def ai_status():
        errors = []
        agents = [agent_view(a) for a in fetch("GET_AGENT_STATUS", errors=errors, key="agents", default=[])]
        return render_template("ai_status.html", errors=errors, agents=agents)

    # ---- JSON for live updates ---------------------------------------------------

    @app.route("/api/status")
    @app.route("/api/os_metrics")  # kept for the original page and any scripts
    def api_status():
        errors = []
        ping = fetch("PING", errors=errors, default={})
        sessions = fetch("GET_ACTIVE_SESSIONS", errors=errors, key="active_sessions")
        agents = [agent_view(a) for a in fetch("GET_AGENT_STATUS", errors=errors, key="agents", default=[])]
        body = {
            "status": "error" if errors else "ok",
            "message": "; ".join(errors),
            "uptime_seconds": ping.get("uptime_seconds"),
            "active_sessions": sessions,
            "agents": agents,
        }
        return jsonify(body), (503 if errors else 200)

    @app.route("/healthz")
    def healthz():
        result = gateway.call("PING")
        return jsonify({"gateway": "up" if result["ok"] else "down", "message": result["message"]}), \
            (200 if result["ok"] else 503)

    return app


app = create_app()

if __name__ == "__main__":
    # Bound to localhost unless JF_DASHBOARD_HOST says otherwise; debug is opt-in.
    app.run(
        host=os.environ.get("JF_DASHBOARD_HOST", "127.0.0.1"),
        port=int(os.environ.get("JF_DASHBOARD_PORT", "5000")),
        debug=os.environ.get("FLASK_DEBUG") == "1",
    )
