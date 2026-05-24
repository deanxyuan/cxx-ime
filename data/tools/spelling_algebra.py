#!/usr/bin/env python3
"""Spelling Algebra engine for CxxIME — port of librime's calculus.cc + algebra.cc.

Applies configurable regex rules to expand a syllabary into a Script
(map<input_str, list[Spelling]>), supporting abbreviation, fuzzy matching,
derivation, transformation, erasion, and transliteration.

Usage:
    from spelling_algebra import SpellingAlgebra, Script, parse_rules_from_yaml
    script = Script()
    for s in syllabary: script.add_syllable(s)
    rules = parse_rules_from_yaml("schema.yaml")
    SpellingAlgebra(rules).apply(script)
"""

import re
import sys
from dataclasses import dataclass, field

# SpellingType — matches librime algo/spelling.h
K_NORMAL = 0
K_FUZZY = 1
K_ABBREV = 2

# Credibility penalties — matches librime algo/calculus.cc
K_ABBREV_PENALTY = -0.6931471805599453   # log(0.5)
K_FUZZY_PENALTY = -0.6931471805599453    # log(0.5)


@dataclass
class Spelling:
    """A possible syllable interpretation — matches librime Spelling."""
    syllable: str
    type: int = K_NORMAL
    credibility: float = 0.0

    def __eq__(self, other):
        return isinstance(other, Spelling) and self.syllable == other.syllable

    def __hash__(self):
        return hash(self.syllable)


class Script(dict):
    """map<input_str, list[Spelling]> — matches librime Script (algebra.h).

    Each key is a possible user input string, each value is the list of
    syllables it can produce with their type and credibility.
    """

    def add_syllable(self, syllable):
        """Add a normal spelling for a syllable — matches Script::AddSyllable."""
        if syllable not in self:
            self[syllable] = [Spelling(syllable, K_NORMAL, 0.0)]

    def merge(self, key, rule_type, rule_credibility, spellings):
        """Merge spellings under a new key — matches Script::Merge.

        For each spelling in the source list:
        - type = max(rule_type, spelling.type) — worse type wins
        - credibility = spelling.credibility + rule_credibility
        If key already exists, deduplicate: keep better type, higher credibility.
        """
        if key not in self:
            self[key] = []
        target = self[key]
        for sp in spellings:
            new_type = max(rule_type, sp.type)
            new_cred = sp.credibility + rule_credibility
            # Deduplicate: find existing entry with same syllable
            existing = None
            for e in target:
                if e.syllable == sp.syllable:
                    existing = e
                    break
            if existing:
                # Keep better (lower) type and higher credibility
                existing.type = min(existing.type, new_type)
                existing.credibility = max(existing.credibility, new_cred)
            else:
                target.append(Spelling(sp.syllable, new_type, new_cred))


class SpellingRule:
    """Base class for spelling rules — matches librime Calculation (calculus.h)."""

    def apply(self, text):
        """Try to transform text. Return (new_text, True) or (text, False)."""
        raise NotImplementedError

    def addition(self):
        """Whether the transformed form is added to the Script."""
        return True

    def deletion(self):
        """Whether the original form is removed from the Script."""
        return True

    def rule_type(self):
        """SpellingType produced by this rule."""
        return K_NORMAL

    def credibility_delta(self):
        """Credibility penalty added by this rule."""
        return 0.0


class TransformationRule(SpellingRule):
    """xform/ptn/rep/ — regex replacement.
    Matches librime Transformation (calculus.cc:98-119).
    addition=True, deletion=True (replaces original).
    """

    def __init__(self, pattern, replacement):
        self.pattern = re.compile(pattern)
        self.replacement = replacement

    def apply(self, text):
        result = self.pattern.sub(self.replacement, text)
        if result != text:
            return result, True
        return text, False


