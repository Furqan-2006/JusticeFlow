from flask import Flask, render_template, jsonify
from client.socket_client import send_ipc_request

app = Flask(__name__)

@app.route("/")
def dashboard():
    return render_template("dashboard.html")

@app.route("/os_metrics")
def os_metrics_page():
    return render_template("os_metrics.html")

@app.route("/api/os_metrics")
def api_os_metrics():
    sessions_resp = send_ipc_request({
        "request_id": 1, "command": "GET_ACTIVE_SESSIONS", "params": {}
    })
    agents_resp = send_ipc_request({
        "request_id": 2, "command": "GET_AGENT_STATUS", "params": {}
    })

    # Defensive: ensure both data keys are present and merge for frontend
    response = {}
    if isinstance(sessions_resp, dict): response.update(sessions_resp)
    if isinstance(agents_resp, dict): response["agents"] = agents_resp.get("agents", [])
    print("Gateway response:", response)
    return jsonify(response)

if __name__ == "__main__":
    app.run(host='0.0.0.0', port=5000, debug=True)