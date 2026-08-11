"""Host UI element and capability evidence checks."""

from __future__ import annotations

from typing import Any


TF_UI_ELEMENT_ENABLED_ONLY = 0x4
TF_IPP_CAPS_UIELEMENTENABLED = 0x4
TF_CLUIE_REQUIRED_FLAGS = 0x3E
VK_OEM_PLUS = 0xBB
VK_OEM_MINUS = 0xBD


def candidate_visibility_gaps(
    records: list[dict[str, Any]], kind: str
) -> list[str]:
    event_name = "probe.ui_element_visibility" if kind == "probe" else "ui_element.show"
    matching = [record for record in records if record.get("event") == event_name]
    applied: list[bool] = []
    for record in matching:
        requested = record.get("requested_show")
        actual = record.get("actual_show")
        if not isinstance(requested, bool) or requested != actual:
            continue
        if kind == "probe":
            succeeded = (
                record.get("show_hr") == 0
                and record.get("is_shown_hr") == 0
                and record.get("result") == "applied"
            )
        else:
            succeeded = record.get("hr") == 0 and record.get("result") == "success"
        if succeeded:
            applied.append(requested)

    try:
        show_index = applied.index(True)
        applied.index(False, show_index + 1)
    except ValueError:
        return [f"missing successful {event_name} TRUE -> FALSE transition"]
    return []


def probe_host_behavior_gaps(records: list[dict[str, Any]]) -> list[str]:
    gaps: list[str] = []

    runtime_ready = any(
        record.get("event") == "probe.runtime"
        and record.get("result") == "ready"
        and isinstance(record.get("activate_flags"), int)
        and record["activate_flags"] & TF_UI_ELEMENT_ENABLED_ONLY
        for record in records
    )
    if not runtime_ready:
        gaps.append("Probe did not activate with UIELEMENTENABLEDONLY")

    profile_fields = (
        "category_is_keyboard",
        "profile_caps_ui_element",
        "keyboard_category_registered",
        "ui_element_category_registered",
        "input_mode_category_registered",
        "display_attribute_category_registered",
    )
    profile_verified = any(
        record.get("event") == "probe.active_profile"
        and record.get("result") == "verified"
        and all(record.get(field) in (True, 1) for field in profile_fields)
        for record in records
    )
    if not profile_verified:
        gaps.append("active keyboard profile categories were not verified")

    snapshots = [
        record for record in records
        if record.get("event") == "probe.candidate_snapshot"
        and record.get("result") == "read"
        and isinstance(record.get("updated_flags"), int)
        and record["updated_flags"] & TF_CLUIE_REQUIRED_FLAGS == TF_CLUIE_REQUIRED_FLAGS
        and all(record.get(field) == 0 for field in (
            "flags_hr", "count_hr", "selection_hr", "page_hr",
            "current_page_hr", "strings_hr", "behavior_hr",
        ))
        and isinstance(record.get("page_count"), int)
        and record["page_count"] > 0
        and isinstance(record.get("current_page"), int)
        and 0 <= record["current_page"] < record["page_count"]
    ]
    if not snapshots:
        gaps.append("candidate UIElement methods or paging metadata were incomplete")

    candidate_ids = {
        record.get("element_id") for record in snapshots
        if isinstance(record.get("element_id"), int)
    }
    lifecycle_actions: set[str] = set()
    for record in records:
        if record.get("event") != "probe.ui_element":
            continue
        if record.get("element_id") not in candidate_ids:
            continue
        action = record.get("action")
        if isinstance(action, str):
            lifecycle_actions.add(action)
    for action in ("begin", "update", "end"):
        if action not in lifecycle_actions:
            gaps.append(f"missing candidate UIElement {action} callback")

    display_attribute_verified = any(
        record.get("event") == "probe.display_attribute"
        and record.get("result") == "verified"
        and record.get("value_hr") == 0
        and record.get("value_type") == 3
        and isinstance(record.get("atom"), int)
        and record["atom"] > 0
        and record.get("atom_guid_hr") == 0
        and record.get("display_info_hr") == 0
        and record.get("attribute_hr") == 0
        for record in records
    )
    if not display_attribute_verified:
        gaps.append("composition display attribute was not resolved")

    if not any(
        record.get("event") == "probe.conversion_subscription"
        and record.get("result") == "subscribed"
        for record in records
    ):
        gaps.append("conversion compartment sink was not subscribed")
    if not any(
        record.get("event") == "probe.conversion_compartment"
        and record.get("result") == "read"
        for record in records
    ):
        gaps.append("conversion compartment value was not read")
    if not any(
        record.get("event") == "probe.conversion_write"
        and record.get("result") == "written"
        for record in records
    ):
        gaps.append("conversion compartment was not actively changed")
    if not any(
        record.get("event") == "probe.conversion_change"
        and record.get("result") == "notified"
        for record in records
    ):
        gaps.append("conversion compartment change notification was not received")

    committed = any(
        record.get("event") == "probe.imm_read"
        and isinstance(record.get("result_bytes"), int)
        and record["result_bytes"] > 0
        for record in records
    )
    if not committed:
        gaps.append("Probe did not read a committed result")

    gaps.extend(candidate_visibility_gaps(records, "probe"))
    return gaps


