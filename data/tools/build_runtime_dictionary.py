#!/usr/bin/env python3
"""Build CxxIME runtime dictionary files from a SQLite source."""

from __future__ import annotations

import argparse
from contextlib import ExitStack
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
    parser.add_argument(
        "--wubi-ranking-source",
        help="Pinyin .dict.db or .dict.db.zip used for Wubi corpus frequencies",
    )
    parser.add_argument(
        "--wubi-ranking-baseline",
        help="Fixed Wubi ranking baseline JSON used for production builds",
    )
    parser.add_argument(
        "--wubi-ranking-overrides",
        help="Reviewed Wubi candidate order overrides used at build time",
    )
    args = parser.parse_args()
    if args.spellings_only and args.dict_only:
        parser.error("--spellings-only and --dict-only cannot be used together")
    if args.skip_idx and args.wubi_prefix_index:
        parser.error("--skip-idx and --wubi-prefix-index cannot be used together")
    if args.wubi_prefix_index and not args.wubi_ranking_source:
        parser.error("--wubi-prefix-index requires --wubi-ranking-source")
    if args.wubi_ranking_source and not args.wubi_prefix_index:
        parser.error("--wubi-ranking-source requires --wubi-prefix-index")
    if args.wubi_ranking_baseline and not args.wubi_prefix_index:
        parser.error("--wubi-ranking-baseline requires --wubi-prefix-index")
    if args.wubi_ranking_overrides and not args.wubi_prefix_index:
        parser.error("--wubi-ranking-overrides requires --wubi-prefix-index")
    return args


def main() -> int:
    args = parse_args()
    with ExitStack() as resources:
        try:
            database_path, cleanup = resolve_source_archive(args.input)
            resources.callback(cleanup)
            ranking_source_path = None
            if args.wubi_ranking_source:
                ranking_source_path, cleanup = resolve_source_archive(
                    args.wubi_ranking_source
                )
                resources.callback(cleanup)
        except (FileNotFoundError, ValueError) as error:
            print(f"Error: {error}", file=sys.stderr)
            return 1

        print(f"Building runtime dictionary from: {database_path}")
        if not args.dict_only:
            build_pinyin_spellings(database_path, args.output + ".spellings.bin")
        if not args.spellings_only:
            dictionary_path = args.output + ".dict.bin"
            build_runtime_dictionary(database_path, dictionary_path)
            if args.wubi_prefix_index:
                build_wubi_prefix_index(
                    dictionary_path,
                    args.output + ".dict.idx",
                    ranking_source_path,
                    args.wubi_ranking_baseline,
                    args.wubi_ranking_overrides,
                )
            elif not args.skip_idx:
                build_pinyin_syllable_index(database_path, args.output + ".dict.idx")
        print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
