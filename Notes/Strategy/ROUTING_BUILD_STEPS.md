# Routing + Virtual WS — build steps (half-hour)

## Done when
1. `top15_week_router` registered: 1-week eval → top 15 containers → equal capital → trade `anchor_date`
2. Non-live proven via real `POST /workbooks/<id>/runs/start` API
3. `Virtual_Websockets` loops 1m CSV as feed; AlgoCraft can switch feed via env
4. RAM/CPU capped (`-j2`, small universe, paced WS)

## Steps
- [x] S1 Architecture notes
- [x] S2 Implement `top15_week_router` + register + CMake + `/routing-algos`
- [x] S3 Build engine, start serve, API hist replay test on 2026-09-25 → `ROUTING_TOP15.md`
- [x] S4 Virtual_Websockets: CSV symlinks + looping WS (`server.py`, `.venv`)
- [x] S5 Env switch `ALGOCRAFT_LIVE_FEED=virtual|upstox` + `VirtualLiveFeed`
- [x] S6 Document API usage in `Notes/Strategy/ROUTING_TOP15.md`

## API (frontend)
```json
POST /workbooks/<id>/runs/start
{"router":"top15_week_router","anchor_date":"YYYY-MM-DD","capital_paise":100000000}
```
- past NSE day → `hist_replay`
- IST today → live (`ALGOCRAFT_LIVE_FEED`)

## Resource caps
- Build `-j2`
- Router: 20 tickers × 3 strategies = 60 evals; top 15 CREATE
- VW: `VW_INTERVAL_MS=200`, one process
