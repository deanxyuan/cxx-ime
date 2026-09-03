#!/usr/bin/env python3
"""Focused tests for the Wubi complete prefix index generator."""

from __future__ import annotations

import json
import sqlite3
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = ROOT / "data" / "tools"
BUILD_TOOL = TOOLS_DIR / "build_runtime_dictionary.py"
sys.path.insert(0, str(TOOLS_DIR))
from dict_builder import runtime_dictionary as RUNTIME_DICTIONARY
from dict_builder import wubi_prefix_index as WUBI_INDEX
from dict_builder import wubi_ranking as WUBI_RANKING


def read_dictionary(path: Path):
    data = path.read_bytes()
    _, _, entry_count, _, entries_offset, strings_offset = struct.unpack_from(
        RUNTIME_DICTIONARY.HEADER_FORMAT, data, 0
    )
    entries = []
    for index in range(entry_count):
        offset = entries_offset + index * RUNTIME_DICTIONARY.ENTRY_SIZE
        code_offset, text_offset, code_length, text_length, frequency = struct.unpack_from(
            RUNTIME_DICTIONARY.ENTRY_FORMAT, data, offset
        )
        code = data[strings_offset + code_offset:strings_offset + code_offset + code_length]
        text = data[strings_offset + text_offset:strings_offset + text_offset + text_length]
        entries.append((code, text, frequency))
    return entries


def read_postings(path: Path, packed_code: int):
    data = path.read_bytes()
    header = struct.unpack_from(WUBI_INDEX.HEADER_FORMAT, data, 0)
    _, _, _, _, _, key_count, _, keys_offset, postings_offset, _, _ = header
    for index in range(key_count):
        offset = keys_offset + index * WUBI_INDEX.KEY_SIZE
        key, posting_start, posting_count = struct.unpack_from(
            WUBI_INDEX.KEY_FORMAT, data, offset
        )
        if key == packed_code:
            return list(
                struct.unpack_from(
                    f"<{posting_count}I",
                    data,
                    postings_offset + posting_start * 4,
                )
            )
    return []


def test_complete_ranked_prefix_index():
    with tempfile.TemporaryDirectory(prefix="cxxime-wubi-index-") as temp_dir:
        root = Path(temp_dir)
        database_path = root / "wubi.db"
        archive_path = root / "wubi.dict.db.zip"
        ranking_path = root / "pinyin.db"
        output_prefix = root / "wubi"
        dictionary_path = root / "wubi.dict.bin"
        index_path = root / "wubi.dict.idx"

        connection = sqlite3.connect(database_path)
        connection.execute("create table dict(text text, code text, frequency integer)")
        connection.executemany(
            "insert into dict values(?, ?, ?)",
            [
                ("exact-primary", "d", 20),
                ("exact-secondary", "d", 10),
                ("left", "da", 10),
                ("long-high-frequency", "daaa", 10000),
                ("care", "db", 10),
                ("friend", "dc", 10),
                ("large", "dd", 10),
                ("beard", "de", 10),
                ("unreachable", "API", 10000),
                ("too-long", "abcde", 10000),
            ],
        )
        connection.commit()
        connection.close()

        connection = sqlite3.connect(ranking_path)
        connection.execute("create table dict(text text, frequency integer)")
        connection.executemany(
            "insert into dict values(?, ?)",
            [
                ("exact-primary", 1000),
                ("exact-secondary", 500),
                ("left", 100),
                ("care", 90),
                ("friend", 80),
            ],
        )
        connection.commit()
        connection.close()

        with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.write(database_path, "wubi.dict.db")
        subprocess.run(
            [
                sys.executable,
                str(BUILD_TOOL),
                "--input",
                str(archive_path),
                "--output",
                str(output_prefix),
                "--dict-only",
                "--wubi-prefix-index",
                "--wubi-ranking-source",
                str(ranking_path),
            ],
            check=True,
        )

        entries = read_dictionary(dictionary_path)
        postings = read_postings(index_path, WUBI_INDEX.pack_code(b"d"))
        ranked_text = [entries[index][1].decode("utf-8") for index in postings]
        assert ranked_text[:5] == [
            "exact-primary",
            "exact-secondary",
            "left",
            "care",
            "friend",
        ]
        assert len(postings) == len(entries) - 2
        assert read_postings(index_path, WUBI_INDEX.pack_code(b"a")) == []


