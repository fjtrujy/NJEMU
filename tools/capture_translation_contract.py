#!/usr/bin/env python3
"""Capture NJEMU's legacy translation contract without target SDKs.

The legacy text IDs and language tables are controlled by the same preprocessor
conditions.  This tool evaluates the small subset of the C preprocessor used by
those files, then pairs each active enum entry with the corresponding raw byte
string.  Source files are decoded as latin-1 so every original byte round-trips.
"""

from __future__ import annotations

import argparse
import hashlib
import itertools
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/common/ui_text_driver.h"
PLATFORM_SOURCES = {
    "desktop": ROOT / "src/desktop/desktop_ui_text.c",
    "ps2": ROOT / "src/ps2/ps2_ui_text.c",
    "psp": ROOT / "src/psp/psp_ui_text.c",
}
CORE_VALUES = {"CPS1": 0, "CPS2": 1, "MVS": 2, "NCDZ": 3}
CORE_USE_CACHE = {"CPS1": 0, "CPS2": 1, "MVS": 1, "NCDZ": 0}
LANGUAGES = {
    "en": "text_ENGLISH",
    "ja": "text_JAPANESE",
    "es": "text_SPANISH",
    "zh-Hans": "text_CHINESE_SIMPLIFIED",
    "zh-Hant": "text_CHINESE_TRADITIONAL",
}
FONT_BYTES = {
    "FONT_UPARROW": b"\x10",
    "FONT_DOWNARROW": b"\x11",
    "FONT_LEFTARROW": b"\x12",
    "FONT_RIGHTARROW": b"\x13",
    "FONT_CIRCLE": b"\x14",
    "FONT_CROSS": b"\x15",
    "FONT_SQUARE": b"\x16",
    "FONT_TRIANGLE": b"\x17",
    "FONT_LTRIGGER": b"\x18",
    "FONT_RTRIGGER": b"\x19",
    "FONT_UPTRIANGLE": b"\x1b",
    "FONT_DOWNTRIANGLE": b"\x1c",
    "FONT_LEFTTRIANGLE": b"\x1d",
    "FONT_RIGHTTRIANGLE": b"\x1e",
}
CORE_TEXT_MACROS = {
    "CPS1": {"SYSTEM_NAME": b"CPS1"},
    "CPS2": {"SYSTEM_NAME": b"CPS2", "CACHE_VERSION": b"V24"},
    "MVS": {"SYSTEM_NAME": b"NEO\xc2\xb7GEO", "CACHE_VERSION": b"V24"},
    "NCDZ": {"SYSTEM_NAME": b"NEO\xc2\xb7GEO CDZ"},
}

# Legacy symbols in these groups carry different bytes depending on EMU_SYSTEM.
# T0 resolves that ambiguity before assigning stable numeric IDs so T1 never has
# to break an already-published ID contract.
STABLE_VARIANTS: dict[str, tuple[tuple[str, tuple[str, ...]], ...]] = {
    "STRETCH_320X224_4_3": (("STRETCH1", ("MVS", "NCDZ")),),
    "STRETCH_360X270_4_3": (
        ("STRETCH1", ("CPS1", "CPS2")),
        ("STRETCH2", ("MVS", "NCDZ")),
    ),
    "STRETCH_366X270_19_14": (("STRETCH3", ("MVS", "NCDZ")),),
    "STRETCH_384X270_24_17": (("STRETCH2", ("CPS1", "CPS2")),),
    "STRETCH_420X270_14_9": (("STRETCH4", ("MVS", "NCDZ")),),
    "STRETCH_466X272_12_7": (("STRETCH3", ("CPS1", "CPS2")),),
    "STRETCH_480X270_16_9": (
        ("STRETCH4", ("CPS1", "CPS2")),
        ("STRETCH5", ("MVS", "NCDZ")),
    ),
    **{
        f"INPUT_BUTTON_{number}": ((f"INPUT_BUTTON{number}", ("CPS1", "CPS2")),)
        for number in range(1, 7)
    },
    **{
        f"INPUT_BUTTON_{letter}": ((f"INPUT_BUTTON{index}", ("MVS", "NCDZ")),)
        for index, letter in enumerate("ABCD", 1)
    },
    **{
        f"AUTOFIRE_{number}": ((f"AUTOFIRE{number}", ("CPS1", "CPS2")),)
        for number in range(1, 7)
    },
    **{
        f"AUTOFIRE_{letter}": ((f"AUTOFIRE{index}", ("MVS", "NCDZ")),)
        for index, letter in enumerate("ABCD", 1)
    },
    "MENUHELP_RESET_EMULATION_CPS1": (("MENUHELP_RESET_EMULATION", ("CPS1",)),),
    "MENUHELP_RESET_EMULATION_CPS2": (("MENUHELP_RESET_EMULATION", ("CPS2",)),),
    "MENUHELP_RESET_EMULATION_MVS": (("MENUHELP_RESET_EMULATION", ("MVS",)),),
    "MENUHELP_RESET_EMULATION_NCDZ": (("MENUHELP_RESET_EMULATION", ("NCDZ",)),),
    "ROMINFO_NOT_FOUND_CPS1": (("ROMINFO_NOT_FOUND", ("CPS1",)),),
    "ROMINFO_NOT_FOUND_CPS2": (("ROMINFO_NOT_FOUND", ("CPS2",)),),
    "ROMINFO_NOT_FOUND_MVS": (("ROMINFO_NOT_FOUND", ("MVS",)),),
}

