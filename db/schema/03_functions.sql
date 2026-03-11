 -- =====================================================
-- FIR NUMBER GENERATION
-- Format: FIR-YYYY-STATIONCODE-0001
-- =====================================================

CREATE OR REPLACE FUNCTION generate_fir_number() RETURNS TRIGGER AS $$
DECLARE
    v_station_code VARCHAR(20);
    v_year TEXT;
    v_sequence INT;
BEGIN
    -- Lock relevant rows to prevent race conditions
    SELECT station_code
    INTO v_station_code
    FROM Stations
    WHERE station_id = NEW.station_id
    FOR SHARE;

    IF v_station_code IS NULL THEN
        RAISE EXCEPTION 'Invalid station_id: %', NEW.station_id;
    END IF;

    v_year := TO_CHAR(NEW.filed_at, 'YYYY');

    SELECT COUNT(*) + 1
    INTO v_sequence
    FROM Cases
    WHERE station_id = NEW.station_id
      AND TO_CHAR(filed_at, 'YYYY') = v_year
    FOR UPDATE;

    NEW.fir_number := FORMAT(
        'FIR-%s-%s-%s',
        v_year,
        v_station_code,
        LPAD(v_sequence::TEXT, 4, '0')
    );

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- =====================================================
-- EVIDENCE NUMBER GENERATION
-- Format: EVD-FIRNUMBER-0001
-- =====================================================

CREATE OR REPLACE FUNCTION generate_evidence_number() RETURNS TRIGGER AS $$
DECLARE
    v_fir VARCHAR(30);
    v_sequence INT;
BEGIN
    SELECT fir_number
    INTO v_fir
    FROM Cases
    WHERE case_id = NEW.case_id
    FOR SHARE;

    IF v_fir IS NULL THEN
        RAISE EXCEPTION 'Invalid case_id: %', NEW.case_id;
    END IF;

    SELECT COUNT(*) + 1
    INTO v_sequence
    FROM Evidence
    WHERE case_id = NEW.case_id
    FOR UPDATE;

    NEW.evidence_number := FORMAT(
        'EVD-%s-%s',
        v_fir,
        LPAD(v_sequence::TEXT, 4, '0')
    );

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- =====================================================
-- PREVENT HARD DELETE ON EVIDENCE
-- =====================================================

CREATE OR REPLACE FUNCTION prevent_evidence_delete() RETURNS TRIGGER AS $$
BEGIN
    RAISE EXCEPTION
        'Hard delete on Evidence is forbidden. Use soft-delete strategy. Evidence ID: %',
        OLD.evidence_id;
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

-- =====================================================
-- WARRANT AUTO-EXPIRY FUNCTION
-- Designed to be called by scheduler (cron / pgAgent)
-- =====================================================

CREATE OR REPLACE FUNCTION expire_warrants() RETURNS VOID AS $$
BEGIN
    UPDATE Warrants
    SET warrant_status = 'EXPIRED',
        updated_at = NOW()
    WHERE valid_until < CURRENT_DATE
      AND warrant_status = 'ISSUED';
END;
$$ LANGUAGE plpgsql;

-- =====================================================
-- ARREST NUMBER GENERATION
-- Format: ARR-YYYY-STATIONCODE-0001
-- =====================================================

CREATE OR REPLACE FUNCTION generate_arrest_number() RETURNS TRIGGER AS $$
DECLARE
    v_station_code VARCHAR(20);
    v_year TEXT;
    v_sequence INT;
BEGIN
    SELECT st.station_code
    INTO v_station_code
    FROM Stations st
    JOIN Cases c ON c.station_id = st.station_id
    WHERE c.case_id = NEW.case_id
    FOR SHARE;

    IF v_station_code IS NULL THEN
        RAISE EXCEPTION 'Invalid case_id for arrest: %', NEW.case_id;
    END IF;

    v_year := TO_CHAR(NEW.arrested_at, 'YYYY');

    SELECT COUNT(*) + 1
    INTO v_sequence
    FROM Arrests
    WHERE TO_CHAR(arrested_at, 'YYYY') = v_year
    FOR UPDATE;

    NEW.arrest_number := FORMAT(
        'ARR-%s-%s-%s',
        v_year,
        v_station_code,
        LPAD(v_sequence::TEXT, 4, '0')
    );

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- =====================================================
-- WARRANT NUMBER GENERATION
-- Format: WRT-YYYY-STATIONCODE-0001
-- =====================================================

CREATE OR REPLACE FUNCTION generate_warrant_number() RETURNS TRIGGER AS $$
DECLARE
    v_station_code VARCHAR(20);
    v_year TEXT;
    v_sequence INT;
BEGIN
    SELECT st.station_code
    INTO v_station_code
    FROM Stations st
    JOIN Cases c ON c.station_id = st.station_id
    WHERE c.case_id = NEW.case_id
    FOR SHARE;

    IF v_station_code IS NULL THEN
        RAISE EXCEPTION 'Invalid case_id for warrant: %', NEW.case_id;
    END IF;

    v_year := TO_CHAR(NEW.issue_date, 'YYYY');

    SELECT COUNT(*) + 1
    INTO v_sequence
    FROM Warrants
    WHERE TO_CHAR(issue_date, 'YYYY') = v_year
    FOR UPDATE;

    NEW.warrant_number := FORMAT(
        'WRT-%s-%s-%s',
        v_year,
        v_station_code,
        LPAD(v_sequence::TEXT, 4, '0')
    );

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;