def test_visible_ranking_guards_short_codes_and_improves_known_completions():
    entries = [
        (b"qdr", "厃".encode("utf-8"), 10),
        (b"qdrg", "然后".encode("utf-8"), 10),
        (b"qdrp", "危迫".encode("utf-8"), 10),
        (b"qdrt", "多面手".encode("utf-8"), 10),
        (b"utqt", "单身狗".encode("utf-8"), 20),
        (b"utqa", "产钳".encode("utf-8"), 10),
        (b"utqc", "颜色".encode("utf-8"), 10),
        (b"utqd", "痴然".encode("utf-8"), 10),
    ]
    frequencies = {
        "然后".encode("utf-8"): 501658,
        "危迫".encode("utf-8"): 225,
        "多面手".encode("utf-8"): 5690,
        "单身狗".encode("utf-8"): 100,
        "产钳".encode("utf-8"): 1,
        "颜色".encode("utf-8"): 500699,
    }

    qdr_source = WUBI_RANKING.unique_source_ranking([0, 1, 2, 3], entries, 3)
    qdr_ranked = WUBI_RANKING.rerank_visible_candidates(
        qdr_source, entries, 3, frequencies
    )
    assert [entries[index][1].decode("utf-8") for index in qdr_ranked] == [
        "然后",
        "厃",
        "危迫",
        "多面手",
    ]
    assert set(qdr_source[:10]) == set(qdr_ranked[:10])

    utq_source = WUBI_RANKING.unique_source_ranking([4, 5, 6, 7], entries, 3)
    utq_ranked = WUBI_RANKING.rerank_visible_candidates(
        utq_source, entries, 3, frequencies
    )
    assert [entries[index][1].decode("utf-8") for index in utq_ranked] == [
        "颜色",
        "单身狗",
        "产钳",
        "痴然",
    ]

    short_source = WUBI_RANKING.unique_source_ranking([0, 1, 2, 3], entries, 2)
    assert WUBI_RANKING.rerank_visible_candidates(
        short_source, entries, 2, frequencies
    ) == short_source


def test_visible_ranking_repairs_rare_single_character_blockers():
    entries = [
        (b"akfw", "歎".encode("utf-8"), 20),
        (b"akfy", "尀".encode("utf-8"), 20),
        (b"akfe", "艱".encode("utf-8"), 10),
        (b"akfi", "菋".encode("utf-8"), 10),
        (b"akfk", "囏".encode("utf-8"), 10),
        (b"akfn", "戁".encode("utf-8"), 10),
        (b"akfp", "其味无穷".encode("utf-8"), 10),
        (b"akft", "或者".encode("utf-8"), 10),
    ]
    frequencies = {"其味无穷".encode("utf-8"): 480, "或者".encode("utf-8"): 504213}
    assert [
        entries[index][1].decode("utf-8")
        for index in WUBI_RANKING.rerank_visible_candidates(
            list(range(len(entries))), entries, 3, frequencies
        )
    ] == [
        "或者",
        "其味无穷",
        "歎",
        "尀",
        "艱",
        "菋",
        "囏",
        "戁",
    ]


