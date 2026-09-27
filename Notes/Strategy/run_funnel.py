#!/usr/bin/env python3
"""Profit funnel: week → 2week → month → aug-sept. Writes RESULTS.md."""
import urllib.request, json, datetime, time, pathlib

BASE = "http://127.0.0.1:8080"
OUT = pathlib.Path(__file__).resolve().parent
STRATEGIES = [
    "dip5_mean_revert",
    "three_red_bounce",
    "bull_engulf_long",
    "ema_trend_pullback",
    "vwap_reclaim_long",
    "open_dump_fade",
    "five_bar_high_break",
    "compression_break",
    "two_green_thrust",
    "reliance_prev5_avg_break",
]
TICKERS = ["RELIANCE", "INFY", "TCS", "HDFCBANK", "ITC", "SBIN"]
CAP = 10_00_000_00

def ist_ns(y, m, d, hh=0, mm=0):
    dt = datetime.datetime(y, m, d, hh, mm, tzinfo=datetime.timezone(datetime.timedelta(hours=5, minutes=30)))
    return int(dt.timestamp() * 1e9)

WINDOWS = {
    "week": (ist_ns(2026, 9, 19), ist_ns(2026, 9, 25, 23, 59)),
    "twoweek": (ist_ns(2026, 9, 12), ist_ns(2026, 9, 25, 23, 59)),
    "month": (ist_ns(2026, 9, 1), ist_ns(2026, 9, 30, 23, 59)),
    "aug_sept": (ist_ns(2026, 8, 1), ist_ns(2026, 9, 30, 23, 59)),
}

def req(method, path, body=None, token=None, timeout=300):
    H = {"Content-Type": "application/json"}
    if token:
        H["Authorization"] = f"Bearer {token}"
    data = None if body is None else json.dumps(body).encode()
    r = urllib.request.Request(BASE + path, data=data, headers=H, method=method)
    with urllib.request.urlopen(r, timeout=timeout) as resp:
        return json.loads(resp.read().decode())

def run_one(token, wid, strategy, ticker, window):
    fr, to = WINDOWS[window]
    out = req(
        "POST",
        f"/workbooks/{wid}/backtests/start",
        {
            "ticker": ticker,
            "strategy": strategy,
            "capital_paise": CAP,
            "from_ns": fr,
            "to_ns": to,
        },
        token,
    )
    return {
        "strategy": strategy,
        "ticker": ticker,
        "window": window,
        "pnl_rs": out["pnl_paise"] / 100,
        "return_pct": out.get("return_pct"),
        "fills": out["fills"],
        "fees_rs": out["fees_paise"] / 100,
        "bars": out["bars"],
    }

