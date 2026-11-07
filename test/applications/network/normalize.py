# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Apply reviewed style changes without modifying the pinned source corpus."""
from pathlib import Path
import re


def apply(name: str, text: str) -> str:
    """Require exact hunk context; never silently rebase a normalization."""
    patch = Path(__file__).with_name("normalization.patch").read_text().splitlines(True)
    marker = f"--- a/{name}.cpp\n"
    if marker not in patch:
        raise ValueError(f"missing normalization: {name}")
    cursor = patch.index(marker) + 2
    original = text.splitlines(True)
    output: list[str] = []
    consumed = 0
    while cursor < len(patch) and not patch[cursor].startswith("--- a/"):
        match = re.fullmatch(r"@@ -(\d+)(?:,\d+)? \+\d+(?:,\d+)? @@\n", patch[cursor])
        if match is None:
            raise ValueError(f"invalid normalization hunk: {patch[cursor]}")
        start = int(match[1]) - 1
        if start < consumed:
            raise ValueError(f"overlapping normalization hunks: {name}")
        output.extend(original[consumed:start])
        consumed = start
        cursor += 1
        while cursor < len(patch) and not patch[cursor].startswith(("@@", "--- a/")):
            line = patch[cursor]
            if line[:1] not in (" ", "+", "-"):
                raise ValueError(f"invalid normalization line: {line}")
            if line[0] in " -":
                if consumed >= len(original) or original[consumed] != line[1:]:
                    raise ValueError(f"{name}: normalization context changed at {consumed + 1}")
                consumed += 1
            if line[0] in " +":
                output.append(line[1:])
            cursor += 1
    return "".join(output + original[consumed:])