def test_visible_ranking_preserves_exact_and_unproven_candidates():
    entries = [
        (b"goi", b"known-exact", 10),
        (b"goiu", b"very-common-completion", 10),
        (b"abc", b"unknown-exact", 10),
        (b"abca", b"unknown-completion", 10),
        (b"def", b"another-unknown-exact", 10),
        (b"defa", b"weak-completion", 10),
        (b"ghi", b"first-exact", 20),
        (b"ghi", b"second-exact", 10),
        (b"jkl", "罕".encode("utf-8"), 10),
        (b"jkla", "常用".encode("utf-8"), 10),
        (b"jklb", "可靠".encode("utf-8"), 10),
        (b"mno", "僻".encode("utf-8"), 20),
        (b"mno", "次".encode("utf-8"), 10),
        (b"mnoa", "高频".encode("utf-8"), 10),
        (b"pqr", b"ASCII-exact", 10),
        (b"pqra", "常用词".encode("utf-8"), 10),
        (b"stu", "罕".encode("utf-8"), 10),
        (b"stua", "非常常用长词".encode("utf-8"), 10),
    ]
    frequencies = {
        b"known-exact": 100,
        b"very-common-completion": 1000000,
        b"weak-completion": WUBI_RANKING.MIN_RELIABLE_GENERAL_FREQUENCY - 1,
        b"second-exact": 1000,
        "常用".encode("utf-8"): 1000000,
        "可靠".encode("utf-8"): 500000,
        "次".encode("utf-8"): 10,
        "高频".encode("utf-8"): 1000000,
        "常用词".encode("utf-8"): 1000000,
        "非常常用长词".encode("utf-8"): 1000000,
    }

    assert WUBI_RANKING.rerank_visible_candidates(
        [0, 1], entries, 3, frequencies
    ) == [0, 1]
    assert WUBI_RANKING.rerank_visible_candidates(
        [2, 3], entries, 3, frequencies
    ) == [2, 3]
    assert WUBI_RANKING.rerank_visible_candidates(
        [4, 5], entries, 3, frequencies
    ) == [4, 5]
    assert WUBI_RANKING.rerank_visible_candidates(
        [6, 7], entries, 3, frequencies
    ) == [6, 7]
    assert WUBI_RANKING.rerank_visible_candidates(
        [8, 9, 10], entries, 3, frequencies
    ) == [9, 8, 10]
    assert WUBI_RANKING.rerank_visible_candidates(
        [11, 12, 13], entries, 3, frequencies
    ) == [11, 12, 13]
    assert WUBI_RANKING.rerank_visible_candidates(
        [14, 15], entries, 3, frequencies
    ) == [14, 15]
    assert WUBI_RANKING.rerank_visible_candidates(
        [16, 17], entries, 3, frequencies
    ) == [16, 17]


def test_visible_ranking_does_not_import_unreviewed_deeper_candidates():
    entries = [
        (f"abc{index:02d}".encode("ascii"), f"candidate-{index:02d}".encode("ascii"), 10)
        for index in range(12)
    ]
    source = list(range(len(entries)))
    frequencies = {
        entries[index][1]: 1000 - index
        for index in range(len(entries))
    }

    ranked = WUBI_RANKING.rerank_visible_candidates(
        source, entries, 3, frequencies
    )
    assert set(ranked[:10]) == set(source[:10])
    assert ranked[10:] == source[10:]


def test_ranking_override_imports_yiy_completion_only():
    entries = [
        (b"yiyh", "就让".encode("utf-8"), 10),
        (b"yiyf", "就读".encode("utf-8"), 10),
        (b"yiyw", "就座".encode("utf-8"), 20),
        (b"yiya", "应试".encode("utf-8"), 10),
        (b"yiyj", "哀鸿遍野".encode("utf-8"), 10),
        (b"yiyl", "应为".encode("utf-8"), 10),
        (b"yiym", "应市".encode("utf-8"), 10),
        (b"yiyo", "应变".encode("utf-8"), 10),
        (b"yiyq", "就义".encode("utf-8"), 10),
        (b"yiyr", "应诉".encode("utf-8"), 10),
        (b"yiyt", "应许".encode("utf-8"), 10),
        (b"yiyu", "就说".encode("utf-8"), 10),
        (b"yiyw", "就诊".encode("utf-8"), 10),
        (b"yiyy", "应该".encode("utf-8"), 10),
    ]
    frequencies = {
        "就让".encode("utf-8"): 133260,
        "就读".encode("utf-8"): 116305,
        "就座".encode("utf-8"): 18100,
        "应试".encode("utf-8"): 91945,
        "哀鸿遍野".encode("utf-8"): 1725,
        "应为".encode("utf-8"): 109460,
        "应市".encode("utf-8"): 2,
        "应变".encode("utf-8"): 45070,
        "就义".encode("utf-8"): 4105,
        "应诉".encode("utf-8"): 46109,
        "应许".encode("utf-8"): 8085,
        "就说".encode("utf-8"): 192065,
        "就诊".encode("utf-8"): 89990,
        "应该".encode("utf-8"): 501106,
    }
    source = list(range(len(entries)))
    default_ranked = WUBI_RANKING.rerank_visible_candidates(
        source, entries, 3, frequencies
    )
    overrides = {
        b"yiy": [
            WUBI_RANKING.RankingOverride(
                input_code=b"yiy",
                candidate_text="应该".encode("utf-8"),
                candidate_code=b"yiyy",
                target_position=0,
                reason="test",
            )
        ]
    }
    ranked, applied = WUBI_RANKING.apply_ranking_overrides(
        b"yiy", default_ranked, entries, overrides
    )
    assert applied
    assert ranked == [13, *range(13)]
    assert [entries[index][1].decode("utf-8") for index in ranked[:10]] == [
        "应该",
        "就让",
        "就读",
        "就座",
        "应试",
        "哀鸿遍野",
        "应为",
        "应市",
        "应变",
        "就义",
    ]

    audit = WUBI_RANKING.RankingAudit(three_code_prefixes=1000)
    WUBI_RANKING.audit_ranking_change(
        source, ranked, entries, 3, frequencies, audit, applied
    )
    assert audit.visible_set_changes == 1
    assert audit.ranking_override_changes == 1
    assert audit.ranking_override_visible_set_changes == 1
    assert audit.unsafe_top_changes == 0
    audit.validate()