STABLE_GROUPS = {
    "STRETCH1": tuple(name for name in STABLE_VARIANTS if name.startswith("STRETCH_")),
    "INPUT_BUTTON1": tuple(
        name for name in STABLE_VARIANTS if name.startswith("INPUT_BUTTON_")
    ),
    "AUTOFIRE1": tuple(name for name in STABLE_VARIANTS if name.startswith("AUTOFIRE_")),
    "MENUHELP_RESET_EMULATION": tuple(
        name for name in STABLE_VARIANTS if name.startswith("MENUHELP_RESET_EMULATION_")
    ),
    "ROMINFO_NOT_FOUND": tuple(
        name for name in STABLE_VARIANTS if name.startswith("ROMINFO_NOT_FOUND_")
    ),
}

REPLACED_LEGACY_KEYS = {
    "STRETCH1",
    "STRETCH2",
    "STRETCH3",
    "STRETCH4",
    "STRETCH5",
    "INPUT_BUTTON1",
    "INPUT_BUTTON2",
    "INPUT_BUTTON3",
    "INPUT_BUTTON4",
    "INPUT_BUTTON5",
    "INPUT_BUTTON6",
    "AUTOFIRE1",
    "AUTOFIRE2",
    "AUTOFIRE3",
    "AUTOFIRE4",
    "AUTOFIRE5",
    "AUTOFIRE6",
    "MENUHELP_RESET_EMULATION",
    "ROMINFO_NOT_FOUND",
}

# Snapshot measured from commit e56539d with the existing feature-on CMake
# builds: GUI=ON, SAVE_STATE=ON, COMMAND_LIST=ON, ADHOC=OFF.  CMake no longer
# wires LARGE_MEMORY to the preprocessor.  These values intentionally remain a
# frozen T0 baseline rather than being recomputed from later migrated builds.
FEATURE_BASELINE = {
    "CPS1": {
        "ids": 247,
        "desktop_object": 6215,
        "desktop_executable": 3705096,
        "ps2_object": 27071,
        "ps2_executable": 6504648,
    },
    "CPS2": {
        "ids": 259,
        "desktop_object": 6633,
        "desktop_executable": 3082680,
        "ps2_object": 29599,
        "ps2_executable": 6049592,
    },
    "MVS": {
        "ids": 283,
        "desktop_object": 7295,
        "desktop_executable": 3189584,
        "ps2_object": 31631,
        "ps2_executable": 6240544,
    },
    "NCDZ": {
        "ids": 254,
        "desktop_object": 6518,
        "desktop_executable": 3117560,
        "ps2_object": 28403,
        "ps2_executable": 6361168,
    },
}


class ContractError(RuntimeError):
    pass


