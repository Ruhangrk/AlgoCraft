-- Agent strategy / router / indicator catalog (enable independently of in-binary registry).
CREATE TABLE IF NOT EXISTS strategy_catalog (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  kind         TEXT    NOT NULL DEFAULT 'strategy',  -- strategy | router | indicator
  name         TEXT    NOT NULL UNIQUE,
  class_name   TEXT    NOT NULL DEFAULT '',
  enabled      INTEGER NOT NULL DEFAULT 0,            -- 0=disabled (after promote), 1=active
  hpp_path     TEXT    NOT NULL DEFAULT '',
  cpp_path     TEXT    NOT NULL DEFAULT '',
  sandbox_path TEXT    NOT NULL DEFAULT '',
  provenance   TEXT    NOT NULL DEFAULT 'agent',     -- agent | manual
  compile_ok   INTEGER NOT NULL DEFAULT 0,
  created_at   TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
  updated_at   TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE INDEX IF NOT EXISTS idx_strategy_catalog_enabled ON strategy_catalog(enabled);
CREATE INDEX IF NOT EXISTS idx_strategy_catalog_kind ON strategy_catalog(kind);

PRAGMA user_version = 9;
