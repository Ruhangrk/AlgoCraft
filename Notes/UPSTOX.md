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
finnhub_sample ws INSTRUMENT_KEY [SECONDS] [MODE]
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
| minutes | 1–15 | **1 month max per request** |
| minutes | 16–300 | ~1 quarter |
| hours | 1–5 | ~1 quarter |
| days | 1 | ~1 decade |
| weeks / months | 1 | no documented short cap |

**Confirmed request rules (write these down; they do not change per session):**

- **1-minute candles:** maximum **~1 month window per HTTP request**. For AlgoCraft we use **28 calendar days**, not a full month and not 1 year.
- **One stock per request.** Historical candle URLs take a single `instrument_key`. You cannot club / batch symbols.
- **~30 API requests per minute** (rough). Stay under that. Three stocks × one 28-day window = **3 requests**.
- Longer 1-min history means **sequential** requests with different date windows, spaced to stay under ~30/min. Do not parallel-blast.

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

Use this section in other Cursor sessions. Do **not** reuse Finnhub (`wss://ws.finnhub.io`, `{"type":"subscribe","symbol":"AAPL"}`).

### Endpoints

- **Obsolete:** `wss://api.upstox.com/v2/feed/market-data-feed`
- **Connect (redirects):** `wss://api.upstox.com/v3/feed/market-data-feed`
- **Authorize (preferred):** `GET https://api.upstox.com/v3/feed/market-data-feed/authorize`
- **Proto:** https://assets.upstox.com/feed/market-data-feed/v3/MarketDataFeed.proto  
  Package: `com.upstox.marketdatafeederv3udapi.rpc.proto`. Top message: `FeedResponse`.

Docs: https://upstox.com/developer/api-documentation/get-market-data-feed-authorize-v3/ and https://upstox.com/developer/api-documentation/v3/get-market-data-feed/

### How to connect (working sequence)

1. **REST authorize** with the same Bearer token as quotes (not in the query string):

   ```
   GET /v3/feed/market-data-feed/authorize
   Authorization: Bearer <access_token>
   Accept: application/json
   ```

   Body includes `data.authorized_redirect_uri` (`wss://…?requestId=…&code=…`). That URL is **one-time**. Do not print the token. Printing the redirect URI leaks a short-lived `code`; avoid logging it.

2. **Open WebSocket** on `authorized_redirect_uri`. Expect HTTP **101**. Do **not** start the handshake on `/v3/feed/market-data-feed` if the client cannot follow a **307** (many native WebSockets reject 307; authorize-first avoids that).

   Handshake headers if you connect to the public v3 URL instead of the redirect URI:

   ```
   Authorization: Bearer <access_token>
   Accept: */*
   ```

3. **After 101, subscribe.** JSON is UTF-8, sent as a **binary** WebSocket frame (`opcode=2` / `CURLWS_BINARY`). A **text** frame is ignored: you only get `market_info` and no ticks.

   ```json
   {"guid":"cpp-sample","method":"sub","data":{"mode":"ltpc","instrumentKeys":["NSE_INDEX|Nifty 50"]}}
   ```

   Methods: `sub`, `unsub`, `change_mode`.  
   Modes: `ltpc`, `full`, `full_d30`, `option_greeks` (JSON still says `full`; proto enum uses `full_d5` for that mode).

4. **Incoming data is protobuf**, not JSON. First frame is often `FeedResponse.type = market_info` with empty `feeds`. Then `initial_feed` and `live_feed` with `map<string, Feed> feeds`. Idle connections get WebSocket **ping**; reply **pong** (libcurl does this unless `CURLWS_NOAUTOPONG`).

5. **Decode** `FeedResponse`: `type` (0/1/2), `feeds` map key = instrument key, `Feed.ltpc` (`ltp` double, `ltt`/`ltq` varint, `cp` double) or nested `fullFeed` / `firstLevelWithGreeks`. V3 payloads are **not** gzip (v2 was).

### This repo (C++)

`UpstoxClient::market_ws()`: authorize via `history_base_url`, libcurl `CURLOPT_CONNECT_ONLY=2`, `curl_ws_send` binary subscribe, `curl_ws_recv` + protobuf LTPC dump.

```bash
./build/finnhub_sample ws "NSE_INDEX|Nifty 50" 8 ltpc
./build/finnhub_sample ws "NSE_EQ|INE002A01018" 8 ltpc
```

`ws INSTRUMENT_KEY [SECONDS] [MODE]`. Quote `|`. One symbol, a few seconds is enough for a smoke test.

### Pitfalls

| Symptom | Cause |
| --- | --- |
| Handshake not 101 / 307 | Client followed redirect poorly; use authorize URI |
| Only `market_info`, empty `feeds` | Subscribe sent as **text**, or never sent |
| JSON ticks / `type:subscribe` | Finnhub protocol; wrong |
| 401 on authorize | Expired `upstox_access_token` |
| No ticks after hours | Normal; `initial_feed` may still have last LTP |

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
- Never start a 28-day (or longer) 1-min pull until the user says to go.

**AlgoCraft Phase 2 (decided 2026-09-15):** no Upstox `DataProvider` in this repo yet. Fetch 28 days of 1-min bars **outside** AlgoCraft, write CSV, backtest via `CsvProvider`. Stocks: RELIANCE, INFY, TCS. Wait for explicit approval before fetching.

**AlgoCraft Phase 4 (approved 2026-09-16):** 7 more NSE cash names, **15 calendar days** of 1-min (`2026-08-28`–`2026-09-11`): HDFCBANK, ICICIBANK, SBIN, BHARTIARTL, ITC, LT, HINDUNILVR. Still CSV-only in AlgoCraft; 7 sequential requests, no batching.

## Adding a new REST operation

1. Confirm v2 vs v3 from current Upstox docs (history and WS are v3; many account APIs remain v2).
2. Add a method on `UpstoxClient` that calls `get()` with the right base URL.
3. Wire a `main.cpp` subcommand. Prefer CLI args over hardcoded dates.
4. Smoke-test with a **short** range and a known key (`NSE_INDEX|Nifty 50` or `NSE_EQ|INE002A01018`).
5. Never open `~/.config/upstox/config.json` in the editor/agent.
