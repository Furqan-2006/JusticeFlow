-- ============================================================
-- JusticeFlow — schema_patch.sql
-- Fixes ALL issues identified against the live database
-- Run this ONCE before running generate_data.py
-- ============================================================

-- ============================================================
-- FIX 1: audited_table_enum — values must be lowercase
-- TG_TABLE_NAME always returns lowercase in PostgreSQL
-- Mixed-case enum fails the cast TG_TABLE_NAME::audited_table_enum
-- ============================================================
DO $$
BEGIN
    -- Only fix if the enum has wrong-case values
    IF EXISTS (
        SELECT 1 FROM pg_enum e
        JOIN pg_type t ON e.enumtypid = t.oid
        WHERE t.typname = 'audited_table_enum'
        AND e.enumlabel ~ '[A-Z]'
    ) THEN
        ALTER TYPE audited_table_enum RENAME TO audited_table_enum_old;

        CREATE TYPE audited_table_enum AS ENUM (
            'cases',
            'evidence',
            'officers',
            'arrests',
            'warrants',
            'charge_sheets',
            'bail_records',
            'accused'
        );

        ALTER TABLE audit.Audit_Log
            ALTER COLUMN table_name TYPE audited_table_enum
            USING table_name::text::audited_table_enum;

        DROP TYPE audited_table_enum_old;

        RAISE NOTICE 'FIX 1: audited_table_enum recreated with lowercase values';
    ELSE
        RAISE NOTICE 'FIX 1: audited_table_enum already correct — skipped';
    END IF;
END
$$;


-- ============================================================
-- FIX 2: persons_cnic_check — replace \d with [0-9]
-- \d shorthand unreliable across PostgreSQL versions
-- ============================================================
DO $$
BEGIN
    IF EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'persons_cnic_check'
        AND pg_get_constraintdef(oid) LIKE '%\\\\d%'
    ) THEN
        ALTER TABLE Persons DROP CONSTRAINT persons_cnic_check;
        ALTER TABLE Persons ADD CONSTRAINT persons_cnic_check
            CHECK (cnic ~ '^[0-9]{5}-[0-9]{7}-[0-9]{1}$');
        RAISE NOTICE 'FIX 2: persons_cnic_check updated to use [0-9]';
    ELSE
        RAISE NOTICE 'FIX 2: persons_cnic_check already correct — skipped';
    END IF;
END
$$;


-- ============================================================
-- FIX 3: Accused.master_accused_cnic — make nullable
-- It represents an optional alias link — NOT NULL is incorrect
-- The chk_no_self_alias constraint already handles integrity
-- ============================================================
DO $$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_name = 'accused'
        AND column_name = 'master_accused_cnic'
        AND is_nullable = 'NO'
    ) THEN
        ALTER TABLE Accused ALTER COLUMN master_accused_cnic DROP NOT NULL;
        RAISE NOTICE 'FIX 3: Accused.master_accused_cnic made nullable';
    ELSE
        RAISE NOTICE 'FIX 3: Accused.master_accused_cnic already nullable — skipped';
    END IF;
END
$$;


-- ============================================================
-- FIX 4: charge_sheets.chk_laws_invoked_not_empty — AND → OR
-- Current constraint:  status='DRAFT' AND array_length > 0
-- This blocks ALL non-DRAFT inserts regardless of laws_invoked
-- Correct intent:      status='DRAFT' OR array_length > 0
-- (DRAFT is exempt from the non-empty requirement)
-- ============================================================
DO $$
BEGIN
    IF EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'chk_laws_invoked_not_empty'
    ) THEN
        ALTER TABLE charge_sheets DROP CONSTRAINT chk_laws_invoked_not_empty;
        ALTER TABLE charge_sheets ADD CONSTRAINT chk_laws_invoked_not_empty
            CHECK (
                charge_sheet_status = 'DRAFT'
                OR array_length(laws_invoked, 1) > 0
            );
        RAISE NOTICE 'FIX 4: chk_laws_invoked_not_empty fixed (AND → OR)';
    ELSE
        RAISE NOTICE 'FIX 4: chk_laws_invoked_not_empty not found — skipped';
    END IF;
END
$$;


-- ============================================================
-- FIX 5: Bail_Records table name
-- Rename Bails_Records → Bail_Records if not already done
-- ============================================================
DO $$
BEGIN
    IF EXISTS (
        SELECT 1 FROM information_schema.tables
        WHERE table_name = 'bails_records'
        AND table_schema = 'public'
    ) THEN
        ALTER TABLE Bails_Records RENAME TO Bail_Records;
        RAISE NOTICE 'FIX 5: Bails_Records renamed to Bail_Records';
    ELSE
        RAISE NOTICE 'FIX 5: Bail_Records already correctly named — skipped';
    END IF;
END
$$;


-- ============================================================
-- FIX 6: charge_sheets.chk_valid_until_after_bail_date
-- Bail_Records: valid_until IS NULL OR valid_until > bail_date
-- Current constraint has no NULL guard — blocks NULL valid_until
-- ============================================================
DO $$
BEGIN
    IF EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conname = 'chk_valid_until_after_bail_date'
        AND pg_get_constraintdef(oid) NOT LIKE '%IS NULL%'
    ) THEN
        ALTER TABLE Bail_Records DROP CONSTRAINT chk_valid_until_after_bail_date;
        ALTER TABLE Bail_Records ADD CONSTRAINT chk_valid_until_after_bail_date
            CHECK (valid_until IS NULL OR valid_until > bail_date);
        RAISE NOTICE 'FIX 6: chk_valid_until_after_bail_date updated with NULL guard';
    ELSE
        RAISE NOTICE 'FIX 6: chk_valid_until_after_bail_date already correct — skipped';
    END IF;
END
$$;


-- ============================================================
-- VERIFICATION — print current state of all fixed objects
-- ============================================================
DO $$
DECLARE
    v_enum_count    INT;
    v_cnic_check    TEXT;
    v_accused_null  TEXT;
    v_laws_check    TEXT;
    v_bail_table    TEXT;
BEGIN
    -- Check 1: enum values
    SELECT COUNT(*) INTO v_enum_count
    FROM pg_enum e JOIN pg_type t ON e.enumtypid = t.oid
    WHERE t.typname = 'audited_table_enum';

    -- Check 2: CNIC regex
    SELECT pg_get_constraintdef(oid) INTO v_cnic_check
    FROM pg_constraint WHERE conname = 'persons_cnic_check';

    -- Check 3: master_accused_cnic nullable
    SELECT is_nullable INTO v_accused_null
    FROM information_schema.columns
    WHERE table_name='accused' AND column_name='master_accused_cnic';

    -- Check 4: laws_invoked check
    SELECT pg_get_constraintdef(oid) INTO v_laws_check
    FROM pg_constraint WHERE conname = 'chk_laws_invoked_not_empty';

    -- Check 5: bail table name
    SELECT table_name INTO v_bail_table
    FROM information_schema.tables
    WHERE table_schema='public' AND table_name IN ('bail_records','bails_records');

    RAISE NOTICE '=== VERIFICATION ===';
    RAISE NOTICE 'audited_table_enum values: %', v_enum_count;
    RAISE NOTICE 'persons_cnic_check: %', v_cnic_check;
    RAISE NOTICE 'accused.master_accused_cnic nullable: %', v_accused_null;
    RAISE NOTICE 'chk_laws_invoked_not_empty: %', v_laws_check;
    RAISE NOTICE 'bail table name: %', v_bail_table;
    RAISE NOTICE '=== ALL FIXES APPLIED ===';
END
$$;
