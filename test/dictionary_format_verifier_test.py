#!/usr/bin/env python3

import os
import struct
import sys
import tempfile
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import verify_dictionary_bundle as verifier


class DictionaryFormatVerifierTest(unittest.TestCase):
    def write_header(self, directory, filename, magic, version, size):
        path = os.path.join(directory, filename)
        data = magic + struct.pack("<I", version)
        with open(path, "wb") as output:
            output.write(data.ljust(size, b"\0"))

    def test_dict_accepts_v2_and_rejects_v1(self):
        with tempfile.TemporaryDirectory() as directory:
            errors = []
            self.write_header(
                directory, "current.bin", verifier.DICT_MAGIC_V2, 2, 28
            )
            self.assertTrue(
                verifier.check_dict_bin(directory, "current.bin", errors)
            )
            self.assertEqual(errors, [])

            errors = []
            self.write_header(
                directory, "old.bin", b"CXDIC\x01\x00\x00", 1, 28
            )
            self.assertFalse(verifier.check_dict_bin(directory, "old.bin", errors))
            self.assertTrue(errors)

    def test_id_index_accepts_v3_and_rejects_v2(self):
        with tempfile.TemporaryDirectory() as directory:
            errors = []
            self.write_header(directory, "current.idx", verifier.IDX_MAGIC, 3, 28)
            self.assertTrue(
                verifier.check_dict_idx(directory, "current.idx", errors)
            )
            self.assertEqual(errors, [])

            errors = []
            self.write_header(directory, "old.idx", verifier.IDX_MAGIC, 2, 28)
            self.assertFalse(verifier.check_dict_idx(directory, "old.idx", errors))
            self.assertTrue(errors)

    def test_spellings_accepts_v2_and_rejects_v1(self):
        with tempfile.TemporaryDirectory() as directory:
            errors = []
            self.write_header(
                directory,
                "pinyin.spellings.bin",
                verifier.SPELLINGS_MAGIC_V2,
                2,
                12,
            )
            self.assertTrue(verifier.check_spellings_bin(directory, errors))
            self.assertEqual(errors, [])

            errors = []
            self.write_header(
                directory,
                "pinyin.spellings.bin",
                b"CXSPL\x01\x00\x00",
                1,
                12,
            )
            self.assertFalse(verifier.check_spellings_bin(directory, errors))
            self.assertTrue(errors)


if __name__ == "__main__":
    unittest.main()
