"""Resolve direct and zipped SQLite dictionary sources."""

from __future__ import annotations

import os
import shutil
import tempfile
import zipfile
from typing import Callable, Tuple


def _database_member(archive: zipfile.ZipFile, archive_path: str) -> str:
    databases = [
        name
        for name in archive.namelist()
        if not name.endswith("/") and name.lower().endswith(".dict.db")
    ]
    if len(databases) != 1:
        raise ValueError(
            f"dictionary archive must contain exactly one .dict.db file: {archive_path}"
        )
    return databases[0]


def copy_database(source_path: str, destination_path: str) -> None:
    """Copy a direct or zipped SQLite dictionary to a caller-owned path."""
    if not source_path.lower().endswith(".zip"):
        shutil.copy2(source_path, destination_path)
        return

    with zipfile.ZipFile(source_path, "r") as archive:
        selected = _database_member(archive, source_path)
        with archive.open(selected, "r") as source, open(destination_path, "wb") as output:
            shutil.copyfileobj(source, output)


def resolve(path: str) -> Tuple[str, Callable[[], None]]:
    if os.path.isfile(path) and not path.lower().endswith(".zip"):
        return path, lambda: None

    archive_path = path if path.lower().endswith(".zip") else path + ".zip"
    if not os.path.isfile(archive_path):
        raise FileNotFoundError(f"{path} not found (also checked {archive_path})")

    temporary_dir = tempfile.mkdtemp(prefix="cxxime_")
    try:
        output_path = os.path.join(temporary_dir, "dictionary.dict.db")
        copy_database(archive_path, output_path)
    except Exception:
        shutil.rmtree(temporary_dir, ignore_errors=True)
        raise

    return output_path, lambda: shutil.rmtree(temporary_dir, ignore_errors=True)
