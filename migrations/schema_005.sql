-- S1a: NSE EQ instrument catalog (ingested from Upstox complete.csv.gz in S1b).
CREATE TABLE IF NOT EXISTS instruments (
  ticker            TEXT    PRIMARY KEY,
  name              TEXT    NOT NULL DEFAULT '',
  isin              TEXT    NOT NULL DEFAULT '',
  exchange          TEXT    NOT NULL DEFAULT 'NSE',
  segment           TEXT    NOT NULL DEFAULT 'EQ',
  lot_size          INTEGER NOT NULL DEFAULT 1,
  tick_size_paise   INTEGER NOT NULL DEFAULT 5,
  instrument_key    TEXT    NOT NULL DEFAULT '',
  active            INTEGER NOT NULL DEFAULT 1,
  updated_at        TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE INDEX IF NOT EXISTS idx_instruments_name ON instruments(name);
CREATE INDEX IF NOT EXISTS idx_instruments_active ON instruments(active);
CREATE INDEX IF NOT EXISTS idx_instruments_instrument_key ON instruments(instrument_key);

PRAGMA user_version = 5;
