#!/usr/bin/env python3

import argparse
import json
import os
import subprocess
import sys
import tempfile
import unittest


BUILD_ID = "cxxime-host-takeover-20260725-b"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIAGNOSTICS = os.path.join(ROOT, "diagnostics", "host_takeover")


def record(component, event, seq, **fields):
    value = {
        "schema_version": 1,
        "build_id": BUILD_ID,
        "stage": 1,
        "arch": "x64",
        "event": event,
        "seq": seq,
        "timestamp_100ns": 1000 + seq,
        "pid": 100,
        "tid": 101,
        "process": "test.exe",
        "component": component,
    }
    value.update(fields)
    return value


class StageTraceToolsTest(unittest.TestCase):
    powershell = "powershell"

    def run_command(self, command):
        return subprocess.run(command, capture_output=True, text=True, check=False)

    def write_records(self, path, records):
        with open(path, "w", encoding="utf-8", newline="\n") as stream:
            for value in records:
                stream.write(json.dumps(value, separators=(",", ":")) + "\n")

    def test_export_and_check_runtime_and_probe_files(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = [
                record("tsf", "runtime.activate", 1, result="activated"),
                record("tsf", "key.route", 2, input_id=7, owner="tsf", engine_calls=1),
                record(
                    "tsf",
                    "candidate.snapshot",
                    3,
                    count=2,
                    selection=0,
                    text_lengths=[1, 2],
                    text_digests=["1" * 64, "2" * 64],
                ),
                record(
                    "tsf",
                    "ui_element.show",
                    4,
                    requested_show=True,
                    actual_show=True,
                    hr=0,
                    result="success",
                ),
                record(
                    "tsf",
                    "ui_element.show",
                    5,
                    requested_show=False,
                    actual_show=False,
                    hr=0,
                    result="success",
                ),
                record("probe", "probe.runtime", 6, result="ready"),
                record("probe", "probe.ui_element", 7, element_id=9, action="begin"),
                record(
                    "probe",
                    "probe.candidate_snapshot",
                    8,
                    count=2,
                    selection=0,
                    text_lengths=[1, 2],
                    text_digests=["1" * 64, "2" * 64],
                ),
                record(
                    "probe",
                    "probe.ui_element_visibility",
                    9,
                    requested_show=True,
                    actual_show=True,
                    show_hr=0,
                    is_shown_hr=0,
                    result="applied",
                ),
                record(
                    "probe",
                    "probe.ui_element_visibility",
                    10,
                    requested_show=False,
                    actual_show=False,
                    show_hr=0,
                    is_shown_hr=0,
                    result="applied",
                ),
            ]
            raw_path = os.path.join(directory, "stage1-test-100-x64.jsonl")
            self.write_records(raw_path, raw)

            export = self.run_command([
                self.powershell,
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                os.path.join(DIAGNOSTICS, "scripts", "export_stage_trace.ps1"),
                "-LogsDir",
                directory,
                "-OutputDir",
                directory,
            ])
            self.assertEqual(export.returncode, 0, export.stdout + export.stderr)

            for kind in ("runtime", "probe"):
                path = os.path.join(directory, f"cxxime-stage1-{kind}-{BUILD_ID}.jsonl")
                self.assertTrue(os.path.isfile(path), path)
                check = self.run_command([
                    sys.executable,
                    os.path.join(DIAGNOSTICS, "scripts", "check_stage_trace.py"),
                    "--kind",
                    kind,
                    "--require-summary",
                    "--require-candidate-visibility-toggle",
                    path,
                ])
                self.assertEqual(check.returncode, 0, check.stdout + check.stderr)

    def test_raw_trace_does_not_require_export_summary(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = [
                record("tsf", "runtime.activate", 1, result="activated"),
                record("tsf", "runtime.component_status", 2, result="loaded"),
                record("tsf", "key.route", 3, input_id=7, owner="tsf", engine_calls=1),
                record(
                    "tsf",
                    "candidate.snapshot",
                    4,
                    count=1,
                    selection=0,
                    text_lengths=[1],
                    text_digests=["1" * 64],
                ),
                record(
                    "tsf",
                    "ui_element.show",
                    5,
                    requested_show=True,
                    actual_show=True,
                    hr=0,
                    result="success",
                ),
                record(
                    "tsf",
                    "ui_element.show",
                    6,
                    requested_show=False,
                    actual_show=False,
                    hr=0,
                    result="success",
                ),
            ]
            raw_path = os.path.join(directory, "stage1-tsf-100-x64.jsonl")
            self.write_records(raw_path, raw)

            command = [
                sys.executable,
                os.path.join(DIAGNOSTICS, "scripts", "check_stage_trace.py"),
                "--kind",
                "runtime",
                "--require-candidate-visibility-toggle",
                raw_path,
            ]
            check = self.run_command(command)
            self.assertEqual(check.returncode, 0, check.stdout + check.stderr)

            check = self.run_command([*command, "--require-summary"])
            self.assertEqual(check.returncode, 2, check.stdout + check.stderr)
            self.assertIn("missing evidence event: stage.summary", check.stderr)

    def test_t2_evidence_accepts_complete_runtime_and_probe_traces(self):
        with tempfile.TemporaryDirectory() as directory:
            probe = [
                record("probe", "probe.runtime", 1,
                       activate_flags=4, result="ready"),
                record(
                    "probe", "probe.active_profile", 2,
                    category_is_keyboard=True,
                    profile_caps_ui_element=True,
                    keyboard_category_registered=True,
                    ui_element_category_registered=True,
                    input_mode_category_registered=True,
                    display_attribute_category_registered=True,
                    result="verified",
                ),
                record("probe", "probe.conversion_subscription", 3,
                       result="subscribed"),
                record("probe", "probe.conversion_compartment", 4,
                       conversion_mode=1025, result="read"),
                record("probe", "probe.conversion_write", 5,
                       previous_mode=1025, requested_mode=0,
                       write_hr=0, result="written"),
                record("probe", "probe.conversion_write", 6,
                       previous_mode=0, requested_mode=1025,
                       write_hr=0, result="written"),
                record("probe", "probe.conversion_write", 7,
                       previous_mode=1025, requested_mode=0,
                       write_hr=0, result="written"),
                record("probe", "probe.conversion_change", 8,
                       result="notified"),
                record("probe", "probe.ui_element", 9,
                       element_id=9, action="begin"),
                record(
                    "probe", "probe.candidate_snapshot", 10,
                    element_id=9,
                    updated_flags=62,
                    count=2,
                    selection=0,
                    page_count=1,
                    current_page=0,
                    text_lengths=[1, 2],
                    text_digests=["1" * 64, "2" * 64],
                    flags_hr=0,
                    count_hr=0,
                    selection_hr=0,
                    page_hr=0,
                    current_page_hr=0,
                    strings_hr=0,
                    behavior_hr=0,
                    result="read",
                ),
                record("probe", "probe.ui_element", 11,
                       element_id=9, action="update"),
                record(
                    "probe", "probe.display_attribute", 12,
                    value_hr=0,
                    value_type=3,
                    atom=7,
                    atom_guid_hr=0,
                    display_info_hr=0,
                    attribute_hr=0,
                    result="verified",
                ),
                record(
                    "probe", "probe.ui_element_visibility", 13,
                    requested_show=True,
                    actual_show=True,
                    show_hr=0,
                    is_shown_hr=0,
                    result="applied",
                ),
                record(
                    "probe", "probe.ui_element_visibility", 14,
                    requested_show=False,
                    actual_show=False,
                    show_hr=0,
                    is_shown_hr=0,
                    result="applied",
                ),
                record("probe", "probe.imm_read", 15,
                       result_bytes=4, result="read"),
                record("probe", "probe.ui_element", 16,
                       element_id=9, action="end"),
            ]
            runtime = [
                record("tsf", "runtime.component_status", 20,
                       result="loaded"),
                record(
                    "tsf", "runtime.activate", 21,
                    activate_flags=4,
                    ui_element_only=True,
                    profile_query_hr=0,
                    profile_caps=4,
                    result="success",
                ),
                record("tsf", "key.route", 22,
                       input_id=1, owner="tsf", engine_calls=1, vk=187),
                record("tsf", "key.route", 23,
                       input_id=2, owner="tsf", engine_calls=1, vk=189),
                record(
                    "tsf", "candidate.snapshot", 24,
                    count=2,
                    selection=0,
                    engine_page_current=1,
                    text_lengths=[1, 2],
                    text_digests=["1" * 64, "2" * 64],
                ),
                record(
                    "tsf", "candidate.snapshot", 25,
                    count=2,
                    selection=0,
                    engine_page_current=2,
                    text_lengths=[1, 2],
                    text_digests=["3" * 64, "4" * 64],
                ),
                record("tsf", "ui_element.begin", 26,
                       element_type="candidate", hr=0, result="success"),
                record("tsf", "ui_element.update", 27,
                       element_type="candidate", hr=0, result="success"),
                record(
                    "tsf", "ui_element.show", 28,
                    requested_show=True,
                    actual_show=True,
                    hr=0,
                    result="success",
                ),
                record(
                    "tsf", "ui_element.show", 29,
                    requested_show=False,
                    actual_show=False,
                    hr=0,
                    result="success",
                ),
                record(
                    "tsf", "runtime.conversion_compartment", 30,
                    chinese_mode=True,
                    requested_native=True,
                    requested_symbol=True,
                    result="set",
                ),
                record(
                    "tsf", "runtime.conversion_compartment", 31,
                    chinese_mode=False,
                    requested_native=False,
                    requested_symbol=False,
                    result="set",
                ),
                record(
                    "tsf", "runtime.conversion_sink", 32,
                    action="advise",
                    operation_hr=0,
                    result="success",
                ),
                record(
                    "tsf", "runtime.conversion_change", 33,
                    self_write=False,
                    composing=False,
                    set_attempted=True,
                    set_succeeded=True,
                    commit_requested=False,
                    commit_text_length=0,
                    ipc_us=120,
                    status_details=True,
                    before_full_shape=False,
                    after_full_shape=False,
                    before_chinese_punct=True,
                    after_chinese_punct=True,
                    before_input_mode=0,
                    after_input_mode=0,
                    requested_chinese=False,
                    after_chinese=False,
                    result="applied",
                ),
                record(
                    "tsf", "runtime.conversion_change", 34,
                    self_write=False,
                    composing=False,
                    set_attempted=True,
                    set_succeeded=True,
                    commit_requested=False,
                    commit_text_length=0,
                    ipc_us=110,
                    status_details=True,
                    before_full_shape=False,
                    after_full_shape=False,
                    before_chinese_punct=True,
                    after_chinese_punct=True,
                    before_input_mode=0,
                    after_input_mode=0,
                    requested_chinese=True,
                    after_chinese=True,
                    result="applied",
                ),
                record(
                    "tsf", "runtime.conversion_change", 35,
                    self_write=False,
                    composing=True,
                    before_chinese=True,
                    after_chinese=False,
                    requested_chinese=False,
                    set_attempted=True,
                    set_succeeded=True,
                    commit_requested=True,
                    commit_text_length=2,
                    ipc_us=130,
                    status_details=True,
                    before_full_shape=False,
                    after_full_shape=False,
                    before_chinese_punct=True,
                    after_chinese_punct=True,
                    before_input_mode=0,
                    after_input_mode=0,
                    result="applied",
                ),
                record("tsf", "ui_element.end", 36,
                       element_type="candidate", hr=0, result="success"),
            ]

            for kind, values in (("probe", probe), ("runtime", runtime)):
                path = os.path.join(directory, f"stage1-{kind}-t2-x64.jsonl")
                self.write_records(path, values)
                check = self.run_command([
                    sys.executable,
                    os.path.join(DIAGNOSTICS, "scripts", "check_stage_trace.py"),
                    "--kind",
                    kind,
                    "--require-t2",
                    path,
                ])
                self.assertEqual(check.returncode, 0, check.stdout + check.stderr)

                check = self.run_command([
                    sys.executable,
                    os.path.join(DIAGNOSTICS, "scripts", "check_stage_trace.py"),
                    "--kind",
                    kind,
                    "--require-conversion-sync",
                    path,
                ])
                self.assertEqual(check.returncode, 0, check.stdout + check.stderr)

            runtime = [
                value for value in runtime
                if not (
                    value.get("event") == "runtime.conversion_change"
                    and value.get("composing") is True
                )
            ]
            self.write_records(path, runtime)
            check = self.run_command([
                sys.executable,
                os.path.join(DIAGNOSTICS, "scripts", "check_stage_trace.py"),
                "--kind",
                "runtime",
                "--require-t2",
                path,
            ])
            self.assertEqual(check.returncode, 2, check.stdout + check.stderr)
            self.assertIn(
                "composition change did not commit raw input and apply mode",
                check.stderr,
            )

    def test_t2_evidence_reports_missing_display_attribute(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "stage1-probe-t2-x64.jsonl")
            values = [
                record("probe", "probe.runtime", 1,
                       activate_flags=4, result="ready"),
                record("probe", "probe.ui_element", 2,
                       element_id=9, action="begin"),
                record(
                    "probe", "probe.candidate_snapshot", 3,
                    element_id=9,
                    count=1,
                    selection=0,
                    text_lengths=[1],
                    text_digests=["1" * 64],
                ),
            ]
            self.write_records(path, values)
            check = self.run_command([
                sys.executable,
                os.path.join(DIAGNOSTICS, "scripts", "check_stage_trace.py"),
                "--kind",
                "probe",
                "--require-t2",
                path,
            ])
            self.assertEqual(check.returncode, 2, check.stdout + check.stderr)
            self.assertIn(
                "T2: composition display attribute was not resolved",
                check.stderr,
            )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--powershell", default="powershell")
    args, remaining = parser.parse_known_args()
    StageTraceToolsTest.powershell = args.powershell
    unittest.main(argv=[sys.argv[0], *remaining])


if __name__ == "__main__":
    main()