@dataclass(frozen=True)
class Config:
    core: str
    adhoc: bool
    save_state: bool
    command_list: bool
    large_memory: bool

    @property
    def label(self) -> str:
        enabled = []
        if self.adhoc:
            enabled.append("ADHOC")
        if self.save_state:
            enabled.append("SAVE_STATE")
        if self.command_list:
            enabled.append("COMMAND_LIST")
        if self.large_memory:
            enabled.append("LARGE_MEMORY")
        return self.core + ("+" + "+".join(enabled) if enabled else "+base")

    def macros(self, platform: str) -> dict[str, int]:
        macros = dict(CORE_VALUES)
        macros.update(
            {
                "EMU_SYSTEM": CORE_VALUES[self.core],
                "USE_CACHE": CORE_USE_CACHE[self.core],
                "BUILD_" + self.core: 1,
                platform.upper(): 1,
            }
        )
        if self.adhoc:
            macros["ADHOC"] = 1
        if self.save_state:
            macros["SAVE_STATE"] = 1
        if self.command_list:
            macros["COMMAND_LIST"] = 1
        if self.large_memory:
            macros["LARGE_MEMORY"] = 1
        return macros


@dataclass
class Frame:
    parent_active: bool
    active: bool
    branch_taken: bool


def read_legacy(path: Path) -> str:
    return path.read_bytes().decode("latin-1")


def macro_int(value: object) -> int:
    if isinstance(value, int):
        return value
    text = str(value).strip()
    try:
        return int(text, 0)
    except ValueError:
        return 1


def eval_pp_expr(expr: str, macros: dict[str, object]) -> bool:
    def replace_defined(match: re.Match[str]) -> str:
        name = match.group(1) or match.group(2)
        return "1" if name in macros else "0"

    expr = re.sub(
        r"defined\s*\(\s*([A-Za-z_]\w*)\s*\)|defined\s+([A-Za-z_]\w*)",
        replace_defined,
        expr,
    )

    def replace_ident(match: re.Match[str]) -> str:
        name = match.group(0)
        return str(macro_int(macros.get(name, 0)))

    expr = re.sub(r"\b[A-Za-z_]\w*\b", replace_ident, expr)
    expr = expr.replace("&&", " and ").replace("||", " or ")
    expr = re.sub(r"!(?!=)", " not ", expr)
    if not re.fullmatch(r"[\s0-9a-fA-FxX()<>!=&|+\-*/%~.andornot]+", expr):
        raise ContractError(f"unsupported preprocessor expression: {expr!r}")
    try:
        return bool(eval(expr, {"__builtins__": {}}, {}))
    except (SyntaxError, TypeError, ValueError) as exc:
        raise ContractError(f"could not evaluate preprocessor expression {expr!r}") from exc


def preprocess(text: str, initial_macros: dict[str, int]) -> str:
    macros: dict[str, object] = dict(initial_macros)
    frames: list[Frame] = []
    output: list[str] = []

    def current_active() -> bool:
        return frames[-1].active if frames else True

    for line_no, line in enumerate(text.splitlines(keepends=True), 1):
        match = re.match(r"\s*#\s*(\w+)(.*)$", line)
        if not match:
            if current_active():
                output.append(line)
            continue

        directive = match.group(1)
        arg = match.group(2).strip()
        if directive in {"if", "ifdef", "ifndef"}:
            parent = current_active()
            if directive == "if":
                cond = eval_pp_expr(arg, macros) if parent else False
            elif directive == "ifdef":
                cond = arg.split()[0] in macros if parent else False
            else:
                cond = arg.split()[0] not in macros if parent else False
            frames.append(Frame(parent, parent and cond, parent and cond))
        elif directive == "elif":
            if not frames:
                raise ContractError(f"line {line_no}: #elif without #if")
            frame = frames[-1]
            cond = frame.parent_active and not frame.branch_taken and eval_pp_expr(arg, macros)
            frame.active = cond
            frame.branch_taken = frame.branch_taken or cond
        elif directive == "else":
            if not frames:
                raise ContractError(f"line {line_no}: #else without #if")
            frame = frames[-1]
            frame.active = frame.parent_active and not frame.branch_taken
            frame.branch_taken = frame.branch_taken or frame.active
        elif directive == "endif":
            if not frames:
                raise ContractError(f"line {line_no}: #endif without #if")
            frames.pop()
        elif directive == "define" and current_active():
            define = re.match(r"([A-Za-z_]\w*)(?:\s+(.*))?$", arg)
            if define and "(" not in define.group(1):
                macros[define.group(1)] = (define.group(2) or "1").strip()
        elif directive == "undef" and current_active():
            macros.pop(arg.split()[0], None)
        # Includes and other directives do not affect these legacy tables.

    if frames:
        raise ContractError("unterminated preprocessor conditional")
    return "".join(output)


