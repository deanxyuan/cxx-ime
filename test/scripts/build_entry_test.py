#!/usr/bin/env python3
# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

from __future__ import annotations

import contextlib
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import package


CMAKE = shutil.which("cmake")
if len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
    CMAKE = sys.argv.pop(1)


def clean_tool_environment() -> dict[str, str]:
    return {
        key: value
        for key, value in os.environ.items()
        if not key.startswith(("CMAKE_", "CXXIME_CMAKE", "CXXIME_BUILD_CONFIG"))
    }


class PackageEntryTest(unittest.TestCase):
    def setUp(self) -> None:
        self.stack = contextlib.ExitStack()
        self.addCleanup(self.stack.close)
        temporary = self.stack.enter_context(
            tempfile.TemporaryDirectory(prefix="cxxime-build-")
        )
        self.root = Path(temporary) / "project with spaces"
        self.root.mkdir()
        (self.root / "CMakeLists.txt").write_text("# source\n", encoding="utf-8")
        self.build_dir = self.root / "build-package"
        self.x86_dir = self.root / "build-package-x86"
        self.dist = self.root / "dist"
        self.output = Path(temporary) / "output"
        for name, value in {
            "ROOT": self.root,
            "DATA": self.root / "data",
            "SCRIPTS": self.root / "scripts",
            "DIST_DIR": self.dist,
            "DEFAULT_BUILD_DIR": self.build_dir,
            "DEFAULT_X86_BUILD_DIR": self.x86_dir,
            "DEFAULT_OUTPUT_DIR": self.output,
        }.items():
            self.stack.enter_context(mock.patch.object(package, name, str(value)))
        self.stack.enter_context(
            mock.patch.dict(os.environ, clean_tool_environment(), clear=True)
        )
        self.stdout = self.stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
        self.stderr = self.stack.enter_context(contextlib.redirect_stderr(io.StringIO()))
        self.dist.mkdir()
        (self.dist / "sentinel").write_text("existing output", encoding="utf-8")

    def invoke(self, *arguments: str) -> None:
        with mock.patch.object(sys, "argv", ["package.py", *arguments]):
            package.main()

    def reject(self, *arguments: str) -> None:
        with self.assertRaises(SystemExit) as result:
            self.invoke(*arguments)
        self.assertNotEqual(result.exception.code, 0)
        self.assertTrue((self.dist / "sentinel").is_file(), self.stderr.getvalue())

    def stub_package_outputs(self) -> None:
        for name in (
            "copy_binaries",
            "copy_config",
            "prepare_dictionaries",
            "finalize_topn_data",
            "verify_dictionary_bundle",
            "check_debug_crt",
            "check_hot_path_logs",
            "check_diagnostics_config",
            "copy_installer_scripts",
            "write_install_payload",
            "verify_package_layout",
            "build_nsis",
            "print_summary",
        ):
            self.stack.enter_context(mock.patch.object(package, name))

    def write_cache(self, directory: Path, source: Path | None = None) -> None:
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "CMakeCache.txt").write_text(
            f"CMAKE_HOME_DIRECTORY:INTERNAL={source or self.root}\n"
            "CXXIME_ENABLE_HOST_DIAGNOSTICS:BOOL=OFF\n"
            "CXXIME_BUILD_HOST_PROBE:BOOL=OFF\n",
            encoding="utf-8",
        )

    def cmake_command(self, command: list[str], **kwargs) -> None:
        if "-S" not in command:
            return
        directory = Path(command[command.index("-B") + 1])
        self.write_cache(directory)
        generator = (
            command[command.index("-G") + 1]
            if "-G" in command
            else "Visual Studio 18 2026"
        )
        instance = os.environ.get("CMAKE_GENERATOR_INSTANCE", str(self.root / "default VS"))
        for argument in command:
            if argument.startswith("-DCMAKE_GENERATOR_INSTANCE="):
                instance = argument.partition("=")[2]
        with (directory / "CMakeCache.txt").open("a", encoding="utf-8") as output:
            output.write(f"CMAKE_GENERATOR:INTERNAL={generator}\n")
            if generator.startswith("Visual Studio "):
                output.write(f"CMAKE_GENERATOR_INSTANCE:INTERNAL={instance}\n")

    def test_invalid_requests_preserve_existing_output(self) -> None:
        for arguments in (
            ("--generator", "Ninja"),
            ("--generator", "Visual Studio 18 2026", "--platform", "Win32"),
            (
                "--generator",
                "Ninja",
                "--skip-x86-tsf",
                "--skip-nsis",
                "--platform",
                "x64",
            ),
            (
                "--generator",
                "Visual Studio 15 2017",
                "--cmake",
                str(self.root / "missing.exe"),
            ),
        ):
            with self.subTest(arguments=arguments):
                self.reject(*arguments)

    def test_dual_build_uses_one_tool_and_independent_platform_selection(self) -> None:
        self.stub_package_outputs()
        cmake = str(self.root / "tools with spaces" / "cmake.exe")
        os.environ["CXXIME_CMAKE"] = cmake
        os.environ["CMAKE_GENERATOR"] = "Visual Studio 18 2026"
        os.environ["CMAKE_GENERATOR_PLATFORM"] = "x64"
        os.environ["CMAKE_GENERATOR_INSTANCE"] = str(self.root / "selected VS")
        self.write_cache(self.build_dir)
        (self.build_dir / "old-output").touch()
        with (
            mock.patch.object(package.shutil, "which", return_value=cmake),
            mock.patch.object(package, "run", side_effect=self.cmake_command) as run,
        ):
            self.invoke(
                "--generator",
                "Visual Studio 15 2017",
                "--skip-dict",
                "--skip-nsis",
            )
        commands = [call.args[0] for call in run.call_args_list]
        self.assertEqual(len(commands), 7)
        self.assertEqual(commands[0], [cmake, "--version"])
        self.assertTrue(all(command[0] == cmake for command in commands))
        configure = [command for command in commands if "-S" in command]
        self.assertEqual(
            {command[command.index("-A") + 1] for command in configure},
            {"x64", "Win32"},
        )
        for command in configure:
            self.assertEqual(command[command.index("-G") + 1], "Visual Studio 15 2017")
            if "-DCXXIME_PRODUCTION_BUILD=ON" in command:
                self.assertNotIn("-DCMAKE_BUILD_TYPE=Release", command)
                self.assertIn(
                    f"-DCMAKE_GENERATOR_INSTANCE={self.root / 'selected VS'}",
                    command,
                )
        for command in commands:
            if "--build" in command:
                self.assertEqual(command[command.index("--config") + 1], "Release")
        for call in run.call_args_list:
            if "-DCXXIME_PRODUCTION_BUILD=ON" in call.args[0]:
                self.assertEqual(call.kwargs["env"]["CXXIME_BUILD_CONFIG"], "Release")
        self.assertNotIn("CXXIME_BUILD_CONFIG", os.environ)
        self.assertFalse((self.build_dir / "old-output").exists())
        self.assertEqual(
            os.environ["CMAKE_GENERATOR_INSTANCE"],
            str(self.root / "selected VS"),
        )

    def test_default_package_pins_cmake_selection_before_cleaning_outputs(self) -> None:
        self.stub_package_outputs()

        def configure(command: list[str], **kwargs) -> None:
            if "-S" in command and "-DCXXIME_PRODUCTION_BUILD=ON" not in command:
                self.assertTrue((self.dist / "sentinel").exists())
            self.cmake_command(command)

        with (
            mock.patch.object(package.shutil, "which", return_value="cmake.exe"),
            mock.patch.object(package, "run", side_effect=configure) as run,
        ):
            self.invoke()
        commands = [
            call.args[0]
            for call in run.call_args_list
            if "-S" in call.args[0]
        ]
        self.assertEqual(len(commands), 4)
        self.assertNotIn("-G", commands[0])
        self.assertEqual(commands[0][commands[0].index("-A") + 1], "x64")
        for command in commands[1:]:
            self.assertEqual(command[command.index("-G") + 1], "Visual Studio 18 2026")
            self.assertIn(f"-DCMAKE_GENERATOR_INSTANCE={self.root / 'default VS'}", command)
        package.build_nsis.assert_called_once()
        self.assertNotIn("CMAKE_GENERATOR", os.environ)
        self.assertNotIn("CMAKE_GENERATOR_INSTANCE", os.environ)

    def test_toolchain_failure_for_either_architecture_preserves_all_outputs(self) -> None:
        self.stub_package_outputs()
        self.write_cache(self.build_dir)
        (self.build_dir / "keep").touch()
        for architecture in ("x64", "Win32"):
            with self.subTest(architecture=architecture):
                def configure(command: list[str], **kwargs) -> None:
                    if "-A" in command and command[command.index("-A") + 1] == architecture:
                        raise subprocess.CalledProcessError(9, command)
                    self.cmake_command(command)

                self.stderr.seek(0)
                self.stderr.truncate()
                with (
                    mock.patch.object(package.shutil, "which", return_value="cmake.exe"),
                    mock.patch.object(package, "run", side_effect=configure),
                ):
                    self.reject()
                self.assertIn(
                    f"Toolchain check failed ({architecture}, exit 9)",
                    self.stderr.getvalue(),
                )
                self.assertTrue((self.build_dir / "keep").exists())
                package.copy_binaries.assert_not_called()
                package.prepare_dictionaries.assert_not_called()

    @unittest.skipUnless(os.name == "nt" and CMAKE, "Windows and CMake are required")
    def test_real_cmake_selects_and_checks_both_visual_studio_architectures(self) -> None:
        with mock.patch.object(package, "run", wraps=package.run) as run:
            generator, platform, instance = package.resolve_build_toolchain(
                str(CMAKE),
                None,
                None,
                True,
            )
        self.assertTrue(generator.startswith("Visual Studio "))
        self.assertEqual(platform, "x64")
        self.assertTrue(instance)
        commands = [call.args[0] for call in run.call_args_list]
        self.assertEqual(len(commands), 2)
        self.assertNotIn("-G", commands[0])
        self.assertEqual(commands[1][commands[1].index("-A") + 1], "Win32")
        self.assertIn(f"-DCMAKE_GENERATOR_INSTANCE={instance}", commands[1])

    def test_generator_argument_does_not_discard_platform_environment(self) -> None:
        os.environ["CXXIME_CMAKE_PLATFORM"] = "Win32"
        os.environ["CMAKE_GENERATOR_PLATFORM"] = "x64"
        self.reject("--generator", "Visual Studio 15 2017")
        self.assertIn("requires platform x64", self.stderr.getvalue())

    def test_native_debug_configurations_keep_commands_and_output_layout(self) -> None:
        self.stub_package_outputs()
        os.environ["VSCMD_ARG_TGT_ARCH"] = "x64"
        for generator in ("NMake Makefiles", "Ninja", "Ninja Multi-Config"):
            with (
                self.subTest(generator=generator),
                mock.patch.object(package.shutil, "which", return_value="cmake.exe"),
                mock.patch.object(package, "run", side_effect=self.cmake_command) as run,
            ):
                self.invoke(
                    "--generator",
                    generator,
                    "--debug",
                    "--skip-dict",
                    "--skip-x86-tsf",
                    "--skip-nsis",
                )
                commands = [call.args[0] for call in run.call_args_list]
                configure = next(
                    command
                    for command in commands
                    if "-DCXXIME_PRODUCTION_BUILD=ON" in command
                )
                self.assertNotIn("-A", configure)
                self.assertNotIn("-DCMAKE_BUILD_TYPE=Debug", configure)
                configure_call = next(
                    call
                    for call in run.call_args_list
                    if call.args[0] == configure
                )
                self.assertEqual(configure_call.kwargs["env"]["CXXIME_BUILD_CONFIG"], "Debug")
                build = next(command for command in commands if "--build" in command)
                self.assertEqual(build[build.index("--config") + 1], "Debug")
                output = self.build_dir / "server"
                if generator == "Ninja Multi-Config":
                    output /= "Debug"
                output.mkdir(parents=True)
                binary = output / "cxxime-server.exe"
                binary.touch()
                self.assertEqual(
                    package.built_binary_path(
                        str(self.build_dir),
                        "Debug",
                        "server",
                        binary.name,
                    ),
                    str(binary),
                )
                self.write_cache(self.build_dir)

    def test_native_build_requires_known_x64_environment(self) -> None:
        for architecture in ("", "x86", "arm64"):
            with self.subTest(architecture=architecture):
                os.environ["VSCMD_ARG_TGT_ARCH"] = architecture
                self.reject("--generator", "Ninja", "--skip-x86-tsf", "--skip-nsis")

    def test_failures_identify_configuration_or_compilation_and_stop_packaging(self) -> None:
        self.stub_package_outputs()
        for stage in ("configure", "build"):
            with self.subTest(stage=stage):
                def fail_command(command: list[str], **kwargs) -> None:
                    if (
                        stage == "configure" and "-DCXXIME_PRODUCTION_BUILD=ON" in command
                        or stage == "build" and "--build" in command
                    ):
                        raise subprocess.CalledProcessError(9, command)
                    self.cmake_command(command)

                self.stderr.seek(0)
                self.stderr.truncate()
                with (
                    mock.patch.object(package.shutil, "which", return_value="cmake.exe"),
                    mock.patch.object(package, "run", side_effect=fail_command),
                ):
                    with self.assertRaises(SystemExit) as result:
                        self.invoke(
                            "--generator",
                            "Visual Studio 18 2026",
                            "--skip-dict",
                            "--skip-x86-tsf",
                            "--skip-nsis",
                        )
                self.assertNotEqual(result.exception.code, 0)
                error = self.stderr.getvalue()
                self.assertIn(str(self.build_dir), error)
                self.assertIn("exit 9", error)
                if stage == "configure":
                    self.assertIn("CMake configuration failed", error)
                    self.assertIn("Windows SDK", error)
                    self.assertIn("CMakeFiles", error)
                else:
                    self.assertIn("Compilation failed", error)
                    self.assertIn("--build", error)
                package.copy_binaries.assert_not_called()

    def test_tool_override_precedence_and_failure_do_not_fall_back(self) -> None:
        os.environ["CXXIME_CMAKE"] = "environment-cmake.exe"
        for explicit, expected in (
            ("chosen.exe", "chosen.exe"),
            (None, "environment-cmake.exe"),
        ):
            with (
                mock.patch.object(package.shutil, "which", return_value=expected) as which,
                mock.patch.object(package, "run"),
            ):
                package.resolve_cmake(explicit)
                which.assert_called_once_with(expected)
        del os.environ["CXXIME_CMAKE"]
        with (
            mock.patch.object(package.shutil, "which", return_value="cmake.exe") as which,
            mock.patch.object(package, "run"),
        ):
            package.resolve_cmake(None)
            which.assert_called_once_with("cmake")
        for failure in (
            OSError("cannot execute"),
            subprocess.CalledProcessError(7, "cmake"),
        ):
            with (
                mock.patch.object(package.shutil, "which", return_value="broken.exe"),
                mock.patch.object(package, "run", side_effect=failure) as run,
            ):
                self.reject(
                    "--generator",
                    "Visual Studio 15 2017",
                    "--cmake",
                    "broken.exe",
                )
                run.assert_called_once()

    def test_unsafe_paths_fail_before_any_cleanup(self) -> None:
        source = self.root / "engine"
        source.mkdir()
        (source / "CMakeLists.txt").touch()
        self.write_cache(source)
        unknown = self.root / "unrecognized"
        unknown.mkdir()
        (unknown / "keep.txt").touch()
        foreign = self.root / "foreign-build"
        self.write_cache(foreign, self.root.parent)
        cases = (
            ("--build-dir", str(source)),
            ("--build-dir", str(unknown)),
            ("--build-dir", str(foreign)),
            ("--build-dir", str(self.root)),
            ("--build-dir", str(self.root.parent)),
            ("--x86-build-dir", str(self.build_dir)),
            ("--x86-build-dir", str(self.build_dir / "nested")),
            ("--build-dir", str(self.dist)),
            ("--output-dir", str(self.build_dir / "output")),
        )
        for arguments in cases:
            with self.subTest(arguments=arguments):
                self.reject("--generator", "Visual Studio 15 2017", *arguments)
        self.assertTrue((source / "CMakeLists.txt").exists())
        self.assertTrue((unknown / "keep.txt").exists())

    @unittest.skipUnless(os.name == "nt", "Windows junction protection")
    def test_redirected_build_and_dictionary_paths_preserve_their_targets(self) -> None:
        target = self.root / "keep"
        target.mkdir()
        sentinel = target / "keep.txt"
        sentinel.touch()
        for link in (self.build_dir, self.dist / "data"):
            with self.subTest(link=link):
                subprocess.run(
                    [
                        "cmd.exe",
                        "/d",
                        "/c",
                        "mklink",
                        "/J",
                        str(link),
                        str(target),
                    ],
                    check=True,
                    capture_output=True,
                )
                try:
                    self.reject("--generator", "Visual Studio 15 2017")
                    self.assertTrue(sentinel.exists())
                finally:
                    os.rmdir(link)

    def test_skip_build_needs_no_toolchain_and_never_removes_builds(self) -> None:
        self.stub_package_outputs()
        for directory in (self.build_dir, self.x86_dir):
            self.write_cache(directory)
            (directory / "keep").touch()
        os.environ["CXXIME_CMAKE"] = "missing.exe"
        with mock.patch.object(package, "run") as run:
            self.invoke("--skip-build", "--skip-dict", "--skip-nsis")
            run.assert_not_called()
        self.assertTrue((self.build_dir / "keep").exists())
        self.assertTrue((self.x86_dir / "keep").exists())


