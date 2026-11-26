import socket
import json
import os

# Matches the C++ socket path
SOCKET_PATH = "/tmp/justiceflow_ui.sock"

def fetch_os_metrics():
    """
    Connects to the C++ Dashboard Bridge via Unix Domain Socket.
    Returns a dictionary of live OS metrics (Shared Memory data).
    """
    if not os.path.exists(SOCKET_PATH):
        return {"error": "C++ UI Bridge is offline (Socket not found)."}

    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    
    try:
        # 1. Connect to C++
        client.connect(SOCKET_PATH)
        
        # 2. Send a simple request command
        request = json.dumps({"command": "GET_METRICS"})
        client.sendall(request.encode('utf-8'))
        
        # 3. Receive the JSON response (Buffer 4096 bytes)
        response_bytes = client.recv(4096)
        
        if not response_bytes:
            return {"error": "Empty response from C++ Bridge."}
            
        # 4. Decode and return as a Python Dictionary for Streamlit
        return json.loads(response_bytes.decode('utf-8'))
        
    except Exception as e:
        return {"error": f"IPC Communication failed: {str(e)}"}
    finally:
        client.close()

if __name__ == "__main__":
    print("Testing connection to C++ Bridge...")
    data = fetch_os_metrics()
    print(json.dumps(data, indent=4))
