-- S5b: Routing decisions + container lifecycle (timeline events).
CREATE TABLE IF NOT EXISTS routing_decisions (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  workbook_id     INTEGER NOT NULL REFERENCES workbooks(id),
  run_id          INTEGER NOT NULL REFERENCES runs(id),
  ticker          TEXT    NOT NULL,
  strategy_name   TEXT    NOT NULL,
  decision        TEXT    NOT NULL,  -- CREATE | SKIP
  score_paise     INTEGER NOT NULL DEFAULT 0,
  reason          TEXT    NOT NULL DEFAULT '',
  timestamp_ns    INTEGER NOT NULL DEFAULT 0,
  created_at      TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE TABLE IF NOT EXISTS container_events (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  workbook_id     INTEGER NOT NULL REFERENCES workbooks(id),
  run_id          INTEGER NOT NULL REFERENCES runs(id),
  container_id    INTEGER REFERENCES containers(id),
  ticker          TEXT    NOT NULL DEFAULT '',
  event_type      TEXT    NOT NULL,  -- CREATED | FORCE_STOPPED
  detail          TEXT    NOT NULL DEFAULT '',
  timestamp_ns    INTEGER NOT NULL DEFAULT 0,
  created_at      TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE INDEX IF NOT EXISTS idx_routing_decisions_run ON routing_decisions(run_id);
CREATE INDEX IF NOT EXISTS idx_container_events_run ON container_events(run_id);

PRAGMA user_version = 7;
