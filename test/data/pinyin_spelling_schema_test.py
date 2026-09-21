#!/usr/bin/env python3

import json
import os
import sqlite3
import sys
import tempfile
import unittest
import zipfile


ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA_TOOLS = os.path.join(ROOT, "data", "tools")
FULL_PINYIN_SCHEMA_PATH = os.path.join(
    ROOT, "data", "schemas", "pinyin.full-pinyin.schema.json"
)
SCHEMA_CASES = (
    (
        "microsoft_shuangpin",
        "pinyin.microsoft-shuangpin.schema.json",
        {
            "ni": "ni",
            "hk": "hao",
            "vs": "zhong",
            "go": "guo",
            "ui": "shi",
            "jx": "jie",
            "y;": "ying",
            "oa": "a",
            "ol": "ai",
            "or": "er",
        },
    ),
    (
        "xiaohe_shuangpin",
        "pinyin.xiaohe-shuangpin.schema.json",
        {
            "ni": "ni",
            "hc": "hao",
            "vs": "zhong",
            "go": "guo",
            "ui": "shi",
            "jp": "jie",
            "yk": "ying",
            "ju": "ju",
            "yu": "yu",
            "aa": "a",
            "an": "an",
            "en": "en",
            "ou": "ou",
            "ad": "ai",
            "er": "er",
        },
    ),
    (
        "ziranma_shuangpin",
        "pinyin.ziranma-shuangpin.schema.json",
        {
            "ni": "ni",
            "hk": "hao",
            "vs": "zhong",
            "go": "guo",
            "ui": "shi",
            "jx": "jie",
            "yy": "ying",
            "ju": "ju",
            "yu": "yu",
            "aa": "a",
            "an": "an",
            "en": "en",
            "ou": "ou",
            "al": "ai",
            "er": "er",
        },
    ),
    (
        "sogou_shuangpin",
        "pinyin.sogou-shuangpin.schema.json",
        {
            "ni": "ni",
            "hk": "hao",
            "vs": "zhong",
            "go": "guo",
            "ui": "shi",
            "jx": "jie",
            "y;": "ying",
            "ju": "ju",
            "yu": "yu",
            "oa": "a",
            "ol": "ai",
            "or": "er",
        },
    ),
)
sys.path.insert(0, DATA_TOOLS)

from dict_builder.pinyin_spellings import PatriciaTrie
from generate_pinyin_spellings import build_shuangpin_script, load_schema


def production_syllabary():
    archive_path = os.path.join(ROOT, "data", "pinyin.dict.db.zip")
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


def serialize_script(script):
    trie = PatriciaTrie()
    for input_code, spellings in sorted(script.items()):
        for spelling in sorted(spellings, key=lambda item: (item.syllable, item.type)):
            trie.insert(
                input_code,
                spelling.syllable,
                spelling.type,
                spelling.credibility,
            )
    return trie.serialize()


class PinyinSpellingSchemaTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.syllabary = production_syllabary()
        cls.schemas = {}
        cls.scripts = {}
        for scheme_id, filename, _ in SCHEMA_CASES:
            schema = load_schema(os.path.join(ROOT, "data", "schemas", filename))
            cls.schemas[scheme_id] = schema
            cls.scripts[scheme_id] = build_shuangpin_script(cls.syllabary, schema)

    def assert_normal_mapping(self, scheme_id, code, syllable):
        script = self.scripts[scheme_id]
        self.assertIn(code, script)
        self.assertTrue(
            any(item.syllable == syllable and item.type == 0 for item in script[code]),
            f"{scheme_id}: {code} does not normally map to {syllable}",
        )

    def test_built_in_schemes_match_golden_mappings(self):
        for scheme_id, _, mappings in SCHEMA_CASES:
            self.assertEqual(self.schemas[scheme_id]["scheme_id"], scheme_id)
            for code, syllable in mappings.items():
                with self.subTest(scheme=scheme_id, code=code, syllable=syllable):
                    self.assert_normal_mapping(scheme_id, code, syllable)

    def test_schema_contract_rejects_unknown_types_and_fields(self):
        schema = self.schemas["microsoft_shuangpin"]
        full_pinyin = load_schema(FULL_PINYIN_SCHEMA_PATH)
        self.assertEqual(full_pinyin["scheme_id"], "full_pinyin")
        self.assertEqual(full_pinyin["speller"]["type"], "full_pinyin")

        unknown_root_field = json.loads(json.dumps(schema))
        unknown_root_field["display_name"] = "duplicate metadata"
        with self.assertRaisesRegex(ValueError, "unknown=\\['display_name'\\]"):
            build_shuangpin_script(self.syllabary, unknown_root_field)

        unknown_type = json.loads(json.dumps(schema))
        unknown_type["speller"]["type"] = "future_type"
        with self.assertRaisesRegex(ValueError, "unsupported speller.type"):
            build_shuangpin_script(self.syllabary, unknown_type)

        unknown_field = json.loads(json.dumps(schema))
        unknown_field["speller"]["future_option"] = True
        with self.assertRaisesRegex(ValueError, "unknown=\\['future_option'\\]"):
            build_shuangpin_script(self.syllabary, unknown_field)

        unsupported_key = json.loads(json.dumps(schema))
        unsupported_key["speller"]["alphabet"] += ","
        with self.assertRaisesRegex(ValueError, "unsupported by the input processor"):
            build_shuangpin_script(self.syllabary, unsupported_key)

    def test_declared_alternatives_and_collisions_are_explicit(self):
        for scheme_id, schema in self.schemas.items():
            for code, syllables in schema["speller"]["expected_collisions"].items():
                for syllable in syllables:
                    with self.subTest(scheme=scheme_id, code=code, syllable=syllable):
                        self.assert_normal_mapping(scheme_id, code, syllable)

        invalid = json.loads(json.dumps(self.schemas["microsoft_shuangpin"]))
        invalid["speller"]["expected_collisions"].pop("lt")
        with self.assertRaisesRegex(ValueError, "collisions differ"):
            build_shuangpin_script(self.syllabary, invalid)

    def test_unreachable_mapping_is_rejected(self):
        invalid = json.loads(json.dumps(self.schemas["microsoft_shuangpin"]))
        invalid["speller"]["finals"]["unused"] = "a"
        with self.assertRaisesRegex(ValueError, "unreachable shuangpin mappings"):
            build_shuangpin_script(self.syllabary, invalid)

    def test_fuzzy_rules_keep_normal_mappings_and_add_fuzzy_syllables(self):
        for scheme_id, script in self.scripts.items():
            self.assert_normal_mapping(scheme_id, "ni", "ni")
            self.assertTrue(
                any(item.syllable == "ni" and item.type == 1 for item in script["li"]),
                scheme_id,
            )

    def test_all_dictionary_syllables_are_covered_or_explicitly_ignored(self):
        for scheme_id, schema in self.schemas.items():
            ignored = set(schema["speller"]["ignored_syllables"])
            generated = {
                spelling.syllable
                for spellings in self.scripts[scheme_id].values()
                for spelling in spellings
                if spelling.type == 0
            }
            self.assertEqual(self.syllabary - ignored, generated, scheme_id)

    def test_binary_generation_is_deterministic(self):
        for scheme_id, schema in self.schemas.items():
            rebuilt = build_shuangpin_script(self.syllabary, schema)
            self.assertEqual(
                serialize_script(self.scripts[scheme_id]),
                serialize_script(rebuilt),
                scheme_id,
            )


if __name__ == "__main__":
    unittest.main()
