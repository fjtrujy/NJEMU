#!/usr/bin/env python3
"""Compare frame dumps written by the Desktop video backends.

Usage: compare_frames.py REFERENCE.ppm CANDIDATE.ppm [DIFF.ppm]

Both backends dump the visible work frame at 1x (NJEMU_DUMP_FRAMES).  Pixels
are compared at 5 bits per channel, the precision of the emulated 555
palette, so 8-bit expansion differences between renderers do not count.

A differing pixel whose colour appears among its 8 neighbours in the other
image (either direction) is reported as a sampling tie: zoomed sprites (Neo-Geo shrink)
map some pixel centres exactly onto texel edges, and each rasterizer breaks
those ties its own way.  Only the remaining differences fail the comparison
(exit status 1); ties are drawn yellow in the diff image, real ones magenta.
"""

import sys


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    fields = []
    pos = 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    if fields[0] != b"P6" or int(fields[3]) != 255:
        raise ValueError(f"{path}: not an 8-bit binary PPM")
    width, height = int(fields[1]), int(fields[2])
    pixels = data[pos + 1:pos + 1 + width * height * 3]
    return width, height, pixels


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2

    rw, rh, ref = read_ppm(sys.argv[1])
    cw, ch, cand = read_ppm(sys.argv[2])
    if (rw, rh) != (cw, ch):
        print(f"size mismatch: {rw}x{rh} vs {cw}x{ch}")
        return 1

    def q(buf, x, y):
        i = (y * rw + x) * 3
        return tuple(c >> 3 for c in buf[i:i + 3])

    diff = bytearray(len(ref))
    real = ties = 0
    bbox = None
    for y in range(rh):
        for x in range(rw):
            i = (y * rw + x) * 3
            a, b = q(ref, x, y), q(cand, x, y)
            if a == b:
                diff[i:i + 3] = bytes(c // 4 for c in ref[i:i + 3])
                continue
            around = [(nx, ny) for ny in range(max(0, y - 1), min(rh, y + 2))
                      for nx in range(max(0, x - 1), min(rw, x + 2))]
            if (b in {q(ref, nx, ny) for nx, ny in around} or
                    a in {q(cand, nx, ny) for nx, ny in around}):
                ties += 1
                diff[i:i + 3] = b"\xff\xff\x00"
                continue
            real += 1
            diff[i:i + 3] = b"\xff\x00\xff"
            bbox = (x, y, x, y) if bbox is None else (
                min(bbox[0], x), min(bbox[1], y), max(bbox[2], x), max(bbox[3], y))

    total = rw * rh
    print(f"{sys.argv[2]}: {real}/{total} pixels differ ({ties} sampling ties)"
          + (f", bbox {bbox}" if bbox else ""))

    if len(sys.argv) > 3:
        with open(sys.argv[3], "wb") as f:
            f.write(f"P6\n{rw} {rh}\n255\n".encode())
            f.write(diff)
    return 1 if real else 0


if __name__ == "__main__":
    sys.exit(main())
