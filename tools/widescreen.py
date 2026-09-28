#!/usr/bin/env python3
"""Author and check NJEMU MVS widescreen profiles without modifying ROMs.

Input is a canonical big-endian, decrypted program dump (not a ZIP).
The emulator can export it with NJEMU_MVS_DUMP_PROGRAM in a debug Desktop build.
Python's standard library is sufficient; 'disasm' optionally uses Capstone.
"""
from __future__ import annotations

import argparse
import configparser
from dataclasses import dataclass
import json
from pathlib import Path
import re
import struct
import sys
import zlib

CRC_START = 0x80
MAX_PROGRAM = 0x4000000
MAX_WORDS = 4096
MAX_LIST = 128
MAX_SECTIONS = 128
MAX_FILE = 65536


@dataclass(frozen=True)
class Word:
    offset: int
    original: int
    wide: int


@dataclass(frozen=True)
class Profile:
    game: str
    ngh: int
    program_size: int
    program_crc32: int
    status: str
    viewport_only: bool
    description: str
    words: tuple[Word, ...]


def number(text: str, maximum: int = 0xffffffff, *, word: bool = False) -> int:
    pattern = r'(?:0[xX])?[0-9a-fA-F]+' if word else r'(?:0[xX][0-9a-fA-F]+|[0-9]+)'
    if not re.fullmatch(pattern, text):
        raise ValueError(f'invalid {"hex word" if word else "integer"}: {text!r}')
    value = int(text, 16 if word or text.lower().startswith('0x') else 10)
    if value > maximum:
        raise ValueError(f'number exceeds {maximum:#x}: {text}')
    return value


def numbers(text: str, maximum: int, *, word: bool = False) -> list[int]:
    values = [number(v, maximum, word=word) for v in re.split(r'[\s,]+', text.strip(' ,\t')) if v]
    if not 1 <= len(values) <= MAX_LIST:
        raise ValueError(f'expected 1..{MAX_LIST} numbers')
    return values


def read_config(path: Path) -> configparser.ConfigParser:
    with path.open('rb') as source:
        raw = source.read(MAX_FILE + 1)
    if len(raw) > MAX_FILE or any(c > 127 or (c < 32 and c not in (9, 10, 13)) for c in raw):
        raise ValueError('profile must be ASCII text, at most 64 KiB, with no control characters')
    if any(len(line) >= 2048 for line in raw.splitlines(keepends=True)):
        raise ValueError('profile line exceeds 2047 bytes')
    parser = configparser.ConfigParser(interpolation=None, delimiters=('=',),
                                       comment_prefixes=('#', ';'), inline_comment_prefixes=('#', ';'),
                                       strict=True, empty_lines_in_values=False)
    parser.optionxform = str
    # C accepts inline comments without a preceding space as well.
    text = '\n'.join(re.split(r'[#;]', line, maxsplit=1)[0].strip() for line in raw.decode('ascii').splitlines())
    if any(line.startswith('[') and not line.endswith(']') for line in text.splitlines()):
        raise ValueError('section header must end at the closing bracket')
    if re.search(r'(?m)^\s*\[DEFAULT\]\s*$', text):
        raise ValueError('DEFAULT is not supported')
    parser.read_string(text)
    if parser.defaults() or not parser.sections() or parser.sections()[0] != 'profile':
        raise ValueError('the first section must be [profile]; DEFAULT is not supported')
    if any('\n' in value for section in parser.values() for value in section.values()):
        raise ValueError('multiline values are not supported; keep each field on one line')
    return parser


