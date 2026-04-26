-- ============================================================================
-- forensic_triggers.sql  —  Module 3: Evidence Status Sync Triggers
-- ============================================================================
--
-- Design Pattern: Observer
--   Evidence observes forensic request state changes entirely through these
--   triggers. The C++ application layer (ForensicManager, ForensicRepository)
--   NEVER updates public.Evidence.evidence_status directly. The triggers are
--   the sole authority for evidence status transitions driven by forensic work.
--
-- Two triggers:
--   Trigger 1: ON INSERT to Forensic_Request_Evidence
--              → Sets evidence_status = 'SENT_TO_LAB' for the linked evidence
--
--   Trigger 2: ON UPDATE to Forensic_Lab_Requests WHEN new status = REPORT_DELIVERED
--              → Sets evidence_status = 'RETURNED_FROM_LAB' for ALL evidence
--                linked to this request (batch update via JOIN)
--
-- Both triggers fire within the same transaction as the application write,
-- so evidence_status is always consistent with the forensic request state.
-- If the application write rolls back, the trigger update rolls back too.
--
-- Immutability note:
--   The audit.log_change trigger (SECURITY DEFINER) also fires on the
--   Evidence UPDATE caused by these triggers, capturing the status change
--   in audit.Audit_Log automatically.
-- ============================================================================

-- ============================================================================
-- TRIGGER FUNCTION 1: sync_evidence_sent_to_lab
-- Fires AFTER INSERT on public.Forensic_Request_Evidence.
-- Sets evidence_status = 'SENT_TO_LAB' for NEW.evidence_id.
-- ============================================================================

CREATE OR REPLACE FUNCTION public.sync_evidence_sent_to_lab()
RETURNS TRIGGER
LANGUAGE plpgsql
SECURITY DEFINER  -- Runs as the function owner (justice_admin), not calling user.
                  -- Analogue: setuid bit on a Unix executable.
AS $$
BEGIN
    -- Guard: only update if evidence is in a linkable state.
    -- Prevents overwriting PRODUCED_IN_COURT or DISPOSED accidentally.
    UPDATE public.Evidence
    SET    evidence_status = 'SENT_TO_LAB',
           updated_at      = NOW()
    WHERE  evidence_id = NEW.evidence_id
      AND  is_deleted  = FALSE
      AND  evidence_status IN ('RECEIVED', 'SEALED', 'RETURNED_FROM_LAB');

    -- If no rows were updated, the evidence was in a non-linkable state.
    -- We do NOT raise an exception here — the application layer already
    -- validated the status in ForensicManager::linkEvidence() before
    -- calling the INSERT. Defensive: log if nothing was updated.
    IF NOT FOUND THEN
        RAISE WARNING
            'sync_evidence_sent_to_lab: evidence_id=% not updated (status incompatible or deleted)',
            NEW.evidence_id;
    END IF;

    RETURN NEW;
END;
$$;

-- Attach to Forensic_Request_Evidence INSERT
DROP TRIGGER IF EXISTS trg_sync_sent_to_lab
    ON public.Forensic_Request_Evidence;

CREATE TRIGGER trg_sync_sent_to_lab
    AFTER INSERT ON public.Forensic_Request_Evidence
    FOR EACH ROW
    EXECUTE FUNCTION public.sync_evidence_sent_to_lab();

COMMENT ON TRIGGER trg_sync_sent_to_lab ON public.Forensic_Request_Evidence IS
    'Observer trigger: sets Evidence.evidence_status = SENT_TO_LAB when evidence '
    'is linked to a forensic request. Application never updates Evidence directly.';


-- ============================================================================
-- TRIGGER FUNCTION 2: sync_evidence_returned_from_lab
-- Fires AFTER UPDATE on public.Forensic_Lab_Requests WHEN new status is
-- REPORT_DELIVERED.
-- Sets evidence_status = 'RETURNED_FROM_LAB' for ALL evidence linked to
-- this request via Forensic_Request_Evidence.
-- ============================================================================

CREATE OR REPLACE FUNCTION public.sync_evidence_returned_from_lab()
RETURNS TRIGGER
LANGUAGE plpgsql
SECURITY DEFINER  -- Runs as the function owner (justice_admin).
AS $$
DECLARE
    v_updated_count INTEGER;
BEGIN
    -- Only act on the specific transition to REPORT_DELIVERED.
    -- The WHEN clause on the trigger already filters this, but we guard here
    -- as well for correctness if the trigger is ever reconfigured.
    IF NEW.request_status <> 'REPORT_DELIVERED' THEN
        RETURN NEW;
    END IF;

    -- Batch update: set RETURNED_FROM_LAB for all evidence in this request.
    -- Join through Forensic_Request_Evidence to find evidence_ids.
    UPDATE public.Evidence e
    SET    evidence_status = 'RETURNED_FROM_LAB',
           updated_at      = NOW()
    FROM   public.Forensic_Request_Evidence fre
    WHERE  fre.request_id  = NEW.request_id
      AND  fre.evidence_id = e.evidence_id
      AND  e.is_deleted    = FALSE
      AND  e.evidence_status = 'SENT_TO_LAB';
      -- Only update evidence that is currently SENT_TO_LAB.
      -- If evidence was already PRODUCED_IN_COURT it stays that way.

    GET DIAGNOSTICS v_updated_count = ROW_COUNT;

    IF v_updated_count = 0 THEN
        RAISE WARNING
            'sync_evidence_returned_from_lab: request_id=% — no SENT_TO_LAB evidence updated',
            NEW.request_id;
    ELSE
        RAISE NOTICE
            'sync_evidence_returned_from_lab: request_id=% — % evidence items set to RETURNED_FROM_LAB',
            NEW.request_id, v_updated_count;
    END IF;

    RETURN NEW;
END;
$$;

-- Attach to Forensic_Lab_Requests UPDATE, only when status changes to REPORT_DELIVERED
DROP TRIGGER IF EXISTS trg_sync_returned_from_lab
    ON public.Forensic_Lab_Requests;

CREATE TRIGGER trg_sync_returned_from_lab
    AFTER UPDATE OF request_status ON public.Forensic_Lab_Requests
    FOR EACH ROW
    WHEN (OLD.request_status IS DISTINCT FROM NEW.request_status
          AND NEW.request_status = 'REPORT_DELIVERED')
    EXECUTE FUNCTION public.sync_evidence_returned_from_lab();

COMMENT ON TRIGGER trg_sync_returned_from_lab ON public.Forensic_Lab_Requests IS
    'Observer trigger: batch-sets Evidence.evidence_status = RETURNED_FROM_LAB '
    'for all evidence linked to this request when it reaches REPORT_DELIVERED. '
    'Application never updates Evidence directly.';


-- ============================================================================
-- VERIFICATION QUERIES  (run after deployment to confirm triggers are installed)
-- ============================================================================

-- Should return 2 rows: trg_sync_sent_to_lab and trg_sync_returned_from_lab
SELECT tgname, tgenabled, tgtype
FROM   pg_trigger
WHERE  tgname IN ('trg_sync_sent_to_lab', 'trg_sync_returned_from_lab')
ORDER  BY tgname;

-- ============================================================================
-- END forensic_triggers.sql
-- ============================================================================