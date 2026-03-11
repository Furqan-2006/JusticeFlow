-- =====================================================
-- TRIGGER DEFINITIONS
-- =====================================================
-- All triggers use IF NOT EXISTS pattern via DROP/CREATE
-- to ensure idempotent deployments

-- FIR NUMBER TRIGGER
DROP TRIGGER IF EXISTS trg_generate_fir_number ON Cases;
CREATE TRIGGER trg_generate_fir_number
BEFORE INSERT ON Cases
FOR EACH ROW
WHEN (NEW.fir_number IS NULL)
EXECUTE FUNCTION generate_fir_number();


-- EVIDENCE NUMBER TRIGGER
DROP TRIGGER IF EXISTS trg_generate_evidence_number ON Evidence;
CREATE TRIGGER trg_generate_evidence_number
BEFORE INSERT ON Evidence
FOR EACH ROW
WHEN (NEW.evidence_number IS NULL)
EXECUTE FUNCTION generate_evidence_number();


-- PREVENT HARD DELETE ON EVIDENCE
DROP TRIGGER IF EXISTS trg_prevent_evidence_delete ON Evidence;
CREATE TRIGGER trg_prevent_evidence_delete
BEFORE DELETE ON Evidence
FOR EACH ROW
EXECUTE FUNCTION prevent_evidence_delete();


-- ARREST NUMBER TRIGGER
DROP TRIGGER IF EXISTS trg_generate_arrest_number ON Arrests;
CREATE TRIGGER trg_generate_arrest_number
BEFORE INSERT ON Arrests
FOR EACH ROW
WHEN (NEW.arrest_number IS NULL)
EXECUTE FUNCTION generate_arrest_number();


-- WARRANT NUMBER TRIGGER
DROP TRIGGER IF EXISTS trg_generate_warrant_number ON Warrants;
CREATE TRIGGER trg_generate_warrant_number
BEFORE INSERT ON Warrants
FOR EACH ROW
WHEN (NEW.warrant_number IS NULL)
EXECUTE FUNCTION generate_warrant_number();
