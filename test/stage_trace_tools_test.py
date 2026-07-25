#!/usr/bin/env python3

import argparse
import json
import os
import subprocess
import sys
import tempfile
import unittest


BUILD_ID = "cxxime-host-takeover-20260725-a"
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
                record("probe", "probe.runtime", 4, result="ready"),
                record("probe", "probe.ui_element", 5, element_id=9, action="begin"),
                record(
                    "probe",
                    "probe.candidate_snapshot",
                    6,
                    count=2,
                    selection=0,
                    text_lengths=[1, 2],
                    text_digests=["1" * 64, "2" * 64],
                ),
            ]
            raw_path = os.path.join(directory, "stage1-test-100-x64.jsonl")
            with open(raw_path, "w", encoding="utf-8", newline="\n") as stream:
                for value in raw:
                    stream.write(json.dumps(value, separators=(",", ":")) + "\n")

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
                    path,
                ])
                self.assertEqual(check.returncode, 0, check.stdout + check.stderr)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--powershell", default="powershell")
    args, remaining = parser.parse_known_args()
    StageTraceToolsTest.powershell = args.powershell
    unittest.main(argv=[sys.argv[0], *remaining])


if __name__ == "__main__":
    main()
