#!/usr/bin/env python3
"""Validate staged host-takeover JSONL evidence without exposing candidate text."""

import argparse
import sys

from conversion_check import conversion_sync_gaps
from t2_check import candidate_visibility_gaps, t2_evidence_gaps
from t3_check import comless_evidence_gaps
from trace_common import (
    DEFAULT_BUILD_ID,
    evidence_gaps,
    load_records,
    report,
    validate_records,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", help="one runtime JSONL or one Probe JSONL")
    parser.add_argument("--stage", type=int, default=1)
    parser.add_argument("--build-id", default=DEFAULT_BUILD_ID)
    parser.add_argument("--kind", choices=("runtime", "probe"), required=True)
    parser.add_argument("--require-summary", action="store_true")
    parser.add_argument("--require-candidate-visibility-toggle", action="store_true")
    parser.add_argument("--require-conversion-sync", action="store_true")
    parser.add_argument("--require-t2", action="store_true")
    parser.add_argument("--require-comless", choices=("uninitialized", "mta"))
    args = parser.parse_args()

    records, errors = load_records(args.paths)
    validation_errors, gaps = validate_records(records, args.stage, args.build_id)
    errors.extend(validation_errors)
    gaps.extend(evidence_gaps(records, args.kind, args.require_summary))
    if args.require_candidate_visibility_toggle:
        gaps.extend(candidate_visibility_gaps(records, args.kind))
    if args.require_conversion_sync:
        gaps.extend(conversion_sync_gaps(records, args.kind))
    if args.require_t2:
        gaps.extend(t2_evidence_gaps(records, args.kind))
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
