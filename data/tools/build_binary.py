#!/usr/bin/env python3
"""Convert SQLite dict.db to optimized binary format files.

spellings.bin v2 (Patricia trie, O(k) prefix search):
  Header: magic("CXSPL\\2\\0\\0"), version(2), node_count, string_data_size,
          nodes_offset, strings_offset
  Nodes[]: variable-size, each node:
    uint32_t key_offset, key_len
    uint8_t  num_spellings
    uint8_t  num_children
    uint16_t padding
    SpellingEntry[num_spellings]: syllable_offset(4), syllable_len(4), type(1), pad(1), cred(4)
    ChildEntry[num_children]: first_char(1), pad(3), node_index(4)
  String data: packed UTF-8 strings

dict.bin v2 (flat sorted array + inline strings):
  Header: magic("CXDIC\\2\\0\\0"), version(2), entry_count, string_data_size,
          entries_offset, strings_offset
  Entries[entry_count]: sorted by syllable_ids
    uint32_t syllable_ids_offset, syllable_ids_len, text_offset, text_len
    int32_t  frequency
  String data: packed UTF-8 strings
"""

import argparse
import os
import sqlite3
import struct
import sys
import tempfile
import zipfile
from dataclasses import dataclass, field
from typing import Dict, List, Tuple

# ── spellings.bin (Patricia trie) ──────────────────────────────────────────

SPELLINGS_MAGIC = b"CXSPL\x02\x00\x00"
SPELLINGS_HEADER_FMT = "<8sIIIII"
SPELLINGS_HEADER_SIZE = struct.calcsize(SPELLINGS_HEADER_FMT)
SPELLING_FMT = "<IIbxf"  # syllable_offset, syllable_len, type, credibility
SPELLING_SIZE = struct.calcsize(SPELLING_FMT)
CHILD_FMT = "<B3xI"  # first_char, node_index
CHILD_SIZE = struct.calcsize(CHILD_FMT)


@dataclass
class TrieNode:
    key: bytes = b""
    spellings: List[Tuple[bytes, int, float]] = field(default_factory=list)
    children: Dict[int, "TrieNode"] = field(default_factory=dict)


class PatriciaTrie:
    def __init__(self):
        self.root = TrieNode()

    def insert(self, key: str, syllable: str, spelling_type: int, credibility: float):
        self._insert(self.root, key.encode("utf-8"), syllable.encode("utf-8"),
                     spelling_type, credibility)

    def _insert(self, node: TrieNode, key: bytes, syllable: bytes, stype: int, cred: float):
        if not key:
            node.spellings.append((syllable, stype, cred))
            return

        first = key[0]
        if first in node.children:
            child = node.children[first]
            common = 0
            while (common < len(key) and common < len(child.key) and
                   key[common] == child.key[common]):
                common += 1

            if common == len(child.key):
                self._insert(child, key[common:], syllable, stype, cred)
            elif common > 0:
                split = TrieNode(key=child.key[:common])
                node.children[first] = split
                old_suffix = child.key[common:]
                split.children[old_suffix[0]] = TrieNode(
                    key=old_suffix, spellings=child.spellings, children=child.children)
                new_suffix = key[common:]
                if new_suffix:
                    split.children[new_suffix[0]] = TrieNode(
                        key=new_suffix, spellings=[(syllable, stype, cred)])
                else:
                    split.spellings.append((syllable, stype, cred))
            else:
                node.children[first] = TrieNode(key=key, spellings=[(syllable, stype, cred)])
        else:
            node.children[first] = TrieNode(key=key, spellings=[(syllable, stype, cred)])

    def serialize(self) -> bytes:
        string_buf = bytearray()
        string_cache: Dict[bytes, int] = {}

        def intern(b: bytes) -> Tuple[int, int]:
            if b in string_cache:
                return string_cache[b], len(b)
            off = len(string_buf)
            string_buf.extend(b)
            string_cache[b] = off
            return off, len(b)

        # BFS to assign indices and serialize
        nodes_data = bytearray()
        queue = [self.root]
        node_to_idx = {id(self.root): 0}
        idx = 1
        serialized = []

        while queue:
            node = queue.pop(0)
            serialized.append(node)
            for fc in sorted(node.children.keys()):
                child = node.children[fc]
                node_to_idx[id(child)] = idx
                idx += 1
                queue.append(child)

        for node in serialized:
            ko, kl = intern(node.key)
            ns = len(node.spellings)
            nc = len(node.children)
            nodes_data.extend(struct.pack("<IIBB2x", ko, kl, ns, nc))
            for syll_b, stype, cred in node.spellings:
                so, sl = intern(syll_b)
                nodes_data.extend(struct.pack(SPELLING_FMT, so, sl, stype, cred))
            for fc in sorted(node.children.keys()):
                nodes_data.extend(struct.pack(CHILD_FMT, fc, node_to_idx[id(node.children[fc])]))

        entries_offset = SPELLINGS_HEADER_SIZE
        strings_offset = entries_offset + len(nodes_data)
        hdr = struct.pack(SPELLINGS_HEADER_FMT,
                          SPELLINGS_MAGIC, 2, len(serialized), len(string_buf),
                          entries_offset, strings_offset)
        return hdr + nodes_data + bytes(string_buf)


