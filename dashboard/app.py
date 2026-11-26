"""JusticeFlow dashboard.

A read-only Flask UI. Every piece of data comes from the C++ daemon through
the API gateway (client/gateway.py); this module never touches PostgreSQL.
"""
import os
from datetime import datetime

from flask import Flask, abort, jsonify, render_template, request

from client import gateway

PAGE_SIZE = 25
PRIORITIES = ["LOW", "MEDIUM", "HIGH", "CRITICAL"]


def create_app():
    app = Flask(__name__)

    # ---- helpers -----------------------------------------------------------

    def fetch(command, params=None, errors=None, key=None, default=None):
        """Call the gateway; on failure record the reason and return `default`."""
        result = gateway.call(command, params)
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
        result = gateway.call("GET_CASE_BY_ID", {"case_id": case_id})
        if not result["ok"] and result["status"] != -1:
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
