-- Manual backtest event timeline (signals / rejections / fills). Separate from run-scoped tables.
CREATE TABLE IF NOT EXISTS backtest_signals (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  workbook_id     INTEGER NOT NULL REFERENCES workbooks(id),
  backtest_id     INTEGER NOT NULL REFERENCES backtests(id),
  ticker          TEXT    NOT NULL,
  strategy_name   TEXT    NOT NULL,
  intent_count    INTEGER NOT NULL DEFAULT 1,
  indicators_json TEXT    NOT NULL DEFAULT '{}',
  timestamp_ns    INTEGER NOT NULL,
  created_at      TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE TABLE IF NOT EXISTS backtest_rejections (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  workbook_id     INTEGER NOT NULL REFERENCES workbooks(id),
  backtest_id     INTEGER NOT NULL REFERENCES backtests(id),
  ticker          TEXT    NOT NULL,
  rule_name       TEXT    NOT NULL,
  reason          TEXT    NOT NULL,
  timestamp_ns    INTEGER NOT NULL,
  created_at      TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE TABLE IF NOT EXISTS backtest_fills (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  workbook_id     INTEGER NOT NULL REFERENCES workbooks(id),
  backtest_id     INTEGER NOT NULL REFERENCES backtests(id),
  ticker          TEXT    NOT NULL,
  side            TEXT    NOT NULL,
  qty             INTEGER NOT NULL,
  price_paise     INTEGER NOT NULL,
  fees_paise      INTEGER NOT NULL DEFAULT 0,
  timestamp_ns    INTEGER NOT NULL,
  created_at      TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE INDEX IF NOT EXISTS idx_backtest_signals_bt ON backtest_signals(backtest_id);
CREATE INDEX IF NOT EXISTS idx_backtest_rejections_bt ON backtest_rejections(backtest_id);
CREATE INDEX IF NOT EXISTS idx_backtest_fills_bt ON backtest_fills(backtest_id);

PRAGMA user_version = 8;