# ── dict.bin (flat sorted array, v1-compatible layout) ─────────────────────

DICT_MAGIC = b"CXDIC\x02\x00\x00"
DICT_HEADER_FMT = "<8sIIIII"
DICT_HEADER_SIZE = struct.calcsize(DICT_HEADER_FMT)
DICT_ENTRY_FMT = "<IIIIi"  # syllable_ids_off, text_off, syllable_ids_len, text_len, freq
DICT_ENTRY_SIZE = struct.calcsize(DICT_ENTRY_FMT)


def build_spellings_trie(db_path: str, output_path: str) -> int:
    conn = sqlite3.connect(db_path)
    cur = conn.cursor()
    cur.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='spellings'")
    if not cur.fetchone():
        print(f"  Warning: no spellings table, skipping")
        conn.close()
        return 0
    cur.execute("SELECT input, syllable, type, credibility FROM spellings")
    rows = cur.fetchall()
    conn.close()
    if not rows:
        print(f"  Warning: spellings empty, skipping")
        return 0

    trie = PatriciaTrie()
    for input_str, syllable, stype, cred in rows:
        trie.insert(input_str, syllable, stype, cred)

    data = trie.serialize()
    with open(output_path, "wb") as f:
        f.write(data)
    print(f"  spellings.bin: {len(rows)} spellings → {len(trie_serialize_nodes(data))} trie nodes, {len(data)} bytes")
    return len(rows)


def trie_serialize_nodes(data: bytes) -> list:
    """Count nodes from serialized data (for logging)."""
    hdr = struct.unpack_from(SPELLINGS_HEADER_FMT, data, 0)
    return [None] * hdr[2]  # node_count


def build_dict_bin(db_path: str, output_path: str) -> int:
    conn = sqlite3.connect(db_path)
    cur = conn.cursor()
    cur.execute("SELECT text, code, frequency, syllable_ids FROM dict ORDER BY syllable_ids, frequency DESC")
    rows = cur.fetchall()
    conn.close()
    if not rows:
        print(f"  Warning: dict empty, skipping")
        return 0

    string_buf = bytearray()
    string_cache: Dict[bytes, int] = {}

    def intern(s: str) -> Tuple[int, int]:
        b = s.encode("utf-8")
        if b in string_cache:
            return string_cache[b], len(b)
        off = len(string_buf)
        string_buf.extend(b)
        string_cache[b] = off
        return off, len(b)

    entries = []
    for text, code, freq, syllable_ids in rows:
        sio, sil = intern(syllable_ids)
        to, tl = intern(text)
        entries.append((sio, to, sil, tl, freq))

    def entry_key(e):
        return bytes(string_buf[e[0]:e[0]+e[2]])
    entries.sort(key=entry_key)

    entries_offset = DICT_HEADER_SIZE
    strings_offset = entries_offset + len(entries) * DICT_ENTRY_SIZE

    with open(output_path, "wb") as f:
        f.write(struct.pack(DICT_HEADER_FMT,
                            DICT_MAGIC, 2, len(entries), len(string_buf),
                            entries_offset, strings_offset))
        for sio, to, sil, tl, freq in entries:
            f.write(struct.pack(DICT_ENTRY_FMT, sio, to, sil, tl, freq))
        f.write(bytes(string_buf))

    print(f"  dict.bin: {len(entries)} entries, {len(string_buf)} bytes strings")
    return len(entries)