@unittest.skipUnless(os.name == "nt" and CMAKE, "Windows and CMake are required")
class BatchEntryTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="cxxime-batch-")
        self.addCleanup(temporary.cleanup)
        self.parent = Path(temporary.name)
        self.root = self.parent / "project with spaces"
        self.root.mkdir()
        shutil.copy2(ROOT / "build.bat", self.root / "build.bat")
        shutil.copy2(ROOT / "VERSION", self.root / "VERSION")
        self.env = clean_tool_environment()
        self.env["CXXIME_CMAKE"] = str(CMAKE)

    def run_batch(self, argument: str = "") -> subprocess.CompletedProcess:
        return subprocess.run(
            f'cmd.exe /d /s /c ""{self.root / "build.bat"}" {argument}"',
            cwd=self.parent,
            env=self.env,
            capture_output=True,
            text=True,
            errors="replace",
            timeout=60,
        )

    def write_project(self, fail: str = "") -> None:
        # Exercise the real project initialization with a lightweight build target.
        preamble, separator, _ = (ROOT / "CMakeLists.txt").read_text(
            encoding="utf-8"
        ).partition("\nproject(")
        self.assertTrue(separator, "Root CMakeLists.txt must contain a project() declaration")
        (self.root / "CMakeLists.txt").write_text(
            preamble
            + "\nproject(BuildEntryFixture NONE)\n"
            "option(CXXIME_PRODUCTION_BUILD \"Production\" OFF)\n"
            "option(CXXIME_BUILD_TOOLS \"Tools\" ON)\n"
            "option(CXXIME_BUILD_TESTS \"Tests\" ON)\n"
            'file(WRITE "${CMAKE_BINARY_DIR}/parallel.txt" '
            '"$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")\n'
            "if(CMAKE_CONFIGURATION_TYPES)\n"
            "  set(suffix /$<CONFIG>)\n"
            "else()\n"
            "  set(suffix)\n"
            '  file(WRITE "${CMAKE_BINARY_DIR}/configuration.txt" "${CMAKE_BUILD_TYPE}")\n'
            "endif()\n"
            "file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/server${suffix}/cxxime-server.exe "
            "CONTENT empty)\n"
            "add_custom_target(fixture ALL COMMAND ${CMAKE_COMMAND} -E echo built)\n"
            + fail,
            encoding="utf-8",
        )

    def test_real_cmake_configures_and_builds_without_changing_caller_directory(self) -> None:
        generators = [None]
        if shutil.which("nmake"):
            generators.append("NMake Makefiles")
        for generator in generators:
            with self.subTest(generator=generator):
                if generator:
                    self.env["CMAKE_GENERATOR"] = generator
                self.env["CMAKE_BUILD_PARALLEL_LEVEL"] = "1"
                for configuration, argument in (
                    ("Release", ""),
                    ("Debug", "debug -j 2"),
                ):
                    self.write_project()
                    result = self.run_batch(argument)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertNotIn("Manually-specified variables were not used", result.stderr)
                    cache = package.read_cmake_cache(
                        str(self.root / "build"),
                        "CMAKE_BUILD_TYPE",
                    )
                    parallel = (self.root / "build" / "parallel.txt").read_text()
                    self.assertEqual(parallel, "2" if argument else "1")
                    multiple = package.read_cmake_cache(
                        str(self.root / "build"),
                        "CMAKE_CONFIGURATION_TYPES",
                    )
                    binary = self.root / "build" / "server"
                    if multiple:
                        self.assertIsNone(cache)
                        binary /= configuration
                    else:
                        self.assertEqual(cache, configuration)
                        self.assertEqual(
                            (self.root / "build" / "configuration.txt").read_text(),
                            configuration,
                        )
                    binary /= "cxxime-server.exe"
                    self.assertTrue(binary.exists(), result.stdout)
                    self.assertIn(str(binary), result.stdout)
                    self.assertNotIn("cxxime-test.exe", result.stdout)
                cleaned = self.run_batch("clean")
                self.assertEqual(cleaned.returncode, 0, cleaned.stdout + cleaned.stderr)

    def test_configure_build_and_tool_failures_cannot_report_success(self) -> None:
        for failure in (
            'message(FATAL_ERROR "configure refused")\n',
            'add_custom_target(broken ALL COMMAND ${CMAKE_COMMAND} -E false)\n',
        ):
            with self.subTest(failure=failure):
                self.write_project(failure)
                result = self.run_batch()
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("Build succeeded", result.stdout)
                cleaned = self.run_batch("clean")
                self.assertEqual(cleaned.returncode, 0, cleaned.stdout + cleaned.stderr)
        self.env["CXXIME_CMAKE"] = str(self.root / "missing.exe")
        result = self.run_batch()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Cannot run CMake", result.stdout)
        cleaned = self.run_batch("clean")
        self.assertEqual(cleaned.returncode, 0, cleaned.stdout + cleaned.stderr)

    def test_clean_rejects_junction_without_needing_cmake(self) -> None:
        target = self.parent / "keep"
        target.mkdir()
        sentinel = target / "keep.txt"
        sentinel.touch()
        link = self.root / "build"
        subprocess.run(
            [
                "cmd.exe",
                "/d",
                "/c",
                "mklink",
                "/J",
                str(link),
                str(target),
            ],
            check=True,
            capture_output=True,
        )
        self.env["CXXIME_CMAKE"] = "missing.exe"
        try:
            result = self.run_batch("clean")
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("Refusing to clean a linked build directory", result.stdout)
            self.assertTrue(sentinel.exists())
        finally:
            os.rmdir(link)

    def test_invalid_job_counts_fail_before_configuration(self) -> None:
        for arguments in ("-j", "-j 0", "-j -1", "-j abc", "-j 2 extra"):
            with self.subTest(arguments=arguments):
                result = self.run_batch(arguments)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn("N must be a positive integer", result.stdout)
                self.assertFalse((self.root / "build").exists())


if __name__ == "__main__":
    unittest.main()
