# Upstox API — C++ agent notes

Use this file in any new Cursor session before writing C++ against Upstox. This repo already has a working libcurl client: `include/finnhub.hpp`, `src/finnhub.cpp`, `src/main.cpp`, binary `./build/finnhub_sample`.

Official docs: https://upstox.com/developer/api-documentation/

## Secrets and config (do not violate)

- Live token lives **outside** the repo: `~/.config/upstox/config.json` (mode `600`).
- **Never** read, print, commit, or paste that file. `config.json` is in `.gitignore` and `.cursorignore`.
- Template only: `config.example.json`.
- Token field: `upstox_access_token`. Optional: `upstox_api_key`.
- Access tokens expire (typically daily). `401` with invalid-token errors means the user must refresh the token, not that the URL is wrong.
- HTTP auth: `Authorization: Bearer <access_token>` and `Accept: application/json`.
- Do **not** put the token in the query string (that was Finnhub). Upstox is header Bearer only.

Default URLs (also in `AppConfig`):

- REST quotes/profile: `https://api.upstox.com/v2`
- REST historical candles: `https://api.upstox.com/v3`
- Market WebSocket: `wss://api.upstox.com/v3/feed/market-data-feed` (v2 feed is gone)

## Build and run

```bash
cd /home/ramdev/Documents/codes/TRADING_PROJECTS/FinnHub_API_test
cmake -S . -B build && cmake --build build
./build/finnhub_sample quote "NSE_INDEX|Nifty 50"
./build/finnhub_sample candles "NSE_INDEX|Nifty 50" 2026-09-10 2026-09-11 days 1
```

CLI:

```text
finnhub_sample profile
finnhub_sample quote INSTRUMENT_KEY
finnhub_sample candles INSTRUMENT_KEY FROM TO UNIT [INTERVAL]
```

`FROM`/`TO` are `YYYY-MM-DD` (start, then end, inclusive). Quote keys that contain `|` must be shell-quoted.

UNIT: `minutes` | `hours` | `days` | `weeks` | `months` (aliases: `min`, `hour`, `day`, `week`, `month`).

INTERVAL: `1`–`300` minutes, `1`–`5` hours, `1` for days/weeks/months. Default `1`.

## Instrument keys (not Yahoo tickers)

Upstox does **not** accept `AAPL` or `RELIANCE` as the path/query symbol.

Format: `SEGMENT|id`

| Kind | Example key | Notes |
| --- | --- | --- |
| Index | `NSE_INDEX\|Nifty 50` | Also `NSE_INDEX\|Nifty Bank` |
| NSE equity | `NSE_EQ\|INE002A01018` | Reliance; ISIN after `\|` |
| Quote JSON name | `NSE_EQ:RELIANCE` | Response map key uses `:` not `\|` |

Instrument dump (look up `trading_symbol` → `instrument_key`):

https://assets.upstox.com/market-quote/instruments/exchange/complete.csv.gz

URL-encode `|` as `%7C` in path segments. This client uses `curl_easy_escape`.

## REST endpoints we use

### Historical candles (v3) — no static-IP lock in practice

```
GET https://api.upstox.com/v3/historical-candle/{instrument_key}/{unit}/{interval}/{to_date}/{from_date}
```

Path order is **to_date then from_date**. The C++ helper takes `(from, to)` and swaps them into the URL.

Verified working:

```bash
./build/finnhub_sample candles "NSE_INDEX|Nifty 50" 2026-09-10 2026-09-11 days 1
./build/finnhub_sample candles "NSE_INDEX|Nifty 50" 2026-09-11 2026-09-11 minutes 15
./build/finnhub_sample candles "NSE_INDEX|Nifty 50" 2026-09-11 2026-09-11 hours 1
./build/finnhub_sample candles "NSE_EQ|INE002A01018" 2026-09-10 2026-09-11 days 1
```

Response:

```json
{"status":"success","data":{"candles":[
  ["2026-09-11T00:00:00+05:30", 23270.3, 23448.1, 23231.4, 23398.1, 0, 0]
]}}
```

Each candle array:

`[timestamp, open, high, low, close, volume, oi]`

- Newest first.
- Times are IST (`+05:30`).
- Index volume/oi are `0`. Equity volume is share count; cash equity oi is often `0`.
- Daily bars use `T00:00:00+05:30`. Intraday uses session times (NSE cash **09:15–15:30** IST). Hour bars are aligned to **09:15**, not 09:00.
- Last 15-min/hour bar of the day should close at the same price as the daily close.

