#!/usr/bin/env python3
"""Decode one AlgoCraft RocksDB session blob to readable OHLCV.

Usage (from repo root):
  python3 scripts/dump_rocks_session.py RELIANCE|1m|2026-09-11
  python3 scripts/dump_rocks_session.py RELIANCE 2026-09-11
  python3 scripts/dump_rocks_session.py RELIANCE 2026-09-11 --all
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
from pathlib import Path


def load_blob(db: Path, key: str) -> bytes:
    out = subprocess.check_output(
        ["ldb", f"--db={db}", "get", key, "--value_hex"],
        text=True,
    ).strip()
    if not out or out == "Key not found":
        raise SystemExit(f"key not found: {key}")
    if out.startswith("0x"):
        out = out[2:]
    return bytes.fromhex(out)


def decode(blob: bytes) -> list[tuple[int, float, float, float, float, int]]:
    if len(blob) < 12 or blob[:4] != b"ACB1":
        raise SystemExit(f"bad magic: {blob[:4]!r}")
    version, count = struct.unpack_from("<II", blob, 4)
    if version != 1:
        raise SystemExit(f"unsupported version: {version}")
    expect = 12 + count * 48
    if len(blob) != expect:
        raise SystemExit(f"size mismatch: got {len(blob)} expected {expect}")
    rows = []
    for i in range(count):
        ts, o, h, l, c, v = struct.unpack_from("<qqqqqq", blob, 12 + i * 48)
        rows.append((ts, o / 100.0, h / 100.0, l / 100.0, c / 100.0, v))
    return rows


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("key_or_ticker", help="Full key RELIANCE|1m|YYYY-MM-DD or ticker")
    p.add_argument("date", nargs="?", help="YYYY-MM-DD if ticker given")
    p.add_argument("--db", default="data/bars", help="RocksDB directory")
    p.add_argument("--all", action="store_true", help="Print every bar (default: head+tail)")
    p.add_argument("-n", type=int, default=5, help="Bars to show at head/tail")
    args = p.parse_args()

    if "|" in args.key_or_ticker:
        key = args.key_or_ticker
    else:
        if not args.date:
            p.error("date required when ticker is given")
        key = f"{args.key_or_ticker}|1m|{args.date}"

    blob = load_blob(Path(args.db), key)
    rows = decode(blob)
    print(f"key={key} bars={len(rows)}")
    print("timestamp_ns open high low close volume")

    if args.all or len(rows) <= 2 * args.n:
        show = rows
    else:
        show = rows[: args.n] + [("...",)] + rows[-args.n :]  # type: ignore

    for row in show:
        if row[0] == "...":
            print("...")
            continue
        ts, o, h, l, c, v = row
        print(f"{ts} {o:.2f} {h:.2f} {l:.2f} {c:.2f} {v}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
