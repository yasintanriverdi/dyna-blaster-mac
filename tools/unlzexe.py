#!/usr/bin/env python3
"""LZEXE 0.91 unpacker (reconstructed from the stub found in DYNA.EXE).

Usage: unlzexe.py IN.EXE OUT.EXE
"""
import struct, sys


def unpack(data: bytes) -> bytes:
    (magic, cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, csum, ip, cs,
     lfarlc, ovno) = struct.unpack_from('<14H', data, 0)
    assert magic == 0x5A4D
    hdr = cparhdr * 16
    stub = hdr + cs * 16
    o_ip, o_cs, o_sp, o_ss, csize, inc, dlen = struct.unpack_from('<7H', data, stub)
    assert data[stub + 0xF0 - 0x0:stub + 0x200].find(b'*FAB*') >= 0 or True

    # ---- decompress code/data image ----
    src = data[hdr:]
    si = 0
    out = bytearray()

    def word():
        nonlocal si
        w = src[si] | (src[si + 1] << 8)
        si += 2
        return w

    bp = word()
    cnt = 16

    def bit():
        nonlocal bp, cnt
        b = bp & 1
        bp >>= 1
        cnt -= 1
        if cnt == 0:
            bp = word()
            cnt = 16
        return b

    while True:
        if bit():
            out.append(src[si]); si += 1
            continue
        if not bit():
            n = (bit() << 1) | bit()
            n += 2
            off = src[si] - 256; si += 1
        else:
            lo = src[si]; hi = src[si + 1]; si += 2
            off = ((((hi >> 3) | 0xE0) << 8) | lo) - 0x10000
            n = hi & 7
            if n:
                n += 2
            else:
                b = src[si]; si += 1
                if b == 0:
                    break
                if b == 1:
                    continue  # segment rollover marker
                n = b + 1
        for _ in range(n):
            out.append(out[off])

    # ---- relocations (packed table at stub+0x158) ----
    rp = stub + 0x158
    relocs = []
    seg = 0
    di = 0
    while True:
        b = data[rp]; rp += 1
        if b:
            di += b
        else:
            w = struct.unpack_from('<H', data, rp)[0]; rp += 2
            if w == 0:
                seg += 0xFFF
                continue
            if w == 1:
                break
            di += w
        seg += di >> 4
        di &= 0xF
        relocs.append((di, seg))

    # ---- build new MZ ----
    nrel = len(relocs)
    hdr_bytes = 0x1C + nrel * 4
    hdr_paras = (hdr_bytes + 15) // 16
    total = hdr_paras * 16 + len(out)
    new_cp = (total + 511) // 512
    new_cblp = total % 512
    img_paras = (len(out) + 15) // 16
    # memory needed beyond image: original stub needed (csize+inc) paras + minalloc
    need = (cs + inc + minalloc) - img_paras
    new_min = max(0, need)
    h = struct.pack('<14H', 0x5A4D, new_cblp, new_cp, nrel, hdr_paras, new_min,
                    0xFFFF, o_ss, o_sp, 0, o_ip, o_cs, 0x1C, 0)
    rel = b''.join(struct.pack('<HH', o, s) for o, s in relocs)
    h = h + rel
    h += b'\0' * (hdr_paras * 16 - len(h))
    return h + bytes(out), dict(ip=o_ip, cs=o_cs, sp=o_sp, ss=o_ss, relocs=nrel,
                                 size=len(out))


if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    o, info = unpack(d)
    open(sys.argv[2], 'wb').write(o)
    print(info)
