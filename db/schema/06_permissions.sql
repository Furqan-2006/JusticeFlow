DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'justice_app') THEN
        CREATE ROLE justice_app WITH LOGIN PASSWORD 'justice_app';
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'justice_ai') THEN
        CREATE ROLE justice_ai WITH LOGIN PASSWORD 'justice_ai';
    END IF;
END
$$;
-- Only audit triggers can write to audit schema
-- No application user can INSERT/UPDATE/DELETE directly
REVOKE ALL ON SCHEMA audit FROM PUBLIC;
REVOKE ALL ON audit.Audit_Log FROM PUBLIC;

-- App user can only read audit log
GRANT USAGE ON SCHEMA audit TO justice_app;
GRANT SELECT ON audit.Audit_Log TO justice_app;

-- AI agents cannot touch audit schema at all
REVOKE ALL ON SCHEMA audit FROM justice_ai;

-- Only the trigger function itself can write
-- SECURITY DEFINER on the function handles this

-- AI agents can only write to analytics schema
-- They cannot touch public schema at all
REVOKE ALL ON SCHEMA analytics FROM PUBLIC;

-- AI agent user: read public, write analytics
GRANT USAGE ON SCHEMA public TO justice_ai;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO justice_ai;

GRANT USAGE ON SCHEMA analytics TO justice_ai;
GRANT INSERT ON ALL TABLES IN SCHEMA analytics TO justice_ai;
-- AI agents INSERT only — cannot UPDATE or DELETE their own predictions

-- Application user: read analytics for dashboard
GRANT USAGE ON SCHEMA analytics TO justice_app;
GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA analytics TO justice_ai;
ALTER DEFAULT PRIVILEGES IN SCHEMA analytics
GRANT USAGE, SELECT ON SEQUENCES TO justice_ai;


-- AI agents have zero access to audit schema
REVOKE ALL ON SCHEMA audit FROM justice_ai;

-- ---------------------------------------------------------------------
-- Application role: the C++ daemon and API gateway connect as justice_app.
-- Read and write the transactional tables; never DELETE (records are
-- legal evidence: cancel or expire them, do not remove them).
-- ---------------------------------------------------------------------
GRANT USAGE ON SCHEMA public TO justice_app;
GRANT SELECT, INSERT, UPDATE ON ALL TABLES IN SCHEMA public TO justice_app;
GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO justice_app;
ALTER DEFAULT PRIVILEGES IN SCHEMA public
GRANT SELECT, INSERT, UPDATE ON TABLES TO justice_app;
ALTER DEFAULT PRIVILEGES IN SCHEMA public
GRANT USAGE, SELECT ON SEQUENCES TO justice_app;

-- Dashboard reads the AI agents' predictions; agents alone write them.
GRANT SELECT ON ALL TABLES IN SCHEMA analytics TO justice_app;
ALTER DEFAULT PRIVILEGES IN SCHEMA analytics
GRANT SELECT ON TABLES TO justice_app;
