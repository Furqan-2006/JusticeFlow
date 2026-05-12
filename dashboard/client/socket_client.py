import socket
import json
import os
import struct

SOCKET_PATH = "/tmp/justiceflow.sock"

def send_ipc_request(payload: dict) -> dict:
    """Send a properly framed JSON command over the Unix socket and parse the response."""
    if not os.path.exists(SOCKET_PATH):
        return {"status": "error", "message": "API Gateway socket not found."}
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.connect(SOCKET_PATH)
            # Proper request
            payload_json = json.dumps(payload).encode("utf-8")
            length_prefix = struct.pack("<I", len(payload_json))
            client.sendall(length_prefix + payload_json)

            # Read _response_ frame length first
            len_buf = b""
            while len(len_buf) < 4:
                part = client.recv(4 - len(len_buf))
                if not part:
                    return {"status": "error", "message": "Short read on response length"}
                len_buf += part
            (resp_len,) = struct.unpack("<I", len_buf)

            # Read that many bytes
            resp_buf = b""
            while len(resp_buf) < resp_len:
                part = client.recv(resp_len - len(resp_buf))
                if not part:
                    return {"status": "error", "message": "Short read on response body"}
                resp_buf += part

            resp = json.loads(resp_buf.decode("utf-8"))
            # Optionally flatten and return "data" if present
            return resp.get("data", resp)
    except Exception as e:
        return {"status": "error", "message": f"IPC error: {e}"}