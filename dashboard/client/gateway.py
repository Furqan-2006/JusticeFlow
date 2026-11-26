"""Client for the JusticeFlow API gateway (Unix domain socket).

The dashboard talks to the daemon only through this module. It never opens a
database connection: the gateway is the single door to the data.

Wire protocol (see src/api_gateway/README.md):
    frame    = [4-byte little-endian payload length][UTF-8 JSON payload]
    request  = {"request_id": n, "command": "GET_CASE_LIST", "params": {...}}
    response = {"request_id": n, "status": 0, "message": "OK", "data": {...}}
status 0 means success; anything else is an error described by "message".
"""
import itertools
import json
import os
import socket
import struct

SOCKET_PATH = os.environ.get("JF_SOCKET", "/tmp/justiceflow.sock")
TIMEOUT_SECONDS = float(os.environ.get("JF_GATEWAY_TIMEOUT", "3"))
MAX_FRAME_BYTES = 8 * 1024 * 1024

_request_ids = itertools.count(1)


def _read_exactly(sock, count):
    buf = b""
    while len(buf) < count:
        chunk = sock.recv(count - len(buf))
        if not chunk:
            raise ConnectionError("gateway closed the connection mid-frame")
        buf += chunk
    return buf


def call(command, params=None, token=None):
    """Send one command and return {"ok", "status", "message", "data"}.

    Never raises: an unreachable gateway or a malformed reply comes back as
    ok=False with status -1, so every page can render an explanation.
    """
    payload = json.dumps(
        {
            "request_id": next(_request_ids),
            "command": command,
            "params": {k: str(v) for k, v in {**(params or {}), **({"token": token} if token else {})}.items()},
        }
    ).encode("utf-8")

    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(TIMEOUT_SECONDS)
            sock.connect(SOCKET_PATH)
            sock.sendall(struct.pack("<I", len(payload)) + payload)
            (length,) = struct.unpack("<I", _read_exactly(sock, 4))
            if length > MAX_FRAME_BYTES:
                raise ValueError(f"response frame too large ({length} bytes)")
            reply = json.loads(_read_exactly(sock, length).decode("utf-8"))
    except FileNotFoundError:
        return _failure(f"Gateway socket {SOCKET_PATH} not found. Is the daemon running?")
    except (OSError, ValueError, struct.error) as exc:
        return _failure(f"Gateway error: {exc}")

    status = reply.get("status", -1)
    return {
        "ok": status == 0,
        "status": status,
        "message": reply.get("message", ""),
        "data": reply.get("data"),
    }


def _failure(message):
    return {"ok": False, "status": -1, "message": message, "data": None}
