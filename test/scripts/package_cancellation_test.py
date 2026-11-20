#!/usr/bin/env python3
# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

from __future__ import annotations

import contextlib
import ctypes
import multiprocessing
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import package
import prepare_dictionary_bundle as bundle


def write_result(directory, name):
    path = Path(directory) / name
    path.write_bytes(name.encode("ascii"))
    return [str(path)]


def hold_output(directory, markers, name):
    # Keep a file open just as a builder does. Cancellation must close it before cleanup.
    with (Path(directory) / name).open("wb") as output:
        output.write(b"incomplete")
        output.flush()
        (Path(markers) / name).write_text(str(os.getpid()), encoding="ascii")
        while True:
            time.sleep(0.05)


def fail_worker():
    raise ValueError("dictionary worker failed")


def enable_console_interrupt():
    if os.name == "nt":
        if not ctypes.windll.kernel32.SetConsoleCtrlHandler(None, False):
            raise ctypes.WinError()


def native_build_fixture(markers):
    enable_console_interrupt()
    signal.signal(signal.SIGINT, lambda *_: sys.exit(130))
    (Path(markers) / "build").write_text(str(os.getpid()), encoding="ascii")
    while True:
        time.sleep(0.05)


def interrupt_package_fixture(directory, workers, build_state):
    root = Path(directory)
    markers = root / "markers"
    markers.mkdir()
    output = root / "dist" / "data"
    output.mkdir(parents=True)
    (output / "existing.bin").write_bytes(b"keep")
    enable_console_interrupt()

    def generate(data_dir, staging, **kwargs):
        # The real bundle owns staging; workers hold real files in that directory.
        tasks = [
            (hold_output, (staging, str(markers), "pinyin")),
            (hold_output, (staging, str(markers), "wubi")),
        ]
        return bundle._run_dictionary_tasks(tasks, kwargs["workers"])

    def build(*args, **kwargs):
        cancel_event = kwargs["cancel_event"]
        original_popen = subprocess.Popen

        def start_process(command, **options):
            if build_state == "during_start" and "--native-build-fixture" in command:
                (markers / "build-ready").touch()
                if not cancel_event.wait(timeout=15):
                    raise AssertionError("build cancellation was not delivered")
                process = original_popen(command, **options)
                (markers / "late-start").write_text(str(process.pid), encoding="ascii")
                return process
            return original_popen(command, **options)

        if build_state == "before_start":
            (markers / "build-ready").touch()
            if not cancel_event.wait(timeout=15):
                raise AssertionError("build cancellation was not delivered")
        with mock.patch.object(subprocess, "Popen", side_effect=start_process):
            package.run(
                [
                    sys.executable,
                    str(Path(__file__).resolve()),
                    "--native-build-fixture",
                    str(markers),
                ],
                cancel_event=cancel_event,
            )

    def interrupt_when_ready():
        expected = ["pinyin"]
        if workers == 2:
            expected.append("wubi")
        if build_state == "running":
            expected.append("build")
        elif build_state != "none":
            expected.append("build-ready")
        deadline = time.monotonic() + 15
        while not all((markers / name).is_file() for name in expected):
            if time.monotonic() >= deadline:
                return
            time.sleep(0.01)
        if os.name == "nt":
            # This fixture has its own hidden console; never signal the user's console.
            if not ctypes.windll.kernel32.GenerateConsoleCtrlEvent(0, 0):
                raise ctypes.WinError()
        else:
            os.killpg(os.getpid(), signal.SIGINT)

    arguments = [
        "package.py",
        "--skip-nsis",
        "--skip-x86-tsf",
        "--dict-workers",
        str(workers),
    ]
    if build_state == "none":
        arguments.append("--skip-build")

    with contextlib.ExitStack() as stack:
        for name in (
            "validate_package_paths",
            "verify_prebuilt_host_mode",
            "clean_dist",
        ):
            stack.enter_context(mock.patch.object(package, name))
        stack.enter_context(mock.patch.object(package, "DIST_DIR", str(output.parent)))
        stack.enter_context(mock.patch.object(
            package,
            "choose_cmake_generator",
            return_value=(None, None),
        ))
        stack.enter_context(mock.patch.object(package, "validate_build_options", return_value=None))
        stack.enter_context(mock.patch.object(package, "resolve_cmake", return_value="cmake"))
        stack.enter_context(mock.patch.object(
            package,
            "resolve_build_toolchain",
            return_value=(None, None, None),
        ))
        stack.enter_context(mock.patch.object(package, "build", side_effect=build))
        stack.enter_context(mock.patch.object(bundle, "_generate_dictionary_bundle", generate))
        copy_files = stack.enter_context(mock.patch.object(package, "copy_binaries"))
        stack.enter_context(mock.patch.object(sys, "argv", arguments))
        interrupt = threading.Thread(target=interrupt_when_ready, daemon=True)
        interrupt.start()
        try:
            package.main()
        except KeyboardInterrupt:
            interrupt.join(timeout=1)
            assert not multiprocessing.active_children(), "dictionary workers survived Ctrl+C"
            assert list(output.iterdir()) == [output / "existing.bin"], "staging was not removed"
            assert (output / "existing.bin").read_bytes() == b"keep"
            if build_state == "before_start":
                assert not (markers / "build").exists(), "build started despite cancellation"
            if build_state == "during_start":
                assert (markers / "late-start").exists(), "process creation race was not exercised"
            copy_files.assert_not_called()
            return 130
    raise AssertionError("packaging did not stop on Ctrl+C")


