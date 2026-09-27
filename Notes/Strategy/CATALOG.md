# Strategy catalog (funnel test batch)

Fixed-parameter long-only MIS strategies. No hyperparameter sweeps.

| Name | Idea |
|------|------|
| `dip5_mean_revert` | Buy ≤−0.25% vs prior-5 avg; exit at avg / −0.50% stop |
| `three_red_bounce` | Buy after 3 red bars; exit 2 green or −0.40% from entry |
| `bull_engulf_long` | Bullish engulfing; TP +0.45% / stop −0.30% |
| `ema_trend_pullback` | EMA9>EMA21; buy pullback to EMA9; exit cross-down / −0.35% |
| `vwap_reclaim_long` | Cross up through VWAP; exit cross down |
| `open_dump_fade` | Fade open dump (−0.30% in first 20m); exit by noon / TP/stop |
| `five_bar_high_break` | Break prior-5 high; exit prior-3 low / +0.50% TP |
| `compression_break` | 4 tiny bars then +0.20% break; TP/stop |
| `two_green_thrust` | 2 greens, 2nd body ≥1.5×1st; exit on red / −0.35% |
| `reliance_prev5_avg_break` | Existing momentum (baseline) |

Also compared in funnel: none of the older strategies unless listed above.

## Funnel
1. **1 week** (2026-09-19 → 09-25) — all strategy×stock  
2. **2 weeks** (2026-09-12 → 09-25) — only week winners (PnL > 0)  
3. **1 month** (2026-09-01 → 09-30) — only 2-week winners  
4. **2 months** (2026-08-01 → 09-30) — only month winners  

Stocks: RELIANCE, INFY, TCS, HDFCBANK, ITC, SBIN. Capital ₹10L each run.