def load_profile(path: Path) -> Profile:
    cfg = read_config(path)
    meta = cfg['profile']
    required = {'version', 'game', 'ngh', 'program_size', 'program_crc32', 'status'}
    if not required <= set(meta) or set(meta) - required - {'description', 'viewport_only'}:
        raise ValueError('missing or unknown profile metadata')
    if meta['version'] != '1' or not re.fullmatch(r'[a-z0-9_-]{1,15}', meta['game']):
        raise ValueError('unsupported version or invalid game identifier')
    size = number(meta['program_size'], MAX_PROGRAM)
    if size < 0x100 or size % 2:
        raise ValueError('program_size must be even and between 0x100 and 0x4000000')
    if meta['status'] not in ('draft', 'experimental', 'verified'):
        raise ValueError('status must be draft, experimental or verified')
    description = meta.get('description', '')
    if len(description) >= 160:
        raise ValueError('description must be shorter than 160 characters')
    viewport = bool(number(meta.get('viewport_only', '0'), 1))
    words: list[Word] = []
    sections = cfg.sections()[1:]
    if len(sections) > MAX_SECTIONS:
        raise ValueError('too many patch sections')
    for name in sections:
        section = cfg[name]
        if not name.startswith('patch ') or len(name) <= 6 or len(name) >= 64:
            raise ValueError(f'unknown or invalid section [{name}]')
        if not {'offsets', 'original', 'wide'} <= set(section) or set(section) - {'offsets', 'original', 'wide', 'repeat', 'stride'}:
            raise ValueError(f'missing or unknown fields in [{name}]')
        offsets = numbers(section['offsets'], MAX_PROGRAM)
        original = numbers(section['original'], 0xffff, word=True)
        wide = numbers(section['wide'], 0xffff, word=True)
        repeat = number(section.get('repeat', '1'), MAX_LIST)
        stride = number(section.get('stride', '0'), MAX_PROGRAM)
        if len(original) != len(wide) or repeat < 1 or stride % 2 or (repeat > 1 and not stride):
            raise ValueError(f'word counts or repeat/stride are invalid in [{name}]')
        if len(words) + len(offsets) * repeat * len(original) > MAX_WORDS:
            raise ValueError('expanded profile exceeds 4096 guard words')
        for offset in offsets:
            for iteration in range(repeat):
                start = offset + iteration * stride
                if start < 0x100 or start % 2 or start + 2 * len(original) > size:
                    raise ValueError(f'out-of-range/unaligned offset in [{name}]: {start:#x}')
                words.extend(Word(start + i * 2, old, new) for i, (old, new) in enumerate(zip(original, wide)))
    words.sort(key=lambda w: w.offset)
    if len({w.offset for w in words}) != len(words):
        raise ValueError('overlapping patch/guard words')
    changed = any(w.original != w.wide for w in words)
    if changed == viewport:
        raise ValueError('use viewport_only=1 only for profiles with no program changes')
    return Profile(meta['game'], number(meta['ngh'], 0xffff), size,
                   number(meta['program_crc32']), meta['status'], viewport, description, tuple(words))


def read_program(path: Path) -> bytes:
    with path.open('rb') as source:
        program = source.read(MAX_PROGRAM + 1)
    if not 0x100 <= len(program) <= MAX_PROGRAM or len(program) % 2:
        raise ValueError('expected an even-sized decrypted program dump, 0x100..0x4000000 bytes')
    if program.startswith(b'PK\x03\x04'):
        raise ValueError('ZIP input is not a decrypted program dump')
    return program


def fingerprint(program: bytes) -> int:
    return zlib.crc32(program[CRC_START:])


def check_program(profile: Profile, program: bytes) -> str:
    if len(program) != profile.program_size:
        raise ValueError('program size does not match the profile')
    mode: str | None = None
    canonical = bytearray(program)
    for w in profile.words:
        actual = struct.unpack_from('>H', program, w.offset)[0]
        if actual not in (w.original, w.wide):
            raise ValueError(f'instruction mismatch at {w.offset:#08x}: {actual:04x}')
        if w.original != w.wide:
            current = 'native' if actual == w.original else 'wide'
            if mode and mode != current:
                raise ValueError(f'mixed native/wide instructions at {w.offset:#08x}')
            mode = current
        struct.pack_into('>H', canonical, w.offset, w.original)
    actual_crc = fingerprint(canonical)
    if actual_crc != profile.program_crc32:
        raise ValueError(f'CRC32 mismatch: expected {profile.program_crc32:08x}, found {actual_crc:08x}')
    return mode or 'viewport-only'


