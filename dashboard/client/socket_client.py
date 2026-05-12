import socket
import json
import os

SOCKET_PATH = '/tmp/justiceflow.sock'

def send_ipc_request(payload: dict) -> dict:
    """Send a JSON request over the Unix domain socket to the C++ API Gateway."""
    if not os.path.exists(SOCKET_PATH):
        return {"status": "error", "message": "API Gateway socket not found."}
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.connect(SOCKET_PATH)
            data = json.dumps(payload).encode("utf-8")
            client.sendall(data)
            response = client.recv(65536)  # 64 KB
            return json.loads(response.decode("utf-8"))
    except Exception as e:
        return {"status": "error", "message": f"IPC error: {e}"}