"""Apply audited readings to a private source copy, never to the upstream archive."""

import csv
import hashlib
import io
import json
import shutil
import sqlite3
import tempfile
from contextlib import closing
from pathlib import Path

from .pinyin_syllables import validate_dictionary, validate_syllables
from .source_archive import copy_database


DEFAULT_RULES = Path(__file__).resolve().parents[2] / "pinyin_source"
CORRECTION_FIELDS = ("id", "text", "code", "old_syllables", "new_syllables")


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_corrections(rules, manifest):
    text = (Path(rules) / "reading_corrections.tsv").read_text(encoding="utf-8")
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    if digest != manifest["corrections_sha256"]:
        raise ValueError("Pinyin reading corrections checksum mismatch")
    reader = csv.DictReader(io.StringIO(text), delimiter="\t")
    if reader.fieldnames != list(CORRECTION_FIELDS):
        raise ValueError("Invalid pinyin reading correction columns")
    corrections = []
    previous_id = -1
    for row in reader:
        if None in row or any(value is None for value in row.values()):
            raise ValueError("Incomplete pinyin reading correction")
        row_id = int(row["id"])
        if row_id <= previous_id:
            raise ValueError("Invalid or unordered pinyin reading correction")
        validate_syllables(row["code"], row["new_syllables"])
        if row["old_syllables"] == row["new_syllables"]:
            raise ValueError("Redundant pinyin reading correction")
        corrections.append(
            (
                row_id,
                row["text"],
                row["code"],
                row["old_syllables"],
                row["new_syllables"],
            )
        )
        previous_id = row_id
    if len(corrections) != manifest["boundary_repairs"]:
        raise ValueError("Pinyin reading correction count mismatch")
    return corrections


def repair_private_database(database, rules=DEFAULT_RULES):
    """The caller owns this temporary DB; unknown valid sources are only validated."""
    manifest = json.loads((Path(rules) / "manifest.json").read_text(encoding="utf-8"))
    if manifest["schema"] != 1:
        raise ValueError("Unsupported pinyin source repair schema")
    known_source = sha256_file(database) == manifest["source_db_sha256"]
    stats = {
        "known_source": known_source,
        "boundary_repairs": 0,
        "unannotated_removed": 0,
    }
    if not known_source:
        # Structural validation cannot prove that a word's otherwise valid reading is correct.
        try:
            validate_dictionary(database)
        except ValueError as error:
            raise ValueError(
                "Unrecognized Pinyin source has invalid readings; "
                f"see data/pinyin_source/README.md: {error}"
            ) from error
        return stats

    corrections = load_corrections(rules, manifest)
    with closing(sqlite3.connect(database)) as connection, connection:
        connection.execute("BEGIN")
        count = connection.execute("SELECT count(*) FROM dict").fetchone()[0]
        if count != manifest["before_rows"]:
            raise ValueError("Pinyin source row count mismatch")
        for row_id, text, code, old, new in corrections:
            cursor = connection.execute(
                "UPDATE dict SET syllable_ids = ? "
                "WHERE id = ? AND text = ? AND code = ? AND syllable_ids = ?",
                (new, row_id, text, code, old),
            )
            if cursor.rowcount != 1:
                raise ValueError(f"Pinyin correction no longer matches row {row_id}")
        # This exact database identity was audited against the text/weight-only source.
        cursor = connection.execute(
            "DELETE FROM dict "
            "WHERE code = '100' AND frequency = 0 AND syllable_ids = '1:0:0'"
        )
        if cursor.rowcount != manifest["unannotated_removed"]:
            raise ValueError("Pinyin unannotated source row count mismatch")
        stats["boundary_repairs"] = len(corrections)
        stats["unannotated_removed"] = cursor.rowcount
        remaining = connection.execute("SELECT count(*) FROM dict").fetchone()[0]
        if remaining != manifest["after_rows"]:
            raise ValueError("Pinyin prepared source row count mismatch")
        connection.execute("DROP TABLE IF EXISTS spellings")
    validate_dictionary(database)
    return stats


def prepare_source(source, output, rules=DEFAULT_RULES):
    """Publish a validated SQLite copy to a new path; preserve all caller-owned files."""
    source = Path(source).resolve()
    output = Path(output).resolve()
    if source == output or output.exists():
        raise ValueError("Pinyin source output must be a new file, separate from the input")
    if output.suffix.lower() != ".db":
        raise ValueError("Pinyin source output must be a SQLite .db file")
    with tempfile.TemporaryDirectory(prefix="cxxime-pinyin-source-") as temporary:
        database = Path(temporary) / "pinyin.dict.db"
        copy_database(str(source), str(database))
        stats = repair_private_database(database, rules)
        created = False
        try:
            with output.open("xb") as destination:
                created = True
                with database.open("rb") as prepared:
                    shutil.copyfileobj(prepared, destination)
        except OSError:
            if created:
                output.unlink()
            raise
    return stats
