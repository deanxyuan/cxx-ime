#!/usr/bin/env python3

import argparse
import os
import sqlite3
import sys
import tempfile
import zipfile


ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA_TOOLS = os.path.join(ROOT, "data", "tools")
sys.path.insert(0, DATA_TOOLS)

from dict_builder.pinyin_spellings import PatriciaTrie
from generate_pinyin_spellings import build_shuangpin_script, load_schema


def load_syllabary(archive_path):
    with zipfile.ZipFile(archive_path) as archive, tempfile.TemporaryDirectory() as directory:
        database_path = archive.extract(archive.namelist()[0], directory)
        connection = sqlite3.connect(database_path)
        try:
            rows = connection.execute("SELECT DISTINCT syllable_ids FROM dict")
            return {
                syllable
                for (value,) in rows
                if value
                for syllable in value.split(":")
                if syllable
            }
        finally:
            connection.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", required=True)
    parser.add_argument("--output", required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--dictionary-archive")
    source.add_argument("--syllable", action="append")
    args = parser.parse_args()

    syllabary = (
        load_syllabary(args.dictionary_archive)
        if args.dictionary_archive
        else set(args.syllable)
    )
    script = build_shuangpin_script(syllabary, load_schema(args.schema))
    trie = PatriciaTrie()
    for input_code, spellings in sorted(script.items()):
        for spelling in sorted(spellings, key=lambda item: (item.syllable, item.type)):
            trie.insert(input_code, spelling.syllable, spelling.type, spelling.credibility)

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "wb") as output:
        output.write(trie.serialize())
    return 0


if __name__ == "__main__":
    sys.exit(main())
