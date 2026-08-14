#!/usr/bin/env python3
# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
#
# Standalone dictionary preparation pipeline.
#
# Runs the full workflow: .zip extraction -> spelling algebra -> binary build.
# Supports both pinyin and wubi86 dictionaries.
#
# Usage:
#   python scripts/prepare_dictionary_bundle.py --data-dir data/ --output-dir dist/data/ \
#       --topn-builder build/tools/topn_index/Release/topn_builder.exe

from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_TOOLS = os.path.join(ROOT, "data", "tools")
WUBI_RANKING_BASELINE = os.path.join(
    DATA_TOOLS, "dict_builder", "wubi_ranking_baseline.json"
)
SCHEMAS = os.path.join(ROOT, "data", "schemas")
SCRIPTS = os.path.join(ROOT, "scripts")
sys.path.insert(0, DATA_TOOLS)

from dict_builder import copy_source_database

MANIFEST_FILES = [
    ("pinyin_dict", "pinyin.dict.bin"),
    ("pinyin_idx", "pinyin.dict.idx"),
    ("pinyin_spellings", "pinyin.spellings.bin"),
    ("pinyin_topn", "pinyin.topn.bin"),
    ("wubi_dict", "wubi86.dict.bin"),
    ("wubi_prefix_index", "wubi86.dict.idx"),
]

REQUIRED_MANIFEST_ROLES = {
    "pinyin_dict",
    "pinyin_idx",
    "pinyin_spellings",
    "pinyin_topn",
    "wubi_dict",
    "wubi_prefix_index",
}


def find_source(data_dir: str, name: str) -> str | None:
    """Find .dict.db or .dict.db.zip for a dictionary name."""
    zip_path = os.path.join(data_dir, f"{name}.dict.db.zip")
    if os.path.isfile(zip_path):
        return zip_path
    db = os.path.join(data_dir, f"{name}.dict.db")
    if os.path.isfile(db):
        return db
    return None


def prepare_source_copy(src: str, work_dir: str) -> str:
    """Return a writable dictionary DB path in work_dir."""
    source_name = os.path.basename(src)
    if source_name.endswith(".zip"):
        source_name = source_name[:-4]
        print(f"  Extracting {os.path.basename(src)}...")
    db_copy = os.path.join(work_dir, source_name)
    copy_source_database(src, db_copy)
    return db_copy


def run_pinyin_spelling_generation(db_path: str) -> None:
    """Regenerate spellings table from schema rules."""
    script = os.path.join(DATA_TOOLS, "generate_pinyin_spellings.py")
    schema = os.path.join(SCHEMAS, "pinyin.schema.json")
    print("  Running spelling algebra...")
    subprocess.run(
        [sys.executable, script, db_path, schema],
        check=True,
        capture_output=False,
    )


def run_build_runtime_dictionary(
    db_path: str,
    output_prefix: str,
    skip_idx: bool = False,
    dict_only: bool = False,
    wubi_prefix_index: bool = False,
    wubi_ranking_source: str | None = None,
    wubi_ranking_baseline: str | None = None,
) -> None:
    """Convert a SQLite dictionary to runtime files."""
    script = os.path.join(DATA_TOOLS, "build_runtime_dictionary.py")
    cmd = [sys.executable, script, "--input", db_path, "--output", output_prefix]
    if dict_only:
        cmd.append("--dict-only")
    if skip_idx:
        cmd.append("--skip-idx")
    if wubi_prefix_index:
        cmd.append("--wubi-prefix-index")
        if not wubi_ranking_source:
            raise RuntimeError("Wubi prefix index requires a ranking source")
        cmd.extend(["--wubi-ranking-source", wubi_ranking_source])
        if wubi_ranking_baseline:
            cmd.extend(["--wubi-ranking-baseline", wubi_ranking_baseline])
    print(f"  Building binary dicts: {os.path.basename(output_prefix)}.*")
    subprocess.run(cmd, check=True, capture_output=False)


def run_wubi_symbol_split(
    source_db: str,
    filtered_db: str,
    symbols_output: str,
) -> None:
    """Split the temporary Wubi source into symbols and dictionary entries."""
    script = os.path.join(DATA_TOOLS, "split_wubi_symbols.py")
    print("  Splitting Wubi symbol entries...")
    subprocess.run(
        [
            sys.executable,
            script,
            "--input",
            source_db,
            "--symbols-output",
            symbols_output,
            "--filtered-output",
            filtered_db,
        ],
        check=True,
        capture_output=False,
    )


