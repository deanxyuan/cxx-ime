#!/usr/bin/env python3
# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
#
# CxxIME packaging script — builds, prepares dictionaries, and creates installer.
#
# Usage:
#   python scripts/package.py                # Release build + package
#   python scripts/package.py --debug        # Debug build + package
#   python scripts/package.py --clean        # Clean rebuild (delete build-package/)
#   python scripts/package.py --skip-build   # Skip cmake build (already built)
#   python scripts/package.py --skip-dict    # Skip dictionary generation
#   python scripts/package.py --generator "Visual Studio 17 2022" --platform x64

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPTS = os.path.join(ROOT, "scripts")
DATA = os.path.join(ROOT, "data")
DEFAULT_BUILD_DIR = os.path.join(ROOT, "build-package")
DIST_DIR = os.path.join(ROOT, "dist")
OUTPUT_DIR = os.path.join(ROOT, "..", "output")
VERSION = "0.1.0"
SINGLE_CONFIG_GENERATORS = (
    "NMake Makefiles",
    "NMake Makefiles JOM",
    "Ninja",
    "Unix Makefiles",
    "MinGW Makefiles",
)


def step(msg: str) -> None:
    """Print a step header."""
    print(f"\n{msg}")


def run(cmd: list[str], **kwargs) -> None:
    """Run a command, raising on failure."""
    subprocess.run(cmd, check=True, **kwargs)


def choose_cmake_generator(
    generator: str | None,
    platform: str | None,
) -> tuple[str | None, str | None]:
    """Choose CMake generator/platform from explicit args or environment."""
    if generator:
        return generator, platform

    env_generator = os.environ.get("CXXIME_CMAKE_GENERATOR") or os.environ.get("CMAKE_GENERATOR")
    env_platform = os.environ.get("CXXIME_CMAKE_PLATFORM") or os.environ.get("CMAKE_GENERATOR_PLATFORM")
    return env_generator, platform or env_platform


def is_single_config_generator(generator: str | None) -> bool:
    """Return true for generators that use CMAKE_BUILD_TYPE."""
    if not generator:
        return False
    return any(generator == name or generator.startswith(name + " ") for name in SINGLE_CONFIG_GENERATORS)


def build(
    build_dir: str,
    config: str,
    clean: bool = False,
    skip_tests: bool = False,
    skip_tools: bool = False,
    generator: str | None = None,
    platform: str | None = None,
) -> None:
    """Configure and build the project."""
    if clean and os.path.exists(build_dir):
        print(f"Cleaning build directory: {build_dir}")
        shutil.rmtree(build_dir)

    print(f"Building {config} with PRODUCTION=ON...")
    os.makedirs(build_dir, exist_ok=True)

    generator, platform = choose_cmake_generator(generator, platform)
    if generator:
        print(f"  CMake generator: {generator}")
    single_config = is_single_config_generator(generator)
    if single_config:
        platform = None
    if platform:
        print(f"  CMake platform:  {platform}")

    cmake_args = [
        "cmake", "-S", ROOT, "-B", build_dir,
        "-DCXXIME_PRODUCTION_BUILD=ON",
    ]
    if generator:
        cmake_args.extend(["-G", generator])
    if platform:
        cmake_args.extend(["-A", platform])
    cmake_args.append(f"-DCXXIME_BUILD_TESTS={'OFF' if skip_tests else 'ON'}")
    cmake_args.append(f"-DCXXIME_BUILD_TOOLS={'OFF' if skip_tools else 'ON'}")
    if single_config:
        cmake_args.append(f"-DCMAKE_BUILD_TYPE={config}")
        build_cmd = ["cmake", "--build", build_dir]
    else:
        build_cmd = ["cmake", "--build", build_dir, "--config", config]

    run(cmake_args)
    run(build_cmd)


def clean_dist(keep_data: bool = False) -> None:
    """Remove and recreate dist directory.

    If keep_data is True, preserves dist/data/ contents (for --skip-dict).
    """
    if keep_data and os.path.exists(DIST_DIR):
        # Remove everything except data/
        for entry in os.listdir(DIST_DIR):
            if entry == "data":
                continue
            path = os.path.join(DIST_DIR, entry)
            if os.path.isdir(path):
                shutil.rmtree(path)
            else:
                os.remove(path)
    else:
        if os.path.exists(DIST_DIR):
            shutil.rmtree(DIST_DIR)
    os.makedirs(os.path.join(DIST_DIR, "data"), exist_ok=True)


