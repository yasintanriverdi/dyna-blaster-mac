#!/usr/bin/env python3
"""Dyna Blaster asset extractor (no third-party deps).

Formats (verified against the original files):
  GFX/NN.IMG  IFF ILBM 320x200x8 planes, ByteRun1. Files 03..19 have their
              FORM / ILBM / BMHD tags overwritten with b'XXXX' (anti-rip);
              chunk layout is otherwise identical: [FORM][ILBM][BMHD 20]
              [CMAP 768][DPPS][CRNG x16][TINY][BODY].
  PIC         raw 320x200 8bpp linear (mode 13h framebuffer dump)
  PAL         256 text lines "idx r g b", 6-bit VGA DAC values (0..63)
  DIGI.DAT    Creative Voice File (VOC 1.10), one 8-bit PCM block
  MUSIC.DAT   Creative Music File (CMF, OPL2 FM)
  MUSIC       4-channel ProTracker MOD ("harleymusic") - used by INTRO.EXE
"""
import os, struct, sys, zlib


def png(path, w, h, pix, pal):
    """pix: bytes of indices, pal: list of (r,g,b) 8-bit."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            raw += bytes(pal[pix[y * w + x]])

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    data += chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(data)


def byterun1(src, n):
    out = bytearray(); i = 0
    while len(out) < n and i < len(src):
        c = src[i]; i += 1
        if c < 128:
            out += src[i:i + c + 1]; i += c + 1
        elif c > 128:
            out += bytes([src[i]]) * (257 - c); i += 1
    return out


def load_ilbm(path):
    d = open(path, 'rb').read()
    p = 12; chunks = {}; order = []
    while p + 8 <= len(d):
        t = d[p:p + 4]; l = struct.unpack('>I', d[p + 4:p + 8])[0]
        body = d[p + 8:p + 8 + l]
        if t == b'XXXX' and l == 20:
            t = b'BMHD'
        chunks.setdefault(t, []).append(body); order.append(t)
        p += 8 + l + (l & 1)
    w, h, _, _, planes, mask, comp = struct.unpack('>HHhhBBB', chunks[b'BMHD'][0][:11])
    cmap = chunks[b'CMAP'][0]
    pal = [tuple(cmap[i * 3:i * 3 + 3]) for i in range(256)]
    rowbytes = ((w + 15) // 16) * 2
    body = chunks[b'BODY'][0]
    raw = byterun1(body, rowbytes * planes * h) if comp == 1 else body
    pix = bytearray(w * h)
    for y in range(h):
        for pl in range(planes):
            row = raw[(y * planes + pl) * rowbytes:(y * planes + pl + 1) * rowbytes]
            bit = 1 << pl
            for x in range(w):
                if row[x >> 3] & (0x80 >> (x & 7)):
                    pix[y * w + x] |= bit
    return w, h, pix, pal


def load_pal_txt(path):
    pal = [(0, 0, 0)] * 256
    for line in open(path):
        f = line.split()
        if len(f) == 4:
            i, r, g, b = map(int, f)
            pal[i] = tuple((v << 2) | (v >> 4) for v in (r, g, b))
    return pal


def main(src, dst):
    os.makedirs(dst, exist_ok=True)
    gdir = os.path.join(src, 'GFX')
    for f in sorted(os.listdir(gdir)):
        if f.upper().endswith('.IMG'):
            w, h, pix, pal = load_ilbm(os.path.join(gdir, f))
            png(os.path.join(dst, f[:-4] + '.png'), w, h, pix, pal)
            print(f, w, h)
    pal = load_pal_txt(os.path.join(src, 'PAL'))
    pic = open(os.path.join(src, 'PIC'), 'rb').read()
    png(os.path.join(dst, 'PIC.png'), 320, 200, pic, pal)
    # VOC -> WAV
    d = open(os.path.join(src, 'DIGI.DAT'), 'rb').read()
    p = struct.unpack_from('<H', d, 20)[0]
    while d[p] != 0:
        t = d[p]; ln = d[p + 1] | d[p + 2] << 8 | d[p + 3] << 16
        if t == 1:
            sr = 1000000 // (256 - d[p + 4]); pcm = d[p + 6:p + 4 + ln]
            hdr = b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' + struct.pack(
                '<IHHIIHH', 16, 1, 1, sr, sr, 1, 8) + b'data' + struct.pack('<I', len(pcm))
            open(os.path.join(dst, 'DIGI.wav'), 'wb').write(hdr + pcm)
            print('DIGI.wav', sr, 'Hz', len(pcm), 'bytes')
        p += 4 + ln


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'dyna', sys.argv[2] if len(sys.argv) > 2 else 'build/assets')