def apply_profile(profile: Profile, program: bytes, *, enable: bool) -> bytes:
    check_program(profile, program)
    result = bytearray(program)
    for w in profile.words:
        struct.pack_into('>H', result, w.offset, w.wide if enable else w.original)
    return bytes(result)


def write_new(path: Path, text: str) -> None:
    # Generated reports/profiles never silently replace existing work.
    with path.open('x', encoding='ascii', newline='\n') as output:
        output.write(text)


def report(data: dict, output: Path | None) -> None:
    text = json.dumps(data, indent=2, ensure_ascii=True) + '\n'
    if output:
        write_new(output, text)
    else:
        print(text, end='')


def describe(profile: Profile) -> dict:
    return dict(game=profile.game, ngh=f'{profile.ngh:04x}', status=profile.status,
                viewport='400x225', viewport_only=profile.viewport_only,
                program_size=profile.program_size, crc32=f'{profile.program_crc32:08x}',
                guard_words=len(profile.words),
                changed_words=sum(w.original != w.wide for w in profile.words),
                description=profile.description)


def command_validate(args: argparse.Namespace) -> None:
    profile = load_profile(args.profile)
    result = describe(profile)
    if args.program:
        program = read_program(args.program)
        result['input_state'] = check_program(profile, program)
        native = apply_profile(profile, program, enable=False)
        wide = apply_profile(profile, native, enable=True)
        restored = apply_profile(profile, wide, enable=False)
        if restored != native or apply_profile(profile, wide, enable=True) != wide:
            raise ValueError('round-trip or idempotence failure')
        result.update(round_trip_byte_exact=True, enable_idempotent=True, rom_files_modified=False)
    report(result, args.output)


def command_draft(args: argparse.Namespace) -> None:
    program = read_program(args.program)
    if not re.fullmatch(r'[a-z0-9_-]{1,15}', args.game):
        raise ValueError('invalid game identifier')
    if program[0x100:0x107] != b'NEO-GEO' and args.ngh is None:
        raise ValueError('missing NEO-GEO header; verify byte order or specify --ngh')
    ngh = args.ngh if args.ngh is not None else int.from_bytes(program[0x108:0x10a], 'big')
    if not 0 <= ngh <= 0xffff:
        raise ValueError('invalid NGH')
    text = (f'# Draft only. The emulator refuses status=draft.\n'
            f'# Review drawing code and captures before changing the status.\n'
            f'[profile]\nversion = 1\ngame = {args.game}\nngh = 0x{ngh:04x}\n'
            f'program_size = 0x{len(program):x}\nprogram_crc32 = 0x{fingerprint(program):08x}\n'
            'status = draft\nviewport_only = 1\ndescription = Viewport-only research probe, artwork and clipping not verified\n\n'
            '# To add instructions, set viewport_only=0 and add sections like:\n'
            '# [patch descriptive-name]\n# offsets = 0xADDRESS, 0xOTHER_ADDRESS\n'
            '# original = 303c 0140\n# wide = 303c 0168\n'
            '# repeat = 1\n# stride = 0\n'
            '# These example words are NOT a proposed patch for this game.\n')
    write_new(args.output, text)
    print(f'Created draft {args.output}; no program changes applied.')


def unsigned_window_test(program: bytes, offset: int, end: int) -> dict | None:
    """Recognize a candidate shape, without assuming its constants or units."""
    if offset + 10 > end:
        return None
    add, bias, compare, limit, branch = struct.unpack_from('>5H', program, offset)
    if (add & 0xfff8) != 0x0640 or compare != (0x0c40 | (add & 7)):
        return None
    if branch & 0xff00 not in (0x6200, 0x6400):
        return None
    return dict(register=f'd{add & 7}', bias=f'0x{bias:04x}', limit=f'0x{limit:04x}',
                unsigned_relation='>' if branch & 0xff00 == 0x6200 else '>=',
                units='unknown: inspect coordinate producer', branch_offset=f'0x{offset + 8:06x}')


