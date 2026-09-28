# MVS widescreen profiles

Place one `<game_name>.ini` here. NJEMU loads it when the game starts; no rebuild
is needed after editing a runtime profile. Source copies are staged into build
and install resources by CMake.

`mslug3.ini` is the migrated original MVS profile. `kof96.ini` is experimental:
request wide mode and set `NJEMU_MVS_ALLOW_EXPERIMENTAL=1` to try it. Neither
profile is assumed compatible with other ROM revisions or clones. `draft`
profiles are never activated.

From the source tree, `python3 tools/widescreen.py --help` lists the authoring
commands. `draft` creates an inert fingerprinted template, `scan` searches for
possible drawing bounds, `disasm` inspects code, `validate` checks guarded
substitutions and exact restoration, and `compare` checks paired frame dumps.
`scene` inspects an optional debug snapshot of RAM, VRAM and CPU registers.
The tool never writes a patched ROM.

See `docs/MVS_WIDESCREEN_PROFILES.md` in the source tree for the complete format,
examples, program-dump instructions and the limits of automatic candidate scans.
