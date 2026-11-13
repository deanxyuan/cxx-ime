#!/usr/bin/env python3
"""Regenerate the audited sparse reading corrections from pinned local sources."""

import argparse
import collections
import csv
import hashlib
import io
import json
import shutil
import sqlite3
import tempfile
import zipfile
from contextlib import closing
from pathlib import Path

from dict_builder.pinyin_source_repair import CORRECTION_FIELDS, sha256_file
from dict_builder.pinyin_syllables import validate_syllables
from dict_builder.source_archive import copy_database


SOURCE_COMMIT = "785489b3220dfca98d7ce632e590f416faea4fdf"
DEFAULT_UPSTREAM = Path(__file__).resolve().parents[1] / "pinyin_source/upstream/rime-ice-785489b"
SOURCE_ZIP_SHA256 = "7ee978b1d839e9b26e08cce4af66d2b664c0df7706ca48288fa70aec0e204fc5"
SOURCE_DB_SHA256 = "6529a859ea914e5e572ed08eb5bc9b21466bdc3111466d0b3a9047b0594d1864"
# Hash the original upstream bytes, not the host platform's decoded line endings.
SOURCE_FILES = {
    "41448.dict.yaml": "e95f1484842c52197c539afb6591d99d5df4f23e47d1a059d5e2e97d51ef61b9",
    "8105.dict.yaml": "98a974f53d042737ad9fb72715ef5dadb08b719753fbca4c55d44d25bfcb80fc",
    "base.dict.yaml": "3e0bd5fac9f21494612d7a8525901bb3d1d4647a05770d0c34ffec6f9108572c",
    "ext.dict.yaml": "83f3d895ee6d9790d2e0462cab16c51bc8193413c2e7bca44f534808d55e4fa6",
    "others.dict.yaml": "6a6b1a77d94c7cdf9203cf426e67f350215d2d73259fe3769c97d2a18f521c28",
    "tencent.dict.yaml": "dcc947386d4a3affd1ff0c3ecf27b926520c08d0da07a466a6bff9d24a8d23b8",
}


def load_upstream(path):
    readings = collections.defaultdict(set)
    unannotated = set()
    for filename, expected in SOURCE_FILES.items():
        source = Path(path) / "cn_dicts" / filename
        data = source.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError(f"Upstream source checksum mismatch: {source}")
        lines = data.decode("utf-8-sig").splitlines()
        start = lines.index("...") + 1
        for line in lines[start:]:
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) < 2:
                # The pinned others table also contains bare, unannotated words.
                # They provide no evidence for retained rows; audit_database still
                # requires an explicit reading for every retained source record.
                if filename == "tencent.dict.yaml":
                    raise ValueError(f"Malformed upstream row: {source}")
                continue
            if filename == "tencent.dict.yaml":
                if fields[1] != "100":
                    raise ValueError("Unexpected unannotated source weight")
                unannotated.add(fields[0])
                continue
            annotation = ":".join(fields[1].split())
            code = annotation.replace(":", "")
            if code.isascii() and code.isalpha() and code.islower():
                validate_syllables(code, annotation)
                readings[(fields[0], code)].add(annotation)
    return readings, unannotated


def audit_database(database, readings, unannotated):
    corrections = []
    before = removed = 0
    with closing(sqlite3.connect(database)) as connection:
        for row_id, text, code, frequency, previous in connection.execute(
            "SELECT id, text, code, frequency, syllable_ids FROM dict ORDER BY id"
        ):
            before += 1
            if code == "100":
                if text not in unannotated or frequency != 0 or previous != "1:0:0":
                    raise ValueError(f"Unproven unannotated source row: {row_id}")
                removed += 1
                continue
            candidates = readings.get((text, code), set())
            if len(candidates) != 1:
                raise ValueError(f"Expected one explicit reading for row {row_id}: {text!r}")
            annotation = next(iter(candidates))
            validate_syllables(code, annotation)
            if annotation != previous:
                corrections.append((row_id, text, code, previous, annotation))
    return corrections, before, removed


def generate(source, upstream, output):
    source = Path(source)
    output = Path(output)
    if sha256_file(source) != SOURCE_ZIP_SHA256:
        raise ValueError("Unsupported original Pinyin ZIP; do not apply historical rules")
    if output.exists():
        raise ValueError("Correction output directory must not already exist")
    readings, unannotated = load_upstream(upstream)
    with tempfile.TemporaryDirectory(prefix="cxxime-pinyin-audit-") as temporary:
        database = Path(temporary) / "pinyin.dict.db"
        copy_database(str(source), str(database))
        if sha256_file(database) != SOURCE_DB_SHA256:
            raise ValueError("Original Pinyin database checksum mismatch")
        corrections, before, removed = audit_database(database, readings, unannotated)
    if (before, len(corrections), removed) != (1909924, 33943, 981634):
        raise ValueError("Historical Pinyin audit totals do not match")
    buffer = io.StringIO(newline="")
    writer = csv.writer(buffer, delimiter="\t", lineterminator="\n")
    writer.writerow(CORRECTION_FIELDS)
    writer.writerows(corrections)
    text = buffer.getvalue()
    manifest = {
        "schema": 1,
        "source_commit": SOURCE_COMMIT,
        "source_zip_sha256": SOURCE_ZIP_SHA256,
        "source_db_sha256": SOURCE_DB_SHA256,
        "before_rows": before,
        "after_rows": before - removed,
        "boundary_repairs": len(corrections),
        "unannotated_removed": removed,
        "corrections_sha256": hashlib.sha256(text.encode("utf-8")).hexdigest(),
        "upstream_files": {"cn_dicts/" + name: digest for name, digest in SOURCE_FILES.items()},
    }
    output.mkdir()
    try:
        (output / "reading_corrections.tsv").write_text(text, encoding="utf-8", newline="\n")
        (output / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=4) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    except OSError:
        shutil.rmtree(output)
        raise
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        required=True,
        help="Unmodified Pinyin source ZIP",
    )
    parser.add_argument(
        "--upstream",
        default=DEFAULT_UPSTREAM,
        help="Local pinned rime-ice directory (default: repository upstream data)",
    )
    parser.add_argument(
        "--output-dir",
        required=True,
        help="New directory for generated TSV/manifest",
    )
    args = parser.parse_args()
    try:
        result = generate(args.input, args.upstream, args.output_dir)
    except (OSError, ValueError, sqlite3.Error, zipfile.BadZipFile) as error:
        parser.exit(1, f"error: {error}\n")
    print(json.dumps(result, ensure_ascii=False, indent=4))


if __name__ == "__main__":
    main()