def strip_comments(text: str) -> str:
    out: list[str] = []
    i = 0
    in_string = False
    while i < len(text):
        ch = text[i]
        if in_string:
            out.append(ch)
            if ch == "\\" and i + 1 < len(text):
                i += 1
                out.append(text[i])
            elif ch == '"':
                in_string = False
            i += 1
            continue
        if ch == '"':
            in_string = True
            out.append(ch)
            i += 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                raise ContractError("unterminated block comment")
            out.extend("\n" for c in text[i : end + 2] if c == "\n")
            i = end + 2
            continue
        if text.startswith("//", i):
            end = text.find("\n", i + 2)
            if end < 0:
                break
            out.append("\n")
            i = end + 1
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def enum_body(text: str) -> str:
    match = re.search(r"\benum\s*\{(.*?)\};", text, re.S)
    if not match:
        raise ContractError("could not find UI text enum")
    return match.group(1)


def parse_enum_names(preprocessed_header: str) -> list[str]:
    body = strip_comments(enum_body(preprocessed_header))
    names: list[str] = []
    for item in body.split(","):
        item = item.strip()
        if not item:
            continue
        match = re.match(r"([A-Za-z_]\w*)", item)
        if not match:
            raise ContractError(f"unsupported enum entry: {item!r}")
        names.append(match.group(1))
    if not names or names[-1] != "UI_TEXT_MAX":
        raise ContractError("UI text enum does not end in UI_TEXT_MAX")
    return names


def legacy_union_names(raw_header: str) -> list[str]:
    body = enum_body(raw_header)
    body = re.sub(r"^\s*#.*$", "", body, flags=re.M)
    body = strip_comments(body)
    names: list[str] = []
    seen: set[str] = set()
    for item in body.split(","):
        match = re.search(r"\b([A-Za-z_]\w*)\b", item)
        if not match:
            continue
        name = match.group(1)
        if name == "UI_TEXT_MAX" or name in seen:
            continue
        seen.add(name)
        names.append(name)
    if "END_OF_TEXT" not in seen:
        raise ContractError("legacy union is missing END_OF_TEXT")
    return names


def stable_manifest_names(legacy_names: list[str]) -> list[str]:
    names: list[str] = []
    for name in legacy_names:
        if name in STABLE_GROUPS:
            names.extend(STABLE_GROUPS[name])
        elif name not in REPLACED_LEGACY_KEYS:
            names.append(name)
    if len(names) != len(set(names)):
        raise ContractError("stable manifest contains duplicate names")
    return names


def split_initializers(body: str) -> list[str]:
    items: list[str] = []
    start = 0
    i = 0
    in_string = False
    while i < len(body):
        ch = body[i]
        if in_string:
            if ch == "\\" and i + 1 < len(body):
                i += 2
                continue
            if ch == '"':
                in_string = False
            i += 1
            continue
        if ch == '"':
            in_string = True
        elif ch == ",":
            items.append(body[start:i].strip())
            start = i + 1
        i += 1
    tail = body[start:].strip()
    if tail:
        items.append(tail)
    return items


