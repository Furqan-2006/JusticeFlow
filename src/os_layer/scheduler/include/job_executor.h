#pragma once

#include "scheduler.h"
#include <vector>
#include <string>

// Forward declaration of libpq PGconn wrapper/pointer
typedef struct pg_conn PGconn;

class SqlJob : public Job
{
protected:
    PGconn *conn; // Managed by ipc_manager, not owned here

public:
    SqlJob(int interval, PGconn *connection) : Job(interval), conn(connection) {}
    virtual ~SqlJob() = default;
    virtual void execute() override = 0;
};

class ExpireWarrantsJob : public SqlJob
{
public:
    ExpireWarrantsJob(int interval, PGconn *conn) : SqlJob(interval, conn) {}
    virtual ~ExpireWarrantsJob() = default;
    void execute() override;
};

class CheckDatabaseHealthJob : public SqlJob
{
public:
    CheckDatabaseHealthJob(int interval, PGconn *conn) : SqlJob(interval, conn) {}
    virtual ~CheckDatabaseHealthJob() = default;
    void execute() override;
};