def test_ranking_override_file_is_strict_and_fingerprint_is_semantic():
    document = {
        "format": 1,
        "overrides": [
            {
                "input_code": "yiy",
                "candidate_text": "应该",
                "candidate_code": "yiyy",
                "target_position": 1,
                "reason": "reviewed",
            },
            {
                "input_code": "abc",
                "candidate_text": "测试",
                "candidate_code": "abcd",
                "target_position": 2,
                "reason": "second reviewed override",
            },
        ],
    }
    with tempfile.TemporaryDirectory(prefix="cxxime-wubi-overrides-") as temp_dir:
        first_path = Path(temp_dir) / "first.json"
        second_path = Path(temp_dir) / "second.json"
        first_path.write_text(json.dumps(document, ensure_ascii=False), encoding="utf-8")
        second_path.write_text(
            json.dumps(
                {
                    "overrides": list(reversed(document["overrides"])),
                    "format": document["format"],
                },
                ensure_ascii=False,
                indent=4,
            ),
            encoding="utf-8",
        )
        first = WUBI_RANKING.load_ranking_overrides(str(first_path))
        second = WUBI_RANKING.load_ranking_overrides(str(second_path))
        assert WUBI_RANKING.ranking_overrides_fingerprint(
            first
        ) == WUBI_RANKING.ranking_overrides_fingerprint(second)

        invalid = dict(document)
        invalid["unexpected"] = True
        first_path.write_text(json.dumps(invalid, ensure_ascii=False), encoding="utf-8")
        try:
            WUBI_RANKING.load_ranking_overrides(str(first_path))
        except ValueError:
            pass
        else:
            raise AssertionError("unknown ranking override fields must be rejected")

        duplicate_position = dict(document)
        duplicate_position["overrides"] = [
            dict(document["overrides"][0]),
            {
                **document["overrides"][0],
                "candidate_text": "另一个词",
                "candidate_code": "yiyx",
            },
        ]
        first_path.write_text(
            json.dumps(duplicate_position, ensure_ascii=False), encoding="utf-8"
        )
        try:
            WUBI_RANKING.load_ranking_overrides(str(first_path))
        except ValueError:
            pass
        else:
            raise AssertionError("duplicate override target positions must be rejected")

    entries = [(b"abcd", "测试".encode("utf-8"), 1)]
    no_op = {
        b"abc": [
            WUBI_RANKING.RankingOverride(
                input_code=b"abc",
                candidate_text="测试".encode("utf-8"),
                candidate_code=b"abcd",
                target_position=0,
                reason="no-op",
            )
        ]
    }
    try:
        WUBI_RANKING.apply_ranking_overrides(b"abc", [0], entries, no_op)
    except ValueError:
        pass
    else:
        raise AssertionError("no-op ranking overrides must be rejected")


def test_ranking_override_cannot_promote_lower_general_frequency():
    entries = [
        (b"abca", b"common", 20),
        (b"abcb", b"rare", 10),
    ]
    frequencies = {b"common": 1000, b"rare": 1}
    audit = WUBI_RANKING.RankingAudit()
    WUBI_RANKING.audit_ranking_change(
        [0, 1], [1, 0], entries, 3, frequencies, audit, True
    )
    assert audit.internal_frequency_regressions == 1
    try:
        audit.validate()
    except ValueError:
        pass
    else:
        raise AssertionError("low-frequency override promotion must fail")