def main():
    auth = req("POST", "/auth/login", {"username": "bt_agent", "password": "bt_agent_pass"})
    token = auth["token"]
    wb = req("POST", "/workbooks", {"name": "strategy_funnel", "capital_paise": CAP}, token)
    wid = wb["id"]
    print(f"workbook={wid}", flush=True)

    all_rows = []
    # Phase 1: week
    print("=== PHASE 1: 1 week ===", flush=True)
    week_winners = []
    for strat in STRATEGIES:
        for t in TICKERS:
            try:
                row = run_one(token, wid, strat, t, "week")
            except Exception as e:
                print(f"FAIL {strat} {t} week: {e}", flush=True)
                continue
            all_rows.append(row)
            mark = "WIN" if row["pnl_rs"] > 0 else "loss"
            print(f"  {mark:4} {strat:28} {t:10} ₹{row['pnl_rs']:9.2f} fills={row['fills']}", flush=True)
            if row["pnl_rs"] > 0:
                week_winners.append((strat, t))

    # Phase 2: 2-week
    print(f"\n=== PHASE 2: 2 weeks ({len(week_winners)} pairs) ===", flush=True)
    two_winners = []
    for strat, t in week_winners:
        try:
            row = run_one(token, wid, strat, t, "twoweek")
        except Exception as e:
            print(f"FAIL {strat} {t} twoweek: {e}", flush=True)
            continue
        all_rows.append(row)
        mark = "WIN" if row["pnl_rs"] > 0 else "loss"
        print(f"  {mark:4} {strat:28} {t:10} ₹{row['pnl_rs']:9.2f} fills={row['fills']}", flush=True)
        if row["pnl_rs"] > 0:
            two_winners.append((strat, t))

    # Phase 3: month
    print(f"\n=== PHASE 3: September ({len(two_winners)} pairs) ===", flush=True)
    month_winners = []
    for strat, t in two_winners:
        try:
            row = run_one(token, wid, strat, t, "month")
        except Exception as e:
            print(f"FAIL {strat} {t} month: {e}", flush=True)
            continue
        all_rows.append(row)
        mark = "WIN" if row["pnl_rs"] > 0 else "loss"
        print(f"  {mark:4} {strat:28} {t:10} ₹{row['pnl_rs']:9.2f} fills={row['fills']}", flush=True)
        if row["pnl_rs"] > 0:
            month_winners.append((strat, t))

    # Phase 4: aug-sept
    print(f"\n=== PHASE 4: Aug–Sep ({len(month_winners)} pairs) ===", flush=True)
    final = []
    for strat, t in month_winners:
        try:
            row = run_one(token, wid, strat, t, "aug_sept")
        except Exception as e:
            print(f"FAIL {strat} {t} aug_sept: {e}", flush=True)
            continue
        all_rows.append(row)
        mark = "WIN" if row["pnl_rs"] > 0 else "loss"
        print(f"  {mark:4} {strat:28} {t:10} ₹{row['pnl_rs']:9.2f} fills={row['fills']}", flush=True)
        if row["pnl_rs"] > 0:
            final.append(row)

    (OUT / "funnel_raw.json").write_text(json.dumps(all_rows, indent=2))

    lines = [
        "# Strategy funnel results",
        "",
        f"Generated: {datetime.datetime.now().isoformat(timespec='seconds')}",
        "",
        "Capital ₹10,00,000 per run. Advance rule: **PnL > 0** to next window.",
        "",
        "## Windows",
        "- week: 2026-09-19 → 09-25",
        "- twoweek: 2026-09-12 → 09-25",
        "- month: 2026-09-01 → 09-30",
        "- aug_sept: 2026-08-01 → 09-30",
        "",
        f"## Phase 1 — 1 week ({len(STRATEGIES)}×{len(TICKERS)} = {len(STRATEGIES)*len(TICKERS)} runs)",
        "",
        "| Strategy | Ticker | PnL ₹ | Fills | Fees ₹ |",
        "|----------|--------|------:|------:|-------:|",
    ]
    for r in all_rows:
        if r["window"] != "week":
            continue
        lines.append(
            f"| {r['strategy']} | {r['ticker']} | {r['pnl_rs']:.2f} | {r['fills']} | {r['fees_rs']:.2f} |"
        )
    lines += ["", f"**Week winners:** {len(week_winners)} pairs", ""]

    lines += [
        f"## Phase 2 — 2 weeks ({len(week_winners)} pairs)",
        "",
        "| Strategy | Ticker | PnL ₹ | Fills | Fees ₹ |",
        "|----------|--------|------:|------:|-------:|",
    ]
    for r in all_rows:
        if r["window"] != "twoweek":
            continue
        lines.append(
            f"| {r['strategy']} | {r['ticker']} | {r['pnl_rs']:.2f} | {r['fills']} | {r['fees_rs']:.2f} |"
        )
    lines += ["", f"**2-week winners:** {len(two_winners)} pairs", ""]

    lines += [
        f"## Phase 3 — September ({len(two_winners)} pairs)",
        "",
        "| Strategy | Ticker | PnL ₹ | Fills | Fees ₹ |",
        "|----------|--------|------:|------:|-------:|",
    ]
    for r in all_rows:
        if r["window"] != "month":
            continue
        lines.append(
            f"| {r['strategy']} | {r['ticker']} | {r['pnl_rs']:.2f} | {r['fills']} | {r['fees_rs']:.2f} |"
        )
    lines += ["", f"**Month winners:** {len(month_winners)} pairs", ""]

    lines += [
        f"## Phase 4 — Aug–Sep ({len(month_winners)} pairs)",
        "",
        "| Strategy | Ticker | PnL ₹ | Return % | Fills | Fees ₹ |",
        "|----------|--------|------:|---------:|------:|-------:|",
    ]
    for r in all_rows:
        if r["window"] != "aug_sept":
            continue
        lines.append(
            f"| {r['strategy']} | {r['ticker']} | {r['pnl_rs']:.2f} | {r['return_pct']} | {r['fills']} | {r['fees_rs']:.2f} |"
        )

    lines += [
        "",
        "## Survivors (profit on all four windows)",
        "",
    ]
    if not final:
        lines.append("_None — no pair stayed profitable through Aug–Sep._")
    else:
        lines.append("| Strategy | Ticker | Aug–Sep PnL ₹ | Fills |")
        lines.append("|----------|--------|--------------:|------:|")
        for r in sorted(final, key=lambda x: -x["pnl_rs"]):
            lines.append(f"| {r['strategy']} | {r['ticker']} | {r['pnl_rs']:.2f} | {r['fills']} |")

    (OUT / "RESULTS.md").write_text("\n".join(lines) + "\n")
    print("\nWrote", OUT / "RESULTS.md", flush=True)
    print("Survivors:", len(final), flush=True)

if __name__ == "__main__":
    main()
