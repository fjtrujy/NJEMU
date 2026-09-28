#!/usr/bin/env python3
"""ROM-free profile/tool regressions, with optional C-parser differential checks."""
from __future__ import annotations

import argparse
from dataclasses import replace
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('widescreen', ROOT / 'tools/widescreen.py')
wide = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = wide
spec.loader.exec_module(wide)
RUNTIME_VALIDATOR: str | None = None


def fixture() -> tuple[str, bytes]:
    program = bytearray(0x400)
    program[0x100:0x107] = b'NEO-GEO'
    struct.pack_into('>H', program, 0x108, 0x214)
    for offset in (0x200, 0x220):
        struct.pack_into('>HHH', program, offset, 0x303c, 0x140, 0x4e75)
    text = (f'[profile]\nversion = 1\ngame = fixture\nngh = 0x0214\n'
            f'program_size = 0x400\nprogram_crc32 = 0x{wide.fingerprint(program):08x}\n'
            'status = verified\n\n[patch columns]\noffsets = 0x200\nrepeat = 2\nstride = 0x20\n'
            'original = 303c 0140 4e75\nwide = 303c 0168 4e75\n')
    return text, bytes(program)


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.path = self.directory / 'fixture.ini'
        self.text, self.program = fixture()
        self.path.write_text(self.text, encoding='ascii')

    def tearDown(self):
        self.temp.cleanup()

    def load(self, text=None):
        if text is not None:
            self.path.write_text(text, encoding='ascii')
        return wide.load_profile(self.path)

    def test_round_trip_and_no_disk_changes(self):
        profile = self.load()
        original_file = self.path.read_bytes()
        self.assertEqual(wide.check_program(profile, self.program), 'native')
        patched = wide.apply_profile(profile, self.program, enable=True)
        self.assertEqual(wide.check_program(profile, patched), 'wide')
        self.assertEqual(wide.apply_profile(profile, patched, enable=True), patched)
        self.assertEqual(wide.apply_profile(profile, patched, enable=False), self.program)
        self.assertEqual(sum(a != b for a, b in zip(self.program, patched)), 2)
        self.assertEqual(self.path.read_bytes(), original_file)
        self.assertEqual(len(profile.words), 6)

    def test_full_fingerprint_and_guards(self):
        profile = self.load()
        for offset in (0x200, 0x220, 0x350):
            bad = bytearray(self.program)
            bad[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                wide.apply_profile(profile, bytes(bad), enable=True)
        with self.assertRaises(ValueError):
            wide.apply_profile(profile, self.program[:-2], enable=True)
        with self.assertRaises(ValueError):
            wide.check_program(replace(profile, program_crc32=0), self.program)

    def test_mixed_mode_rejected_both_directions(self):
        profile = self.load()
        mixed = bytearray(self.program)
        struct.pack_into('>H', mixed, 0x202, 0x168)
        for enable in (False, True):
            with self.assertRaisesRegex(ValueError, 'mixed'):
                wide.apply_profile(profile, bytes(mixed), enable=enable)

    def test_bios_vectors_ignored_not_program(self):
        profile = self.load()
        changed = bytearray(self.program)
        changed[:0x80] = b'\xff' * 0x80
        self.assertEqual(wide.check_program(profile, bytes(changed)), 'native')
        changed[0x80] ^= 1
        with self.assertRaises(ValueError):
            wide.check_program(profile, bytes(changed))

    def test_strict_profiles_and_c_parser_parity(self):
        valid = [self.text, self.text.replace('\n', '\r\n'), self.text.replace('\n', '\r'), self.text.rstrip('\n'),
                 self.text.replace('0x200', '512').replace('0x20\n', '32\n'),
                 self.text.replace('0x200', '0x200 # a comment'),
                 self.text.replace('status = verified', 'status = draft'),
                 self.text.split('[patch')[0] + 'viewport_only = 1\n']
        invalid = [
            self.text.replace('version = 1', 'version = 2'),
            self.text.replace('ngh = 0x0214', ''),
            self.text.replace('ngh = 0x0214', 'ngh = 0x10000'),
            self.text.replace('ngh = 0x0214', 'ngh = -1'),
            self.text.replace('ngh = 0x0214', 'ngh = +1'),
            self.text.replace('ngh = 0x0214', 'ngh = 0x0214\nngh = 1'),
            self.text.replace('game = fixture', 'game = ../fixture'),
            self.text.replace('game = fixture', 'game = FIXTURE'),
            self.text.replace('program_size = 0x400', 'program_size = 0x401'),
            self.text.replace('program_size = 0x400', 'program_size = 0x4000002'),
            self.text.replace('program_crc32 = ', 'unknown_key = '),
            self.text.replace('status = verified', 'status = approved'),
            self.text.replace('offsets = 0x200', 'offsets = 0x201'),
            self.text.replace('offsets = 0x200', 'offsets = 0x80'),
            self.text.replace('offsets = 0x200', 'offsets = 0x400'),
            self.text.replace('offsets = 0x200', 'offsets = 0x200,0x200'),
            self.text.replace('offsets = 0x200', 'offsets = '),
            self.text.replace('repeat = 2', 'repeat = 0'),
            self.text.replace('repeat = 2', 'repeat = 129'),
            self.text.replace('stride = 0x20', 'stride = 0'),
            self.text.replace('stride = 0x20', 'stride = 1'),
            self.text.replace('stride = 0x20', 'stride = 2'),
            self.text.replace('original = 303c', 'original = 10000'),
            self.text.replace('wide = 303c 0168 4e75', 'wide = 303c'),
            self.text.replace('wide = 303c 0168 4e75', 'wide = 303c 0140 4e75'),
            self.text.replace('status = verified', 'status = verified\nviewport_only = 1'),
            self.text.replace('[patch columns]', '[unknown]'),
            self.text.replace('[profile]', '[profile] trailing'),
            self.text.replace('[patch columns]', '[patch columns] trailing'),
            '[DEFAULT]\n' + self.text,
            self.text + self.text[self.text.index('[patch'):],
            self.text + 'wide = 303c 0168 4e75\n',
            self.text + '[profile]\nversion=1\n',
            self.text + '#\x00hidden\n',
            self.text + '#\x01control\n',
            self.text + '#' + 'x' * 2048 + '\n',
            self.text + ('#padding\n' * 8192),
        ]
        for expected, cases in ((True, valid), (False, invalid)):
            for index, text in enumerate(cases):
                with self.subTest(valid=expected, case=index):
                    self.path.write_text(text, encoding='ascii')
                    try:
                        wide.load_profile(self.path)
                        accepted = True
                    except (ValueError, wide.configparser.Error):
                        accepted = False
                    self.assertEqual(accepted, expected)
                    if RUNTIME_VALIDATOR:
                        result = subprocess.run([RUNTIME_VALIDATOR, str(self.path)], capture_output=True, timeout=15)
                        self.assertEqual(result.returncode == 0, expected, result.stderr.decode())

    def test_draft_scan_and_exclusive_output(self):
        program_path = self.directory / 'program.be'
        program_path.write_bytes(self.program)
        output = self.directory / 'new.ini'
        self.assertEqual(wide.main(['draft', '--game', 'fixture', '--program', str(program_path), '--output', str(output)]), 0)
        self.assertEqual(wide.load_profile(output).status, 'draft')
        self.assertEqual(wide.main(['draft', '--game', 'fixture', '--program', str(program_path), '--output', str(output)]), 1)
        report_path = self.directory / 'scan.json'
        self.assertEqual(wide.main(['scan', '--program', str(program_path), '--start', '0x100', '--output', str(report_path)]), 0)
        report = wide.json.loads(report_path.read_text())
        self.assertGreaterEqual(report['candidates_found'], 2)
        self.assertTrue(report['warnings'])
        self.assertEqual(program_path.read_bytes(), self.program)

    def test_mslug3_profile_matches_independent_site_oracle(self):
        profile = wide.load_profile(ROOT / 'resources/mvs/widescreen/mslug3.ini')
        self.assertEqual(profile.game, 'mslug3')
        self.assertEqual(profile.program_crc32, 0x714dc992)
        self.assertEqual(sum(w.original != w.wide for w in profile.words), 131)
        self.assertEqual(len(profile.words), 487)

    def test_kof96_different_units_and_reversible_profile(self):
        profile = wide.load_profile(ROOT / 'resources/mvs/widescreen/kof96.ini')
        self.assertEqual(profile.status, 'experimental')
        self.assertEqual(profile.program_crc32, 0xcb72d23c)
        self.assertEqual(len(profile.words), 53)
        self.assertEqual(sum(w.original != w.wide for w in profile.words), 17)
        fixed = (0x6fe0, 0x7004, 0x7262, 0x728e)
        integer = (0x7042, 0x706a, 0x72d8, 0x7308)
        words = {w.offset: w for w in profile.words}
        for offset in fixed:
            self.assertEqual((words[offset + 2].original, words[offset + 2].wide), (0x0780, 0x1b80))
            self.assertEqual((words[offset + 6].original, words[offset + 6].wide), (0xa200, 0xcf00))
        for offset in integer:
            self.assertEqual((words[offset + 2].original, words[offset + 2].wide), (15, 55))
            self.assertEqual((words[offset + 6].original, words[offset + 6].wide), (324, 414))
        for raw in range(65536):
            self.assertEqual(((raw + 0x1b80) & 0xffff) <= 0xcf00,
                             raw <= 359 * 128 or raw >= (512 - 55) * 128)
            signed = raw if raw < 32768 else raw - 65536
            self.assertEqual(((raw + 55) & 0xffff) <= 414, -55 <= signed <= 359)
        synthetic = bytearray(profile.program_size)
        for w in profile.words:
            struct.pack_into('>H', synthetic, w.offset, w.original)
        test_profile = replace(profile, program_crc32=wide.fingerprint(synthetic))
        patched = wide.apply_profile(test_profile, bytes(synthetic), enable=True)
        self.assertEqual(wide.apply_profile(test_profile, patched, enable=False), bytes(synthetic))

    def test_scan_finds_new_unsigned_windows_without_known_constants(self):
        program = bytearray(self.program)
        # Deliberately use constants absent from the old width-constant heuristic.
        struct.pack_into('>5H', program, 0x280, 0x0642, 0x0780, 0x0c42, 0xa200, 0x6204)
        window = wide.unsigned_window_test(program, 0x280, len(program))
        self.assertEqual(window['bias'], '0x0780')
        self.assertEqual(window['unsigned_relation'], '>')
        struct.pack_into('>H', program, 0x284, 0x0c43)  # different register, not this pattern
        self.assertIsNone(wide.unsigned_window_test(program, 0x280, len(program)))
        self.assertIsNone(wide.unsigned_window_test(program, len(program) - 2, len(program)))
        struct.pack_into('>H', program, 0x284, 0x0c42)
        path = self.directory / 'program.be'
        path.write_bytes(program)
        result_path = self.directory / 'windows.json'
        self.assertEqual(wide.main(['scan', '--program', str(path), '--start', '0x100',
                                   '--output', str(result_path)]), 0)
        result = wide.json.loads(result_path.read_text())
        self.assertEqual(result['candidates'][0]['offset'], '0x000280')
        self.assertEqual(result['candidates'][0]['unsigned_window']['register'], 'd2')

    def test_frame_comparison_and_ppm_validation(self):
        native = self.directory / 'native.ppm'
        candidate = self.directory / 'wide.ppm'
        native.write_bytes(b'P6\n#test\n304 224\n255\n' + bytes([100, 120, 140]) * (304 * 224))
        pixels = bytearray([100, 120, 140] * (400 * 225))
        candidate.write_bytes(b'P6\n400 225\n255\n' + pixels)
        result = wide.compare_frames(native, candidate)
        self.assertTrue(result['central_match'])
        self.assertEqual(result['side_strips']['left']['nonblack_pixels'], 48 * 224)
        pixels[(400 + 48) * 3] = 0
        candidate.write_bytes(b'P6\n400 225\n255\n' + pixels)
        self.assertEqual(wide.compare_frames(native, candidate)['central_differences_5bit'], 1)
        for bad in (b'', b'P6\n#unfinished', b'P3 1 1 255\n', b'P6 2 2 255\n' + bytes(3)):
            candidate.write_bytes(bad)
            with self.assertRaises(ValueError):
                wide.read_ppm(candidate)

    def test_scene_snapshot_inspection_is_bounded_and_read_only(self):
        scene = dict(version=1, ngh=0x214, frame=6500, ram_base=0x100000,
                     registers={f'{kind}{i}': 0 for kind in 'DA' for i in range(8)},
                     ram_words=[0] * 0x8000, vram_words=[0] * 0x10000)
        scene['registers']['PC'] = 0x1234
        scene['ram_words'][2] = 0xbeef
        scene['vram_words'][0x8401] = 15 << 7
        scene['vram_words'][0x8201] = 0x40 | 32
        path = self.directory / 'scene.json'
        path.write_text(wide.json.dumps(scene))
        original = path.read_bytes()
        output = self.directory / 'scene-report.json'
        self.assertEqual(wide.main(['scene', str(path), '--ram-start', '0x100004', '--words', '1',
                                   '--sprite-start', '1', '--sprites', '1', '--output', str(output)]), 0)
        report = wide.json.loads(output.read_text())
        self.assertEqual(report['ram'], {'0x100004': 'beef'})
        self.assertEqual(report['scb'][0]['x_raw'], 15)
        self.assertTrue(report['scb'][0]['sticky'])
        self.assertEqual(path.read_bytes(), original)
        self.assertEqual(wide.main(['scene', str(path), '--ram-start', '0xfffffe']), 1)
        self.assertEqual(wide.main(['scene', str(path), '--sprite-start', '511', '--sprites', '2']), 1)
        scene['ram_words'].pop()
        path.write_text(wide.json.dumps(scene))
        with self.assertRaises(ValueError):
            wide.read_scene(path)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--runtime-validator')
    args, remaining = parser.parse_known_args()
    RUNTIME_VALIDATOR = args.runtime_validator
    unittest.main(argv=[sys.argv[0]] + remaining)
