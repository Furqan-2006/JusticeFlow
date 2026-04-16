#pragma once

#include <pthread.h>
#include <time.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SHM_NAME "/agent_status_shm"
#define FIFO_PATH_HOTSPOT "/tmp/agent_fifo_hotspot"
#define FIFO_PATH_PRIORITY "/tmp/agent_fifo_priority"
#define FIFO_PATH_WORKLOAD "/tmp/agent_fifo_workload"

#define AGENT_INDEX_HOTSPOT 0
#define AGENT_INDEX_PRIORITY 1
#define AGENT_INDEX_WORKLOAD 2
#define MAX_AGENTS 3

    typedef struct
    {
        char agent_name[32];
        int status_code;
        char error_detail[128];
        time_t timestamp;
    } AgentStatusMessage;
    typedef struct
    {
        char agent_name[32];
        int current_status;
        char error_detail[128];
        time_t last_updated;
    } AgentStatus;
    typedef struct
    {
        pthread_mutex_t mutex;
        AgentStatus agents[MAX_AGENTS];
    } SharedStatusTable;
}