def runtime_host_behavior_gaps(records: list[dict[str, Any]]) -> list[str]:
    gaps: list[str] = []

    activation_verified = any(
        record.get("event") == "runtime.activate"
        and record.get("result") == "success"
        and record.get("ui_element_only") is True
        and isinstance(record.get("activate_flags"), int)
        and record["activate_flags"] & TF_UI_ELEMENT_ENABLED_ONLY
        and record.get("profile_query_hr") == 0
        and isinstance(record.get("profile_caps"), int)
        and record["profile_caps"] & TF_IPP_CAPS_UIELEMENTENABLED
        for record in records
    )
    if not activation_verified:
        gaps.append("TSF activation flags or profile capability bits were incomplete")

    for event in ("ui_element.begin", "ui_element.update", "ui_element.end"):
        if not any(
            record.get("event") == event
            and record.get("element_type") == "candidate"
            and record.get("hr") == 0
            for record in records
        ):
            gaps.append(f"missing successful runtime {event}")

    page_records = [
        record for record in records
        if record.get("event") == "candidate.snapshot"
        and isinstance(record.get("engine_page_current"), int)
    ]
    pages = {record["engine_page_current"] for record in page_records}
    if 1 not in pages or not any(page > 1 for page in pages):
        gaps.append("candidate snapshots did not cover first and later pages")

    routed_keys = {
        record.get("vk") for record in records
        if record.get("event") == "key.route"
    }
    if VK_OEM_PLUS not in routed_keys or VK_OEM_MINUS not in routed_keys:
        gaps.append("'-' and '=' pagination keys were not both routed")

    aligned_modes: set[bool] = set()
    for record in records:
        if record.get("event") != "runtime.conversion_compartment":
            continue
        chinese_mode = record.get("chinese_mode")
        if not isinstance(chinese_mode, bool):
            continue
        if record.get("result") not in {"set", "already_aligned"}:
            continue
        if (record.get("requested_native") is chinese_mode and
                record.get("requested_symbol") is chinese_mode):
            aligned_modes.add(chinese_mode)
    if aligned_modes != {False, True}:
        gaps.append(
            "conversion compartment was not aligned in both Chinese and English modes"
        )

    sink_subscribed = any(
        record.get("event") == "runtime.conversion_sink"
        and record.get("action") == "advise"
        and record.get("operation_hr") == 0
        and record.get("result") == "success"
        for record in records
    )
    if not sink_subscribed:
        gaps.append("conversion compartment sink was not subscribed")

    externally_applied_modes = {
        record.get("requested_chinese")
        for record in records
        if record.get("event") == "runtime.conversion_change"
        and record.get("result") == "applied"
        and record.get("self_write") is False
        and record.get("set_attempted") is True
        and record.get("set_succeeded") is True
        and record.get("status_details") is True
        and record.get("before_full_shape") is record.get("after_full_shape")
        and record.get("before_chinese_punct") is record.get("after_chinese_punct")
        and record.get("before_input_mode") == record.get("after_input_mode")
        and isinstance(record.get("requested_chinese"), bool)
        and record.get("after_chinese") is record.get("requested_chinese")
    }
    if externally_applied_modes != {False, True}:
        gaps.append("external conversion changes were not applied in both directions")

    composition_change_applied = any(
        record.get("event") == "runtime.conversion_change"
        and record.get("result") == "applied"
        and record.get("self_write") is False
        and record.get("composing") is True
        and record.get("set_attempted") is True
        and record.get("set_succeeded") is True
        and record.get("commit_requested") is True
        and isinstance(record.get("commit_text_length"), int)
        and record.get("commit_text_length") > 0
        and record.get("status_details") is True
        and record.get("before_full_shape") is record.get("after_full_shape")
        and record.get("before_chinese_punct") is record.get("after_chinese_punct")
        and record.get("before_input_mode") == record.get("after_input_mode")
        and record.get("after_chinese") is record.get("requested_chinese")
        for record in records
    )
    if not composition_change_applied:
        gaps.append("composition change did not commit raw input and apply mode")

    gaps.extend(candidate_visibility_gaps(records, "runtime"))
    return gaps


def host_behavior_evidence_gaps(
    records: list[dict[str, Any]], kind: str
) -> list[str]:
    return probe_host_behavior_gaps(records) if kind == "probe" else runtime_host_behavior_gaps(records)
