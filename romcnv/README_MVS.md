# ROMCNV - Neo-Geo MVS ROM Converter

A tool to convert Neo-Geo MVS arcade ROMs into processed assets for use with NJEMU. These assets are shared by full-resident and streaming-cache runtime modes.

## Why Convert ROMs?

The MVS runtime can choose between fully resident data and streaming according to
the memory that is actually available when a game starts. The converter prepares
the decrypted/decoded C-ROM, S-ROM, and V-ROM assets needed by those runtime
paths. The generated assets are not tied to a PSP model or memory tier.

## Converter Availability

The converter runs on Windows, Linux/macOS, or in the
[online converter](https://fjtrujy.github.io/NJEMU/). The generated processed
assets are portable across NJEMU platforms.

## Usage

### Basic Usage

Convert a single ROM:
```bash
romcnv_mvs /path/to/game.zip
```

### Command Line Options

| Option | Description |
|--------|-------------|
| `-all` | Convert all ROMs in the specified directory |
| `-zip` | Create a ZIP-compressed processed asset instead of a folder |
| `-lang <tag>` | Select converter messages at runtime (`en` or `zh-Hans`) |

### Examples

**Convert a single game:**
```bash
romcnv_mvs "D:\roms\kof99.zip"
```

**Convert all ROMs in a directory:**
```bash
romcnv_mvs "D:\roms" -all
```

**Convert with ZIP compression:**
```bash
romcnv_mvs "D:\roms\kof99.zip" -zip
```

**Use Simplified Chinese converter messages:**
```bash
romcnv_mvs "D:\roms\kof99.zip" -lang zh-Hans
```

### Linux/macOS Examples

```bash
# Convert a single ROM
./romcnv_mvs /home/user/roms/kof99.zip

# Convert with ZIP compression
./romcnv_mvs /home/user/roms/kof99.zip -zip

# Convert all ROMs in directory
./romcnv_mvs /home/user/roms -all

# Convert all with ZIP compression
./romcnv_mvs /home/user/roms -all -zip
```

## Output

The converter creates a `processed` directory containing one of:
- `gamename_cache/` — Folder with individual block files (default)
- `gamename_cache.zip` — ZIP-compressed processed asset (with `-zip`)

Both formats are supported by the emulator on all platforms.

### File Structure for Emulators

**PSP (folder format):**
```
/PSP/GAME/MVSPSP/
├── roms/
│   └── game.zip
└── processed/
    └── game_cache/
```

**PSP (zip format):**
```
/PSP/GAME/MVSPSP/
├── roms/
│   └── game.zip
└── processed/
    └── game_cache.zip
```

**PS2 (folder format):**
```
mass:/MVSPSP/
├── roms/
│   └── game.zip
└── processed/
    └── game_cache/
```

**PS2 (zip format):**
```
mass:/MVSPSP/
├── roms/
│   └── game.zip
└── processed/
    └── game_cache.zip
```

## Processed Asset Format Comparison

The emulator supports the same processed MVS assets in **folder** and **zip**
formats on every platform. The difference is storage versus runtime I/O cost,
not compatibility.

### Memory

| Aspect | Folder (default) | ZIP (`-zip`) |
|---|---|---|
| Persistent open FD | 1 (`crom` file kept open) | Zip archive kept open (central directory in RAM) |
| ZIP central directory overhead | — | ~32 bytes × num_entries |
| Sleep/resume cost | `close()`/`open()` 1 FD | `zip_close()`/`zip_open()` (re-parse central dir) |
| **Overall RAM overhead** | **Lowest** | **Medium** |

### Read Performance

| Aspect | Folder (default) | ZIP (`-zip`) |
|---|---|---|
| Cache miss (block load) | `lseek()` + `read()` — 1 syscall pair, direct offset | `zopen()` → scan zip dir + `zread()` decompress 64 KB + `zclose()` — **slowest** |
| Cache hit | LRU pointer update only | LRU pointer update only |
| I/O pattern | Random seek in single `crom` file ✅ | Sequential scan of zip entries + inflate ❌ |
| Decompression CPU | **None** | miniz inflate per 64 KB block — **significant on PSP/PS2** |
| Startup (`fill_cache`) | Sequential `lseek`+`read` — fast | Open+decompress+close each block — **slowest** |

### Disk / Storage

| Aspect | Folder (default) | ZIP (`-zip`) |
|---|---|---|
| Disk size | Larger — uncompressed blocks | **Smallest** — deflate typically 30–70% compression |
| File count | Multiple files (`cache_info`, `crom`, `srom`, `vrom`) | **1 file** |
| Filesystem friendliness | ✅ Few large files | ✅ Single file |

### Recommendation

Use the default folder format when runtime performance matters. Use `-zip` when
smaller storage is more important and the extra decompression work is acceptable.
The web converter is only another way to run ROMCNV; it does not produce a
separate "Web" asset format.

## Building from Source

### Windows
```bash
mkdir build && cd build
cmake .. -DTARGET=MVS
cmake --build .
```

### Linux/macOS
```bash
mkdir build && cd build
cmake .. -DTARGET=MVS
make
```

### WebAssembly
```bash
mkdir build && cd build
emcmake cmake .. -DTARGET=MVS
emmake make
```

## Notes

- Parent ROM sets must be in the same directory as the game ROM
- The converter requires `rominfo.mvs` file to be present in the same directory as the executable
- Converter language is selected at runtime. `NJEMU_LANG`, `LC_ALL`, `LC_MESSAGES`, and `LANG` are used when `-lang` is omitted; unsupported languages fall back to English.
- Processed assets are version-specific - regenerate if you update the emulator
- Cache-enabled MVS builds can still read the legacy `cache/` layout for migration; `USE_CACHE=OFF` intentionally reads only `processed/`.

## Troubleshooting

**"ROM not found" error:**
- Ensure the ROM filename matches entries in `rominfo.mvs`
- Check that parent ROMs are available for clone sets

**Large processed asset sizes:**
- Processed assets intentionally contain the data required by both resident and streaming runtime paths.
- ZIP output can reduce storage use without changing emulator compatibility.
