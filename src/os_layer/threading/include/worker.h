#pragma once

#include "session_manager.h"
#include "connection_gate.h"

// The raw data passed to a worker when a new connection comes in
struct WorkerTask {
    int client_socket_fd;
    int thread_id;
};

class Worker {
public:
    // The actual processing logic for a single task/officer request
    static void process_task(WorkerTask task);
};