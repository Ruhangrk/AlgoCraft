-- 5.5 Signal + risk rejection logging (async-friendly; written post-run for now).
CREATE TABLE IF NOT EXISTS strategy_signals (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  workbook_id     INTEGER NOT NULL REFERENCES workbooks(id),
  run_id          INTEGER NOT NULL REFERENCES runs(id),
  container_id    INTEGER NOT NULL REFERENCES containers(id),
  ticker          TEXT    NOT NULL,
  strategy_name   TEXT    NOT NULL,
  intent_count    INTEGER NOT NULL DEFAULT 1,
  indicators_json TEXT    NOT NULL DEFAULT '{}',
  timestamp_ns    INTEGER NOT NULL,
  created_at      TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE TABLE IF NOT EXISTS risk_rejections (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  workbook_id     INTEGER NOT NULL REFERENCES workbooks(id),
  run_id          INTEGER NOT NULL REFERENCES runs(id),
  container_id    INTEGER NOT NULL REFERENCES containers(id),
  ticker          TEXT    NOT NULL,
  rule_name       TEXT    NOT NULL,
  reason          TEXT    NOT NULL,
  timestamp_ns    INTEGER NOT NULL,
  created_at      TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

PRAGMA user_version = 4;
