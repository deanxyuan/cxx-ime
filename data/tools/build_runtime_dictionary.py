#!/usr/bin/env python3
"""Build CxxIME runtime dictionary files from a SQLite source."""

from __future__ import annotations

import argparse
import sys

from dict_builder import (
    build_pinyin_spellings,
    build_pinyin_syllable_index,
    build_runtime_dictionary,
    build_wubi_prefix_index,
    resolve_source_archive,
)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Build CxxIME runtime dictionary files from SQLite"
    )
    parser.add_argument(
        "--input",
        "-i",
        required=True,
        help="Input .dict.db or .dict.db.zip file",
    )
    parser.add_argument("--output", "-o", required=True, help="Output file prefix")
    parser.add_argument(
        "--spellings-only",
        action="store_true",
        help="Build only the Pinyin spelling trie",
    )
    parser.add_argument(
        "--dict-only",
        action="store_true",
        help="Skip the Pinyin spelling trie",
    )
    parser.add_argument(
        "--skip-idx",
        action="store_true",
        help="Skip the dictionary index",
    )
    parser.add_argument(
        "--wubi-prefix-index",
        action="store_true",
        help="Build the complete Wubi prefix index instead of the Pinyin syllable index",
    )
    args = parser.parse_args()
    if args.spellings_only and args.dict_only:
        parser.error("--spellings-only and --dict-only cannot be used together")
    if args.skip_idx and args.wubi_prefix_index:
        parser.error("--skip-idx and --wubi-prefix-index cannot be used together")
    return args


def main() -> int:
    args = parse_args()
    try:
        database_path, cleanup = resolve_source_archive(args.input)
    except (FileNotFoundError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    try:
        print(f"Building runtime dictionary from: {database_path}")
        if not args.dict_only:
            build_pinyin_spellings(database_path, args.output + ".spellings.bin")
        if not args.spellings_only:
            dictionary_path = args.output + ".dict.bin"
            build_runtime_dictionary(database_path, dictionary_path)
            if args.wubi_prefix_index:
                build_wubi_prefix_index(dictionary_path, args.output + ".dict.idx")
            elif not args.skip_idx:
                build_pinyin_syllable_index(database_path, args.output + ".dict.idx")
        print("Done.")
    finally:
        cleanup()
    return 0


if __name__ == "__main__":
    sys.exit(main())