class PackageCancellationTest(unittest.TestCase):
    def test_dictionary_workers_return_files_or_propagate_failure(self):
        with tempfile.TemporaryDirectory(prefix="cxxime-workers-test-") as directory:
            for workers in (1, 2):
                with self.subTest(workers=workers):
                    result = bundle._run_dictionary_tasks(
                        [
                            (write_result, (directory, "pinyin")),
                            (write_result, (directory, "wubi")),
                        ],
                        workers,
                    )
                    self.assertEqual(
                        [Path(path).read_bytes() for path in result],
                        [b"pinyin", b"wubi"],
                    )
                    with self.assertRaisesRegex(ValueError, "dictionary worker failed"):
                        bundle._run_dictionary_tasks(
                            [
                                (fail_worker, ()),
                                (hold_output, (directory, directory, "unfinished")),
                            ],
                            workers,
                        )
                    self.assertFalse(multiprocessing.active_children())

    def test_ctrl_c_stops_dictionary_workers_and_parallel_build_without_publishing(self):
        for workers in (1, 2):
            for build_state in ("none", "running", "before_start", "during_start"):
                with self.subTest(workers=workers, build_state=build_state):
                    with tempfile.TemporaryDirectory(prefix="cxxime-cancel-test-") as directory:
                        options = {}
                        if os.name == "nt":
                            startup = subprocess.STARTUPINFO()
                            startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
                            startup.wShowWindow = subprocess.SW_HIDE
                            options.update(
                                creationflags=subprocess.CREATE_NEW_CONSOLE,
                                startupinfo=startup,
                            )
                        else:
                            options["start_new_session"] = True
                        command = [
                            sys.executable,
                            str(Path(__file__).resolve()),
                            "--interrupt-fixture",
                            directory,
                            str(workers),
                            build_state,
                        ]
                        with subprocess.Popen(
                            command,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            **options,
                        ) as process:
                            try:
                                output, _ = process.communicate(timeout=25)
                            except subprocess.TimeoutExpired:
                                if os.name == "nt":
                                    subprocess.run(
                                        ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                        capture_output=True,
                                        check=False,
                                    )
                                else:
                                    os.killpg(process.pid, signal.SIGKILL)
                                output, _ = process.communicate(timeout=5)
                                self.fail(f"Ctrl+C did not stop packaging: {output!r}")
                        self.assertEqual(
                            process.returncode,
                            130,
                            output.decode(errors="replace"),
                        )


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--native-build-fixture":
        native_build_fixture(sys.argv[2])
    elif len(sys.argv) > 1 and sys.argv[1] == "--interrupt-fixture":
        sys.exit(interrupt_package_fixture(
            sys.argv[2],
            int(sys.argv[3]),
            sys.argv[4],
        ))
    else:
        unittest.main()