def decode_c_string(content: str) -> bytes:
    raw = content.encode("latin-1")
    out = bytearray()
    i = 0
    simple = {
        ord("a"): 7,
        ord("b"): 8,
        ord("f"): 12,
        ord("n"): 10,
        ord("r"): 13,
        ord("t"): 9,
        ord("v"): 11,
        ord("\\"): 92,
        ord("\""): 34,
        ord("'"): 39,
        ord("?"): 63,
    }
    while i < len(raw):
        if raw[i] != 92:
            out.append(raw[i])
            i += 1
            continue
        i += 1
        if i >= len(raw):
            raise ContractError("trailing backslash in C string")
        esc = raw[i]
        if esc in simple:
            out.append(simple[esc])
            i += 1
        elif esc == ord("x"):
            i += 1
            start = i
            while i < len(raw) and chr(raw[i]) in "0123456789abcdefABCDEF":
                i += 1
            if i == start:
                raise ContractError("empty hex escape")
            out.append(int(raw[start:i].decode("ascii"), 16) & 0xFF)
        elif ord("0") <= esc <= ord("7"):
            start = i
            i += 1
            while i < len(raw) and i - start < 3 and ord("0") <= raw[i] <= ord("7"):
                i += 1
            out.append(int(raw[start:i].decode("ascii"), 8) & 0xFF)
        elif esc == 10:
            i += 1
        else:
            raise ContractError(f"unsupported C escape: \\{chr(esc)}")
    return bytes(out)


def parse_initializer(expr: str, text_macros: dict[str, bytes]) -> bytes | None:
    expr = expr.strip()
    if expr == "NULL":
        return None
    result = bytearray()
    pos = 0
    token = re.compile(r'"((?:\\.|[^"\\])*)"|([A-Za-z_]\w*)', re.S)
    for match in token.finditer(expr):
        if expr[pos : match.start()].strip():
            raise ContractError(f"unsupported initializer syntax: {expr!r}")
        if match.group(1) is not None:
            result.extend(decode_c_string(match.group(1)))
        else:
            name = match.group(2)
            if name in FONT_BYTES:
                result.extend(FONT_BYTES[name])
            elif name in text_macros:
                result.extend(text_macros[name])
            else:
                raise ContractError(f"unsupported initializer macro {name} in {expr!r}")
        pos = match.end()
    if expr[pos:].strip() or pos == 0:
        raise ContractError(f"unsupported initializer syntax: {expr!r}")
    return bytes(result)


def parse_arrays(
    preprocessed_source: str, text_macros: dict[str, bytes]
) -> dict[str, list[bytes | None]]:
    source = strip_comments(preprocessed_source)
    arrays: dict[str, list[bytes | None]] = {}
    for language, array_name in LANGUAGES.items():
        match = re.search(
            rf"static\s+const\s+char\s*\*\s*{re.escape(array_name)}\s*\[\s*UI_TEXT_MAX\s*\]\s*=\s*\{{(.*?)\}}\s*;",
            source,
            re.S,
        )
        if not match:
            raise ContractError(f"could not find {array_name}")
        arrays[language] = [
            parse_initializer(item, text_macros) for item in split_initializers(match.group(1))
        ]
    return arrays


def all_configs() -> list[Config]:
    return [
        Config(core, adhoc, save, command, large)
        for core in CORE_VALUES
        for adhoc, save, command, large in itertools.product((False, True), repeat=4)
    ]


def escape_preview(value: bytes | None) -> str:
    if value is None:
        return "NULL"
    escaped = []
    for byte in value:
        if 32 <= byte < 127 and byte not in (34, 92):
            escaped.append(chr(byte))
        elif byte == 34:
            escaped.append('\\"')
        elif byte == 92:
            escaped.append("\\\\")
        elif byte == 10:
            escaped.append("\\n")
        elif byte == 13:
            escaped.append("\\r")
        elif byte == 9:
            escaped.append("\\t")
        else:
            escaped.append(f"\\x{byte:02x}")
    rendered = "".join(escaped)
    if len(rendered) > 100:
        rendered = rendered[:97] + "..."
    return '"' + rendered + '"'