def verify_generated_text_file(generated_path: str, expected_path: str) -> None:
    """Require generated UTF-8 text to match its reviewed repository copy."""
    if not os.path.isfile(expected_path):
        raise RuntimeError(f"Expected generated file not found: {expected_path}")
    with open(generated_path, "r", encoding="utf-8", newline=None) as generated, open(
        expected_path, "r", encoding="utf-8", newline=None
    ) as expected:
        if generated.read() != expected.read():
            raise RuntimeError(
                f"Generated {os.path.basename(generated_path)} does not match "
                f"{expected_path}; regenerate the repository copy"
            )


def run_build_pinyin_topn(db_path: str, output_path: str) -> None:
    """Build the intermediate representation consumed by topn_builder."""
    script = os.path.join(SCRIPTS, "build_pinyin_topn.py")
    print("  Building Top-N index intermediate...")
    subprocess.run(
        [sys.executable, script, "--input", db_path, "--output", output_path],
        check=True,
        capture_output=False,
    )


def finalize_topn_index(output_dir: str, topn_builder: str) -> str:
    """Convert the Top-N intermediate to the runtime DAT-16 format in place."""
    topn_path = os.path.join(output_dir, "pinyin.topn.bin")
    if not os.path.isfile(topn_path):
        raise RuntimeError(f"Top-N intermediate not found: {topn_path}")
    if not os.path.isfile(topn_builder):
        raise RuntimeError(f"topn_builder not found: {topn_builder}")

    with open(topn_path, "rb") as f:
        header = f.read(20)
    if len(header) < 20:
        raise RuntimeError("pinyin.topn.bin is too small")

    magic = header[:8]
    if magic == b"CXTOPN\x01\x00":
        print("  Converting Top-N index to DAT-16...")
        subprocess.run(
            [
                topn_builder,
                "--input",
                topn_path,
                "--output",
                topn_path,
                "--format",
                "dat16",
            ],
            check=True,
            capture_output=False,
        )
        with open(topn_path, "rb") as f:
            header = f.read(20)
        magic = header[:8]

    if magic != b"CXTOPN\x02\x00" or len(header) < 20:
        raise RuntimeError("pinyin.topn.bin is not a CXTOPN v2 file")
    version, header_size, layout = struct.unpack_from("<III", header, 8)
    if version != 2 or header_size != 80 or layout != 2:
        raise RuntimeError(
            "pinyin.topn.bin is not the required DAT-16 layout "
            f"(version={version}, header={header_size}, layout={layout})"
        )
    return topn_path


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def write_dictionary_manifest(output_dir: str) -> str:
    """Write dictionary_manifest.json last, after all large files are stable."""
    files = []
    for role, filename in MANIFEST_FILES:
        path = os.path.join(output_dir, filename)
        if not os.path.isfile(path):
            continue
        files.append({
            "role": role,
            "path": filename,
            "size": os.path.getsize(path),
            "sha256": sha256_file(path),
            "required": True,
        })

    roles = {item["role"] for item in files}
    missing = sorted(REQUIRED_MANIFEST_ROLES - roles)
    if missing:
        raise RuntimeError(
            "Missing required dictionary manifest role(s): " + ", ".join(missing)
        )

    manifest = {
        "schema": 1,
        "generation": datetime.datetime.utcnow().replace(microsecond=0).isoformat() + "Z",
        "files": files,
    }

    path = os.path.join(output_dir, "dictionary_manifest.json")
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
        f.write("\n")
    os.replace(tmp, path)
    return path


def prepare_pinyin_dictionary(data_dir: str, output_dir: str) -> list[str]:
    """Prepare pinyin binary dictionary files."""
    print("--- Pinyin dictionary ---")
    src = find_source(data_dir, "pinyin")
    if src is None:
        print("  WARNING: pinyin.dict.db(.zip) not found, skipping pinyin dictionary")
        return []

    generated = []
    with tempfile.TemporaryDirectory(prefix="cxxime_prep_pinyin_") as tmpdir:
        db_path = prepare_source_copy(src, tmpdir)
        run_pinyin_spelling_generation(db_path)

        output_prefix = os.path.join(output_dir, "pinyin")
        run_build_runtime_dictionary(db_path, output_prefix)
        generated.extend([
            output_prefix + ".dict.bin",
            output_prefix + ".dict.idx",
            output_prefix + ".spellings.bin",
        ])

        topn_path = os.path.join(output_dir, "pinyin.topn.bin")
        run_build_pinyin_topn(db_path, topn_path)
        generated.append(topn_path)

    return generated


