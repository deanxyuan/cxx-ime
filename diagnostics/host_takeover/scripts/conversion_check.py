"""Focused conversion compartment evidence checks."""

from __future__ import annotations

from typing import Any


TF_CONVERSIONMODE_NATIVE = 0x1


def conversion_sync_gaps(
    records: list[dict[str, Any]], kind: str
) -> list[str]:
    gaps: list[str] = []
    if kind == "probe":
        if not any(
            record.get("event") == "probe.conversion_subscription"
            and record.get("result") == "subscribed"
            for record in records
        ):
            gaps.append("conversion: Probe compartment sink was not subscribed")
        writes = [
            record for record in records
            if record.get("event") == "probe.conversion_write"
            and record.get("result") == "written"
            and record.get("write_hr") == 0
        ]
        if len(writes) != 3:
            gaps.append("conversion: Probe did not complete exactly three toggle writes")
        else:
            mode_changes = [
                (
                    bool(record.get("previous_mode", 0) & TF_CONVERSIONMODE_NATIVE),
                    bool(record.get("requested_mode", 0) & TF_CONVERSIONMODE_NATIVE),
                )
                for record in writes
            ]
            if mode_changes != [(True, False), (False, True), (True, False)]:
                gaps.append("conversion: Probe toggle directions were not CN->EN, EN->CN, CN->EN")
        if not any(
            record.get("event") == "probe.conversion_change"
            and record.get("result") == "notified"
            for record in records
        ):
            gaps.append("conversion: Probe did not receive a change notification")
        return gaps

    if not any(
        record.get("event") == "runtime.conversion_sink"
        and record.get("action") == "advise"
        and record.get("operation_hr") == 0
        and record.get("result") == "success"
        for record in records
    ):
        gaps.append("conversion: runtime compartment sink was not subscribed")

    composition_applied = any(
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
    if not composition_applied:
        gaps.append("conversion: composition change did not commit raw input and apply mode")

    if any(record.get("event") == "runtime.conversion_deferred" for record in records):
        gaps.append("conversion: obsolete deferred compartment restore was used")

    if any(
        record.get("event") == "runtime.conversion_compartment"
        and record.get("result") == "failed"
        for record in records
    ):
        gaps.append("conversion: a compartment write failed")

    applied_modes = {
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
        and record.get("after_chinese") is record.get("requested_chinese")
        and isinstance(record.get("requested_chinese"), bool)
    }
    if applied_modes != {False, True}:
        gaps.append("conversion: mode settings were not applied in both directions")
    return gaps
