#ifndef SHM_LAYOUT_H
#define SHM_LAYOUT_H

#include <pthread.h>
#include <time.h>

namespace os_layer {
namespace ipc {

// Contract Page 13: Live Agent Status Table
struct AgentStatus {
    char agent_name[32];
    time_t last_run_at;
    time_t next_run_at;
    int predictions_generated;
    double model_accuracy;
    bool is_running;
    int last_error_code;
};

// Contract Page 13: Full shared memory segment layout
struct SharedStatusTable {
    pthread_mutex_t mutex; // Protects the entire struct from race conditions
    int active_sessions;
    AgentStatus agents[3]; // [0]=hotspot [1]=priority [2]=workload
};

} // namespace ipc
} // namespace os_layer

#endif // SHM_LAYOUT_H
