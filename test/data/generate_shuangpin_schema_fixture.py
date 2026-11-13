#!/usr/bin/env python3

import argparse
import os
import sys
import tempfile


ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA_TOOLS = os.path.join(ROOT, "data", "tools")
sys.path.insert(0, DATA_TOOLS)

from dict_builder.pinyin_spellings import PatriciaTrie
from dict_builder.pinyin_source_repair import prepare_source
from dict_builder.pinyin_syllables import validate_dictionary
from generate_pinyin_spellings import build_shuangpin_script, load_schema


def load_syllabary(archive_path):
    with tempfile.TemporaryDirectory() as directory:
        database_path = os.path.join(directory, "pinyin.dict.db")
        prepare_source(archive_path, database_path)
        return validate_dictionary(database_path)


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