def resolve_input(path: str) -> tuple:
    """Resolve input path, extracting .zip if needed. Returns (db_path, cleanup_fn)."""
    # Direct .db file exists
    if os.path.isfile(path) and not path.endswith(".zip"):
        return path, lambda: None

    # Try .zip variant
    zip_path = path if path.endswith(".zip") else path + ".zip"
    if os.path.isfile(zip_path):
        tmpdir = tempfile.mkdtemp(prefix="cxxime_")
        print(f"  Extracting {zip_path}...")
        with zipfile.ZipFile(zip_path, "r") as zf:
            names = zf.namelist()
            zf.extractall(tmpdir)
        # Find the extracted .db file
        for name in names:
            if name.endswith(".dict.db"):
                extracted = os.path.join(tmpdir, name)
                return extracted, lambda: (
                    os.remove(extracted),
                    os.rmdir(tmpdir),
                )
        # Fallback: use first extracted file
        extracted = os.path.join(tmpdir, names[0])
        return extracted, lambda: (os.remove(extracted), os.rmdir(tmpdir))

    print(f"Error: {path} not found (also checked {zip_path})", file=sys.stderr)
    sys.exit(1)


def build_id_index_file(db_path: str, output_path: str) -> int:
    """Build syllabary + ID index and serialize to .dict.idx file."""
    import sqlite3
    conn = sqlite3.connect(db_path)
    cur = conn.cursor()
    cur.execute("SELECT syllable_ids FROM dict ORDER BY syllable_ids")
    rows = cur.fetchall()
    conn.close()

    # Build syllabary
    syl_to_id = {}
    syllabary = []
    for (sid,) in rows:
        if sid:
            for s in sid.split(":"):
                if s and s not in syl_to_id:
                    syl_to_id[s] = len(syllabary)
                    syllabary.append(s)

    # Build ID index
    id_entries = []  # list of (ids_list, entry_index)
    for idx, (sid,) in enumerate(rows):
        if not sid:
            continue
        ids = []
        for s in sid.split(":"):
            if s:
                ids.append(syl_to_id[s])
        if ids:
            id_entries.append((ids, idx))

    id_entries.sort(key=lambda x: x[0])

    # Serialize syllabary
    syl_data = bytearray()
    syl_offsets = []
    for s in syllabary:
        syl_offsets.append(len(syl_data))
        syl_data.extend(s.encode("utf-8"))
        syl_data.append(0)

    # Serialize ID index
    idx_data = bytearray()
    for ids, entry_idx in id_entries:
        idx_data.extend(struct.pack("<I", len(ids)))
        for i in ids:
            idx_data.extend(struct.pack("<I", i))
        idx_data.extend(struct.pack("<I", entry_idx))

    # Header: magic "CXIDX\x02\0\0" + 4*uint32
    header = b"CXIDX\x02\x00\x00"
    header += struct.pack("<IIII",
        len(syllabary), len(syl_data),
        len(id_entries), len(idx_data))

    with open(output_path, "wb") as f:
        f.write(header)
        f.write(struct.pack(f"<{len(syl_offsets)}I", *syl_offsets))
        f.write(bytes(syl_data))
        f.write(bytes(idx_data))

    size_mb = os.path.getsize(output_path) / (1024 * 1024)
    print(f"  dict.idx: {len(syllabary)} syllables, {len(id_entries)} entries, {size_mb:.1f} MB")
    return len(id_entries)


def main():
    parser = argparse.ArgumentParser(description="Convert SQLite dict.db to binary format")
    parser.add_argument("--input", "-i", required=True,
                        help="Input .dict.db or .dict.db.zip file")
    parser.add_argument("--output", "-o", required=True)
    parser.add_argument("--spellings-only", action="store_true")
    parser.add_argument("--dict-only", action="store_true")
    args = parser.parse_args()

    db_path, cleanup = resolve_input(args.input)
    try:
        print(f"Building binary dict from: {db_path}")
        if not args.dict_only:
            build_spellings_trie(db_path, args.output + ".spellings.bin")
        if not args.spellings_only:
            build_dict_bin(db_path, args.output + ".dict.bin")
            build_id_index_file(db_path, args.output + ".dict.idx")
        print("Done.")
    finally:
        cleanup()
    return 0


if __name__ == "__main__":
    sys.exit(main())