def test_ranking_override_coverage_rejects_missing_prefix():
    override = WUBI_RANKING.RankingOverride(
        input_code=b"abc",
        candidate_text=b"candidate",
        candidate_code=b"abcd",
        target_position=0,
        reason="test",
    )
    try:
        WUBI_RANKING.validate_ranking_override_coverage({b"abc": [override]}, set())
    except ValueError:
        pass
    else:
        raise AssertionError("unmatched override prefix must fail")


def test_visible_ranking_promotes_reliable_four_code_character():
    entries = [
        (b"kkkk", "串口".encode("utf-8"), 60),
        (b"kkkk", "叮叮咣咣".encode("utf-8"), 50),
        (b"kkkk", "口".encode("utf-8"), 40),
        (b"kkkk", "咒骂".encode("utf-8"), 30),
    ]
    frequencies = {
        "串口".encode("utf-8"): 77910,
        "叮叮咣咣".encode("utf-8"): 102,
        "口".encode("utf-8"): 2375024,
        "咒骂".encode("utf-8"): 22470,
    }
    assert WUBI_RANKING.rerank_visible_candidates(
        [0, 1, 2, 3], entries, 4, frequencies
    ) == [2, 0, 1, 3]


def test_visible_ranking_preserves_unproven_four_code_order():
    entries = [
        (b"abcd", "原首选".encode("utf-8"), 20),
        (b"abcd", "低频字".encode("utf-8"), 10),
        (b"abcd", "常用词".encode("utf-8"), 5),
        (b"abcd", "常".encode("utf-8"), 1),
        (b"abcd", "高".encode("utf-8"), 1),
    ]
    frequencies = {
        "原首选".encode("utf-8"): 2000000,
        "低频字".encode("utf-8"): 99999,
        "常用词".encode("utf-8"): 1000000,
        "常".encode("utf-8"): 500000,
        "高".encode("utf-8"): 1500000,
    }
    assert WUBI_RANKING.rerank_visible_candidates(
        [0, 1, 2, 3, 4], entries, 4, frequencies
    ) == [0, 1, 2, 3, 4]


def test_visible_ranking_does_not_replace_short_completion_with_longer_text():
    entries = [
        (b"xyza", "短".encode("utf-8"), 10),
        (b"xyzb", "常用长词".encode("utf-8"), 10),
    ]
    frequencies = {
        "常用长词".encode("utf-8"): 1000000,
    }
    assert WUBI_RANKING.rerank_visible_candidates(
        [0, 1], entries, 3, frequencies
    ) == [0, 1]


def test_visible_ranking_does_not_reduce_general_frequency():
    entries = [
        (b"xyza", "原首选".encode("utf-8"), 10),
        (b"xyzb", "新候选".encode("utf-8"), 10),
    ]
    frequencies = {
        "原首选".encode("utf-8"): 500000,
        "新候选".encode("utf-8"): 200000,
    }
    assert WUBI_RANKING.rerank_visible_candidates(
        [0, 1], entries, 3, frequencies
    ) == [0, 1]


def test_visible_ranking_does_not_promote_non_han_completion():
    entries = [
        (b"vux", "甲".encode("utf-8"), 10),
        (b"vuxa", b"ASCII", 10),
    ]
    frequencies = {b"ASCII": 1000000}
    assert WUBI_RANKING.rerank_visible_candidates(
        [0, 1], entries, 3, frequencies
    ) == [0, 1]


def test_ranking_audit_rejects_visible_set_changes_without_key_error():
    entries = [
        (b"abc", "甲".encode("utf-8"), 10),
        (b"abca", "乙".encode("utf-8"), 10),
        (b"abcb", "丙".encode("utf-8"), 10),
    ]
    audit = WUBI_RANKING.RankingAudit()
    WUBI_RANKING.audit_ranking_change(
        [0, 1], [1, 2], entries, 3, {}, audit
    )
    assert audit.visible_set_changes == 1
    try:
        audit.validate()
    except ValueError:
        pass
    else:
        raise AssertionError("visible set drift must fail the ranking audit")


