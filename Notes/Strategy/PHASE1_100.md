# Phase 1 — 100 stocks × new pattern/volatility strategies

Window: **2026-09-19 → 2026-09-25** (1 week). Capital ₹10L.
Strategies: 7. Stocks: 100. Completed runs: 693. Fails: 7.
Elapsed: 3.5 min.

## Universe

RELIANCE, INFY, TCS, HDFCBANK, ICICIBANK, SBIN, BHARTIARTL, ITC, LT, HINDUNILVR, AXISBANK, KOTAKBANK, BAJFINANCE, MARUTI, SUNPHARMA, TITAN, ASIANPAINT, ULTRACEMCO, NTPC, POWERGRID, ONGC, COALINDIA, WIPRO, HCLTECH, TECHM, ADANIENT, ADANIPORTS, JSWSTEEL, TATASTEEL, HINDALCO, CIPLA, DRREDDY, APOLLOHOSP, DIVISLAB, NESTLEIND, BRITANNIA, HEROMOTOCO, EICHERMOT, BAJAJ-AUTO, INDUSINDBK, BEL, TRENT, PIDILITIND, GODREJCP, DABUR, HAVELLS, VOLTAS, SIEMENS, ABB, HAL, GRASIM, SHREECEM, AMBUJACEM, DLF, GODREJPROP, BANKBARODA, PNB, CANBK, UNIONBANK, PFC, IRCTC, PAYTM, MOTHERSON, BOSCHLTD, MRF, PAGEIND, PERSISTENT, COFORGE, OFSS, MPHASIS, TATACONSUM, TATAPOWER, TVSMOTOR, CHOLAFIN, BAJAJFINSV, HDFCLIFE, SBILIFE, ICICIPRULI, ICICIGI, PIIND, SRF, DEEPAKNTR, NAUKRI, INDHOTEL, JUBLFOOD, DMART, AUROPHARMA, BIOCON, LUPIN, TORNTPHARM, ALKEM, VEDL, JINDALSTEL, SAIL, NATIONALUM, PETRONET, GAIL, IGL, BPCL, IOC

## Strategy ideas (distinct from prior batch)

| Strategy | Family | Rule sketch |
|----------|--------|-------------|
| hammer_reversal | Candlestick | Hammer after ≥2 reds; TP/stop |
| piercing_line_long | Candlestick | Piercing line (relaxed intraday) |
| bull_harami_break | Candlestick | Harami → buy break of mother high |
| morning_star_long | Candlestick | 3-bar morning star |
| three_white_soldiers | Candlestick | 3 strong ascending greens |
| atr_expansion_long | Volatility | TR ≥ 1.8×ATR14 + green; ATR exits |
| nr7_breakout | Volatility | NR7 compression then upside break |

## Winners (PnL > 0): 103 / 693

### hammer_reversal — 37 winners

| Ticker | PnL ₹ | Fills | Fees ₹ |
|--------|------:|------:|-------:|
| ONGC | 11682.42 | 22 | 1708.58 |
| PIIND | 6879.58 | 26 | 2009.86 |
| DEEPAKNTR | 6819.44 | 32 | 2475.60 |
| DIVISLAB | 6029.59 | 24 | 1842.85 |
| JUBLFOOD | 5723.77 | 20 | 1546.25 |
| COALINDIA | 4665.25 | 12 | 928.55 |
| PAGEIND | 3586.74 | 14 | 1061.86 |
| TECHM | 3544.92 | 26 | 2009.92 |
| JINDALSTEL | 3531.77 | 4 | 309.75 |
| BOSCHLTD | 3449.08 | 16 | 1134.80 |
| CANBK | 3346.16 | 20 | 1546.73 |
| WIPRO | 2895.08 | 26 | 2010.16 |
| SAIL | 2805.36 | 8 | 619.73 |
| RELIANCE | 2779.81 | 20 | 1545.75 |
| INFY | 2562.52 | 16 | 1235.26 |
| ITC | 2341.52 | 22 | 1700.11 |
| MPHASIS | 2256.93 | 24 | 1852.03 |
| TORNTPHARM | 1971.31 | 34 | 2607.81 |
| HDFCBANK | 1932.48 | 18 | 1389.16 |
| NATIONALUM | 1646.07 | 14 | 1080.05 |
| TATAPOWER | 1326.22 | 16 | 1235.82 |
| BIOCON | 1321.64 | 16 | 1236.42 |
| UNIONBANK | 1227.38 | 22 | 1697.02 |
| MRF | 1192.83 | 6 | 456.99 |
| DRREDDY | 930.29 | 12 | 926.65 |
| BANKBARODA | 903.04 | 14 | 1081.69 |
| PFC | 855.88 | 16 | 1235.61 |
| GAIL | 754.91 | 20 | 1543.13 |
| ADANIPORTS | 671.96 | 24 | 1852.14 |
| LUPIN | 488.84 | 8 | 617.22 |
| HEROMOTOCO | 484.71 | 14 | 1076.45 |
| TITAN | 473.22 | 14 | 1077.94 |
| TATASTEEL | 443.10 | 14 | 1080.84 |
| GODREJCP | 430.64 | 16 | 1235.59 |
| PNB | 390.76 | 12 | 926.26 |
| DLF | 259.52 | 16 | 1235.91 |
| SIEMENS | 136.18 | 16 | 1229.10 |

