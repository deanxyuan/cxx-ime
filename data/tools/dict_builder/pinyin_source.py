"""Read the supported Rime dictionary columns without guessing missing readings."""

import re

from .pinyin_syllables import validate_syllables


def parse_rime_entries(content, source="<input>"):
    lines = content.lstrip("\ufeff").splitlines()
    columns = ["text", "code", "weight"]
    metadata = {}
    start = 0
    collecting_columns = False
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped == "...":
            start = index + 1
            break
        if not stripped or stripped.startswith("#") or stripped == "---":
            continue
        if "\t" in line:
            start = index
            break
        if collecting_columns and stripped.startswith("- "):
            columns.append(stripped[2:].strip())
            continue
        collecting_columns = False
        key, separator, value = stripped.partition(":")
        if not separator:
            raise ValueError(f"{source}:{index + 1}: unsupported dictionary header")
        metadata[key] = value.strip()
        if key == "columns":
            columns = []
            if value.strip():
                if not re.fullmatch(r"\[\s*\w+(?:\s*,\s*\w+)*\s*\]", value.strip()):
                    raise ValueError(f"{source}:{index + 1}: unsupported columns declaration")
                columns = [part.strip() for part in value.strip()[1:-1].split(",")]
            else:
                collecting_columns = True
    else:
        start = len(lines)

    if len(columns) != len(set(columns)) or "text" not in columns:
        raise ValueError(f"{source}: invalid columns declaration: {columns!r}")
    if "code" not in columns:
        raise ValueError(f"{source}: missing explicit readings (columns={columns!r})")
    if set(columns) - {"text", "code", "weight", "stem"}:
        raise ValueError(f"{source}: unsupported columns: {columns!r}")

    entries = []
    for index in range(start, len(lines)):
        line = lines[index]
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        fields = dict(zip(columns, line.split("\t")))
        text = fields.get("text", "").strip()
        annotation = ":".join(fields.get("code", "").split())
        code = annotation.replace(":", "")
        try:
            if not text:
                raise ValueError("missing entry text")
            validate_syllables(code, annotation)
            weight = fields.get("weight", "").strip()
            frequency = int(weight) if weight else 0
            if frequency < 0:
                raise ValueError("negative frequency")
        except ValueError as error:
            raise ValueError(f"{source}:{index + 1}, {text!r}: {error}") from error
        entries.append((text, code, frequency, annotation))
    return metadata, entries