class ErasionRule(SpellingRule):
    """erase/ptn/ — remove entries matching pattern.
    Matches librime Erasion (calculus.cc:123-141).
    addition=False (removes the entry entirely).
    """

    def __init__(self, pattern):
        self.pattern = re.compile(pattern)

    def apply(self, text):
        if self.pattern.fullmatch(text):
            return "", True
        return text, False

    def addition(self):
        return False


class DerivationRule(TransformationRule):
    """derive/ptn/rep/ — keep original + add transformed.
    Matches librime Derivation (calculus.cc:145-156).
    addition=True, deletion=False (keeps original).
    """

    def deletion(self):
        return False


class FuzzingRule(DerivationRule):
    """fuzz/ptn/rep/ — fuzzy spelling variant.
    Matches librime Fuzzing (calculus.cc:160-180).
    Like Derivation but sets type=kFuzzySpelling, penalty=-0.693.
    """

    def rule_type(self):
        return K_FUZZY

    def credibility_delta(self):
        return K_FUZZY_PENALTY


class AbbreviationRule(DerivationRule):
    """abbrev/ptn/rep/ — abbreviation.
    Matches librime Abbreviation (calculus.cc:184-204).
    Like Derivation but sets type=kAbbreviation, penalty=-0.693.
    """

    def rule_type(self):
        return K_ABBREV

    def credibility_delta(self):
        return K_ABBREV_PENALTY


class TransliterationRule(SpellingRule):
    """xlit/abc/ABC/ — character-by-character mapping.
    Matches librime Transliteration (calculus.cc:48-94).
    """

    def __init__(self, char_map):
        self.char_map = char_map  # dict{from_char: to_char}

    def apply(self, text):
        result = []
        modified = False
        for ch in text:
            if ch in self.char_map:
                result.append(self.char_map[ch])
                modified = True
            else:
                result.append(ch)
        if modified:
            return "".join(result), True
        return text, False


class SpellingAlgebra:
    """Applies a list of rules to a Script — matches librime Projection::Apply(Script*).

    Source: librime algo/algebra.cc:117-150
    """

    def __init__(self, rules):
        self.rules = rules

    def apply(self, script):
        """Apply all rules sequentially, expanding the Script.

        For each rule, for each key in the Script:
        - Try regex replacement on the key
        - If matched:
            if !deletion: keep original key (merge its spellings)
            if addition: add transformed key (merge with rule type/penalty)
        - If not matched: keep original key unchanged
        """
        for rule in self.rules:
            temp = Script()
            for key, spellings in script.items():
                new_key, applied = rule.apply(key)
                if applied:
                    if not rule.deletion():
                        # Keep original key
                        temp.merge(key, K_NORMAL, 0.0, spellings)
                    if rule.addition() and new_key:
                        # Add transformed key with rule's type and penalty
                        temp.merge(new_key, rule.rule_type(),
                                   rule.credibility_delta(), spellings)
                else:
                    # Rule didn't match — keep unchanged
                    temp.merge(key, K_NORMAL, 0.0, spellings)
            script.clear()
            script.update(temp)
        return script


def _convert_boost_replacement(replacement):
    """Convert boost::regex $N backreferences to Python \\N format."""
    return re.sub(r'\$(\d+)', r'\\\1', replacement)


def _parse_rule(definition):
    """Parse a single rule definition string — matches librime Calculus::Parse.

    Format: token/regex/replacement/ or token/regex/
    Separator is the first non-lowercase-alpha character.
    """
    # Find separator (first char not in [a-z])
    sep = None
    for i, ch in enumerate(definition):
        if not ch.isalpha() or ch.isupper():
            sep = ch
            break
    if sep is None:
        return None

    parts = definition.split(sep)
    if not parts:
        return None

    token = parts[0]
    args = parts[1:]  # split produces empty string at end for trailing sep

    if token == "xform":
        if len(args) >= 2:
            return TransformationRule(args[0], _convert_boost_replacement(args[1]))
    elif token == "derive":
        if len(args) >= 2:
            return DerivationRule(args[0], _convert_boost_replacement(args[1]))
    elif token == "fuzz":
        if len(args) >= 2:
            return FuzzingRule(args[0], _convert_boost_replacement(args[1]))
    elif token == "abbrev":
        if len(args) >= 2:
            return AbbreviationRule(args[0], _convert_boost_replacement(args[1]))
    elif token == "erase":
        if len(args) >= 1:
            return ErasionRule(args[0])
    elif token == "xlit":
        if len(args) >= 2:
            left, right = args[0], args[1]
            if len(left) == len(right):
                char_map = dict(zip(left, right))
                return TransliterationRule(char_map)

    return None