### piercing_line_long — 22 winners

| Ticker | PnL ₹ | Fills | Fees ₹ |
|--------|------:|------:|-------:|
| ONGC | 12129.27 | 18 | 1396.45 |
| MPHASIS | 9547.80 | 34 | 2633.84 |
| UNIONBANK | 8617.91 | 26 | 2017.03 |
| ICICIGI | 8042.96 | 30 | 2327.18 |
| VOLTAS | 6198.67 | 34 | 2623.83 |
| TCS | 5899.51 | 32 | 2473.65 |
| LUPIN | 5448.87 | 28 | 2163.87 |
| SAIL | 5320.21 | 44 | 3394.59 |
| COALINDIA | 5197.14 | 14 | 1082.85 |
| CANBK | 3996.56 | 22 | 1702.23 |
| DIVISLAB | 3892.57 | 26 | 1992.17 |
| MOTHERSON | 3434.57 | 26 | 2008.94 |
| PNB | 2990.73 | 24 | 1854.22 |
| DLF | 2630.43 | 26 | 2011.33 |
| HCLTECH | 2391.26 | 28 | 2161.68 |
| WIPRO | 2020.15 | 18 | 1391.42 |
| TECHM | 1349.48 | 30 | 2315.12 |
| CIPLA | 816.67 | 30 | 2311.23 |
| TITAN | 784.70 | 28 | 2157.54 |
| SBIN | 464.59 | 16 | 1233.91 |
| HDFCBANK | 396.55 | 30 | 2310.40 |
| TATAPOWER | 28.53 | 14 | 1079.94 |

### bull_harami_break — 10 winners

| Ticker | PnL ₹ | Fills | Fees ₹ |
|--------|------:|------:|-------:|
| HCLTECH | 5761.28 | 62 | 4803.22 |
| CANBK | 3606.36 | 38 | 2938.51 |
| ICICIGI | 3489.30 | 44 | 3393.22 |
| HDFCLIFE | 1144.53 | 50 | 3851.82 |
| TORNTPHARM | 752.26 | 46 | 3533.70 |
| MPHASIS | 355.34 | 48 | 3702.80 |
| TATASTEEL | 264.53 | 58 | 4473.02 |
| PIIND | 226.55 | 54 | 4156.91 |
| SIEMENS | 34.62 | 38 | 2920.08 |
| SAIL | 21.65 | 30 | 2313.96 |

### morning_star_long — 22 winners

| Ticker | PnL ₹ | Fills | Fees ₹ |
|--------|------:|------:|-------:|
| UNIONBANK | 14681.17 | 36 | 2792.22 |
| ONGC | 9386.20 | 18 | 1396.28 |
| ICICIGI | 8313.02 | 40 | 3095.72 |
| TECHM | 6894.73 | 28 | 2167.15 |
| HDFCLIFE | 6040.31 | 42 | 3235.46 |
| VEDL | 5460.08 | 32 | 2470.77 |
| NATIONALUM | 5202.66 | 32 | 2471.69 |
| TITAN | 4854.72 | 44 | 3389.18 |
| COALINDIA | 4582.32 | 28 | 2164.60 |
| MPHASIS | 4351.54 | 42 | 3239.12 |
| TCS | 3505.14 | 36 | 2777.88 |
| PAGEIND | 3218.71 | 36 | 2727.69 |
| LUPIN | 2704.00 | 30 | 2314.36 |
| CANBK | 2602.81 | 22 | 1701.69 |
| DLF | 2472.34 | 42 | 3245.54 |
| SBILIFE | 2452.26 | 38 | 2934.44 |
| HCLTECH | 1997.75 | 48 | 3713.85 |
| DEEPAKNTR | 1639.08 | 36 | 2774.08 |
| TATASTEEL | 988.00 | 50 | 3859.67 |
| ITC | 854.51 | 32 | 2470.24 |
| SIEMENS | 127.65 | 34 | 2608.19 |
| TORNTPHARM | 74.04 | 40 | 3074.92 |

### three_white_soldiers — 8 winners

