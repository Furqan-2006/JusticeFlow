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
    response = send_ipc_request({"action": "get_dashboard_stats"})
    return jsonify(response)

if __name__ == "__main__":
    app.run(host='0.0.0.0', port=5000, debug=True)