def parse_rules_from_yaml(yaml_path):
    """Parse spelling algebra rules from a YAML schema file.

    Looks for speller.algebra list in the YAML.
    Uses simple text parsing (no PyYAML dependency).
    """
    rules = []
    in_algebra = False
    indent_level = 0

    with open(yaml_path, "r", encoding="utf-8") as f:
        for line in f:
            stripped = line.rstrip()
            if not stripped or stripped.lstrip().startswith("#"):
                continue

            current_indent = len(line) - len(line.lstrip())
            content = stripped.lstrip()

            # Detect "algebra:" under "speller:"
            if content == "algebra:" or content.startswith("algebra:"):
                in_algebra = True
                indent_level = current_indent
                continue

            if in_algebra:
                if current_indent <= indent_level and not content.startswith("-"):
                    in_algebra = False
                    continue

                # Parse list item: "- rule_definition"
                if content.startswith("- "):
                    rule_str = content[2:].strip()
                    # Remove optional quotes
                    if (rule_str.startswith('"') and rule_str.endswith('"')) or \
                       (rule_str.startswith("'") and rule_str.endswith("'")):
                        rule_str = rule_str[1:-1]
                    rule = _parse_rule(rule_str)
                    if rule:
                        rules.append(rule)
                    else:
                        print(f"WARNING: Failed to parse rule: {rule_str}",
                              file=sys.stderr)

    return rules


def parse_rules_simple(rules_list):
    """Parse rules from a list of definition strings (for testing)."""
    rules = []
    for definition in rules_list:
        rule = _parse_rule(definition)
        if rule:
            rules.append(rule)
    return rules


