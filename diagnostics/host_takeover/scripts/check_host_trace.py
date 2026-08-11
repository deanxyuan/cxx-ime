#!/usr/bin/env python3
"""Validate host trace JSONL evidence without exposing candidate text."""

import argparse
import sys

from conversion_check import conversion_sync_gaps
from comless_check import comless_evidence_gaps
from host_behavior_check import candidate_visibility_gaps, host_behavior_evidence_gaps
from trace_common import (
    evidence_gaps,
    load_records,
    report,
    validate_records,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", help="one runtime JSONL or one Probe JSONL")
    parser.add_argument("--product-version")
    parser.add_argument("--kind", choices=("runtime", "probe"), required=True)
    parser.add_argument("--require-summary", action="store_true")
    parser.add_argument("--require-candidate-visibility-toggle", action="store_true")
    parser.add_argument("--require-conversion-sync", action="store_true")
    parser.add_argument("--require-host-behavior", action="store_true")
    parser.add_argument("--require-comless", choices=("uninitialized", "mta"))
    args = parser.parse_args()

    records, errors = load_records(args.paths)
    validation_errors, gaps = validate_records(records, args.product_version)
    errors.extend(validation_errors)
    gaps.extend(evidence_gaps(records, args.kind, args.require_summary))
    if args.require_candidate_visibility_toggle:
        gaps.extend(candidate_visibility_gaps(records, args.kind))
    if args.require_conversion_sync:
        gaps.extend(conversion_sync_gaps(records, args.kind))
    if args.require_host_behavior:
        gaps.extend(host_behavior_evidence_gaps(records, args.kind))
    if args.require_comless:
        gaps.extend(comless_evidence_gaps(records, args.kind, args.require_comless))
    report(records, args.kind)
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    for gap in gaps:
        print(f"EVIDENCE GAP: {gap}", file=sys.stderr)
    if errors:
        return 1
    if gaps:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
