-- S3a: Manual backtest results (1m engine path; HTTP in S3c).
CREATE TABLE IF NOT EXISTS backtests (
  id                   INTEGER PRIMARY KEY AUTOINCREMENT,
  workbook_id          INTEGER NOT NULL REFERENCES workbooks(id),
  strategy_name        TEXT    NOT NULL,
  ticker               TEXT    NOT NULL,
  capital_paise        INTEGER NOT NULL DEFAULT 0,
  from_ns              INTEGER NOT NULL DEFAULT 0,
  to_ns                INTEGER NOT NULL DEFAULT 0,
  ending_equity_paise  INTEGER NOT NULL DEFAULT 0,
  pnl_paise            INTEGER NOT NULL DEFAULT 0,
  fees_paise           INTEGER NOT NULL DEFAULT 0,
  return_pct_bp        INTEGER NOT NULL DEFAULT 0,  -- basis points (10000 = 100%)
  max_drawdown_paise   INTEGER NOT NULL DEFAULT 0,
  fills                INTEGER NOT NULL DEFAULT 0,
  bars                 INTEGER NOT NULL DEFAULT 0,
  status               TEXT    NOT NULL DEFAULT 'completed',
  created_at           TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
  deleted_at           TEXT
);

CREATE INDEX IF NOT EXISTS idx_backtests_workbook ON backtests(workbook_id);
CREATE INDEX IF NOT EXISTS idx_backtests_workbook_created ON backtests(workbook_id, created_at);

PRAGMA user_version = 6;