def copy_binaries(build_dir: str, config: str) -> None:
    """Copy built binaries to dist."""
    for name in ["cxxime_tsf.dll", "cxxime-server.exe", "cxxime-settings.exe"]:
        target_dir = {
            "cxxime_tsf.dll": os.path.join(build_dir, "tsf"),
            "cxxime-server.exe": os.path.join(build_dir, "server"),
            "cxxime-settings.exe": os.path.join(build_dir, "settings"),
        }[name]
        src_dir = os.path.join(target_dir, config)
        if not os.path.isdir(src_dir):
            src_dir = target_dir
        src = os.path.join(src_dir, name)
        if not os.path.isfile(src):
            print(f"  ERROR: binary not found: {src}", file=sys.stderr)
            sys.exit(1)
        shutil.copy2(src, os.path.join(DIST_DIR, name))
        print(f"  {name}")


def copy_config() -> None:
    """Copy config and themes to dist/data."""
    data_dir = os.path.join(DIST_DIR, "data")
    shutil.copy2(os.path.join(DATA, "default.json"), data_dir)
    print("  default.json")

    presets = os.path.join(DATA, "settings_presets.json")
    if os.path.isfile(presets):
        shutil.copy2(presets, data_dir)
        print("  settings_presets.json")
    else:
        print("  WARNING: settings_presets.json not found")

    themes = os.path.join(DATA, "themes.json")
    if os.path.isfile(themes):
        shutil.copy2(themes, data_dir)
        print("  themes.json")
    else:
        print("  WARNING: themes.json not found")

    punct = os.path.join(DATA, "punctuation.json")
    if os.path.isfile(punct):
        shutil.copy2(punct, data_dir)
        print("  punctuation.json")
    else:
        print("  WARNING: punctuation.json not found")


def prepare_dictionaries() -> None:
    """Run prepare_dict.py for pinyin and wubi."""
    from prepare_dict import prepare_dict as do_prepare

    data_dir = DATA
    output_dir = os.path.join(DIST_DIR, "data")

    generated = do_prepare(data_dir, output_dir)
    if not generated:
        print("  ERROR: No dictionary files generated.", file=sys.stderr)
        print("  Need .dict.db or .dict.db.zip in data/.", file=sys.stderr)
        sys.exit(1)


def verify_data_files() -> None:
    """Run verify_data_files.py."""
    script = os.path.join(SCRIPTS, "verify_data_files.py")
    data_dir = os.path.join(DIST_DIR, "data")
    run([sys.executable, script, "--data-dir", data_dir])
    print("  All data file checks PASSED.")


def check_debug_crt(config: str) -> None:
    """Ensure Release builds don't link Debug CRT."""
    if config == "Debug":
        print("  Debug CRT expected for Debug build, skipping check.")
        return

    print("  Checking for Debug CRT dependencies...")
    debug_crts = {"ucrtbased.dll", "vcruntimed.dll"}
    has_debug = False

    for name in ["cxxime_tsf.dll", "cxxime-server.exe", "cxxime-settings.exe"]:
        path = os.path.join(DIST_DIR, name)
        try:
            result = subprocess.run(
                ["dumpbin", "/dependents", path],
                capture_output=True, text=True,
            )
            for crt in debug_crts:
                if crt.lower() in result.stdout.lower():
                    print(f"  ERROR: {name} links to Debug CRT ({crt})", file=sys.stderr)
                    has_debug = True
        except FileNotFoundError:
            print(f"  WARNING: dumpbin not available, skipping CRT check for {name}")
            return

    if has_debug:
        print("  ERROR: Release build must not depend on Debug CRT.", file=sys.stderr)
        sys.exit(1)
    print("  No Debug CRT dependencies.")


