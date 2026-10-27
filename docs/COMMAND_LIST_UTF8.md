# MAME Plus 0.168 command.dat support (issue #24)

## Scope

The MAME Plus 0.168 archive at https://e2j.net/files/command_0.168.zip
contains a 6,542,052-byte `command.dat` with a UTF-8 BOM, CRLF lines and
Japanese text. Its SHA-256 is
`1d30ec47f6d455e2a2cc0c3a5f2ae83ba382de914ab0f46f6990056d743b0352`.
The source file is a test input, **not** distributed with NJEMU.

The old command viewer guessed Latin-1 versus GBK for undeclared text and
shared a decoder for GBK/Shift-JIS. More importantly, it scanned past the
end of untrusted input, copied unbounded lines to a 512-byte stack buffer,
and inspected uninitialized memory to guess line endings.

`src/common/command_data.c` now owns byte-length-bounded scanning,
per-game command selection, parent fallback and in-memory reduction.
It rejects embedded NULs, overlarge files (16 MiB cap), unfinished
command sections and empty entries rather than trusting malformed offsets.
It detects BOM/valid UTF-8, accepts explicit `$charset=` declarations,
supports LF, CRLF, CR and no final terminator, and retains the original
bytes and line endings when reducing a file.

The renderer recognizes two-byte MAME direction/button tokens before
decoding Unicode. It reuses the existing `ui_utf8_decode` and
`ui_unicode_glyph_lookup` helpers and the external `gbk_s14` font.
`metadata/command_list_unicode.txt` contains the unique, GBK-font-supported
glyph repertoire from the public 0.168 command file, not its game commands.
The Unicode map is extended only when `COMMAND_LIST=ON`.
The KOF '98 and Metal Slug 3 sections have 656 and 491 distinct non-ASCII
characters, respectively. Their only missing characters after the map
extension were the UTF-8 BOM and Japanese middle dot: the BOM is ignored
and middle dot uses the existing font bullet.

Legacy Latin-1 and GBK decoding are retained. Shift-JIS is tracked as
its own charset rather than sharing the GBK constant; the existing GBK
glyph fallback is retained for it until a proper Shift-JIS glyph mapping
is implemented. Other Unicode characters not present in the shipped
glyph data fall back to a visible question mark.

The file reduction menu preserves an existing `command.org` backup rather
than overwriting it, writes complete buffers (handling short writes), and
attempts to restore the original file if output writing fails.

## RAM-efficient game loading

The game command viewer no longer allocates the complete `command.dat` file.
`command_data_load_fd()` scans it in 4 KiB read chunks using a fixed-size tag
prefix, remembers the matching `$info` section's file offsets (falling back to
the parent entry when necessary), then allocates and reads **only that
section**. Parsing works in place, without making a second copy of the
selected game. The scan still traverses the file to detect charset
declarations and rejects embedded NUL bytes or files over 16 MiB.

Measured with the unmodified 6,542,052-byte MAME Plus 0.168 fixture:

| Game | Former temporary full-file buffer | New retained text allocation | File-buffer reduction |
|------|-----------------------------------:|-----------------------------:|----------------------:|
| `kof98` | 6,542,052 B | 48,896 B | 99.25% |
| `mslug3` | 6,542,052 B | 13,282 B | 99.80% |

These figures compare **file-text allocations**, not the total process heap.
The viewer also allocates line-pointer arrays for the selected commands; the
streaming scan uses a small stack buffer. The standalone **size-reduction
menu**, which processes many games at once, still buffers its input and
output files and is not included in these RAM savings.

## Validation

- Host parser tests include multi-game aliases, parent lookup, UTF-8 BOM,
  LF/CRLF/CR, old charset tags, malformed/truncated sections, NUL bytes,
  an 11 KiB line, command tokens and byte-preserving reduction.
- The real 0.168 file parses successfully for `kof98` and `mslug3`;
  a reduced version retaining `kof98` parses as UTF-8.
- Streaming results match the existing in-memory parser line-for-line for
  both games, mixed CRLF/LF/CR files, unclosed sections, a long line, parent
  fallback, declarations after the target section, and CRLF split over an
  I/O-buffer boundary. Optional fixture tests print the allocation sizes.
- Desktop MVS (`COMMAND_LIST=ON`): 55/55 tests passed.
- PS2 and PSP: CPS1, CPS2, MVS and NCDZ all compile successfully with
  `GUI=ON` and `COMMAND_LIST=ON`.

Optional local fixture command (the archive and extracted command.dat
remain outside version control):

```sh
./build_command24/desktop/command_data_tests build_command24/command.dat
```

## Remaining manual check

The final on-screen command viewer has **not** been visually verified on
physical PS2 or inside PCSX2. That remains the acceptance check for
glyph legibility and menu positioning at each output resolution; build,
parser and glyph coverage tests do not replace a rendered screenshot.