def capture() -> tuple[list[str], list[str], dict, list[str], list[str], dict[str, set[int]]]:
    raw_header = read_legacy(HEADER)
    legacy_names = legacy_union_names(raw_header)
    stable_names = stable_manifest_names(legacy_names)
    raw_platform = {name: read_legacy(path) for name, path in PLATFORM_SOURCES.items()}

    values: dict[str, dict[str, dict[bytes | None, set[str]]]] = defaultdict(
        lambda: defaultdict(lambda: defaultdict(set))
    )
    platform_errors: list[str] = []
    alignment_errors: list[str] = []
    counts: dict[str, set[int]] = defaultdict(set)

    for config in all_configs():
        platform_contracts: dict[str, dict[str, dict[str, bytes | None]]] = {}
        for platform, source in raw_platform.items():
            macros = config.macros(platform)
            header = preprocess(raw_header, macros)
            enum_names = parse_enum_names(header)
            active_names = enum_names[:-1]
            arrays = parse_arrays(preprocess(source, macros), CORE_TEXT_MACROS[config.core])
            counts[config.core].add(len(active_names))

            contract: dict[str, dict[str, bytes | None]] = {}
            for language, entries in arrays.items():
                if len(entries) != len(active_names):
                    alignment_errors.append(
                        f"{platform} {config.label} {language}: {len(entries)} strings for "
                        f"{len(active_names)} IDs"
                    )
                    continue
                contract[language] = dict(zip(active_names, entries))
                for key, value in contract[language].items():
                    values[key][language][value].add(config.label)
            platform_contracts[platform] = contract

        reference = platform_contracts["desktop"]
        for platform in ("ps2", "psp"):
            if platform_contracts[platform] != reference:
                platform_errors.append(f"{config.label}: desktop != {platform}")

    return legacy_names, stable_names, values, platform_errors, alignment_errors, counts


def values_for_scope(
    values: dict,
    legacy_key: str,
    language: str,
    cores: tuple[str, ...],
) -> set[bytes | None]:
    selected: set[bytes | None] = set()
    for value, scopes in values[legacy_key][language].items():
        if any(scope.split("+", 1)[0] in cores for scope in scopes):
            selected.add(value)
    return selected


def validate_stable_contract(stable_names: list[str], values: dict) -> None:
    reverse_variants = set(STABLE_VARIANTS)
    for stable_name in stable_names:
        for language in LANGUAGES:
            if stable_name in reverse_variants:
                candidates: set[bytes | None] = set()
                for legacy_key, cores in STABLE_VARIANTS[stable_name]:
                    candidates.update(values_for_scope(values, legacy_key, language, cores))
            else:
                candidates = set(values[stable_name][language])
            if len(candidates) != 1:
                rendered = ", ".join(sorted(escape_preview(value) for value in candidates))
                raise ContractError(
                    f"stable key {stable_name} ({language}) resolves to {len(candidates)} values: {rendered}"
                )


def manifest_text(stable_names: list[str]) -> str:
    lines = [
        "/* Generated by tools/capture_translation_contract.py. */",
        "/* Numeric IDs are explicit and must never be renumbered implicitly. */",
        "",
    ]
    width = max(len(name) for name in stable_names)
    for index, name in enumerate(stable_names):
        lines.append(f"UI_TEXT_ID({name + ',':<{width + 1}} {index})")
    lines.append("")
    return "\n".join(lines)


def source_hashes() -> dict[str, str]:
    paths = {
        "ui_text_driver.h": HEADER,
        "emucfg.h": ROOT / "src/emucfg.h",
        **{path.name: path for path in PLATFORM_SOURCES.values()},
    }
    return {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in paths.items()}


