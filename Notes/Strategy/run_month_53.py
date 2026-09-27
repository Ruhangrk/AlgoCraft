#!/usr/bin/env python3
"""Retest 53 good Phase-1 pairs on trailing ~1 month. Writes MONTH_53.md"""
import urllib.request, json, datetime, time, pathlib

BASE = "http://127.0.0.1:8080"
OUT = pathlib.Path(__file__).resolve().parent
CAP = 10_00_000_00  # ₹10L
CAP_RS = CAP / 100

# Trailing ~1 month ending same day as Phase-1 window
def ist_ns(y, m, d, hh=0, mm=0):
    dt = datetime.datetime(y, m, d, hh, mm, tzinfo=datetime.timezone(datetime.timedelta(hours=5, minutes=30)))
    return int(dt.timestamp() * 1e9)

FROM, TO = ist_ns(2026, 8, 26), ist_ns(2026, 9, 25, 23, 59)
RANGE_LABEL = "26 Aug – 25 Sep 2026"

# Same inclusion as GOOD_PERFORMERS.md
def load_pairs():
    raw = json.loads((OUT / "phase1_100_raw.json").read_text())["rows"]
    bad = {"atr_expansion_long", "nr7_breakout"}
    good = [
        r for r in raw
        if r["pnl_rs"] >= 2000 and r["fills"] <= 50 and r["strategy"] not in bad
    ]
    good = sorted(good, key=lambda x: -x["pnl_rs"])
    assert len(good) == 53, len(good)
    return good

def req(method, path, body=None, token=None, timeout=300):
    H = {"Content-Type": "application/json"}
    if token:
        H["Authorization"] = f"Bearer {token}"
    data = None if body is None else json.dumps(body).encode()
    r = urllib.request.Request(BASE + path, data=data, headers=H, method=method)
    with urllib.request.urlopen(r, timeout=timeout) as resp:
        return json.loads(resp.read().decode())

def main():
    pairs = load_pairs()
    auth = req("POST", "/auth/login", {"username": "bt_agent", "password": "bt_agent_pass"})
    token = auth["token"]
    wb = req("POST", "/workbooks", {"name": "good53_month", "capital_paise": CAP}, token)
    wid = wb["id"]
    print(f"workbook={wid} pairs={len(pairs)} range={RANGE_LABEL}", flush=True)

    rows = []
    fails = []
    t0 = time.time()
    # ticker-outer for Rocks cache
    by_ticker = {}
    for p in pairs:
        by_ticker.setdefault(p["ticker"], []).append(p)

    i = 0
    total = len(pairs)
    for ticker, plist in by_ticker.items():
        for p in plist:
            i += 1
            strat = p["strategy"]
            try:
                out = req(
                    "POST",
                    f"/workbooks/{wid}/backtests/start",
                    {
                        "ticker": ticker,
                        "strategy": strat,
                        "capital_paise": CAP,
                        "from_ns": FROM,
                        "to_ns": TO,
                    },
                    token,
                )
                pnl = out["pnl_paise"] / 100
                row = {
                    "strategy": strat,
                    "ticker": ticker,
                    "week_pnl_rs": p["pnl_rs"],
                    "week_pct": round(100 * p["pnl_rs"] / CAP_RS, 3),
                    "month_pnl_rs": pnl,
                    "month_pct": round(100 * pnl / CAP_RS, 3),
                    "fills": out["fills"],
                    "fees_rs": out["fees_paise"] / 100,
                    "bars": out["bars"],
                }
                rows.append(row)
                flag = "+" if pnl > 0 else "-"
                print(
                    f"[{i}/{total}] {flag} {strat:22} {ticker:12} "
                    f"week={p['pnl_rs']:8.0f} month={pnl:9.2f} ({row['month_pct']:.3f}%) fills={row['fills']}",
                    flush=True,
                )
            except Exception as e:
                fails.append({"strategy": strat, "ticker": ticker, "error": str(e)})
                print(f"[{i}/{total}] FAIL {strat} {ticker}: {e}", flush=True)
                time.sleep(2.0)

    (OUT / "month_53_raw.json").write_text(json.dumps({"rows": rows, "fails": fails, "range": RANGE_LABEL}, indent=2))

    still_good = [r for r in rows if r["month_pnl_rs"] >= 2000]
    profitable = [r for r in rows if r["month_pnl_rs"] > 0]
    still_good = sorted(still_good, key=lambda x: -x["month_pnl_rs"])
    profitable = sorted(profitable, key=lambda x: -x["month_pnl_rs"])

    lines = [
        "# Month retest — 53 Phase-1 good pairs",
        "",
        f"**Range:** {RANGE_LABEL} (~1 month)",
        f"**Capital:** ₹10,00,000",
        f"**Pairs:** {len(pairs)}. Completed: {len(rows)}. Fails: {len(fails)}.",
        f"**Elapsed:** {(time.time()-t0)/60:.1f} min.",
        "",
        f"Still profitable (PnL > 0): **{len(profitable)}** / {len(rows)}",
        f"Still “good” (PnL ≥ ₹2,000): **{len(still_good)}** / {len(rows)}",
        "",
        "## Still good on 1-month (PnL ≥ ₹2,000)",
        "",
        "| Strategy | Stock | Range tested | Week profit % | Month profit % | Month PnL ₹ | Fills |",
        "|----------|-------|--------------|--------------:|---------------:|------------:|------:|",
    ]
    for r in still_good:
        lines.append(
            f"| {r['strategy']} | {r['ticker']} | {RANGE_LABEL} | "
            f"{r['week_pct']:.2f}% | {r['month_pct']:.2f}% | {r['month_pnl_rs']:.2f} | {r['fills']} |"
        )

    lines += [
        "",
        "## All 53 — month results (sorted by month PnL)",
        "",
        "| Strategy | Stock | Range tested | Week % | Month % | Month PnL ₹ | Fills |",
        "|----------|-------|--------------|-------:|--------:|------------:|------:|",
    ]
    for r in sorted(rows, key=lambda x: -x["month_pnl_rs"]):
        lines.append(
            f"| {r['strategy']} | {r['ticker']} | {RANGE_LABEL} | "
            f"{r['week_pct']:.2f}% | {r['month_pct']:.2f}% | {r['month_pnl_rs']:.2f} | {r['fills']} |"
        )

    if fails:
        lines += ["", f"## Failures ({len(fails)})", ""]
        for f in fails:
            lines.append(f"- {f['strategy']} / {f['ticker']}: {f['error'][:140]}")

    (OUT / "MONTH_53.md").write_text("\n".join(lines) + "\n")
    print("Wrote", OUT / "MONTH_53.md", "still_good", len(still_good), "profitable", len(profitable), flush=True)

if __name__ == "__main__":
    main()