def check_hot_path_logs() -> None:
    """Warn about CXXIME_LOG calls in hot paths."""
    print("  Checking for high-frequency CXXIME_LOG in hot paths...")
    hot_files = [
        os.path.join(ROOT, "engine", "src", "pinyin_translator.cc"),
        os.path.join(ROOT, "engine", "src", "dict.cc"),
        os.path.join(ROOT, "ipc", "src", "ipc_server.cc"),
    ]
    log_pattern = re.compile(r"^\s*CXXIME_LOG", re.MULTILINE)

    for path in hot_files:
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8") as f:
            content = f.read()
        # Count non-commented CXXIME_LOG lines
        lines = content.split("\n")
        count = 0
        for line in lines:
            stripped = line.strip()
            if stripped.startswith("CXXIME_LOG") and not stripped.startswith("//"):
                count += 1
        if count > 0:
            basename = os.path.basename(path)
            print(f"  WARNING: {basename}: {count} CXXIME_LOG call(s) in hot path")


def check_log_rotation() -> None:
    """Check that diagnostics log rotation is configured."""
    print("  Checking log rotation config...")
    default_json = os.path.join(DIST_DIR, "data", "default.json")
    if os.path.isfile(default_json):
        with open(default_json, encoding="utf-8") as f:
            cfg = json.load(f)
            diag = cfg.get("diagnostics", {})
            required = ("trace_mode", "log_max_size", "log_max_files")
            if isinstance(diag, dict) and all(k in diag for k in required):
                print("  Diagnostics log rotation config found.")
                return
    print("  WARNING: default.json does not contain diagnostics log rotation config.")


def copy_installer_scripts(config: str) -> None:
    """Copy install/uninstall scripts and NSIS template."""
    files = [
        "install.bat", "uninstall.bat",
        "install.ps1", "uninstall.ps1",
        "collect_diagnostics.ps1",
    ]
    for fn in files:
        src = os.path.join(SCRIPTS, fn)
        if os.path.isfile(src):
            shutil.copy2(src, DIST_DIR)
            print(f"  {fn}")

    # NSIS template
    shutil.copy2(os.path.join(SCRIPTS, "cxxime-setup.nsi"), DIST_DIR)

    # License: copy LICENSE (root) as license.txt for NSIS
    license_src = os.path.join(ROOT, "LICENSE")
    if os.path.isfile(license_src):
        shutil.copy2(license_src, os.path.join(DIST_DIR, "license.txt"))
        print("  license.txt (from LICENSE)")
    else:
        print("  WARNING: LICENSE not found, NSIS may fail")


def build_nsis(config: str, fast: bool = False) -> None:
    """Run makensis to create the installer."""
    # Find makensis
    makensis = shutil.which("makensis")
    if makensis is None:
        # Try registry paths
        import winreg
        for key_path in [
            r"SOFTWARE\WOW6432Node\NSIS",
            r"SOFTWARE\NSIS",
        ]:
            try:
                key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key_path)
                path, _ = winreg.QueryValueEx(key, "")
                candidate = os.path.join(path, "makensis.exe")
                if os.path.isfile(candidate):
                    makensis = candidate
                    break
            except OSError:
                continue

    if makensis is None:
        print("  WARNING: makensis not found. Install NSIS 3.x or add it to PATH.")
        print(f"  Distribution files are in: {DIST_DIR}")
        return

    print(f"  Using NSIS: {makensis}")
    nsi_file = os.path.join(DIST_DIR, "cxxime-setup.nsi")
    cmd = [makensis]
    if fast:
        cmd.append("/DFAST")
    cmd.append(nsi_file)
    run(cmd, cwd=DIST_DIR)

    # Move installer to output
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    installer = os.path.join(DIST_DIR, f"cxxime-v{VERSION}-setup.exe")
    dest = os.path.join(OUTPUT_DIR, f"cxxime-v{VERSION}-setup.exe")
    if os.path.isfile(installer):
        shutil.move(installer, dest)
        print(f"  Installer created: {dest}")
    else:
        print("  ERROR: NSIS completed but installer not found.", file=sys.stderr)


