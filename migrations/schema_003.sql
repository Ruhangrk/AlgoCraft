-- 5.4 Activity tables: users, workbooks, capital events, runs, containers, fills.
-- "today" / live blobs are never in last_date; see schema_002 for coverage rules.

CREATE TABLE IF NOT EXISTS users (
  id         INTEGER PRIMARY KEY AUTOINCREMENT,
  username   TEXT    NOT NULL UNIQUE,
  password_hash TEXT NOT NULL,
  role       TEXT    NOT NULL DEFAULT 'user',
  created_at TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
  deleted_at TEXT
);

CREATE TABLE IF NOT EXISTS workbooks (
  id                   INTEGER PRIMARY KEY,   -- matches WorkbookId (u64 low bytes)
  user_id              INTEGER NOT NULL REFERENCES users(id),
  name                 TEXT    NOT NULL,
  main_capital_paise   INTEGER NOT NULL DEFAULT 0,
  available_paise      INTEGER NOT NULL DEFAULT 0,
  status               TEXT    NOT NULL DEFAULT 'active',
  created_at           TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
  deleted_at           TEXT
);

CREATE TABLE IF NOT EXISTS workbook_capital_events (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  workbook_id  INTEGER NOT NULL REFERENCES workbooks(id),
  type         TEXT    NOT NULL,         -- created | capital_added | borrow | return
  amount_paise INTEGER NOT NULL,
  created_at   TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE TABLE IF NOT EXISTS runs (
  id               INTEGER PRIMARY KEY AUTOINCREMENT,
  workbook_id      INTEGER NOT NULL REFERENCES workbooks(id),
  router           TEXT    NOT NULL,
  strategies_json  TEXT    NOT NULL DEFAULT '[]',
  tickers_json     TEXT    NOT NULL DEFAULT '[]',
  capital_paise    INTEGER NOT NULL DEFAULT 0,
  selected         INTEGER NOT NULL DEFAULT 0,
  skipped          INTEGER NOT NULL DEFAULT 0,
  fills            INTEGER NOT NULL DEFAULT 0,
  returned_paise   INTEGER NOT NULL DEFAULT 0,
  status           TEXT    NOT NULL DEFAULT 'completed',
  created_at       TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
  deleted_at       TEXT
);

CREATE TABLE IF NOT EXISTS containers (
  id               INTEGER PRIMARY KEY AUTOINCREMENT,
  run_id           INTEGER NOT NULL REFERENCES runs(id),
  workbook_id      INTEGER NOT NULL REFERENCES workbooks(id),
  ticker           TEXT    NOT NULL,
  strategy_name    TEXT    NOT NULL,
  mode             TEXT    NOT NULL,         -- backtest | paper | real
  allocation_paise INTEGER NOT NULL DEFAULT 0,
  realized_paise   INTEGER NOT NULL DEFAULT 0,
  fills            INTEGER NOT NULL DEFAULT 0,
  created_at       TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
  deleted_at       TEXT
);

CREATE TABLE IF NOT EXISTS fills (
  id             INTEGER PRIMARY KEY AUTOINCREMENT,
  workbook_id    INTEGER NOT NULL REFERENCES workbooks(id),
  run_id         INTEGER NOT NULL REFERENCES runs(id),
  container_id   INTEGER NOT NULL REFERENCES containers(id),
  ticker         TEXT    NOT NULL,
  side           TEXT    NOT NULL,           -- buy | sell
  qty            INTEGER NOT NULL,
  price_paise    INTEGER NOT NULL,
  fees_paise     INTEGER NOT NULL,
  timestamp_ns   INTEGER NOT NULL,
  created_at     TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

PRAGMA user_version = 3;
