#!/usr/bin/env python3

import json
import os
import sqlite3
import struct
import sys
import tempfile
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "data", "tools"))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

from dict_builder import reverse_index
from dict_builder import runtime_dictionary
from package_checks.dictionary import check_dictionary_manifest
from prepare_dictionary_bundle import MANIFEST_FILES, write_dictionary_manifest
import verify_dictionary_bundle as verifier


class ReverseIndexPipelineTest(unittest.TestCase):
    SOURCE_ENTRIES = [
        ("在", "z", 2),
        ("啊", "b", 5),
        ("啊", "a", 1),
        ("啊", "a", 7),
        ("啊", "a", 7),
        ("撤单", "chedan", 3),
    ]

    def build_fixture(self, directory):
        database_path = os.path.join(directory, "source.db")
        dictionary_path = os.path.join(directory, "pinyin.dict.bin")
        index_path = os.path.join(directory, "pinyin.reverse.idx")
        connection = sqlite3.connect(database_path)
        connection.execute(
            "CREATE TABLE dict (text TEXT, code TEXT, frequency INTEGER)"
        )
        connection.executemany(
            "INSERT INTO dict (text, code, frequency) VALUES (?, ?, ?)",
            self.SOURCE_ENTRIES,
        )
        connection.commit()
        connection.close()
        runtime_dictionary.build(database_path, dictionary_path)
        reverse_index.build(dictionary_path, index_path, chunk_entries=2)
        with open(dictionary_path, "rb") as source:
            string_size = struct.unpack_from("<I", source.read(20), 16)[0]
        return dictionary_path, index_path, string_size

    def read_reverse_entries(self, dictionary_path, index_path):
        with open(dictionary_path, "rb") as source:
            dictionary = source.read()
        with open(index_path, "rb") as source:
            index = source.read()
        _, _, entry_count, _, entries_offset, strings_offset = struct.unpack_from(
            "<8sIIIII", dictionary, 0
        )
        entry_ids = struct.unpack_from(f"<{entry_count}I", index, 36)
        result = []
        for entry_id in entry_ids:
            code_offset, text_offset, code_length, text_length, frequency = (
                struct.unpack_from("<IIIIi", dictionary, entries_offset + entry_id * 20)
            )
            code = dictionary[
                strings_offset + code_offset:strings_offset + code_offset + code_length
            ].decode("utf-8")
            text = dictionary[
                strings_offset + text_offset:strings_offset + text_offset + text_length
            ].decode("utf-8")
            result.append((text, code, frequency))
        return list(entry_ids), result

    def test_exact_format_total_permutation_and_stable_order(self):
        with tempfile.TemporaryDirectory() as directory:
            dictionary_path, index_path, string_size = self.build_fixture(directory)
            with open(index_path, "rb") as source:
                data = source.read()

            header = struct.unpack_from("<8s7I", data, 0)
            self.assertEqual(
                header,
                (
                    b"CXRIDX\x00\x00",
                    1,
                    36 + len(self.SOURCE_ENTRIES) * 4,
                    len(self.SOURCE_ENTRIES),
                    len(self.SOURCE_ENTRIES),
                    string_size,
                    36,
                    0,
                ),
            )
            entry_ids, reverse_entries = self.read_reverse_entries(
                dictionary_path, index_path
            )
            self.assertEqual(sorted(entry_ids), list(range(len(self.SOURCE_ENTRIES))))
            self.assertEqual(
                reverse_entries,
                sorted(self.SOURCE_ENTRIES, key=lambda item: (item[0], item[1], -item[2])),
            )

            errors = []
            self.assertTrue(
                verifier.check_reverse_index(
                    directory,
                    "pinyin.reverse.idx",
                    "pinyin.dict.bin",
                    errors,
                )
            )
            self.assertEqual(errors, [])

    def test_verifier_rejects_corrupt_header_permutation_and_order(self):
        corruptions = {
            "metadata": lambda data: struct.pack_into("<I", data, 24, 999),
            "permutation": lambda data: struct.pack_into("<I", data, 40, 3),
            "ordering": lambda data: (
                struct.pack_into("<I", data, 36, 4),
                struct.pack_into("<I", data, 40, 3),
            ),
        }
        for name, corrupt in corruptions.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                _, index_path, _ = self.build_fixture(directory)
                with open(index_path, "rb") as source:
                    data = bytearray(source.read())
                corrupt(data)
                with open(index_path, "wb") as output:
                    output.write(data)

                errors = []
                self.assertFalse(
                    verifier.check_reverse_index(
                        directory,
                        "pinyin.reverse.idx",
                        "pinyin.dict.bin",
                        errors,
                    )
                )
                self.assertTrue(errors)

    def test_reverse_indexes_are_mandatory_manifest_roles(self):
        with tempfile.TemporaryDirectory() as directory:
            data_directory = os.path.join(directory, "data")
            os.makedirs(data_directory)
            for _, filename in MANIFEST_FILES:
                with open(os.path.join(data_directory, filename), "wb") as output:
                    output.write(filename.encode("ascii"))

            manifest_path = write_dictionary_manifest(data_directory)
            with open(manifest_path, encoding="utf-8") as source:
                manifest = json.load(source)
            roles = {item["role"] for item in manifest["files"]}
            self.assertIn("pinyin_reverse_index", roles)
            self.assertIn("wubi_reverse_index", roles)

            errors = []
            check_dictionary_manifest(errors, directory)
            self.assertEqual(errors, [])


if __name__ == "__main__":
    unittest.main()
