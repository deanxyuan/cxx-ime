"""Shared JSONL loading and schema checks for host trace tools."""

from __future__ import annotations

import collections
import json
import os
from typing import Any


DEFAULT_BUILD_ID = "cxxime-host-takeover-20260725-b"
COMMON_FIELDS = {
    "schema_version",
    "build_id",
    "arch",
    "event",
    "seq",
    "timestamp_100ns",
    "pid",
    "tid",
    "process",
    "component",
}
TEXT_PAYLOAD_FIELDS = {"candidate", "candidates", "commit", "preedit", "reading", "text"}


def load_records(paths: list[str]) -> tuple[list[dict[str, Any]], list[str]]:
    records: list[dict[str, Any]] = []
    errors: list[str] = []
    for path in paths:
        try:
            with open(path, encoding="utf-8-sig") as stream:
                for line_number, line in enumerate(stream, 1):
                    if not line.strip():
                        continue
                    try:
                        value = json.loads(line)
                    except json.JSONDecodeError as error:
                        errors.append(f"{path}:{line_number}: invalid JSON: {error.msg}")
                        continue
                    if not isinstance(value, dict):
                        errors.append(f"{path}:{line_number}: record is not an object")
                        continue
                    value["_source"] = f"{os.path.basename(path)}:{line_number}"
                    records.append(value)
        except OSError as error:
            errors.append(f"{path}: cannot read: {error}")
    return records, errors


def validate_records(
    records: list[dict[str, Any]], build_id: str
) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    gaps: list[str] = []
    for record in records:
        source = record["_source"]
        missing = sorted(COMMON_FIELDS - record.keys())
        if missing:
            errors.append(f"{source}: missing common field(s): {', '.join(missing)}")
        if record.get("schema_version") != 2:
            errors.append(f"{source}: unsupported schema_version")
        if record.get("build_id") != build_id:
            errors.append(f"{source}: expected build_id {build_id}")
        leaked = sorted(TEXT_PAYLOAD_FIELDS & record.keys())
        if leaked:
            errors.append(f"{source}: raw text field(s) are forbidden: {', '.join(leaked)}")

        if record.get("event") in {"candidate.snapshot", "probe.candidate_snapshot"}:
            count = record.get("count")
            lengths = record.get("text_lengths")
            digests = record.get("text_digests")
            selection = record.get("selection")
            if not isinstance(count, int) or count < 0:
                errors.append(f"{source}: candidate count is invalid")
            if not isinstance(lengths, list) or any(
                not isinstance(length, int) or length < 0 for length in lengths
            ):
                errors.append(f"{source}: text_lengths is invalid")
            elif isinstance(count, int) and len(lengths) != count:
                errors.append(f"{source}: count does not match text_lengths")
            if not isinstance(digests, list) or any(
                not isinstance(digest, str) or len(digest) != 64 for digest in digests
            ):
                errors.append(f"{source}: text_digests is invalid")
            elif isinstance(count, int) and len(digests) != count:
                errors.append(f"{source}: count does not match text_digests")
            if isinstance(count, int) and count > 0 and (
                not isinstance(selection, int) or selection < 0 or selection >= count
            ):
                errors.append(f"{source}: candidate selection is out of range")

    if not records:
        gaps.append("no records")
    return errors, gaps


def evidence_gaps(
    records: list[dict[str, Any]], kind: str, require_summary: bool
) -> list[str]:
    events = {str(record.get("event", "")) for record in records}
    if kind == "probe":
        required = {"probe.runtime", "probe.ui_element", "probe.candidate_snapshot"}
    else:
        required = {"runtime.component_status", "key.route", "candidate.snapshot"}
        if not ({"runtime.activate", "legacy.callback"} & events):
            required.add("runtime.activate or legacy.callback")
    if require_summary:
        required.add("trace.summary")
    gaps = []
    for event in sorted(required):
        if event == "runtime.activate or legacy.callback":
            if not ({"runtime.activate", "legacy.callback"} & events):
                gaps.append(f"missing evidence event: {event}")
        elif event not in events:
            gaps.append(f"missing evidence event: {event}")
    return gaps


def report(records: list[dict[str, Any]], kind: str) -> None:
    event_counts = collections.Counter(str(record.get("event", "")) for record in records)
    component_counts = collections.Counter(str(record.get("component", "")) for record in records)
    routes: dict[Any, set[str]] = collections.defaultdict(set)
    for record in records:
        if record.get("event") == "key.route" and record.get("input_id"):
            routes[record["input_id"]].add(str(record.get("owner", "")))
    conflicts = {input_id: owners for input_id, owners in routes.items() if len(owners) > 1}

    print(f"kind={kind} records={len(records)}")
    print("components=" + ", ".join(
        f"{name}:{count}" for name, count in sorted(component_counts.items())
    ))
    print("events=" + ", ".join(
        f"{name}:{count}" for name, count in sorted(event_counts.items())
    ))
    print(f"owner_conflicts={len(conflicts)}")
