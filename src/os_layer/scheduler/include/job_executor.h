#pragma once
#include "scheduler.h"

// Forward declaration of libpq PGconn wrapper/pointer
typedef struct pg_conn PGconn;

class SqlJob : public Job {
protected:
    PGconn* conn; // Managed by ipc_manager, not owned here
public:
    SqlJob(int interval, PGconn* connection) : Job(interval), conn(connection) {}
    virtual void execute() override = 0;
};

class ExpireWarrantsJob : public SqlJob {
public:
    ExpireWarrantsJob(int interval, PGconn* conn) : SqlJob(interval, conn) {}
    void execute() override;
};

class CheckDatabaseHealthJob : public SqlJob {
public:
    CheckDatabaseHealthJob(int interval, PGconn* conn) : SqlJob(interval, conn) {}
    void execute() override;
};