| Ticker | PnL ₹ | Fills | Fees ₹ |
|--------|------:|------:|-------:|
| SBILIFE | 4242.75 | 24 | 1854.25 |
| TCS | 2265.11 | 18 | 1387.67 |
| HCLTECH | 1409.90 | 22 | 1697.92 |
| MOTHERSON | 1053.37 | 24 | 1851.53 |
| ALKEM | 743.40 | 10 | 768.08 |
| HAVELLS | 454.85 | 6 | 463.03 |
| NESTLEIND | 426.94 | 10 | 771.84 |
| DMART | 30.39 | 34 | 2614.65 |

### atr_expansion_long — 3 winners

| Ticker | PnL ₹ | Fills | Fees ₹ |
|--------|------:|------:|-------:|
| JUBLFOOD | 2115.76 | 90 | 6947.08 |
| DIVISLAB | 1787.39 | 102 | 7814.29 |
| AUROPHARMA | 245.41 | 104 | 8001.39 |

### nr7_breakout — 1 winners

| Ticker | PnL ₹ | Fills | Fees ₹ |
|--------|------:|------:|-------:|
| HDFCLIFE | 4439.91 | 168 | 12976.33 |

## Top 30 pairs by PnL

| Rank | Strategy | Ticker | PnL ₹ | Fills |
|-----:|----------|--------|------:|------:|
| 1 | morning_star_long | UNIONBANK | 14681.17 | 36 |
| 2 | piercing_line_long | ONGC | 12129.27 | 18 |
| 3 | hammer_reversal | ONGC | 11682.42 | 22 |
| 4 | piercing_line_long | MPHASIS | 9547.80 | 34 |
| 5 | morning_star_long | ONGC | 9386.20 | 18 |
| 6 | piercing_line_long | UNIONBANK | 8617.91 | 26 |
| 7 | morning_star_long | ICICIGI | 8313.02 | 40 |
| 8 | piercing_line_long | ICICIGI | 8042.96 | 30 |
| 9 | morning_star_long | TECHM | 6894.73 | 28 |
| 10 | hammer_reversal | PIIND | 6879.58 | 26 |
| 11 | hammer_reversal | DEEPAKNTR | 6819.44 | 32 |
| 12 | piercing_line_long | VOLTAS | 6198.67 | 34 |
| 13 | morning_star_long | HDFCLIFE | 6040.31 | 42 |
| 14 | hammer_reversal | DIVISLAB | 6029.59 | 24 |
| 15 | piercing_line_long | TCS | 5899.51 | 32 |
| 16 | bull_harami_break | HCLTECH | 5761.28 | 62 |
| 17 | hammer_reversal | JUBLFOOD | 5723.77 | 20 |
| 18 | morning_star_long | VEDL | 5460.08 | 32 |
| 19 | piercing_line_long | LUPIN | 5448.87 | 28 |
| 20 | piercing_line_long | SAIL | 5320.21 | 44 |
| 21 | morning_star_long | NATIONALUM | 5202.66 | 32 |
| 22 | piercing_line_long | COALINDIA | 5197.14 | 14 |
| 23 | morning_star_long | TITAN | 4854.72 | 44 |
| 24 | hammer_reversal | COALINDIA | 4665.25 | 12 |
| 25 | morning_star_long | COALINDIA | 4582.32 | 28 |
| 26 | nr7_breakout | HDFCLIFE | 4439.91 | 168 |
| 27 | morning_star_long | MPHASIS | 4351.54 | 42 |
| 28 | three_white_soldiers | SBILIFE | 4242.75 | 24 |
| 29 | piercing_line_long | CANBK | 3996.56 | 22 |
| 30 | piercing_line_long | DIVISLAB | 3892.57 | 26 |

## Strategy scoreboard (sum PnL across 100 names)

| Strategy | Wins | Sum PnL ₹ | Avg win ₹ |
|----------|-----:|----------:|----------:|
| hammer_reversal | 37 | -203335.40 | 2506.51 |
| piercing_line_long | 22 | -326332.07 | 4163.60 |
| bull_harami_break | 10 | -547270.55 | 1565.64 |
| morning_star_long | 22 | -507285.83 | 4200.14 |
| three_white_soldiers | 8 | -240292.50 | 1328.34 |
| atr_expansion_long | 3 | -1038586.63 | 1382.85 |
| nr7_breakout | 1 | -1654647.79 | 4439.91 |

## Failures (7)

- hammer_reversal / CHOLAFIN: HTTP Error 500: Internal Server Error
- piercing_line_long / CHOLAFIN: HTTP Error 500: Internal Server Error
- bull_harami_break / CHOLAFIN: HTTP Error 500: Internal Server Error
- morning_star_long / CHOLAFIN: HTTP Error 500: Internal Server Error
- three_white_soldiers / CHOLAFIN: HTTP Error 500: Internal Server Error
- atr_expansion_long / CHOLAFIN: HTTP Error 500: Internal Server Error
- nr7_breakout / CHOLAFIN: HTTP Error 500: Internal Server Error
