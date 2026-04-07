-- Migration 003: subtasks
--
-- Adds an optional parent task pointer to support a single level of
-- subtasks. ON DELETE SET NULL means deleting a parent promotes its
-- children to top-level tasks (rather than wiping them out).
--
-- The new STATUS_ABANDONED value (5) is purely a C-level enum
-- addition; statuses are stored as integers, so no schema change is
-- required for it.

ALTER TABLE tasks ADD COLUMN parent_id INTEGER REFERENCES tasks(id) ON DELETE SET NULL;

CREATE INDEX IF NOT EXISTS idx_tasks_parent_id ON tasks(parent_id);
