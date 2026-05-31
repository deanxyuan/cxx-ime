#!/usr/bin/env python3
# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
#
# Check query_bench JSONL output against performance thresholds.
#
# Usage: python check_query_bench.py --input <jsonl> --threshold <json> [--output-dir <dir>]
#
# Exit codes:
#   0 = all checks passed
#   3 = threshold or field validation failed

import argparse
import csv
import json
import math
import os
import sys

REQUIRED_FIELDS = [
    "input", "repeat_index", "mode", "page_size", "deadline_ms",
    "elapsed_us", "processor_us", "translate_us", "lookup_us", "merge_us",
    "candidate_count", "exact_scan_count", "prefix_scan_count", "user_scan_count",
    "syllable_path_count", "live_path_count",
    "cache_hit", "truncated", "deadline_exceeded",
]


def nearest_rank(sorted_values, p):
    """Nearest-rank percentile (1-indexed). sorted_values must be sorted ascending."""
    if not sorted_values:
        return 0
    n = len(sorted_values)
    idx = math.ceil(p * n) - 1
    idx = max(0, min(idx, n - 1))
    return sorted_values[idx]


def load_jsonl(path):
    """Load JSONL file, return list of dicts."""
    records = []
    with open(path, "r", encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as e:
                print(f"ERROR: invalid JSON at line {lineno}: {e}", file=sys.stderr)
                return None
    return records


def validate_fields(records):
    """Check that all required fields exist in every record. Return list of errors."""
    errors = []
    for i, rec in enumerate(records):
        for field in REQUIRED_FIELDS:
            if field not in rec:
                errors.append(f"record {i}: missing field '{field}'")
                break  # one error per record is enough
    return errors


def compute_metrics(records):
    """Group records by input, compute per-group metrics."""
    groups = {}
    for rec in records:
        key = rec["input"]
        if key not in groups:
            groups[key] = []
        groups[key].append(rec)

    summaries = []
    for inp, recs in groups.items():
        elapsed = sorted(r["elapsed_us"] for r in recs)
        candidates = sorted(r["candidate_count"] for r in recs)
        exact_scans = sorted(r["exact_scan_count"] for r in recs)
        prefix_scans = sorted(r["prefix_scan_count"] for r in recs)
        user_scans = sorted(r["user_scan_count"] for r in recs)

        n = len(recs)
        cache_hit_count = sum(1 for r in recs if r["cache_hit"])
        truncated_count = sum(1 for r in recs if r["truncated"])
        deadline_count = sum(1 for r in recs if r["deadline_exceeded"])

        scan_p95 = (
            nearest_rank(exact_scans, 0.95)
            + nearest_rank(prefix_scans, 0.95)
            + nearest_rank(user_scans, 0.95)
        )

        summaries.append({
            "input": inp,
            "n": n,
            "p50_us": nearest_rank(elapsed, 0.50),
            "p95_us": nearest_rank(elapsed, 0.95),
            "p99_us": nearest_rank(elapsed, 0.99),
            "max_us": elapsed[-1] if elapsed else 0,
            "candidate_p50": nearest_rank(candidates, 0.50),
            "exact_scan_p95": nearest_rank(exact_scans, 0.95),
            "prefix_scan_p95": nearest_rank(prefix_scans, 0.95),
            "user_scan_p95": nearest_rank(user_scans, 0.95),
            "scan_p95": scan_p95,
            "cache_hit_rate": cache_hit_count / n if n > 0 else 0.0,
            "truncated_rate": truncated_count / n if n > 0 else 0.0,
            "deadline_rate": deadline_count / n if n > 0 else 0.0,
        })

    return summaries


def write_summary_csv(path, summaries):
    """Write summary CSV."""
    fields = [
        "input", "p50_us", "p95_us", "p99_us", "max_us",
        "candidate_p50", "exact_scan_p95", "prefix_scan_p95", "user_scan_p95",
        "cache_hit_rate", "truncated_rate", "deadline_rate",
    ]
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for s in summaries:
            writer.writerow(s)
    print(f"Summary CSV: {path}")


def check_thresholds(summaries, thresholds):
    """Check summaries against thresholds. Return list of failure messages."""
    failures = []

    # Build lookup: input -> summary
    by_input = {s["input"]: s for s in summaries}

    for group_name, group_cfg in thresholds.items():
        inputs = group_cfg.get("inputs")  # None for "default" = applies to all
        for s in summaries:
            if inputs is not None and s["input"] not in inputs:
                continue
            if inputs is None and any(
                s["input"] in cfg.get("inputs", [])
                for name, cfg in thresholds.items()
                if name != "default" and cfg.get("inputs")
            ):
                continue  # skip inputs handled by specific groups

            prefix = f"[{group_name}] {s['input']}"

            # p95
            limit = group_cfg.get("p95_us")
            if limit is not None and s["p95_us"] > limit:
                failures.append(f"{prefix}: p95_us={s['p95_us']} > {limit}")

            # p99
            limit = group_cfg.get("p99_us")
            if limit is not None and s["p99_us"] > limit:
                failures.append(f"{prefix}: p99_us={s['p99_us']} > {limit}")

            # max
            limit = group_cfg.get("max_us")
            if limit is not None and s["max_us"] > limit:
                failures.append(f"{prefix}: max_us={s['max_us']} > {limit}")

            # cache_hit_rate
            limit = group_cfg.get("cache_hit_rate")
            if limit is not None and s["cache_hit_rate"] < limit:
                failures.append(f"{prefix}: cache_hit_rate={s['cache_hit_rate']:.4f} < {limit}")

            # scan_p95
            limit = group_cfg.get("scan_p95")
            if limit is not None and s["scan_p95"] > limit:
                failures.append(f"{prefix}: scan_p95={s['scan_p95']} > {limit}")

            # deadline_rate
            limit = group_cfg.get("deadline_rate")
            if limit is not None and s["deadline_rate"] > limit:
                failures.append(f"{prefix}: deadline_rate={s['deadline_rate']:.4f} > {limit}")

    # candidate_count=0 check — any fixed case with 0 candidates is a failure
    for s in summaries:
        if s["candidate_p50"] == 0 and s["n"] > 0:
            failures.append(f"{s['input']}: candidate_count=0 in all iterations")

    return failures


def write_failures(path, failures):
    """Write failures to text file."""
    with open(path, "w", encoding="utf-8") as f:
        for fail in failures:
            f.write(fail + "\n")
    print(f"Failures written to: {path}")


def main():
    parser = argparse.ArgumentParser(description="Check query_bench JSONL against thresholds")
    parser.add_argument("--input", required=True, help="JSONL trace file from query_bench")
    parser.add_argument("--threshold", required=True, help="Thresholds JSON file")
    parser.add_argument("--output-dir", default="reports", help="Output directory for CSV/failures")
    args = parser.parse_args()

    # Load JSONL
    records = load_jsonl(args.input)
    if records is None:
        return 1
    if not records:
        print("ERROR: JSONL file is empty", file=sys.stderr)
        return 3

    # Validate required fields
    field_errors = validate_fields(records)
    if field_errors:
        print(f"ERROR: {len(field_errors)} record(s) missing required fields:", file=sys.stderr)
        for err in field_errors[:10]:
            print(f"  {err}", file=sys.stderr)
        if len(field_errors) > 10:
            print(f"  ... and {len(field_errors) - 10} more", file=sys.stderr)
        return 3

    # Load thresholds
    with open(args.threshold, "r", encoding="utf-8") as f:
        thresholds = json.load(f)

    # Compute metrics
    summaries = compute_metrics(records)

    # Write summary CSV
    os.makedirs(args.output_dir, exist_ok=True)
    csv_path = os.path.join(args.output_dir, "query-bench-summary.csv")
    write_summary_csv(csv_path, summaries)

    # Check thresholds
    failures = check_thresholds(summaries, thresholds)
    if failures:
        fail_path = os.path.join(args.output_dir, "query-bench-failures.txt")
        write_failures(fail_path, failures)
        print(f"\nFAILED: {len(failures)} threshold violation(s):", file=sys.stderr)
        for fail in failures:
            print(f"  {fail}", file=sys.stderr)
        return 3

    print("\nAll threshold checks PASSED.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
