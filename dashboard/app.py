import os
import json
import socket
from flask import Flask, render_template, request, jsonify

app = Flask(__name__)

# The socket path exactly as defined in your architecture diagram
SOCKET_PATH = "/tmp/justiceflow.sock"

def send_to_cpp_gateway(payload_dict):
    """
    Opens a Unix Domain Socket, sends a JSON payload to the C++ API Gateway,
    and waits for the JSON response.
    """
    if not os.path.exists(SOCKET_PATH):
        return {"status": "error", "message": "C++ Backend is offline (Socket not found)."}

    # Create a Unix Domain Socket 
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    
    try:
        client.connect(SOCKET_PATH)
        
        # Convert Python dictionary to JSON string and encode to bytes
        json_data = json.dumps(payload_dict)
        client.sendall(json_data.encode('utf-8'))
        
        # Wait for the C++ response (assuming buffer size of 4096 bytes)
        response_bytes = client.recv(4096)
        if not response_bytes:
            return {"status": "error", "message": "Empty response from C++ Backend."}
            
        # Decode bytes back to Python dictionary
        return json.loads(response_bytes.decode('utf-8'))
        
    except Exception as e:
        return {"status": "error", "message": f"IPC Communication failed: {str(e)}"}
    finally:
        client.close()


# --- FLASK ROUTES (The Web Pages) ---

@app.route('/')
def dashboard():
    """Renders the main dashboard page."""
    # Example: Ask C++ for the latest stats to show on the dashboard
    payload = {"action": "get_dashboard_stats"}
    stats = send_to_cpp_gateway(payload)
    
    # Passing the data to Abu Bakr's HTML template
    return render_template('dashboard.html', stats=stats)

@app.route('/register_fir', methods=['POST'])
def register_fir():
    """Handles the form submission from the UI to register an FIR."""
    # Get the form data from Abu Bakr's HTML form
    form_data = request.form.to_dict()
    
    # Wrapping it in a command payload for Subsystem 2 (Your C++ subsystem!)
    payload = {
        "action": "register_fir",
        "data": {
            "type": form_data.get("case_type"),
            "description": form_data.get("description"),
            "lat": float(form_data.get("lat", 0.0)),
            "lon": float(form_data.get("lon", 0.0)),
            "station_id": int(form_data.get("station_id", 1))
        }
    }
    
    # Send it down the IPC socket to the C++ CaseManager
    response = send_to_cpp_gateway(payload)
    
    return jsonify(response)

if __name__ == '__main__':
    print("[Flask] Starting JusticeFlow UI...")
    # Run on port 5000
    app.run(host='0.0.0.0', port=5000, debug=True)