def print_summary(config: str) -> None:
    """Print final distribution summary."""
    print(f"\n{'=' * 60}")
    print("=== Packaging complete ===")
    print(f"{'=' * 60}")
    print(f"\nConfiguration: {config}")
    print(f"Distribution:  {DIST_DIR}")
    print()
    print("Contents:")
    print("  cxxime_tsf.dll           TSF text service DLL")
    print("  cxxime-server.exe        Background server process")
    print("  cxxime-settings.exe      Configuration editor")
    print("  data/")
    print("    default.json           Default configuration")
    print("    settings_presets.json  Settings UI presets")
    print("    themes.json            Color themes")
    print("    punctuation.json       Punctuation mapping")
    print("    pinyin.dict.bin        Pinyin binary dictionary (runtime)")
    print("    pinyin.dict.idx        Pinyin syllable index (runtime)")
    print("    pinyin.spellings.bin   Pinyin spelling trie (runtime)")
    print("    pinyin.topn.bin        Short code cache (runtime)")
    print("    wubi86.dict.bin        Wubi binary dictionary (if available)")
    print("  install.bat              Installer (run as admin)")
    print("  uninstall.bat            Uninstaller (run as admin)")
    print("  install.ps1              PowerShell installer (optional)")
    print("  uninstall.ps1            PowerShell uninstaller (optional)")
    print("  collect_diagnostics.ps1  Diagnostics collector")
    print("  cxxime-setup.nsi         NSIS script")
    print("  license.txt              License")


def main():
    parser = argparse.ArgumentParser(
        description="CxxIME packaging - build, prepare dicts, create installer"
    )
    parser.add_argument(
        "--debug", action="store_true",
        help="Build Debug configuration (default: Release)",
    )
    parser.add_argument(
        "--clean", action="store_true",
        help="Clean rebuild - delete package build directory before cmake",
    )
    parser.add_argument(
        "--build-dir", default=DEFAULT_BUILD_DIR,
        help="CMake build directory for package builds (default: build-package)",
    )
    parser.add_argument(
        "--skip-build", action="store_true",
        help="Skip cmake build step (binaries already built)",
    )
    parser.add_argument(
        "--skip-dict", action="store_true",
        help="Skip dictionary generation (use existing dist/data/ files)",
    )
    parser.add_argument(
        "--fast", action="store_true",
        help="Fast NSIS build: skip compression for large data files",
    )
    parser.add_argument(
        "--skip-nsis", action="store_true",
        help="Skip NSIS installer build (only update dist/ contents)",
    )
    parser.add_argument(
        "--skip-tests", action="store_true", default=True,
        help="Skip building unit tests (default: on)",
    )
    parser.add_argument(
        "--with-tests", action="store_false", dest="skip_tests",
        help="Build unit tests",
    )
    parser.add_argument(
        "--skip-tools", action="store_true", default=True,
        help="Skip building development tools (default: on)",
    )
    parser.add_argument(
        "--with-tools", action="store_false", dest="skip_tools",
        help="Build development tools",
    )
    parser.add_argument(
        "--generator",
        help="CMake generator (default: let CMake choose, or use CMAKE_GENERATOR)",
    )
    parser.add_argument(
        "--platform",
        help="CMake platform for generators that support -A, such as Visual Studio",
    )
    args = parser.parse_args()

    config = "Debug" if args.debug else "Release"
    build_dir = os.path.abspath(args.build_dir)

    print(f"=== CxxIME Packager v{VERSION} ({config}) ===")

    # 1. Build
    if args.skip_build:
        print(f"\nSkipping build (--skip-build). Using existing binaries in {build_dir}.")
    else:
        step("[1/6] Building...")
        build(
            build_dir,
            config,
            clean=args.clean,
            skip_tests=args.skip_tests,
            skip_tools=args.skip_tools,
            generator=args.generator,
            platform=args.platform,
        )

    # 2. Clean + copy
    step("[2/6] Preparing distribution directory...")
    clean_dist(keep_data=args.skip_dict)

    print("  Copying binaries...")
    copy_binaries(build_dir, config)

    print("  Copying config and themes...")
    copy_config()

    # 3. Dictionary preparation
    if args.skip_dict:
        step("[3/6] Skipping dictionary generation (--skip-dict).")
        print("  Using existing files in dist/data/.")
    else:
        step("[3/6] Preparing dictionaries...")
        prepare_dictionaries()

    # 4. Verification
    step("[4/6] Verifying data files...")
    verify_data_files()
    check_debug_crt(config)
    check_hot_path_logs()
    check_log_rotation()

    # 5. Installer scripts
    step("[5/6] Copying installer scripts...")
    copy_installer_scripts(config)

    # 6. NSIS
    if args.skip_nsis:
        step("[6/6] Skipping NSIS installer (--skip-nsis).")
    else:
        step("[6/6] Building NSIS installer...")
        build_nsis(config, fast=args.fast)

    print_summary(config)


if __name__ == "__main__":
    main()
