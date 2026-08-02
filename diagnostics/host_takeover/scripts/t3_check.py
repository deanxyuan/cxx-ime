"""T3 COM-less activation evidence checks."""

from __future__ import annotations

from typing import Any


TF_COMLESS = 0x8
TF_IPP_CAPS_UIELEMENTENABLED = 0x4


def probe_comless_gaps(
    records: list[dict[str, Any]], com_mode: str
) -> list[str]:
    gaps: list[str] = []
    activation_verified = any(
        record.get("event") == "probe.runtime"
        and record.get("result") == "ready"
        and isinstance(record.get("activate_flags"), int)
        and (record["activate_flags"] & TF_COMLESS) != 0
        and record.get("com_mode") == com_mode
        and record.get("thread_manager_factory") == "TF_CreateThreadMgr"
        for record in records
    )
    if not activation_verified:
        gaps.append(
            f"T3: Probe did not use the TSF factory in {com_mode} COM mode"
        )

    if not any(
        record.get("event") == "probe.active_profile"
        and record.get("result") == "verified"
        and record.get("manager_creation") == "without_com"
        and record.get("comless_category_hr") == 0
        and record.get("comless_category_registered") is True
        for record in records
    ):
        gaps.append(
            "T3: Probe did not verify the COM-less profile through "
            "without-COM manager creation"
        )
    if not any(
        record.get("event") == "probe.display_attribute"
        and record.get("result") == "verified"
        and record.get("manager_creation") == "without_com"
        for record in records
    ):
        gaps.append(
            "T3: Probe did not resolve the composition display attribute through "
            "without-COM manager creation"
        )
    if not any(
        record.get("event") == "probe.candidate_snapshot"
        and record.get("result") == "read"
        and isinstance(record.get("count"), int)
        and record["count"] > 0
        for record in records
    ):
        gaps.append("T3: Probe did not read a non-empty candidate snapshot")
    if not any(
        record.get("event") == "probe.imm_read"
        and isinstance(record.get("result_bytes"), int)
        and record["result_bytes"] > 0
        for record in records
    ):
        gaps.append("T3: Probe did not read a committed result")
    return gaps


def runtime_comless_gaps(records: list[dict[str, Any]]) -> list[str]:
    gaps: list[str] = []
    activation_verified = any(
        record.get("event") == "runtime.activate"
        and record.get("result") == "success"
        and isinstance(record.get("activate_flags"), int)
        and (record["activate_flags"] & TF_COMLESS) != 0
        and record.get("manager_creation") == "without_com"
        and record.get("profile_manager_hr") == 0
        and record.get("profile_query_hr") == 0
        and isinstance(record.get("profile_caps"), int)
        and (record["profile_caps"] & TF_IPP_CAPS_UIELEMENTENABLED) != 0
        for record in records
    )
    if not activation_verified:
        gaps.append(
            "T3: runtime COM-less activation or without-COM profile factory "
            "evidence is incomplete"
        )

    if not any(
        record.get("event") == "candidate.snapshot"
        and isinstance(record.get("count"), int)
        and record["count"] > 0
        for record in records
    ):
        gaps.append("T3: runtime did not publish a non-empty candidate snapshot")
    return gaps


def comless_evidence_gaps(
    records: list[dict[str, Any]], kind: str, com_mode: str
) -> list[str]:
    return (
        probe_comless_gaps(records, com_mode)
        if kind == "probe"
        else runtime_comless_gaps(records)
    )