def command_scan(args: argparse.Namespace) -> None:
    program = read_program(args.program)
    start = args.start
    end = args.end if args.end is not None else min(len(program), 0x100000)
    if start < 0x100 or start % 2 or end <= start or end > len(program):
        raise ValueError('invalid scan range (byte offsets, not banked CPU addresses)')
    writers: dict[int, int] = {}
    if args.trace:
        for pc, count in re.findall(r'PC=([0-9a-fA-F]+) writes=(\d+)', args.trace.read_text()):
            address = int(pc, 16)
            writers[address] = writers.get(address, 0) + int(count)
    constants = {0x130, 0x138, 0x140, 0x148, 0x150, 0x160, 0x168, 0x180, 0x190, 0x1a0,
                 0x200, 0x400, 0x0e00, 0x1c00, 0x2800, 0x5000, 0x6800, 0xa000, 0xd000}
    candidates = []
    for offset in range(start, end - 3, 2):
        opcode, immediate = struct.unpack_from('>HH', program, offset)
        name = {0x0c40: 'cmpi.w', 0x0640: 'addi.w', 0x0440: 'subi.w'}.get(opcode & 0xfff8)
        if opcode & 0xf1ff == 0x303c:
            name = 'move.w immediate'
        window = unsigned_window_test(program, offset, end)
        if not window and (not name or immediate not in constants):
            continue
        lo, hi = max(start, offset - 192), min(end, offset + 256)
        port_reference = any(program.find(port, lo, hi) >= 0 for port in (b'\x00\x3c\x00\x00', b'\x00\x3c\x00\x02'))
        nearby = [pc for pc in writers if start <= pc < end and abs(pc - offset) <= 256]
        if args.writers_only and not nearby:
            continue
        candidates.append(dict(offset=f'0x{offset:06x}', instruction=name, immediate=f'0x{immediate:04x}',
                               words=program[offset:min(end, offset + 16)].hex(' ', 2),
                               unsigned_window=window,
                               nearby_vram_port=port_reference,
                               nearby_writer_pcs=[f'{pc:06x}' for pc in nearby],
                               writer_samples=sum(writers[pc] for pc in nearby)))
    candidates.sort(key=lambda c: (-bool(c['unsigned_window']), -bool(c['nearby_writer_pcs']),
                                    -c['nearby_vram_port'], -c['writer_samples'], int(c['offset'], 16)))
    report(dict(program_bytes=len(program), crc32=f'{fingerprint(program):08x}',
                scan_range=[hex(start), hex(end)], candidates_found=len(candidates),
                candidates=candidates[:args.limit],
                warnings=['Candidates are byte-pattern matches, not proven instructions or safe patches.',
                          'Constants can be sprite-pool limits, data or gameplay bounds. Review control/data flow.',
                          'Banked writer PCs need a bank mapping; do not equate them with program file offsets.',
                          'No ROMs or profiles were modified.']), args.output)


def command_disasm(args: argparse.Namespace) -> None:
    try:
        from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
    except ImportError as exc:
        raise ValueError("optional dependency missing: install Capstone to use 'disasm'") from exc
    program = read_program(args.program)
    if args.start < 0 or args.start % 2 or args.end > len(program) or args.end <= args.start:
        raise ValueError('invalid disassembly range')
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    decoded_end = args.start
    for instruction in decoder.disasm(program[args.start:args.end], args.start):
        print(f'{instruction.address:06x} {instruction.bytes.hex():20} {instruction.mnemonic:10} {instruction.op_str}')
        decoded_end = instruction.address + instruction.size
    if decoded_end != args.end:
        raise ValueError(f'disassembly stopped at {decoded_end:#x}; check instruction alignment, data or a truncated range')


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    with path.open('rb') as source:
        data = source.read(64 * 1024 * 1024 + 1)
    if len(data) > 64 * 1024 * 1024:
        raise ValueError('PPM exceeds 64 MiB')
    position = 0
    fields = []
    while len(fields) < 4:
        while position < len(data) and data[position:position + 1].isspace():
            position += 1
        if data[position:position + 1] == b'#':
            end = data.find(b'\n', position)
            if end < 0:
                raise ValueError('unterminated PPM comment')
            position = end + 1
            continue
        start = position
        while position < len(data) and not data[position:position + 1].isspace():
            position += 1
        if position == start:
            raise ValueError('incomplete PPM header')
        fields.append(data[start:position])
    if fields[0] != b'P6' or fields[3] != b'255' or not data[position:position + 1].isspace():
        raise ValueError('expected P6 PPM with 8-bit channels')
    width, height = int(fields[1]), int(fields[2])
    position += 2 if data[position:position + 2] == b'\r\n' else 1
    pixels = data[position:]
    if width <= 0 or height <= 0 or len(pixels) != width * height * 3:
        raise ValueError('PPM dimensions and data length disagree')
    return width, height, pixels


