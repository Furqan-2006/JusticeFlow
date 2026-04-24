#include "../include/worker.h"
#include <iostream>
#include <stdexcept>
#include <unistd.h>

// ---------------------------------------------------------
// STUB: Required by cross-module coordination contract
// Furqan will build the real AuthManager later.
// ---------------------------------------------------------
class AuthManagerStub
{
public:
    static bool authenticate(int socket_fd, int &out_officer_id)
    {
        out_officer_id = 1001; // Mocking Constable Raj's UID
        return true;
    }
};

// ---------------------------------------------------------
// The Worker Execution Loop
// ---------------------------------------------------------
void Worker::process_task(WorkerTask task)
{
    try
    {
        int officer_id = 0;

        // 1. Authenticate the incoming Unix Domain Socket
        if (!AuthManagerStub::authenticate(task.client_socket_fd, officer_id))
        {
            std::cerr << "[Worker " << task.thread_id << "] Auth failed. Dropping.\n";
            close(task.client_socket_fd);
            return;
        }

        // 2. Register the active session in your unordered_map
        SessionContext ctx = {officer_id, task.client_socket_fd, 0};
        SessionManager::getInstance().register_session(task.thread_id, ctx);

        // 3. The Critical Section (Database Querying)
        {
            // Acquire the DB slot. If 50 officers are active, this thread BLOCKS here.
            SemGuard gate_lock(ConnectionGate::getInstance().getSemaphore());

            std::cout << "[Worker " << task.thread_id << "] UID " << officer_id
                      << " acquired DB slot. Processing query...\n";

            // TODO: In Phase 5, Furqan's job_executor will hook in right here
            // to call Abdullah's ipc_manager.executeQuery()

            usleep(100000); // Simulating database execution time

        } // <-- RAII MAGIC: SemGuard goes out of scope here. The DB slot is instantly released!

        // 4. Teardown
        SessionManager::getInstance().unregister_session(task.thread_id);
        close(task.client_socket_fd);
        std::cout << "[Worker " << task.thread_id << "] Finished and disconnected.\n";
    }
    catch (const std::exception &e)
    {
        // Architecture Mandate: Top-level catch to prevent thread death on crash
        std::cerr << "[Worker " << task.thread_id << "] CRASH INTERCEPTED: " << e.what() << "\n";

        // Even if we crash, we must clean up the session registry and socket
        SessionManager::getInstance().unregister_session(task.thread_id);
        close(task.client_socket_fd);
    }
    catch (...)
    {
        std::cerr << "[Worker " << task.thread_id << "] Unknown FATAL exception caught.\n";
        SessionManager::getInstance().unregister_session(task.thread_id);
        close(task.client_socket_fd);
    }
}