# Self-test
if __name__ == "__main__":
    import os

    # CLI mode: spelling_algebra.py <dict.db> [schema.yaml]
    if len(sys.argv) >= 2:
        db_path = sys.argv[1]
        schema_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
            os.path.dirname(__file__), "..", "schemas", "pinyin.schema.yaml")

        if not os.path.exists(schema_path):
            print(f"Schema not found: {schema_path}", file=sys.stderr)
            sys.exit(1)

        # Load distinct syllables from dict table
        import sqlite3
        conn = sqlite3.connect(db_path)
        cur = conn.cursor()
        cur.execute("SELECT DISTINCT syllable_ids FROM dict")
        syllabary = set()
        for (sid,) in cur.fetchall():
            if sid:
                for s in sid.split(":"):
                    if s:
                        syllabary.add(s)
        conn.close()
        print(f"Loaded {len(syllabary)} unique syllables")

        # Apply spelling algebra
        script = Script()
        for s in sorted(syllabary):
            script.add_syllable(s)
        rules = parse_rules_from_yaml(schema_path)
        print(f"Loaded {len(rules)} rules from {schema_path}")
        SpellingAlgebra(rules).apply(script)

        # Write spellings table to SQLite
        conn = sqlite3.connect(db_path)
        cur = conn.cursor()
        cur.execute("DROP TABLE IF EXISTS spellings")
        cur.execute("""
            CREATE TABLE spellings (
                input TEXT NOT NULL,
                syllable TEXT NOT NULL,
                type INTEGER NOT NULL,
                credibility REAL NOT NULL
            )
        """)
        rows = []
        for input_str, spellings_list in sorted(script.items()):
            for sp in spellings_list:
                rows.append((input_str, sp.syllable, sp.type, sp.credibility))
        cur.executemany(
            "INSERT INTO spellings (input, syllable, type, credibility) VALUES (?, ?, ?, ?)",
            rows)
        conn.commit()
        print(f"Wrote {len(rows)} spellings entries to {db_path}")
        conn.close()
        sys.exit(0)

    print("=== Spelling Algebra Self-Test ===")

    # Test 1: Abbreviation
    script = Script()
    for s in ["ni", "hao", "da", "di"]:
        script.add_syllable(s)
    rules = parse_rules_simple(["abbrev/^(.+).$/$1/"])
    SpellingAlgebra(rules).apply(script)
    assert "d" in script, f"'d' not in script: {list(script.keys())}"
    d_spellings = {s.syllable: s for s in script["d"]}
    assert "da" in d_spellings and d_spellings["da"].type == K_ABBREV
    assert "di" in d_spellings and d_spellings["di"].type == K_ABBREV
    assert "n" in script
    print("  [OK] abbrev: 'd' → da,di (type=abbrev)")

    # Test 2: Derivation (fuzzy zh→z)
    script = Script()
    for s in ["ca", "cha"]:
        script.add_syllable(s)
    rules = parse_rules_simple(["derive/^([zcs])h/$1/"])
    SpellingAlgebra(rules).apply(script)
    assert "ca" in script
    ca_spellings = {s.syllable: s for s in script["ca"]}
    assert "ca" in ca_spellings and ca_spellings["ca"].type == K_NORMAL
    assert "cha" in ca_spellings and ca_spellings["cha"].type == K_NORMAL  # derive doesn't change type
    print("  [OK] derive: 'ca' → ca(normal), cha(normal)")

    # Test 3: Fuzzing (n↔l)
    script = Script()
    for s in ["na", "la"]:
        script.add_syllable(s)
    rules = parse_rules_simple(["fuzz/^n(.*)/l$1/", "fuzz/^l(.*)/n$1/"])
    SpellingAlgebra(rules).apply(script)
    assert "na" in script
    na_spellings = {s.syllable: s for s in script["na"]}
    assert "na" in na_spellings
    assert "la" in na_spellings and na_spellings["la"].type == K_FUZZY
    print("  [OK] fuzz: 'na' → na(normal), la(fuzzy)")

    # Test 4: Multi-rule (abbrev + derive)
    script = Script()
    for s in ["ni", "hao", "da", "di", "chu", "ca"]:
        script.add_syllable(s)
    rules = parse_rules_simple([
        "abbrev/^(.+).$/$1/",
        "derive/^([zcs])h/$1/",
    ])
    SpellingAlgebra(rules).apply(script)
    # abbrev: "da"→"d", "di"→"d" → "d" maps to [da, di]
    assert "d" in script
    d_spellings = {s.syllable: s for s in script["d"]}
    assert "da" in d_spellings and "di" in d_spellings
    # derive: "chu"→"cu" (ch→c), "ca" unchanged
    assert "cu" in script
    cu_spellings = {s.syllable: s for s in script["cu"]}
    assert "chu" in cu_spellings  # derive keeps original
    print(f"  [OK] multi-rule: 'd' → {[s.syllable for s in script['d']]}, 'cu' → {[s.syllable for s in script['cu']]}")

    # Test 5: Schema YAML parsing
    import os
    schema_path = os.path.join(os.path.dirname(__file__), "..", "schemas", "pinyin.schema.yaml")
    if os.path.exists(schema_path):
        rules = parse_rules_from_yaml(schema_path)
        print(f"  [OK] schema YAML: loaded {len(rules)} rules")
        for r in rules:
            print(f"       {r.__class__.__name__}")
    else:
        print(f"  [SKIP] schema YAML not found: {schema_path}")

    print("\n=== All tests passed ===")
