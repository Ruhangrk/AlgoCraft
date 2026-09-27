# top15_week_router — API hist_replay test

**anchor_date:** 2026-09-25 (mode=`hist_replay`)
**capital:** ₹10,00,000
**selected:** 15  **skipped:** 45  **fills:** 72
**returned_paise:** 99783556 (₹997835.56)
**run_id:** 11
**eval_sessions:** 5 (1 week lookback before anchor)

## Frontend request

```http
POST /workbooks/<id>/runs/start
Authorization: Bearer <token>
Content-Type: application/json

{
  "router": "top15_week_router",
  "anchor_date": "2026-09-25",
  "capital_paise": 100000000
}
```

Equal capital split across the 15 CREATE containers; trade tape = full anchor session.

## Top 15 containers created

| Strategy | Stock | Week eval PnL ₹ |
|----------|-------|----------------:|
| hammer_reversal | ONGC | 14992.71 |
| morning_star_long | VEDL | 14377.81 |
| piercing_line_long | ONGC | 13720.64 |
| morning_star_long | ONGC | 13145.65 |
| morning_star_long | UNIONBANK | 11158.38 |
| piercing_line_long | VEDL | 9315.06 |
| hammer_reversal | PAGEIND | 7748.49 |
| morning_star_long | CANBK | 7596.06 |
| piercing_line_long | ICICIGI | 7517.14 |
| hammer_reversal | DIVISLAB | 7421.46 |
| piercing_line_long | MPHASIS | 6961.30 |
| piercing_line_long | CANBK | 6641.54 |
| hammer_reversal | CANBK | 5926.32 |
| morning_star_long | TECHM | 5920.60 |
| piercing_line_long | UNIONBANK | 4862.98 |

## Live switch

- Hist / past `anchor_date` → `hist_replay` (this test).
- `anchor_date` = IST today → live feed (`ALGOCRAFT_LIVE_FEED=upstox|virtual`).

See `Virtual_Websockets/README.md`.
