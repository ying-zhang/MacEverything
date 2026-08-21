#!/usr/bin/env python3
"""
MacEverything HTTP search benchmark.

Loads test queries from test_queries.json, runs multiple iterations per query,
and outputs detailed performance statistics. Designed to minimize noise from
machine load fluctuation by using warmup rounds, multiple iterations, and
statistical outlier filtering.

Usage:
    python3 bench_search.py [options]

Options:
    --host HOST         Server host (default: 127.0.0.1)
    --port PORT         Server port (default: 19860)
    --iterations N      Iterations per query (default: 10)
    --warmup N          Warmup iterations (not counted) (default: 3)
    --limit N           Max results per query (default: 100)
    --category CAT      Run only this category (can repeat)
    --output FILE       Write JSON report to file
    --csv FILE          Write CSV report to file
    --quiet             Suppress per-query output, only show summary
"""

import argparse
import json
import math
import os
import statistics
import sys
import time
import urllib.parse
import urllib.request
from dataclasses import dataclass, field, asdict
from pathlib import Path


@dataclass
class QueryResult:
    label: str
    query: str
    category: str
    search_path: str = ""
    result_count: int = 0
    total_records: int = 0
    used_trigram: bool = False
    timings_ms: list = field(default_factory=list)

    # computed stats
    mean_ms: float = 0.0
    median_ms: float = 0.0
    min_ms: float = 0.0
    max_ms: float = 0.0
    stddev_ms: float = 0.0
    p95_ms: float = 0.0
    p99_ms: float = 0.0
    cv_pct: float = 0.0  # coefficient of variation

    def compute_stats(self):
        t = sorted(self.timings_ms)
        n = len(t)
        if n == 0:
            return
        self.mean_ms = statistics.mean(t)
        self.median_ms = statistics.median(t)
        self.min_ms = t[0]
        self.max_ms = t[-1]
        self.stddev_ms = statistics.stdev(t) if n > 1 else 0.0
        self.p95_ms = t[int(n * 0.95)] if n >= 20 else t[-1]
        self.p99_ms = t[int(n * 0.99)] if n >= 100 else t[-1]
        self.cv_pct = (self.stddev_ms / self.mean_ms * 100) if self.mean_ms > 0 else 0.0


def search(host: str, port: int, query: str, limit: int, trigram: str = "1") -> dict:
    params = urllib.parse.urlencode({
        "q": query,
        "limit": limit,
        "trigram": trigram,
    })
    url = f"http://{host}:{port}/api/search?{params}"
    with urllib.request.urlopen(url, timeout=30) as resp:
        return json.loads(resp.read())


def check_server(host: str, port: int) -> dict:
    url = f"http://{host}:{port}/api/status"
    try:
        with urllib.request.urlopen(url, timeout=5) as resp:
            return json.loads(resp.read())
    except Exception as e:
        print(f"ERROR: Cannot connect to MacEverything at {host}:{port}")
        print(f"  {e}")
        print("  Make sure the app is running.")
        sys.exit(1)


def percentile(data: list, p: float) -> float:
    if not data:
        return 0.0
    k = (len(data) - 1) * p / 100.0
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return data[int(k)]
    return data[int(f)] * (c - k) + data[int(c)] * (k - f)


