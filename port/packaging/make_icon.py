#!/usr/bin/env python3
"""Draws an original (non-game-asset) bomb icon and writes an .iconset."""
import math, os, struct, sys, zlib


def png(path, n, px):
    raw = b''.join(b'\0' + bytes(px[y * n * 4:(y + 1) * n * 4]) for y in range(n))
    ck = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ck(b'IHDR', struct.pack('>IIBBBBB', n, n, 8, 6, 0, 0, 0))
                           + ck(b'IDAT', zlib.compress(raw, 9)) + ck(b'IEND', b''))


def render(n):
    px = bytearray(n * n * 4)
    ss = 3
    for y in range(n):
        for x in range(n):
            acc = [0, 0, 0, 0]
            for sy in range(ss):
                for sx in range(ss):
                    u = (x + (sx + .5) / ss) / n
                    v = (y + (sy + .5) / ss) / n
                    c = None
                    # background rounded square (orange gradient)
                    r = .2
                    dx = max(abs(u - .5) - (.5 - r - .03), 0)
                    dy = max(abs(v - .5) - (.5 - r - .03), 0)
                    if dx * dx + dy * dy <= r * r:
                        c = (250, int(170 - 90 * v), 40, 255)
                    # spark
                    sxc, syc = .74, .2
                    d = math.hypot(u - sxc, v - syc)
                    ang = math.atan2(v - syc, u - sxc)
                    if d < .09 * (0.55 + 0.45 * abs(math.cos(ang * 4))):
                        c = (255, 245, 120, 255)
                    # fuse
                    t = (u - .55) / .17
                    if 0 <= t <= 1 and abs(v - (.36 - .14 * t - .05 * math.sin(t * 3.1))) < .025:
                        c = (120, 80, 50, 255)
                    # cap
                    if abs(u - .54) < .07 and abs(v - .37) < .05:
                        c = (90, 90, 100, 255)
                    # bomb body
                    bd = math.hypot(u - .45, v - .58)
                    if bd < .27:
                        sh = max(0, 1 - math.hypot(u - .37, v - .49) / .2)
                        g = int(30 + 120 * sh ** 2)
                        c = (g, g, g + 15, 255)
                    if c:
                        for i in range(4):
                            acc[i] += c[i]
            o = (y * n + x) * 4
            for i in range(4):
                px[o + i] = acc[i] // (ss * ss)
    return px


out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for s in (16, 32, 128, 256, 512):
    png(os.path.join(out, 'icon_%dx%d.png' % (s, s)), s, render(s))
    png(os.path.join(out, 'icon_%dx%d@2x.png' % (s, s)), s * 2, render(s * 2))
