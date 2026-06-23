-- Migration: Add dag_snapshot column to workflow_instances
-- Fix #152: Save dag_json snapshot at instance creation time so that
-- in-flight instances are not affected by concurrent workflow edits.

ALTER TABLE workflow_instances
    ADD COLUMN IF NOT EXISTS dag_snapshot JSONB;

-- Backfill existing instances with the current dag_json from their workflows.
-- This ensures old instances also have a snapshot for DagDriver to use.
UPDATE workflow_instances wi
SET dag_snapshot = w.dag_json
FROM workflows w
WHERE wi.workflow_id = w.id
  AND wi.dag_snapshot IS NULL;