def compare_frames(native_path: Path, wide_path: Path) -> dict:
    nw, nh, native = read_ppm(native_path)
    ww, wh, wide = read_ppm(wide_path)
    if (nw, nh, ww, wh) != (304, 224, 400, 225):
        raise ValueError('profile v1 comparison needs native 304x224 and wide 400x225 source-frame PPMs')
    differences = 0
    for y in range(nh):
        for x in range(nw):
            a, b = (y * nw + x) * 3, ((y + 1) * ww + x + 48) * 3
            differences += any(native[a + c] >> 3 != wide[b + c] >> 3 for c in range(3))
    strips = {}
    for name, left, right in (('left', 0, 48), ('right', 352, 400)):
        colors = {wide[(y * ww + x) * 3:(y * ww + x) * 3 + 3]
                  for y in range(1, 225) for x in range(left, right)}
        nonblack = sum(any(wide[(y * ww + x) * 3:(y * ww + x) * 3 + 3])
                       for y in range(1, 225) for x in range(left, right))
        strips[name] = dict(pixels=48 * 224, nonblack_pixels=nonblack, unique_rgb_colors=len(colors))
    return dict(native_size=[nw, nh], wide_size=[ww, wh], crop=[48, 1, 352, 225],
                central_pixels=nw * nh, central_differences_5bit=differences,
                central_match=differences == 0, side_strips=strips,
                warnings=['Compare the same scene and input sequence; differing timing invalidates a pixel comparison.',
                          'Side-strip colors are measurements, not proof of correct scenery or gameplay.',
                          'Source-frame comparison does not validate physical window margins.'])


def command_compare(args: argparse.Namespace) -> None:
    result = compare_frames(args.native, args.wide)
    report(result, args.output)
    if args.require_center_match and not result['central_match']:
        raise ValueError('native center differs; inspect scene timing and rendering before approving the profile')


def read_scene(path: Path) -> dict:
    with path.open('rb') as source:
        raw = source.read(2 * 1024 * 1024 + 1)
    if len(raw) > 2 * 1024 * 1024:
        raise ValueError('scene snapshot exceeds 2 MiB')
    scene = json.loads(raw)
    if not isinstance(scene, dict) or type(scene.get('version')) is not int or scene.get('version') != 1 or scene.get('ram_base') != 0x100000:
        raise ValueError('unsupported scene snapshot')
    for key, length in (('ram_words', 0x8000), ('vram_words', 0x10000)):
        values = scene.get(key)
        if not isinstance(values, list) or len(values) != length or any(type(v) is not int or not 0 <= v <= 0xffff for v in values):
            raise ValueError(f'invalid {key} in scene snapshot')
    registers = scene.get('registers')
    expected = {f'{kind}{i}' for kind in 'DA' for i in range(8)} | {'PC'}
    if not isinstance(registers, dict) or set(registers) != expected or any(type(v) is not int or not 0 <= v <= 0xffffffff for v in registers.values()):
        raise ValueError('invalid scene registers')
    if type(scene.get('frame')) is not int or not 1 <= scene['frame'] <= 0xffffffff or type(scene.get('ngh')) is not int or not 0 <= scene['ngh'] <= 0xffff:
        raise ValueError('invalid scene identity/frame')
    return scene


