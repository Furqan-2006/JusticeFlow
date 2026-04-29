#include "../include/shared_memory.h"
#include "../../../common/json.hpp"
#include "../../../common/ipc_types.h" // Ensures we see Furqan's struct
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h> // FIX 1: Added for chmod
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <csignal>

using json = nlohmann::json;
using namespace ipc;

const std::string UI_SOCKET_PATH = "/tmp/justiceflow_ui.sock";
bool is_running = true;

// Handle Ctrl+C gracefully
void signalHandler(int signum) {
    is_running = false;
}

int main() {
    signal(SIGINT, signalHandler);

    // 1. Attach to the POSIX Shared Memory you built earlier!
    SharedMemory shm("/jf_status_shm");
    if (shm.attach() != JusticeFlow::ResultCode::OK) {
        std::cerr << "[Bridge] Failed to attach to OS Shared Memory. Is the Scheduler running?\n";
        return 1;
    }

    // 2. Setup the Unix Domain Socket Server
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    unlink(UI_SOCKET_PATH.c_str());

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, UI_SOCKET_PATH.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "[Bridge] Bind failed.\n";
        return 1;
    }

    listen(server_fd, 5);
    chmod(UI_SOCKET_PATH.c_str(), 0666); // Fix 1 applied!

    std::cout << "[Bridge] Listening for Streamlit UI on " << UI_SOCKET_PATH << "...\n";

    // 3. The Main Loop
    while (is_running) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd == -1) continue;

        char buffer[1024] = {0};
        read(client_fd, buffer, sizeof(buffer));

        // FIX 2: Force the compiler to use your local ipc namespace struct
        ::ipc::SharedStatusTable* table = shm.getTable();
        
        json response;
        if (table == nullptr) {
            response["error"] = "Shared Memory Unavailable";
        } else {
            // Read from Shared Memory safely
            pthread_mutex_lock(&table->mutex);
            
            //response["active_sessions"] = table->active_sessions;
            
            std::vector<std::string> agent_names = {"Hotspot (DBSCAN)", "Priority (RandomForest)", "Workload (Hungarian)"};
            
            for (int i = 0; i < 3; ++i) {
                json agent_data;
                agent_data["name"] = agent_names[i];
                
                // FIX: Updated to match Furqan's actual ipc_types.h struct
                agent_data["current_status"] = table->agents[i].current_status;
                agent_data["error_detail"] = std::string(table->agents[i].error_detail);
                agent_data["last_updated"] = table->agents[i].last_updated;
                
                response["ai_agents"].push_back(agent_data);
            } 
            pthread_mutex_unlock(&table->mutex);
        }

        // Send JSON string back to Python
        std::string res_str = response.dump();
        write(client_fd, res_str.c_str(), res_str.length());
        
        close(client_fd);
    }

    // Cleanup
    close(server_fd);
    unlink(UI_SOCKET_PATH.c_str());
    std::cout << "\n[Bridge] Offline.\n";
    return 0;
}