def prepare_wubi_dictionary(data_dir: str, output_dir: str) -> list[str]:
    """Prepare wubi86 binary dictionary files."""
    print("--- Wubi86 dictionary ---")
    src = find_source(data_dir, "wubi86")
    if src is None:
        raise RuntimeError("wubi86.dict.db(.zip) not found")
    ranking_source = find_source(data_dir, "pinyin")
    if ranking_source is None:
        raise RuntimeError("pinyin.dict.db(.zip) is required for Wubi ranking")

    generated = []
    with tempfile.TemporaryDirectory(prefix="cxxime_prep_wubi86_") as tmpdir:
        source_db = prepare_source_copy(src, tmpdir)
        generated_symbols = os.path.join(tmpdir, "symbols.json")
        db_path = os.path.join(tmpdir, "wubi86.filtered.dict.db")
        run_wubi_symbol_split(source_db, db_path, generated_symbols)
        verify_generated_text_file(generated_symbols, os.path.join(data_dir, "symbols.json"))
        symbols_output = os.path.join(output_dir, "symbols.json")
        shutil.copy2(generated_symbols, symbols_output)
        output_prefix = os.path.join(output_dir, "wubi86")
        run_build_runtime_dictionary(
            db_path,
            output_prefix,
            dict_only=True,
            wubi_prefix_index=True,
            wubi_ranking_source=ranking_source,
            wubi_ranking_baseline=WUBI_RANKING_BASELINE,
        )
        generated.extend([
            symbols_output,
            output_prefix + ".dict.bin",
            output_prefix + ".dict.idx",
        ])

    return generated


def prepare_dictionary_bundle(
    data_dir: str,
    output_dir: str,
    workers: int = 2,
    topn_builder: str | None = None,
    defer_topn_conversion: bool = False,
) -> list[str]:
    """Run dictionary preparation, using separate workers for pinyin and wubi86."""
    os.makedirs(output_dir, exist_ok=True)
    workers = max(1, min(workers, 2))
    tasks = [
        ("pinyin", prepare_pinyin_dictionary),
        ("wubi86", prepare_wubi_dictionary),
    ]
    generated = []

    if workers == 1:
        for _, task in tasks:
            generated.extend(task(data_dir, output_dir))
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
            futures = [
                (name, executor.submit(task, data_dir, output_dir))
                for name, task in tasks
            ]
            for name, future in futures:
                try:
                    generated.extend(future.result())
                except Exception as e:
                    raise RuntimeError(f"{name} dictionary preparation failed: {e}") from e

    if defer_topn_conversion:
        return generated

    if not topn_builder:
        raise RuntimeError("topn_builder is required to produce the DAT-16 runtime index")
    finalize_topn_index(output_dir, os.path.abspath(topn_builder))

    manifest_path = write_dictionary_manifest(output_dir)
    generated.append(manifest_path)
    return generated


def main():
    parser = argparse.ArgumentParser(
        description="Prepare dictionary binaries for CxxIME packaging"
    )
    parser.add_argument(
        "--data-dir", required=True,
        help="Source data directory (contains .dict.db or .dict.db.zip)",
    )
    parser.add_argument(
        "--output-dir", required=True,
        help="Output directory for binary dictionary files",
    )
    parser.add_argument(
        "--workers", type=int, default=min(2, os.cpu_count() or 1),
        help="Dictionary worker count (default: 2, capped by available dictionaries)",
    )
    parser.add_argument(
        "--topn-builder", required=True,
        help="Path to the x64 topn_builder executable",
    )
    args = parser.parse_args()

    data_dir = os.path.abspath(args.data_dir)
    output_dir = os.path.abspath(args.output_dir)

    if not os.path.isdir(data_dir):
        print(f"ERROR: data directory not found: {data_dir}", file=sys.stderr)
        return 1
    if args.workers < 1:
        print("ERROR: --workers must be >= 1", file=sys.stderr)
        return 1

    print(f"Data source: {data_dir}")
    print(f"Output:      {output_dir}")
    print()

    try:
        generated = prepare_dictionary_bundle(
            data_dir,
            output_dir,
            workers=args.workers,
            topn_builder=args.topn_builder,
        )
    except subprocess.CalledProcessError as e:
        print(f"ERROR: subprocess failed: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    print()
    print(f"Done. {len(generated)} file(s) generated:")
    for path in generated:
        size_mb = os.path.getsize(path) / (1024 * 1024)
        print(f"  {os.path.basename(path):30s} {size_mb:.1f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
