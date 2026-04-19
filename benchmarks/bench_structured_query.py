#!/usr/bin/env python3
"""
Performance benchmark for Node-Centric Structured Query System (Feature 103).
Tests PLAIN, SEGMENTS, DIR_EXACT, DIR_LIST modes via HTTP API.

Usage:
    python3 benchmarks/bench_structured_query.py [--iterations N] [--warmup N]
"""

import argparse
import json
import time
import urllib.request
import statistics
import sys

BASE_URL = "http://localhost:19860/api/search"

# ── Test Cases ──────────────────────────────────────────
# Each: (label, query_string, expected_mode, description)
TEST_CASES = [
    # PLAIN mode (baseline)
    ("PLAIN-short",       "ls",                    "PLAIN",     "2-char linear scan"),
    ("PLAIN-trigram",     "test",                  "PLAIN",     "Common keyword via trigram"),
    ("PLAIN-long",       "SearchEngine",           "PLAIN",     "Long specific keyword"),
    ("PLAIN-rare",       "StructuredQueryParser",  "PLAIN",     "Very rare keyword"),
    ("PLAIN-nomatch",    "qzxwvuts_nonexist",      "PLAIN",     "No match (trigram early exit)"),

    # SEGMENTS mode (node-centric slash queries)
    ("SEG-2part",        "bin/ls",                 "SEGMENTS",  "2-segment: name=ls, path has bin"),
    ("SEG-2part-long",   "Core/SearchEngine",      "SEGMENTS",  "2-segment: specific file in Core"),
    ("SEG-3part",        "/usr/local/bin",         "SEGMENTS",  "3-segment: name=bin, path has usr,local"),
    ("SEG-3part-file",   "/usr/bin/python",        "SEGMENTS",  "3-segment: python under /usr/bin"),
    ("SEG-deep",         "/Users/username/data",     "SEGMENTS",  "3-segment: data under Users/username"),
    ("SEG-wildcard",     "/usr/*/bin",             "SEGMENTS",  "Non-adjacent wildcard"),
    ("SEG-nomatch",      "/nonexist/qzxwv",        "SEGMENTS",  "No match structured"),
    ("SEG-single",       "/brew",                  "SEGMENTS",  "Single segment (name only)"),

    # DIR_LIST mode (trailing /*)
    ("DIRLIST-usrbin",   "/usr/bin/*",             "DIR_LIST",  "List /usr/bin children"),
    ("DIRLIST-etc",      "/etc/*",                 "DIR_LIST",  "List /etc children"),
    ("DIRLIST-local",    "/usr/local/*",           "DIR_LIST",  "List /usr/local children"),
    ("DIRLIST-deep",     "/username/data/*",         "DIR_LIST",  "List data dir children"),
    ("DIRLIST-nomatch",  "/nonexistdir/*",         "DIR_LIST",  "No match dir list"),

    # DIR_EXACT mode (trailing /)
    ("DIREXACT-bin",     "/usr/bin/",              "DIR_EXACT", "Exact dir /usr/bin"),
    ("DIREXACT-etc",     "/etc/",                  "DIR_EXACT", "Exact dir /etc"),
    ("DIREXACT-local",   "/usr/local/",            "DIR_EXACT", "Exact dir /usr/local"),
]


def run_query(query: str, limit: int = 100) -> dict:
    """Execute a single query and return the full JSON response."""
    url = f"{BASE_URL}?q={urllib.request.quote(query)}&limit={limit}"
    with urllib.request.urlopen(url, timeout=10) as resp:
        return json.loads(resp.read().decode())


def benchmark_query(query: str, iterations: int, warmup: int, limit: int = 100) -> dict:
    """Benchmark a query over multiple iterations, return stats."""
    # Warmup
    for _ in range(warmup):
        run_query(query, limit)

    times = []
    last_result = None
    for _ in range(iterations):
        result = run_query(query, limit)
        times.append(result["timing"]["totalMs"])
        last_result = result

    return {
        "avg_ms": statistics.mean(times),
        "min_ms": min(times),
        "max_ms": max(times),
        "median_ms": statistics.median(times),
        "p95_ms": sorted(times)[int(len(times) * 0.95)] if len(times) >= 5 else max(times),
        "stddev_ms": statistics.stdev(times) if len(times) > 1 else 0,
        "result_count": last_result["count"],
        "candidates": last_result["timing"].get("candidates", 0),
        "search_path": last_result["timing"].get("searchPath", "unknown"),
        "used_trigram": last_result["timing"].get("usedTrigram", False),
        "times": times,
        "last_timing": last_result["timing"],
    }