def command_scene(args: argparse.Namespace) -> None:
    scene = read_scene(args.snapshot)
    result = dict(ngh=f"{scene['ngh']:04x}", frame=scene['frame'],
                  registers={k: f'0x{v:08x}' for k, v in scene['registers'].items()})
    if args.ram_start is not None:
        offset = args.ram_start - scene['ram_base']
        if offset < 0 or offset % 2 or offset + args.words * 2 > 0x10000:
            raise ValueError('RAM slice must be aligned and within 0x100000..0x10ffff')
        result['ram'] = {f'0x{args.ram_start + i * 2:06x}': f'{v:04x}' for i, v in
                         enumerate(scene['ram_words'][offset // 2:offset // 2 + args.words])}
    if args.sprite_start + args.sprites > 512:
        raise ValueError('SCB slice exceeds 512 entries')
    vram = scene['vram_words']
    result['scb'] = [dict(sprite=i, scb2=f'{vram[0x8000+i]:04x}', scb3=f'{vram[0x8200+i]:04x}',
                          scb4=f'{vram[0x8400+i]:04x}', x_raw=vram[0x8400+i] >> 7,
                          rows_raw=vram[0x8200+i] & 63, sticky=bool(vram[0x8200+i] & 64),
                          first_tile_words=[f'{w:04x}' for w in vram[i*64:i*64+4]])
                     for i in range(args.sprite_start, args.sprite_start + args.sprites)]
    result['warnings'] = ['SCB values are raw: sticky columns inherit position/height from their chain.',
                          'RAM fields are game-specific; a numeric limit is not automatically a camera bound.']
    report(result, args.output)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    validate = sub.add_parser('validate', help='validate syntax, identity, round-trip and idempotence in memory')
    validate.add_argument('profile', type=Path)
    validate.add_argument('--program', type=Path)
    validate.add_argument('--output', type=Path)
    validate.set_defaults(func=command_validate)
    draft = sub.add_parser('draft', help='create an inert, fingerprinted INI template from a program dump')
    draft.add_argument('--program', type=Path, required=True)
    draft.add_argument('--game', required=True)
    draft.add_argument('--ngh', type=lambda x: number(x, 0xffff))
    draft.add_argument('--output', type=Path, required=True)
    draft.set_defaults(func=command_draft)
    scan = sub.add_parser('scan', help='read-only search for possible rendering bounds, optionally ranked by SCB4 traces')
    scan.add_argument('--program', type=Path, required=True)
    scan.add_argument('--trace', type=Path)
    scan.add_argument('--start', type=lambda x: number(x), default=0x400)
    scan.add_argument('--end', type=lambda x: number(x))
    scan.add_argument('--limit', type=int, choices=range(1, 501), default=100, metavar='1..500')
    scan.add_argument('--writers-only', action='store_true')
    scan.add_argument('--output', type=Path)
    scan.set_defaults(func=command_scan)
    disasm = sub.add_parser('disasm', help='inspect a reviewed code range using optional Capstone')
    disasm.add_argument('--program', type=Path, required=True)
    disasm.add_argument('--start', type=lambda x: number(x), required=True)
    disasm.add_argument('--end', type=lambda x: number(x), required=True)
    disasm.set_defaults(func=command_disasm)
    compare = sub.add_parser('compare', help='measure native-center preservation and side-strip coverage from source PPMs')
    compare.add_argument('--native', type=Path, required=True)
    compare.add_argument('--wide', type=Path, required=True)
    compare.add_argument('--output', type=Path)
    compare.add_argument('--require-center-match', action='store_true')
    compare.set_defaults(func=command_compare)
    scene = sub.add_parser('scene', help='inspect a read-only RAM/VRAM/register snapshot at a chosen game frame')
    scene.add_argument('snapshot', type=Path)
    scene.add_argument('--ram-start', type=lambda x: number(x))
    scene.add_argument('--words', type=int, choices=range(1, 4097), default=32, metavar='1..4096')
    scene.add_argument('--sprite-start', type=lambda x: number(x, 511), default=0)
    scene.add_argument('--sprites', type=int, choices=range(0, 513), default=16, metavar='0..512')
    scene.add_argument('--output', type=Path)
    scene.set_defaults(func=command_scene)
    args = parser.parse_args(argv)
    try:
        args.func(args)
    except (OSError, ValueError, configparser.Error) as exc:
        print(f'widescreen: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