def report_text(
    legacy_names: list[str],
    stable_names: list[str],
    values: dict,
    platform_errors: list[str],
    alignment_errors: list[str],
    counts: dict[str, set[int]],
) -> str:
    conflicts = {
        key: langs
        for key, langs in values.items()
        if any(len(variants) > 1 for variants in langs.values())
    }
    lines = [
        "# Translation T0 baseline",
        "",
        "Generated by `tools/capture_translation_contract.py` from the legacy embedded tables.",
        "The extractor evaluates all 16 combinations of `ADHOC`, `SAVE_STATE`,",
        "`COMMAND_LIST` and `LARGE_MEMORY` for each of CPS1/CPS2/MVS/NCDZ, and",
        "checks Desktop, PS2 and PSP tables byte-for-byte.",
        "",
        "## Contract summary",
        "",
        f"- Legacy symbolic-key union: **{len(legacy_names)}** names (including `END_OF_TEXT`).",
        f"- Stable manifest: **{len(stable_names)}** unambiguous IDs (including `END_OF_TEXT`).",
        f"- Evaluated configurations: **{len(all_configs())}** per platform, **{len(all_configs()) * len(PLATFORM_SOURCES)}** table instances total.",
        f"- Platform table mismatches: **{len(platform_errors)}**.",
        f"- Enum/table alignment mismatches: **{len(alignment_errors)}**.",
        f"- Legacy symbolic keys whose bytes vary by core: **{len(conflicts)}**.",
        "- Stable manifest keys whose bytes vary by core: **0**.",
        "",
        "Legacy active-ID counts (`UI_TEXT_MAX`) vary because the current enum is conditional:",
        "",
        "| Core | Min | Max |",
        "| --- | ---: | ---: |",
    ]
    for core in CORE_VALUES:
        lines.append(f"| {core} | {min(counts[core])} | {max(counts[core])} |")

    lines.extend(["", "## Source hashes", "", "| Source | SHA-256 |", "| --- | --- |"])
    for name, digest in source_hashes().items():
        lines.append(f"| `{name}` | `{digest}` |")

    lines.extend(["", "## Platform consistency", ""])
    if platform_errors:
        lines.append("The following configurations differ between duplicated platform tables:")
        lines.extend(f"- {error}" for error in platform_errors)
    else:
        lines.append(
            "Desktop, PS2 and PSP provide identical bytes for every language, key and evaluated configuration."
        )

    lines.extend(["", "## Enum/table alignment", ""])
    if alignment_errors:
        lines.append(
            "The following legacy arrays do not contain exactly one initializer per active enum ID. "
            "These are positional-table bugs and must be resolved before T1 can claim byte-equivalent symbolic lookup."
        )
        lines.append("")
        for error in sorted(set(alignment_errors)):
            lines.append(f"- {error}")
    else:
        lines.append("Every evaluated language table contains exactly one initializer per active enum ID.")

    lines.extend(["", "## Configuration-dependent symbolic keys", ""])
    if not conflicts:
        lines.append("No key changes bytes across the evaluated build matrix.")
    else:
        lines.append(
            "These legacy symbols do not identify one build-independent message. The stable manifest below "
            "already disambiguates them; T1 must update runtime call sites to use those semantic IDs."
        )
        lines.append("")
        for key in legacy_names:
            if key not in conflicts:
                continue
            lines.append(f"### `{key}`")
            lines.append("")
            for language in LANGUAGES:
                variants = values[key][language]
                if len(variants) <= 1:
                    continue
                lines.append(f"- `{language}`:")
                for value, scopes in sorted(
                    variants.items(), key=lambda item: escape_preview(item[0])
                ):
                    cores = sorted(
                        {scope.split("+", 1)[0] for scope in scopes},
                        key=list(CORE_VALUES).index,
                    )
                    lines.append(f"  - {', '.join(cores)}: `{escape_preview(value)}`")
            lines.append("")

    lines.extend(["", "## Stable conflict normalization", ""])
    lines.append(
        "The stable manifest does not publish the ambiguous legacy names below. They are replaced before IDs "
        "are assigned, so T1 can update call sites without ever renumbering a stable ID."
    )
    lines.append("")
    for trigger, replacements in STABLE_GROUPS.items():
        legacy_group = sorted(
            {
                legacy_key
                for stable_name in replacements
                for legacy_key, _cores in STABLE_VARIANTS[stable_name]
            }
        )
        lines.append(
            f"- `{', '.join(legacy_group)}` -> "
            + ", ".join(f"`{replacement}`" for replacement in replacements)
        )

    lines.extend(["", "## Feature-on size/RAM baseline", ""])
    lines.extend(
        [
            "Snapshot commit: `e56539d` (`Fix MVS translation table alignment`).",
            "Configuration: `GUI=ON`, `SAVE_STATE=ON`, `COMMAND_LIST=ON`, `ADHOC=OFF`; CMake",
            "does not define legacy `LARGE_MEMORY`. Object sizes are section bytes reported by the",
            "platform `size` tool; executable sizes are file bytes and are intended for like-for-like T8",
            "comparisons with the same build configuration.",
            "",
            "| Core | IDs | Desktop text object | Desktop executable | Desktop copied-pointer heap | PS2 text object | PS2 executable | PS2/PSP copied-pointer heap |",
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for core, baseline in FEATURE_BASELINE.items():
        ids = baseline["ids"]
        desktop_heap = 8 * (ids + 1)
        target_heap = 4 * (ids + 1)
        lines.append(
            f"| {core} | {ids} | {baseline['desktop_object']} B | "
            f"{baseline['desktop_executable']} B | {desktop_heap} B | "
            f"{baseline['ps2_object']} B | {baseline['ps2_executable']} B | {target_heap} B |"
        )
    lines.extend(
        [
            "",
            "The copied-pointer heap size is the exact `sizeof(platform_ui_text_t)` under the current",
            "LP64 Desktop ABI or 32-bit PS2/PSP ABI: one language field plus `UI_TEXT_MAX` pointers",
            "including ABI padding on Desktop. It excludes allocator bookkeeping.",
            "",
            "A native PSP toolchain is not installed on this workstation, so the PSP object/executable",
            "binary snapshot was not measured locally. The PSP copied-pointer heap baseline above is exact",
            "for its 32-bit ABI, and commit `e56539d` plus the recorded source hashes preserve the pre-T1",
            "binary input for a later CI/toolchain measurement without relying on migrated sources.",
        ]
    )

    lines.extend(
        [
            "",
            "## Stable manifest policy",
            "",
            "`translations/messages.def` follows declaration order in the legacy enum, but replaces legacy",
            "symbols whose bytes depended on `EMU_SYSTEM` with semantic, build-independent keys before assigning",
            "numbers. IDs are explicit so later feature or core changes cannot renumber existing entries.",
            "`END_OF_TEXT` remains reserved because it is part of the legacy index space, although its legacy",
            "table value is `NULL`.",
            "",
        ]
    )
    return "\n".join(lines)


def write_if_changed(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = content.encode("utf-8")
    if path.exists() and path.read_bytes() == encoded:
        return
    path.write_bytes(encoded)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true", help="write the T0 manifest and baseline report")
    parser.add_argument(
        "--manifest",
        type=Path,
        default=ROOT / "translations/messages.def",
        help="stable manifest output path",
    )
    parser.add_argument(
        "--report",
        type=Path,
        default=ROOT / "docs/TRANSLATION_T0_BASELINE.md",
        help="baseline report output path",
    )
    args = parser.parse_args()

    try:
        legacy_names, stable_names, values, platform_errors, alignment_errors, counts = capture()
        validate_stable_contract(stable_names, values)
        manifest = manifest_text(stable_names)
        report = report_text(
            legacy_names, stable_names, values, platform_errors, alignment_errors, counts
        )
    except ContractError as exc:
        print(f"translation contract capture failed: {exc}", file=sys.stderr)
        return 1

    structural_errors = platform_errors + alignment_errors
    if structural_errors:
        print("translation contract capture found structural mismatches:", file=sys.stderr)
        for error in structural_errors:
            print(f"  {error}", file=sys.stderr)

    if args.write:
        write_if_changed(args.manifest, manifest)
        write_if_changed(args.report, report)
    else:
        expected = [(args.manifest, manifest), (args.report, report)]
        stale = [str(path.relative_to(ROOT)) for path, content in expected if not path.exists() or path.read_bytes() != content.encode("utf-8")]
        if stale:
            print("generated translation contract files are stale: " + ", ".join(stale), file=sys.stderr)
            print("run tools/capture_translation_contract.py --write", file=sys.stderr)
            return 1

    if structural_errors:
        return 1

    conflicts = sum(
        1 for langs in values.values() if any(len(variants) > 1 for variants in langs.values())
    )
    print(
        f"captured {len(stable_names)} stable keys across {len(all_configs()) * len(PLATFORM_SOURCES)} "
        f"table instances; {conflicts} configuration-dependent key(s); platform tables match"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
