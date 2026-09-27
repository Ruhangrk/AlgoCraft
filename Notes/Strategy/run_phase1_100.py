#!/usr/bin/env python3
"""Phase-1 only: new pattern/vol strategies × 100 stocks × 1 week. Writes PHASE1_100.md"""
import urllib.request, json, datetime, time, pathlib, collections

BASE = "http://127.0.0.1:8080"
OUT = pathlib.Path(__file__).resolve().parent
STRATEGIES = [
    "hammer_reversal",
    "piercing_line_long",
    "bull_harami_break",
    "morning_star_long",
    "three_white_soldiers",
    "atr_expansion_long",
    "nr7_breakout",
]
TICKERS = [t.strip() for t in (OUT / "universe_100.txt").read_text().splitlines() if t.strip()][:100]
CAP = 10_00_000_00

def ist_ns(y, m, d, hh=0, mm=0):
    dt = datetime.datetime(y, m, d, hh, mm, tzinfo=datetime.timezone(datetime.timedelta(hours=5, minutes=30)))
    return int(dt.timestamp() * 1e9)

FROM, TO = ist_ns(2026, 9, 19), ist_ns(2026, 9, 25, 23, 59)

def req(method, path, body=None, token=None, timeout=180):
    H = {"Content-Type": "application/json"}
    if token:
        H["Authorization"] = f"Bearer {token}"
    data = None if body is None else json.dumps(body).encode()
    r = urllib.request.Request(BASE + path, data=data, headers=H, method=method)
    with urllib.request.urlopen(r, timeout=timeout) as resp:
        return json.loads(resp.read().decode())

def main():
    assert len(TICKERS) == 100, len(TICKERS)
    auth = req("POST", "/auth/login", {"username": "bt_agent", "password": "bt_agent_pass"})
    token = auth["token"]
    wb = req("POST", "/workbooks", {"name": "phase1_100", "capital_paise": CAP}, token)
    wid = wb["id"]
    print(f"workbook={wid} strategies={len(STRATEGIES)} tickers={len(TICKERS)} runs={len(STRATEGIES)*len(TICKERS)}", flush=True)

    rows = []
    fails = []
    t0 = time.time()
    i = 0
    total = len(STRATEGIES) * len(TICKERS)
    # Ticker-outer so 1m fetch is paid once per name; later strategies hit Rocks cache.
    for t in TICKERS:
        for strat in STRATEGIES:
            i += 1
            try:
                out = req(
                    "POST",
                    f"/workbooks/{wid}/backtests/start",
                    {
                        "ticker": t,
                        "strategy": strat,
                        "capital_paise": CAP,
                        "from_ns": FROM,
                        "to_ns": TO,
                    },
                    token,
                )
                row = {
                    "strategy": strat,
                    "ticker": t,
                    "pnl_rs": out["pnl_paise"] / 100,
                    "fills": out["fills"],
                    "fees_rs": out["fees_paise"] / 100,
                    "bars": out["bars"],
                }
                rows.append(row)
                flag = "+" if row["pnl_rs"] > 0 else "-"
                if i % 25 == 0 or row["pnl_rs"] > 500:
                    print(f"[{i}/{total}] {flag} {strat:22} {t:12} ₹{row['pnl_rs']:9.2f} fills={row['fills']}", flush=True)
            except Exception as e:
                fails.append({"strategy": strat, "ticker": t, "error": str(e)})
                print(f"[{i}/{total}] FAIL {strat} {t}: {e}", flush=True)
                time.sleep(2.0)

    (OUT / "phase1_100_raw.json").write_text(json.dumps({"rows": rows, "fails": fails}, indent=2))

    winners = [r for r in rows if r["pnl_rs"] > 0]
    by_strat = collections.defaultdict(list)
    for r in winners:
        by_strat[r["strategy"]].append(r)

    lines = [
        "# Phase 1 — 100 stocks × new pattern/volatility strategies",
        "",
        f"Window: **2026-09-19 → 2026-09-25** (1 week). Capital ₹10L.",
        f"Strategies: {len(STRATEGIES)}. Stocks: {len(TICKERS)}. Completed runs: {len(rows)}. Fails: {len(fails)}.",
        f"Elapsed: {(time.time()-t0)/60:.1f} min.",
        "",
        "## Universe",
        "",
        ", ".join(TICKERS),
        "",
        "## Strategy ideas (distinct from prior batch)",
        "",
        "| Strategy | Family | Rule sketch |",
        "|----------|--------|-------------|",
        "| hammer_reversal | Candlestick | Hammer after ≥2 reds; TP/stop |",
        "| piercing_line_long | Candlestick | Piercing line (relaxed intraday) |",
        "| bull_harami_break | Candlestick | Harami → buy break of mother high |",
        "| morning_star_long | Candlestick | 3-bar morning star |",
        "| three_white_soldiers | Candlestick | 3 strong ascending greens |",
        "| atr_expansion_long | Volatility | TR ≥ 1.8×ATR14 + green; ATR exits |",
        "| nr7_breakout | Volatility | NR7 compression then upside break |",
        "",
        f"## Winners (PnL > 0): {len(winners)} / {len(rows)}",
        "",
    ]
    for strat in STRATEGIES:
        ws = sorted(by_strat.get(strat, []), key=lambda x: -x["pnl_rs"])
        lines.append(f"### {strat} — {len(ws)} winners")
        lines.append("")
        if not ws:
            lines.append("_none_")
            lines.append("")
            continue
        lines.append("| Ticker | PnL ₹ | Fills | Fees ₹ |")
        lines.append("|--------|------:|------:|-------:|")
        for r in ws:
            lines.append(f"| {r['ticker']} | {r['pnl_rs']:.2f} | {r['fills']} | {r['fees_rs']:.2f} |")
        lines.append("")

    # Top 30 overall
    top = sorted(winners, key=lambda x: -x["pnl_rs"])[:30]
    lines += ["## Top 30 pairs by PnL", "", "| Rank | Strategy | Ticker | PnL ₹ | Fills |", "|-----:|----------|--------|------:|------:|"]
    for i, r in enumerate(top, 1):
        lines.append(f"| {i} | {r['strategy']} | {r['ticker']} | {r['pnl_rs']:.2f} | {r['fills']} |")

    # Per-strategy totals
    lines += ["", "## Strategy scoreboard (sum PnL across 100 names)", "", "| Strategy | Wins | Sum PnL ₹ | Avg win ₹ |", "|----------|-----:|----------:|----------:|"]
    for strat in STRATEGIES:
        all_s = [r for r in rows if r["strategy"] == strat]
        ws = [r for r in all_s if r["pnl_rs"] > 0]
        s = sum(r["pnl_rs"] for r in all_s)
        avgw = (sum(r["pnl_rs"] for r in ws) / len(ws)) if ws else 0
        lines.append(f"| {strat} | {len(ws)} | {s:.2f} | {avgw:.2f} |")

    if fails:
        lines += ["", f"## Failures ({len(fails)})", ""]
        for f in fails[:50]:
            lines.append(f"- {f['strategy']} / {f['ticker']}: {f['error'][:120]}")

    (OUT / "PHASE1_100.md").write_text("\n".join(lines) + "\n")
    print("Wrote", OUT / "PHASE1_100.md", "winners", len(winners), flush=True)

if __name__ == "__main__":
    main()
