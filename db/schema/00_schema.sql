-- core application tables
CREATE SCHEMA IF NOT EXISTS public;
-- append-only legal audit trail
CREATE SCHEMA IF NOT EXISTS audit;
-- AI agents prediction output
CREATE SCHEMA IF NOT EXISTS analytics;
-- password hashing (auth_manager verifies with crypt())
CREATE EXTENSION IF NOT EXISTS pgcrypto;
