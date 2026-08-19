"""Rank visible Wubi candidates with conservative corpus-based rules."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import sqlite3
import struct
from typing import Dict, List, Sequence, Set, Tuple


VISIBLE_CANDIDATE_CAPACITY = 10
MIN_RELIABLE_GENERAL_FREQUENCY = 100000
MIN_RELIABLE_FOUR_CODE_CHARACTER_FREQUENCY = 1000000
MAX_PROMOTED_COMPLETION_LENGTH = 3
MAX_FOUR_CODE_PROMOTION_PERMILLE = 1
MIN_RARE_SINGLE_CHARACTER_BLOCKERS = 3
MIN_BLOCKED_COMPLETION_FREQUENCY = 500000
MAX_BLOCKED_COMPLETION_LENGTH = 4
MAX_BLOCKED_COMPLETION_PROMOTION_PERMILLE = 1
DictionaryEntry = Tuple[bytes, bytes, int]


def ranking_rules() -> Dict[str, int]:
    return {
        "visible_candidate_capacity": VISIBLE_CANDIDATE_CAPACITY,
        "min_reliable_general_frequency": MIN_RELIABLE_GENERAL_FREQUENCY,
        "min_reliable_four_code_character_frequency": (
            MIN_RELIABLE_FOUR_CODE_CHARACTER_FREQUENCY
        ),
        "max_promoted_completion_length": MAX_PROMOTED_COMPLETION_LENGTH,
        "max_four_code_promotion_permille": MAX_FOUR_CODE_PROMOTION_PERMILLE,
        "min_rare_single_character_blockers": MIN_RARE_SINGLE_CHARACTER_BLOCKERS,
        "min_blocked_completion_frequency": MIN_BLOCKED_COMPLETION_FREQUENCY,
        "max_blocked_completion_length": MAX_BLOCKED_COMPLETION_LENGTH,
        "max_blocked_completion_promotion_permille": (
            MAX_BLOCKED_COMPLETION_PROMOTION_PERMILLE
        ),
    }


def dictionary_fingerprint(entries: Sequence[DictionaryEntry]) -> str:
    digest = hashlib.sha256()
    for code, text, frequency in entries:
        digest.update(struct.pack("<IIi", len(code), len(text), frequency))
        digest.update(code)
        digest.update(text)
    return digest.hexdigest()


def frequency_fingerprint(frequencies: Dict[bytes, int]) -> str:
    digest = hashlib.sha256()
    for text in sorted(frequencies):
        digest.update(struct.pack("<II", len(text), frequencies[text]))
        digest.update(text)
    return digest.hexdigest()


def ranking_fingerprint(
    records: Sequence[Tuple[bytes, Sequence[int]]],
) -> str:
    digest = hashlib.sha256()
    for prefix, ranking in records:
        digest.update(struct.pack("<II", len(prefix), len(ranking)))
        digest.update(prefix)
        for entry_index in ranking:
            digest.update(struct.pack("<I", entry_index))
    return digest.hexdigest()


@dataclass
class RankingAudit:
    prefix_count: int = 0
    three_code_prefixes: int = 0
    four_code_prefixes: int = 0
    short_prefix_changes: int = 0
    visible_set_changes: int = 0
    unsafe_top_changes: int = 0
    three_code_top_changes: int = 0
    four_code_top_changes: int = 0
    safe_four_code_promotions: int = 0
    blocked_completion_promotions: int = 0
    unknown_exact_demotions: int = 0
    visible_order_changes: int = 0
    visible_position_moves: int = 0
    internal_frequency_regressions: int = 0

    def validate(self) -> None:
        errors = []
        if self.short_prefix_changes:
            errors.append(f"short prefix changes: {self.short_prefix_changes}")
        if self.visible_set_changes:
            errors.append(f"visible candidate set changes: {self.visible_set_changes}")
        if self.internal_frequency_regressions:
            errors.append(
                "visible candidate frequency regressions: "
                f"{self.internal_frequency_regressions}"
            )
        if self.unsafe_top_changes:
            errors.append(f"unsafe top candidate changes: {self.unsafe_top_changes}")
        if self.three_code_top_changes > self.three_code_prefixes * 20 // 100:
            errors.append(
                "three-code top candidate changes exceed the 20% review budget: "
                f"{self.three_code_top_changes}/{self.three_code_prefixes}"
            )
        blocked_completion_budget = max(
            1,
            self.three_code_prefixes
            * MAX_BLOCKED_COMPLETION_PROMOTION_PERMILLE
            // 1000,
        )
        if self.blocked_completion_promotions > blocked_completion_budget:
            errors.append(
                "blocked completion promotions exceed the review budget: "
                f"{self.blocked_completion_promotions}/"
                f"{self.three_code_prefixes}"
            )
        if self.four_code_top_changes != self.safe_four_code_promotions:
            errors.append(
                "unclassified four-code top candidate changes: "
                f"{self.four_code_top_changes - self.safe_four_code_promotions}"
            )
        four_code_promotion_budget = max(
            1,
            self.four_code_prefixes * MAX_FOUR_CODE_PROMOTION_PERMILLE // 1000,
        )
        if self.safe_four_code_promotions > four_code_promotion_budget:
            errors.append(
                "safe four-code promotions exceed the review budget: "
                f"{self.safe_four_code_promotions}/{self.four_code_prefixes}"
            )
        if self.unknown_exact_demotions > self.three_code_prefixes // 100:
            errors.append(
                "unknown exact demotions exceed the 1% review budget: "
                f"{self.unknown_exact_demotions}/{self.three_code_prefixes}"
            )
        if errors:
            raise ValueError("Wubi ranking audit failed: " + ", ".join(errors))


def load_general_frequencies(
    database_path: str,
    needed_texts: Set[bytes],
) -> Dict[bytes, int]:
    """Load the maximum corpus frequency for each text from the Pinyin source."""
    frequencies: Dict[bytes, int] = {}
    connection = sqlite3.connect(database_path)
    try:
        for text, frequency in connection.execute("SELECT text, frequency FROM dict"):
            encoded = text.encode("utf-8")
            if encoded not in needed_texts:
                continue
            value = max(int(frequency or 0), 0)
            if value > frequencies.get(encoded, -1):
                frequencies[encoded] = value
    finally:
        connection.close()
    return frequencies


def source_rank_key(
    entry_index: int,
    entries: Sequence[DictionaryEntry],
    prefix_length: int,
):
    code, text, frequency = entries[entry_index]
    return (
        0 if len(code) == prefix_length else 1,
        len(code),
        -frequency,
        code,
        len(text),
        text,
        entry_index,
    )


def unique_source_ranking(
    entry_indexes: Sequence[int],
    entries: Sequence[DictionaryEntry],
    prefix_length: int,
) -> List[int]:
    ranked = sorted(
        entry_indexes,
        key=lambda index: source_rank_key(index, entries, prefix_length),
    )
    unique = []
    seen_text = set()
    for entry_index in ranked:
        text = entries[entry_index][1]
        if text in seen_text:
            continue
        seen_text.add(text)
        unique.append(entry_index)
    return unique


def _text_length(text: bytes) -> int:
    return len(text.decode("utf-8"))


def _is_han_text(text: bytes) -> bool:
    decoded = text.decode("utf-8")
    return bool(decoded) and all(
        0x3400 <= ord(character) <= 0x4DBF
        or 0x4E00 <= ord(character) <= 0x9FFF
        or 0xF900 <= ord(character) <= 0xFAFF
        or 0x20000 <= ord(character) <= 0x3134F
        for character in decoded
    )


def _completion_key(
    item: Tuple[int, int],
    entries: Sequence[DictionaryEntry],
    general_frequencies: Dict[bytes, int],
):
    source_position, entry_index = item
    text = entries[entry_index][1]
    return (
        -general_frequencies.get(text, 0),
        _text_length(text),
        source_position,
    )


def _safe_four_code_promotion(
    visible: Sequence[Tuple[int, int]],
    entries: Sequence[DictionaryEntry],
    general_frequencies: Dict[bytes, int],
) -> Tuple[int, int] | None:
    eligible = []
    for source_position, entry_index in visible[1:]:
        code, text, _ = entries[entry_index]
        frequency = general_frequencies.get(text, 0)
        if (
            len(code) != 4
            or _text_length(text) != 1
            or not _is_han_text(text)
            or frequency < MIN_RELIABLE_FOUR_CODE_CHARACTER_FREQUENCY
        ):
            continue
        prior_frequency = max(
            (
                general_frequencies.get(entries[prior_index][1], 0)
                for _, prior_index in visible[:source_position]
            ),
            default=0,
        )
        if frequency < prior_frequency:
            continue
        eligible.append((source_position, entry_index))

    if not eligible:
        return None
    return min(
        eligible,
        key=lambda item: (
            -general_frequencies.get(entries[item[1]][1], 0),
            item[0],
        ),
    )


def _repair_rare_single_character_blockers(
    visible: Sequence[Tuple[int, int]],
    entries: Sequence[DictionaryEntry],
    general_frequencies: Dict[bytes, int],
) -> List[Tuple[int, int]] | None:
    leading_blocker_count = 0
    for _, entry_index in visible:
        text = entries[entry_index][1]
        if (
            _text_length(text) != 1
            or not _is_han_text(text)
            or general_frequencies.get(text, 0) != 0
        ):
            break
        leading_blocker_count += 1
    if leading_blocker_count < MIN_RARE_SINGLE_CHARACTER_BLOCKERS:
        return None

    strong_completions = [
        item
        for item in visible[leading_blocker_count:]
        if (
            2
            <= _text_length(entries[item[1]][1])
            <= MAX_BLOCKED_COMPLETION_LENGTH
            and _is_han_text(entries[item[1]][1])
            and general_frequencies.get(entries[item[1]][1], 0)
            >= MIN_BLOCKED_COMPLETION_FREQUENCY
        )
    ]
    if not strong_completions:
        return None
    strong_completions.sort(
        key=lambda item: _completion_key(item, entries, general_frequencies)
    )
    rare_blockers = [
        item
        for item in visible
        if (
            _text_length(entries[item[1]][1]) == 1
            and _is_han_text(entries[item[1]][1])
            and general_frequencies.get(entries[item[1]][1], 0) == 0
        )
    ]
    meaningful = [
        item
        for item in visible
        if (
            item not in strong_completions
            and item not in rare_blockers
            and 2
            <= _text_length(entries[item[1]][1])
            <= MAX_BLOCKED_COMPLETION_LENGTH
            and _is_han_text(entries[item[1]][1])
        )
    ]
    other = [
        item
        for item in visible
        if (
            item not in strong_completions
            and item not in meaningful
            and item not in rare_blockers
        )
    ]
    return strong_completions + meaningful + rare_blockers + other


def rerank_visible_candidates(
    source_ranking: Sequence[int],
    entries: Sequence[DictionaryEntry],
    prefix_length: int,
    general_frequencies: Dict[bytes, int],
) -> List[int]:
    """Rerank only the existing visible set; deeper candidates remain untouched."""
    if prefix_length <= 2 or not source_ranking:
        return list(source_ranking)

    visible = list(enumerate(source_ranking[:VISIBLE_CANDIDATE_CAPACITY]))
    if prefix_length == 3:
        exact = []
        reliable_completions = []
        other_completions = []
        for item in visible:
            _, entry_index = item
            code, text, _ = entries[entry_index]
            general_frequency = general_frequencies.get(text, 0)
            if len(code) == prefix_length:
                exact.append(item)
            elif (
                general_frequency >= MIN_RELIABLE_GENERAL_FREQUENCY
                and _text_length(text) <= MAX_PROMOTED_COMPLETION_LENGTH
                and _is_han_text(text)
            ):
                reliable_completions.append(item)
            else:
                other_completions.append(item)

        if not reliable_completions:
            return list(source_ranking)
        if exact:
            reliable_completions.sort(
                key=lambda item: _completion_key(item, entries, general_frequencies)
            )
            exact_has_general_evidence = any(
                general_frequencies.get(entries[item[1]][1], 0) > 0
                for item in exact
            )
            exact_is_han = all(_is_han_text(entries[item[1]][1]) for item in exact)
            if not exact_has_general_evidence and exact_is_han:
                promoted = reliable_completions[:1]
                remaining = exact + reliable_completions[1:] + other_completions
                remaining.sort(key=lambda item: item[0])
                reordered = promoted + remaining
            else:
                reordered = exact + reliable_completions + other_completions
        else:
            source_top_length = _text_length(entries[visible[0][1]][1])
            source_top_frequency = general_frequencies.get(
                entries[visible[0][1]][1], 0
            )
            promotable = [
                item
                for item in reliable_completions
                if _text_length(entries[item[1]][1]) <= source_top_length
                and general_frequencies.get(entries[item[1]][1], 0)
                >= source_top_frequency
                and general_frequencies.get(entries[item[1]][1], 0)
                >= max(
                    (
                        general_frequencies.get(entries[other[1]][1], 0)
                        for other in visible[: item[0]]
                    ),
                    default=0,
                )
            ]
            if not promotable:
                repaired = _repair_rare_single_character_blockers(
                    visible, entries, general_frequencies
                )
                if repaired is None:
                    return list(source_ranking)
                reordered = repaired
            else:
                promotable.sort(
                    key=lambda item: _completion_key(
                        item, entries, general_frequencies
                    )
                )
                promoted_indexes = {item[1] for item in promotable}
                remaining = [
                    item for item in visible if item[1] not in promoted_indexes
                ]
                reordered = promotable + remaining
    elif prefix_length == 4:
        promoted = _safe_four_code_promotion(
            visible, entries, general_frequencies
        )
        if promoted is None:
            return list(source_ranking)
        reordered = [promoted] + [item for item in visible if item != promoted]
    else:
        return list(source_ranking)

    return [entry_index for _, entry_index in reordered] + list(
        source_ranking[VISIBLE_CANDIDATE_CAPACITY:]
    )


def audit_ranking_change(
    source_ranking: Sequence[int],
    ranked: Sequence[int],
    entries: Sequence[DictionaryEntry],
    prefix_length: int,
    general_frequencies: Dict[bytes, int],
    audit: RankingAudit,
) -> None:
    audit.prefix_count += 1
    if prefix_length == 3:
        audit.three_code_prefixes += 1
    elif prefix_length == 4:
        audit.four_code_prefixes += 1
    if prefix_length <= 2 and list(source_ranking) != list(ranked):
        audit.short_prefix_changes += 1

    source_visible = source_ranking[:VISIBLE_CANDIDATE_CAPACITY]
    ranked_visible = ranked[:VISIBLE_CANDIDATE_CAPACITY]
    if set(source_visible) != set(ranked_visible):
        audit.visible_set_changes += 1
    if list(source_visible) != list(ranked_visible):
        audit.visible_order_changes += 1
        source_positions = {
            entry_index: position
            for position, entry_index in enumerate(source_visible)
        }
        ranked_positions = {
            entry_index: position
            for position, entry_index in enumerate(ranked_visible)
        }
        common_entries = set(source_positions) & set(ranked_positions)
        audit.visible_position_moves += sum(
            abs(source_positions[entry_index] - ranked_positions[entry_index])
            for entry_index in common_entries
        )
        if common_entries == set(source_positions) == set(ranked_positions):
            for left_position, left_entry in enumerate(source_visible):
                left_frequency = general_frequencies.get(entries[left_entry][1], 0)
                for right_entry in source_visible[left_position + 1 :]:
                    if (
                        ranked_positions[left_entry] > ranked_positions[right_entry]
                        and general_frequencies.get(entries[right_entry][1], 0)
                        < left_frequency
                    ):
                        audit.internal_frequency_regressions += 1
    if not source_ranking or not ranked or source_ranking[0] == ranked[0]:
        return

    source_code, source_text, _ = entries[source_ranking[0]]
    _, ranked_text, _ = entries[ranked[0]]
    source_frequency = general_frequencies.get(source_text, 0)
    ranked_frequency = general_frequencies.get(ranked_text, 0)

    if prefix_length == 3:
        audit.three_code_top_changes += 1
        if len(source_code) == prefix_length:
            source_exact = [
                entry_index
                for entry_index in source_visible
                if len(entries[entry_index][0]) == prefix_length
            ]
            valid_unknown_exact_demotion = (
                source_frequency == 0
                and all(
                    general_frequencies.get(entries[entry_index][1], 0) == 0
                    and _is_han_text(entries[entry_index][1])
                    for entry_index in source_exact
                )
                and len(entries[ranked[0]][0]) > prefix_length
                and _text_length(ranked_text) <= MAX_PROMOTED_COMPLETION_LENGTH
                and ranked_frequency >= MIN_RELIABLE_GENERAL_FREQUENCY
                and list(ranked_visible[1 : 1 + len(source_exact)]) == source_exact
            )
            if valid_unknown_exact_demotion:
                audit.unknown_exact_demotions += 1
                return
            audit.unsafe_top_changes += 1
            return
        repaired = _repair_rare_single_character_blockers(
            list(enumerate(source_visible)), entries, general_frequencies
        )
        if repaired is not None and repaired[0][1] == ranked[0]:
            audit.blocked_completion_promotions += 1
            return
    elif prefix_length == 4:
        audit.four_code_top_changes += 1
        visible = list(enumerate(source_visible))
        safe_promotion = _safe_four_code_promotion(
            visible, entries, general_frequencies
        )
        if safe_promotion is not None and safe_promotion[1] == ranked[0]:
            audit.safe_four_code_promotions += 1
        else:
            audit.unsafe_top_changes += 1
            return

    source_length = _text_length(source_text)
    ranked_length = _text_length(ranked_text)
    if ranked_length > source_length or ranked_frequency < source_frequency:
        audit.unsafe_top_changes += 1
