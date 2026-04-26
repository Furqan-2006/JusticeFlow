import os
import time
import struct
import psycopg2
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.preprocessing import LabelEncoder

# --- OS/IPC CONFIGURATION ---
# Matches exactly what you wrote in C++ ipc_manager.cpp
FIFO_PATH = "/tmp/jf_priority.fifo"
AGENT_NAME = b"RandomForest_Priority\0" # Null-terminated for C++ char[32]

def report_status_to_cpp(predictions_count, accuracy, is_running, error_code):
    """
    Packs data into a C-compatible struct and writes to the POSIX FIFO.
    Matches AgentStatus in shm_layout.h:
    char[32], time_t(8), time_t(8), int(4), double(8), bool(1), int(4)
    """
    try:
        if not os.path.exists(FIFO_PATH):
            return # C++ Daemon isn't running yet, skip reporting

        now = int(time.time())
        next_run = now + 60 # Scheduled to run again in 60 secs
        
        # '32s q q i d ? i' -> 32-byte string, 2 long longs, int, double, bool, int
        packed_data = struct.pack('32s q q i d ? i', 
                                  AGENT_NAME, now, next_run, 
                                  predictions_count, accuracy, is_running, error_code)
        
        # Open FIFO strictly as Non-Blocking Write-Only
        fd = os.open(FIFO_PATH, os.O_WRONLY | os.O_NONBLOCK)
        os.write(fd, packed_data)
        os.close(fd)
        print(f"[AI][Priority] Status pulsed to C++ FIFO: {predictions_count} predictions made.")
    except Exception as e:
        print(f"[AI][Priority] IPC Write failed (C++ Daemon might be down): {e}")

def run_agent():
    print("[AI][Priority] Starting Random Forest Agent...")
    report_status_to_cpp(0, 0.0, True, 0)
    
    try:
        # 1. Secure DB Connection (using the AI user role)
        conn = psycopg2.connect(
            dbname="justiceflow", user="justiceflow", # Change user to "justice_ai" if Furqan set it up!
            password="justiceflow123", host="localhost"
        )
        cursor = conn.cursor()

        # 2. Fetch Training Data (Cases that ALREADY have a priority/severity)
        # Assuming severity/priority is mapped. We'll use case_type and station_id as features.
        query_train = """
            SELECT case_type, station_id, case_status 
            FROM public.cases 
            WHERE case_status != 'REGISTERED' LIMIT 5000;
        """
        df_train = pd.read_sql_query(query_train, conn)
        
        if len(df_train) < 10:
            print("[AI][Priority] Not enough historical data to train model.")
            report_status_to_cpp(0, 0.0, False, 1)
            return

        # 3. Data Preprocessing & Training
        # Convert enums/strings to integers for the ML Model
        le_type = LabelEncoder()
        df_train['case_type_encoded'] = le_type.fit_transform(df_train['case_type'])
        
        X = df_train[['case_type_encoded', 'station_id']]
        y = df_train['case_status'] # Using status as a proxy for priority for the demo

        clf = RandomForestClassifier(n_estimators=50, random_state=42)
        clf.fit(X, y)
        accuracy = clf.score(X, y) * 100.0

        # 4. Fetch New Unprocessed FIRs
        query_predict = "SELECT case_id, case_type, station_id FROM public.cases WHERE case_status = 'REGISTERED';"
        df_predict = pd.read_sql_query(query_predict, conn)

        predictions_made = 0
        if not df_predict.empty:
            # Handle unseen labels gracefully
            df_predict['case_type_encoded'] = df_predict['case_type'].apply(
                lambda x: le_type.transform([x])[0] if x in le_type.classes_ else -1
            )
            
            X_new = df_predict[['case_type_encoded', 'station_id']]
            predictions = clf.predict(X_new)

            # 5. Update Database with predictions
            for idx, case_id in enumerate(df_predict['case_id']):
                predicted_priority = "HIGH" if predictions[idx] in ["UNDER_INVESTIGATION", "PENDING_TRIAL"] else "MEDIUM"
                
                # In a real scenario, you'd UPDATE a priority column. We'll just print it for the demo log.
                print(f"[AI][Priority] Case {case_id} ({df_predict['case_type'].iloc[idx]}) -> Evaluated Priority: {predicted_priority}")
                predictions_made += 1
            
            conn.commit()

        # 6. Report Success to C++
        report_status_to_cpp(predictions_made, accuracy, False, 0)
        print(f"[AI][Priority] Run complete. Accuracy: {accuracy:.2f}%. Cases evaluated: {predictions_made}.")

    except Exception as e:
        print(f"[AI][Priority] FATAL ERROR: {e}")
        report_status_to_cpp(0, 0.0, False, 99) # Send error code 99 to C++ Dashboard
    finally:
        if 'conn' in locals() and conn:
            cursor.close()
            conn.close()

if __name__ == "__main__":
    run_agent()
