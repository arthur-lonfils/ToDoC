-- Migration 004: labels (many-to-many cross-cutting tags)
--
-- Labels are free-form tags that can be attached to many tasks. Unlike
-- 'scope' (one primary string per task), labels are unbounded and a
-- task can carry multiple. The 'label' / 'unlabel' / 'add --label foo'
-- commands auto-create rows in this table on first use.

CREATE TABLE IF NOT EXISTS labels (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT    NOT NULL UNIQUE,
    color       TEXT,
    created_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now','localtime'))
);

CREATE TABLE IF NOT EXISTS task_labels (
    task_id  INTEGER NOT NULL REFERENCES tasks(id) ON DELETE CASCADE,
    label_id INTEGER NOT NULL REFERENCES labels(id) ON DELETE CASCADE,
    PRIMARY KEY (task_id, label_id)
);

CREATE INDEX IF NOT EXISTS idx_task_labels_label ON task_labels(label_id);
