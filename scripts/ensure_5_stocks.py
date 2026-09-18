#!/usr/bin/env python3
"""Call AlgoCraft HTTP API to ensure 5 DefaultRouter stocks are in RocksDB+SQLite.

Does NOT talk to Upstox directly — AlgoCraft's /market-data/ensure does that.

Usage:
  1. Start server (loads ~/.config/upstox/config.json if present):
       ./build/algocraft_engine serve data/1min 8080
  2. Run this:
       python3 scripts/ensure_5_stocks.py [host] [port]

Default window: 2026-08-18 .. 2026-09-11 (28 calendar days, 1-min).
Tickers (first 5 of DefaultRouter universe):
  RELIANCE, INFY, TCS, HDFCBANK, ICICIBANK
"""

from __future__ import annotations

import json
import socket
import sys
import uuid

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8080

TICKERS = ["RELIANCE", "INFY", "TCS", "HDFCBANK", "ICICIBANK"]
FROM = "2026-08-18"
TO = "2026-09-11"


def http(method: str, path: str, body: str | None = None, token: str | None = None):
    payload = body.encode() if body is not None else b""
    headers = [
        f"{method} {path} HTTP/1.1",
        f"Host: {HOST}",
        "Connection: close",
    ]
    if token:
        headers.append(f"Authorization: Bearer {token}")
    if body is not None:
        headers.append("Content-Type: application/json")
        headers.append(f"Content-Length: {len(payload)}")
    req = ("\r\n".join(headers) + "\r\n\r\n").encode() + payload
    with socket.create_connection((HOST, PORT), timeout=600) as s:
        s.sendall(req)
        data = b""
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
    text = data.decode()
    status = int(text.split()[1])
    resp_body = text.split("\r\n\r\n", 1)[1] if "\r\n\r\n" in text else ""
    return status, resp_body


def main() -> int:
    user = f"fetch_{uuid.uuid4().hex[:8]}"
    st, body = http(
        "POST",
        "/auth/register",
        json.dumps({"username": user, "password": "secret12"}),
    )
    print("register", st)
    if st != 200:
        print(body)
        return 1
    token = json.loads(body)["token"]

    payload = json.dumps({"tickers": TICKERS, "from": FROM, "to": TO})
    print(f"ensure {TICKERS} {FROM}..{TO} via AlgoCraft @ {HOST}:{PORT}")
    st, body = http("POST", "/market-data/ensure", payload, token=token)
    print("ensure", st)
    print(body)
    return 0 if st == 200 else 1


if __name__ == "__main__":
    raise SystemExit(main())
