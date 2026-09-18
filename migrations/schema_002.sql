-- 5.2 symbol coverage for RocksDB session blobs. Today is never in last_date.
CREATE TABLE IF NOT EXISTS symbol_data_coverage (
  ticker TEXT NOT NULL,
  resolution TEXT NOT NULL,
  first_date TEXT,
  last_date TEXT,
  live_date TEXT,
  last_fetched_at TEXT,
  source TEXT,
  sessions INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (ticker, resolution)
);

PRAGMA user_version = 2;