def run_benchmark(args):
    # Load test queries
    queries_file = Path(__file__).parent / "test_queries.json"
    with open(queries_file) as f:
        test_data = json.load(f)

    # Check server
    status = check_server(args.host, args.port)
    total_records = status.get("liveRecordCount", status.get("recordCount", 0))
    print(f"Server status: {total_records:,} live records")
    print(f"Benchmark config: {args.iterations} iterations, {args.warmup} warmup, limit={args.limit}")
    print()

    # Filter categories if specified
    categories = test_data["categories"]
    if args.category:
        categories = [c for c in categories if c["name"] in args.category]
        if not categories:
            print(f"ERROR: No matching categories. Available: {[c['name'] for c in test_data['categories']]}")
            sys.exit(1)

    results: list[QueryResult] = []
    total_queries = sum(len(c["queries"]) for c in categories)
    query_idx = 0

    for cat in categories:
        cat_name = cat["name"]
        if not args.quiet:
            print(f"{'=' * 80}")
            print(f"Category: {cat_name} — {cat['description']}")
            print(f"{'=' * 80}")
            print(f"{'Label':<25} {'Query':<20} {'Path':<15} {'Results':>8} "
                  f"{'Mean':>8} {'Median':>8} {'Min':>8} {'Max':>8} {'P95':>8} {'StdDev':>8} {'CV%':>6}")
            print(f"{'-' * 25} {'-' * 20} {'-' * 15} {'-' * 8} "
                  f"{'-' * 8} {'-' * 8} {'-' * 8} {'-' * 8} {'-' * 8} {'-' * 8} {'-' * 6}")

        for qdef in cat["queries"]:
            query_idx += 1
            q = qdef["q"]
            label = qdef.get("label", q)
            trigram = qdef.get("trigram", "1")

            qr = QueryResult(label=label, query=q, category=cat_name)

            # Warmup
            for _ in range(args.warmup):
                try:
                    search(args.host, args.port, q, args.limit, trigram)
                except Exception as e:
                    print(f"  WARNING: warmup failed for '{q}': {e}")

            # Timed iterations
            for i in range(args.iterations):
                try:
                    resp = search(args.host, args.port, q, args.limit, trigram)
                    timing = resp.get("timing", {})
                    qr.timings_ms.append(timing.get("totalMs", 0.0))
                    # Capture metadata from first iteration
                    if i == 0:
                        qr.search_path = timing.get("searchPath", "")
                        qr.result_count = resp.get("count", 0)
                        qr.total_records = timing.get("totalRecords", 0)
                        qr.used_trigram = timing.get("usedTrigram", False)
                except Exception as e:
                    print(f"  WARNING: iteration {i} failed for '{q}': {e}")

            qr.compute_stats()
            results.append(qr)

            if not args.quiet:
                print(f"{label:<25} {q:<20} {qr.search_path:<15} {qr.result_count:>8} "
                      f"{qr.mean_ms:>7.1f}{'ms':} {qr.median_ms:>7.1f}{'ms':} "
                      f"{qr.min_ms:>7.1f}{'ms':} {qr.max_ms:>7.1f}{'ms':} "
                      f"{qr.p95_ms:>7.1f}{'ms':} {qr.stddev_ms:>7.1f}{'ms':} "
                      f"{qr.cv_pct:>5.1f}%")

            # Brief pause between queries to avoid overloading
            time.sleep(0.05)

        if not args.quiet:
            print()

    # Print summary
    print_summary(results, total_records, args)

    # Write reports
    if args.output:
        write_json_report(results, total_records, args)
    if args.csv:
        write_csv_report(results, args)

    return results


