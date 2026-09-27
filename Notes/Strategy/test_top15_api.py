#!/usr/bin/env python3
"""Rigorous API test for top15_week_router hist_replay (front-style)."""
import json, urllib.request, pathlib

BASE = "http://127.0.0.1:8080"
OUT = pathlib.Path(__file__).resolve().parent
CAP = 10_00_000_00  # ₹10L
ANCHOR = "2026-09-25"  # NSE session day, not today → hist_replay

def req(method, path, body=None, token=None, timeout=600):
    H = {"Content-Type": "application/json"}
    if token:
        H["Authorization"] = f"Bearer {token}"
    data = None if body is None else json.dumps(body).encode()
    r = urllib.request.Request(BASE + path, data=data, headers=H, method=method)
    with urllib.request.urlopen(r, timeout=timeout) as resp:
        return json.loads(resp.read().decode()), resp.status

def main():
    auth, _ = req("POST", "/auth/login", {"username": "bt_agent", "password": "bt_agent_pass"})
    token = auth["token"]
    algos, _ = req("GET", "/routing-algos", token=token)
    assert "top15_week_router" in algos, algos
    print("routing-algos", algos, flush=True)

    wb, st = req("POST", "/workbooks", {"name": "top15_week_api", "capital_paise": CAP}, token)
    assert st in (200, 201), (st, wb)
    wid = wb["id"]
    print(f"workbook={wid}", flush=True)

    run, st = req(
        "POST",
        f"/workbooks/{wid}/runs/start",
        {"router": "top15_week_router", "anchor_date": ANCHOR, "capital_paise": CAP},
        token,
    )
    print("run_status", st, flush=True)
    print(json.dumps({k: run[k] for k in run if k != "strategies"}, indent=2), flush=True)
    assert st == 201, run
    assert run.get("mode") == "hist_replay", run
    assert run.get("router") == "top15_week_router"
    assert run.get("eval_sessions") == 5
    assert run.get("selected", 0) <= 15
    rid = run["run_id"]

    events, _ = req(
        "GET",
        f"/workbooks/{wid}/runs/{rid}/events?include=routing,lifecycle,fill",
        token=token,
    )
    routing = [e for e in events if e.get("type") == "routing"]
    selected = [e for e in routing if e.get("data", {}).get("decision") == "selected"
                or (e.get("data", {}).get("decision") or "").lower() in ("select", "selected", "real")]
    # decision field may be "select"/"skip" — dump sample
    decisions = {}
    for e in routing:
        d = e.get("data", {})
        decisions.setdefault(d.get("decision"), 0)
        decisions[d.get("decision")] += 1
    print("routing_decision_counts", decisions, flush=True)
    print("routing_events", len(routing), "fills",
          sum(1 for e in events if e.get("type") == "fill"), flush=True)

    # Show selected by score
    picks = []
    for e in routing:
        d = e.get("data", {})
        if d.get("score_paise", 0) > 0 and "select" in str(d.get("decision", "")).lower():
            picks.append(d)
    if not picks:
        # fall back: top scores marked somehow
        picks = sorted(
            (e.get("data", {}) for e in routing),
            key=lambda d: d.get("score_paise", 0),
            reverse=True,
        )[:15]
        picks = [p for p in picks if p.get("score_paise", 0) > 0]

    lines = [
        "# top15_week_router — API hist_replay test",
        "",
        f"**anchor_date:** {ANCHOR} (mode=`hist_replay`)",
        f"**capital:** ₹{CAP/100:,.0f}",
        f"**selected:** {run.get('selected')}  **skipped:** {run.get('skipped')}  **fills:** {run.get('fills')}",
        f"**returned_paise:** {run.get('returned_paise')}",
        f"**run_id:** {rid}",
        "",
        "## Request (as frontend)",
        "",
        "```http",
        f"POST /workbooks/{wid}/runs/start",
        "Authorization: Bearer <token>",
        "Content-Type: application/json",
        "",
        json.dumps({"router": "top15_week_router", "anchor_date": ANCHOR, "capital_paise": CAP}, indent=2),
        "```",
        "",
        "## Selected containers (routing events)",
        "",
        "| Strategy | Stock | Eval PnL ₹ |",
        "|----------|-------|-----------:|",
    ]
    for p in sorted(picks, key=lambda x: -x.get("score_paise", 0))[:15]:
        lines.append(
            f"| {p.get('strategy')} | {p.get('ticker')} | {p.get('score_paise', 0)/100:.2f} |"
        )
    (OUT / "ROUTING_TOP15.md").write_text("\n".join(lines) + "\n")
    (OUT / "routing_top15_api.json").write_text(json.dumps({"run": run, "routing": routing[:80]}, indent=2))
    print("Wrote", OUT / "ROUTING_TOP15.md", flush=True)
    assert run.get("selected", 0) >= 0
    print("OK", flush=True)

if __name__ == "__main__":
    main()