def test_ranking_audit_accepts_classified_four_code_promotion():
    entries = [
        (b"kkkk", "串口".encode("utf-8"), 60),
        (b"kkkk", "口".encode("utf-8"), 40),
    ]
    frequencies = {
        "串口".encode("utf-8"): 77910,
        "口".encode("utf-8"): 2375024,
    }
    audit = WUBI_RANKING.RankingAudit()
    WUBI_RANKING.audit_ranking_change(
        [0, 1], [1, 0], entries, 4, frequencies, audit
    )
    assert audit.four_code_top_changes == 1
    assert audit.safe_four_code_promotions == 1
    assert audit.unsafe_top_changes == 0
    audit.validate()


def test_ranking_audit_accepts_blocked_completion_repair():
    entries = [
        (b"akfw", "歎".encode("utf-8"), 20),
        (b"akfy", "尀".encode("utf-8"), 20),
        (b"akfe", "艱".encode("utf-8"), 10),
        (b"akft", "或者".encode("utf-8"), 10),
    ]
    frequencies = {"或者".encode("utf-8"): 504213}
    audit = WUBI_RANKING.RankingAudit(three_code_prefixes=1000)
    WUBI_RANKING.audit_ranking_change(
        [0, 1, 2, 3], [3, 0, 1, 2], entries, 3, frequencies, audit
    )
    assert audit.blocked_completion_promotions == 1
    assert audit.unsafe_top_changes == 0
    audit.validate()


def test_visible_ranking_keeps_non_han_after_rare_blockers():
    entries = [
        (b"akfw", "歎".encode("utf-8"), 20),
        (b"akfy", "尀".encode("utf-8"), 20),
        (b"akfe", "艱".encode("utf-8"), 10),
        (b"akfp", "其味无穷".encode("utf-8"), 10),
        (b"akft", "或者".encode("utf-8"), 10),
        (b"akfx", b"ASCII", 10),
    ]
    frequencies = {"或者".encode("utf-8"): 504213}
    ranked = WUBI_RANKING.rerank_visible_candidates(
        list(range(len(entries))), entries, 3, frequencies
    )
    assert ranked == [4, 3, 0, 1, 2, 5]


def test_ranking_audit_rejects_unclassified_four_code_promotion():
    entries = [
        (b"abcd", "原首选".encode("utf-8"), 20),
        (b"abcd", "常用词".encode("utf-8"), 10),
    ]
    frequencies = {
        "原首选".encode("utf-8"): 100,
        "常用词".encode("utf-8"): 2000000,
    }
    audit = WUBI_RANKING.RankingAudit()
    WUBI_RANKING.audit_ranking_change(
        [0, 1], [1, 0], entries, 4, frequencies, audit
    )
    assert audit.safe_four_code_promotions == 0
    assert audit.unsafe_top_changes == 1
    try:
        audit.validate()
    except ValueError:
        pass
    else:
        raise AssertionError("unclassified four-code promotion must fail")


if __name__ == "__main__":
    test_complete_ranked_prefix_index()
    test_visible_ranking_guards_short_codes_and_improves_known_completions()
    test_visible_ranking_preserves_exact_and_unproven_candidates()
    test_visible_ranking_does_not_import_unreviewed_deeper_candidates()
    test_ranking_override_imports_yiy_completion_only()
    test_ranking_override_file_is_strict_and_fingerprint_is_semantic()
    test_ranking_override_cannot_promote_lower_general_frequency()
    test_ranking_override_coverage_rejects_missing_prefix()
    test_visible_ranking_repairs_rare_single_character_blockers()
    test_visible_ranking_promotes_reliable_four_code_character()
    test_visible_ranking_preserves_unproven_four_code_order()
    test_visible_ranking_does_not_replace_short_completion_with_longer_text()
    test_visible_ranking_does_not_reduce_general_frequency()
    test_visible_ranking_does_not_promote_non_han_completion()
    test_ranking_audit_rejects_visible_set_changes_without_key_error()
    test_ranking_audit_accepts_classified_four_code_promotion()
    test_ranking_audit_accepts_blocked_completion_repair()
    test_visible_ranking_keeps_non_han_after_rare_blockers()
    test_ranking_audit_rejects_unclassified_four_code_promotion()