def format_table(results: list[dict]) -> str:
    """Format results as a readable table."""
    header = f"{'Label':<20} {'Mode':<10} {'SearchPath':<12} {'Avg(ms)':>8} {'Min':>8} {'Med':>8} {'P95':>8} {'Max':>8} {'Results':>8} {'Cands':>8}"
    sep = "─" * len(header)
    lines = [sep, header, sep]

    current_mode = None
    for r in results:
        mode = r["expected_mode"]
        if mode != current_mode:
            if current_mode is not None:
                lines.append(sep)
            current_mode = mode
        lines.append(
            f"{r['label']:<20} {mode:<10} {r['search_path']:<12} "
            f"{r['avg_ms']:>8.3f} {r['min_ms']:>8.3f} {r['median_ms']:>8.3f} "
            f"{r['p95_ms']:>8.3f} {r['max_ms']:>8.3f} "
            f"{r['result_count']:>8} {r['candidates']:>8}"
        )
    lines.append(sep)
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Structured Query Performance Benchmark")
    parser.add_argument("--iterations", type=int, default=20, help="Iterations per query (default: 20)")
    parser.add_argument("--warmup", type=int, default=3, help="Warmup iterations (default: 3)")
    parser.add_argument("--limit", type=int, default=100, help="Max results per query (default: 100)")
    args = parser.parse_args()

    # Check connectivity
    try:
        status = json.loads(urllib.request.urlopen("http://localhost:19860/api/status", timeout=5).read())
        print(f"Engine status: {status['liveRecordCount']:,} live records\n")
    except Exception as e:
        print(f"ERROR: Cannot connect to MacEverything: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Benchmark config: {args.iterations} iterations, {args.warmup} warmup, limit={args.limit}")
    print(f"{'='*90}\n")

    results = []
    for label, query, expected_mode, desc in TEST_CASES:
        sys.stdout.write(f"  {label:<20} ({desc})... ")
        sys.stdout.flush()
        try:
            stats = benchmark_query(query, args.iterations, args.warmup, args.limit)
            stats["label"] = label
            stats["query"] = query
            stats["expected_mode"] = expected_mode
            stats["description"] = desc
            results.append(stats)
            print(f"{stats['avg_ms']:.3f}ms (n={stats['result_count']})")
        except Exception as e:
            print(f"FAILED: {e}")
            results.append({
                "label": label, "query": query, "expected_mode": expected_mode,
                "description": desc, "avg_ms": -1, "min_ms": -1, "max_ms": -1,
                "median_ms": -1, "p95_ms": -1, "result_count": -1, "candidates": 0,
                "search_path": "error", "stddev_ms": 0,
            })

    # ── Summary Table ──
    print(f"\n{'='*90}")
    print("RESULTS SUMMARY")
    print(f"{'='*90}\n")
    print(format_table(results))

    # ── Mode Comparison ──
    print(f"\n{'='*90}")
    print("MODE COMPARISON (average latency)")
    print(f"{'='*90}\n")

    modes = {}
    for r in results:
        if r["avg_ms"] < 0:
            continue
        mode = r["expected_mode"]
        if mode not in modes:
            modes[mode] = []
        modes[mode].append(r["avg_ms"])

    for mode in ["PLAIN", "SEGMENTS", "DIR_EXACT", "DIR_LIST"]:
        if mode in modes:
            vals = modes[mode]
            avg = statistics.mean(vals)
            print(f"  {mode:<12}  avg={avg:.3f}ms  (across {len(vals)} queries)")

    # ── Detailed Timing Breakdown ──
    print(f"\n{'='*90}")
    print("DETAILED TIMING BREAKDOWN (last iteration)")
    print(f"{'='*90}\n")

    for r in results:
        if "last_timing" not in r:
            continue
        t = r["last_timing"]
        print(f"  {r['label']:<20} q=\"{r['query']}\"")
        print(f"    total={t.get('totalMs',0):.3f}ms  lock={t.get('lockWaitMs',0):.3f}ms  "
              f"trigram={t.get('trigramMs',0):.3f}ms  phase1={t.get('phase1Ms',0):.3f}ms  "
              f"phase2={t.get('phase2Ms',0):.3f}ms  sort={t.get('sortMs',0):.3f}ms  "
              f"path={t.get('searchPath','?')}")
        print()

    # ── Save JSON ──
    out_path = "benchmarks/bench_structured_query_results.json"
    with open(out_path, "w") as f:
        json.dump({
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
            "config": {"iterations": args.iterations, "warmup": args.warmup, "limit": args.limit},
            "record_count": status["liveRecordCount"],
            "results": [{k: v for k, v in r.items() if k != "times" and k != "last_timing"} for r in results],
        }, f, indent=2)
    print(f"Results saved to {out_path}")


if __name__ == "__main__":
    main()
