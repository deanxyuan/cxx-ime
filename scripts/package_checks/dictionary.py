# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

from __future__ import annotations

import json
import os

from package_checks.common import add_error, require_file


def normalize_manifest_path(path: object) -> str | None:
    if not isinstance(path, str) or not path:
        return None
    normalized = os.path.normpath(path.replace("\\", os.sep).replace("/", os.sep))
    if os.path.isabs(normalized):
        return None
    if normalized == ".." or normalized.startswith(".." + os.sep):
        return None
    return normalized


def check_dictionary_manifest(errors: list[str], dist_dir: str) -> list[str]:
    manifest_path = os.path.join(dist_dir, "data", "dictionary_manifest.json")
    if not require_file(errors, manifest_path, dist_dir):
        return []

    try:
        with open(manifest_path, "r", encoding="utf-8") as f:
            manifest = json.load(f)
    except (OSError, json.JSONDecodeError) as exc:
        add_error(errors, f"invalid dictionary manifest: {exc}")
        return []

    files = manifest.get("files")
    if not isinstance(files, list):
        add_error(errors, "dictionary manifest files must be an array")
        return []

    roles: set[str] = set()
    manifest_files: list[str] = []
    seen_paths: set[str] = set()
    for index, item in enumerate(files):
        if not isinstance(item, dict):
            add_error(errors, f"dictionary manifest file entry #{index} must be an object")
            continue

        role = item.get("role")
        if isinstance(role, str) and role:
            roles.add(role)

        normalized_path = normalize_manifest_path(item.get("path"))
        if normalized_path is None:
            add_error(errors, f"dictionary manifest file entry #{index} has unsafe path")
            continue

        path_key = normalized_path.lower()
        if path_key in seen_paths:
            add_error(errors, f"dictionary manifest duplicate path: {normalized_path}")
            continue
        seen_paths.add(path_key)
        manifest_files.append(normalized_path.replace(os.sep, "\\"))
        require_file(errors, os.path.join(dist_dir, "data", normalized_path), dist_dir)

    required_roles = {
        "pinyin_dict",
        "pinyin_idx",
        "pinyin_spellings",
        "pinyin_topn",
        "pinyin_reverse_index",
        "wubi_dict",
        "wubi_prefix_index",
        "wubi_reverse_index",
    }
    missing = sorted(required_roles - roles)
    if missing:
        add_error(errors, "dictionary manifest missing role(s): " + ", ".join(missing))

    return manifest_files