def print_summary(results: list[QueryResult], total_records: int, args):
    print(f"\n{'#' * 80}")
    print(f"# BENCHMARK SUMMARY")
    print(f"# Records: {total_records:,}  |  Iterations: {args.iterations}  |  Warmup: {args.warmup}")
    print(f"{'#' * 80}\n")

    # Group by category
    cats = {}
    for r in results:
        cats.setdefault(r.category, []).append(r)

    for cat_name, cat_results in cats.items():
        medians = [r.median_ms for r in cat_results]
        means = [r.mean_ms for r in cat_results]
        print(f"  {cat_name}:")
        print(f"    Queries: {len(cat_results)}")
        print(f"    Median range: {min(medians):.1f}ms - {max(medians):.1f}ms")
        print(f"    Mean range:   {min(means):.1f}ms - {max(means):.1f}ms")
        print(f"    Avg median:   {statistics.mean(medians):.1f}ms")
        print()

    # Overall stats
    all_medians = [r.median_ms for r in results]
    print(f"  Overall:")
    print(f"    Total queries tested:  {len(results)}")
    print(f"    Fastest median:        {min(all_medians):.1f}ms")
    print(f"    Slowest median:        {max(all_medians):.1f}ms")
    print(f"    Avg median across all: {statistics.mean(all_medians):.1f}ms")

    # Strategy breakdown
    by_path = {}
    for r in results:
        by_path.setdefault(r.search_path, []).append(r)
    print(f"\n  By search strategy:")
    for path, rs in sorted(by_path.items()):
        meds = [r.median_ms for r in rs]
        print(f"    {path:<20} avg_median={statistics.mean(meds):>7.1f}ms  "
              f"range=[{min(meds):.1f}, {max(meds):.1f}]  n={len(rs)}")

    # Trigram vs linear comparison (same queries)
    trigram_queries = {r.query: r for r in results if r.category != "linear_forced" and r.used_trigram}
    linear_forced = {r.query: r for r in results if r.category == "linear_forced"}
    common = set(trigram_queries.keys()) & set(linear_forced.keys())
    if common:
        print(f"\n  Trigram vs Linear (same queries, {len(common)} compared):")
        print(f"    {'Query':<20} {'Trigram':>10} {'Linear':>10} {'Speedup':>10}")
        print(f"    {'-' * 20} {'-' * 10} {'-' * 10} {'-' * 10}")
        for q in sorted(common):
            t_ms = trigram_queries[q].median_ms
            l_ms = linear_forced[q].median_ms
            speedup = l_ms / t_ms if t_ms > 0 else float('inf')
            print(f"    {q:<20} {t_ms:>9.1f}ms {l_ms:>9.1f}ms {speedup:>9.1f}x")

    print()


def write_json_report(results: list[QueryResult], total_records: int, args):
    report = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "server": {"host": args.host, "port": args.port, "total_records": total_records},
        "config": {"iterations": args.iterations, "warmup": args.warmup, "limit": args.limit},
        "results": [],
    }
    for r in results:
        d = asdict(r)
        d.pop("timings_ms", None)  # keep report compact
        report["results"].append(d)

    with open(args.output, "w") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    print(f"JSON report written to: {args.output}")


def write_csv_report(results: list[QueryResult], args):
    headers = ["category", "label", "query", "search_path", "result_count",
               "mean_ms", "median_ms", "min_ms", "max_ms", "p95_ms", "stddev_ms", "cv_pct"]
    with open(args.csv, "w") as f:
        f.write(",".join(headers) + "\n")
        for r in results:
            vals = [r.category, r.label, r.query, r.search_path, str(r.result_count),
                    f"{r.mean_ms:.2f}", f"{r.median_ms:.2f}", f"{r.min_ms:.2f}",
                    f"{r.max_ms:.2f}", f"{r.p95_ms:.2f}", f"{r.stddev_ms:.2f}",
                    f"{r.cv_pct:.1f}"]
            f.write(",".join(vals) + "\n")
    print(f"CSV report written to: {args.csv}")


def main():
    parser = argparse.ArgumentParser(description="MacEverything search benchmark")
    parser.add_argument("--host", default="127.0.0.1", help="Server host")
    parser.add_argument("--port", type=int, default=19860, help="Server port")
    parser.add_argument("--iterations", type=int, default=10, help="Iterations per query")
    parser.add_argument("--warmup", type=int, default=3, help="Warmup iterations")
    parser.add_argument("--limit", type=int, default=100, help="Max results per query")
    parser.add_argument("--category", action="append", help="Run only these categories")
    parser.add_argument("--output", help="JSON report output file")
    parser.add_argument("--csv", help="CSV report output file")
    parser.add_argument("--quiet", action="store_true", help="Only show summary")
    args = parser.parse_args()

    print(f"MacEverything Search Benchmark")
    print(f"{'=' * 80}")
    print(f"Time: {time.strftime('%Y-%m-%d %H:%M:%S')}")

    t0 = time.monotonic()
    run_benchmark(args)
    elapsed = time.monotonic() - t0
    print(f"Total benchmark time: {elapsed:.1f}s")


if __name__ == "__main__":
    main()