History caps (do not fill these when testing):

| Unit | Interval | Rough max window |
| --- | --- | --- |
| minutes | 1–15 | ~1 month |
| minutes | 16–300 | ~1 quarter |
| hours | 1–5 | ~1 quarter |
| days | 1 | ~1 decade |
| weeks / months | 1 | no documented short cap |

Intraday-only (today): `GET /v3/historical-candle/intraday/{instrument_key}/{unit}/{interval}` — not wrapped yet.

### Full market quote (v2)

```
GET https://api.upstox.com/v2/market-quote/quotes?instrument_key=NSE_EQ%7CINE002A01018
```

Worked from this machine (not IP-locked). After hours, OHLC matches the last completed daily candle.

Useful fields: `last_price`, `ohlc.{open,high,low,close}`, `net_change`, `volume`, `average_price` (VWAP), `symbol`, `instrument_token`, `depth.buy/sell` (bid/ask ladders; empty on indices), `lower_circuit_limit` / `upper_circuit_limit`, `timestamp` (response time, not always last trade).

### User profile (v2) — static IP

```
GET https://api.upstox.com/v2/user/profile
```

From a non-whitelisted IP: HTTP 401 `UDAPI1221` (“permitted only when requested from the static IP configured in your account”). Same class of APIs: funds, orders, portfolio. Do not treat this as a bad Bearer header if quote/candles work.

## WebSocket (live) — v3 only

- v2 `wss://api.upstox.com/v2/feed/market-data-feed` is obsolete.
- v3: protobuf, not JSON ticks. Proto: https://assets.upstox.com/feed/market-data-feed/v3/MarketDataFeed.proto
- Typical flow: REST authorize `GET /v3/feed/market-data-feed/authorize`, then connect to the returned `wss` URL with Bearer.
- Subscribe JSON (text control frame), then binary protobuf payloads:

```json
{"guid":"...","method":"sub","data":{"mode":"ltpc","instrumentKeys":["NSE_INDEX|Nifty 50"]}}
```

Modes: `ltpc`, `full`, `full_d30`, `option_greeks`. Methods: `sub`, `unsub`, `change_mode`.

This sample binary does **not** implement the feed yet. New C++ should use libcurl `CURLOPT_CONNECT_ONLY=2` (websocket) plus protobuf decode, or an Upstox SDK. Do not copy the old Finnhub JSON `{"type":"subscribe","symbol":"AAPL"}` protocol.

## C++ client conventions in this repo

- HTTP: libcurl, C++17, CMake `CURL::libcurl`.
- `UpstoxClient::get(base_url, path, query)` builds `base + path + ?k=v` and sends Bearer.
- `candles()` encodes the instrument key, then  
  `/historical-candle/{key}/{unit}/{interval}/{to}/{from}` on **v3**.
- `quote()` / `profile()` hit **v2**.
- Load config with a tiny string-field parser (not nlohmann). Keep it working if you add keys: `history_base_url`, `ws_url`.
- Quote `|` in argv: `"NSE_INDEX|Nifty 50"`.
- Do not log Authorization headers or the token.

## Errors seen in the wild

| Code | Meaning |
| --- | --- |
| HTTP 401 `UDAPI1221` | Static IP required (profile/orders), not market history |
| HTTP 401 invalid token | Expired access token |
| `UDAPI1021` / `UDAPI100011` | Bad or unknown instrument key |
| `UDAPI1146` / `UDAPI1147` | Bad unit/interval for v3 candles |

## Testing rules (mandatory)

When the agent tests Upstox:

- Use **1–2 trading days**, or one session of `minutes 15` / `hours 1`.
- Do not pull months of 1-minute data unless the user explicitly asks.
- Do not loop many symbols. One success is enough.

## Adding a new REST operation

1. Confirm v2 vs v3 from current Upstox docs (history and WS are v3; many account APIs remain v2).
2. Add a method on `UpstoxClient` that calls `get()` with the right base URL.
3. Wire a `main.cpp` subcommand. Prefer CLI args over hardcoded dates.
4. Smoke-test with a **short** range and a known key (`NSE_INDEX|Nifty 50` or `NSE_EQ|INE002A01018`).
5. Never open `~/.config/upstox/config.json` in the editor/agent.
