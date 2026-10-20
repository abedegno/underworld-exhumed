"""A recording with its game clock moved on: every value the TIME stream gives (runtime/replay/
replay.c's format, tools/replay.py's read_log) plus TICKS, everything else as it was. It makes the
introclock session from intro: the same inputs with the clock past FFFFh ticks from the start, so
that the code that takes 16-bit differences of the 32-bit clock (CUTS.C's anm_cycle, issue 8's
audit) runs where the high word is not 0. DOS replays the result as it replays any recording (its
golden is made from DOS like every other), so the port is held to what DOS does with such a clock.

    python3 tools/clockshift.py IN OUT TICKS     (TICKS in hex or decimal)

The first run's value is written in full (FFh and the doubleword) and each later run that is
written in full gets TICKS too; the runs given as a step from the one before need nothing. The
first TIME chunk grows by the five bytes of the full value (its chunks are at most 195 bytes, and
replay.c's buffer holds 208)."""
import struct, sys


def chunks(d):
    out, p = [], 12
    while p + 3 <= len(d):
        s, n = d[p], struct.unpack_from('<H', d, p + 1)[0]
        out.append([s, bytearray(d[p + 3:p + 3 + n])])
        p += 3 + n
    return out


def shift(d, ticks):
    cs = chunks(d)
    # the TIME stream as one byte string, with where each byte came from
    where, data = [], bytearray()
    for i, (s, b) in enumerate(cs):
        if s == 1:
            where += [(i, j) for j in range(len(b))]
            data += b
    q, first, absolutes = 0, None, []
    while q < len(data):
        q += 2                                  # the run's count
        if data[q] == 0xFF:
            absolutes.append(q + 1); q += 5
        else:
            if first is None: first = q
            q += 1
        if first is None and absolutes: first = -1      # the first run was written in full
    for a in absolutes:
        v = struct.unpack_from('<I', data, a)[0] + ticks
        for k, byte in enumerate(struct.pack('<I', v)):
            i, j = where[a + k]
            cs[i][1][j] = byte
    if first is not None and first >= 0:
        i, j = where[first]
        dt = cs[i][1][j]
        cs[i][1][j:j + 1] = b'\xff' + struct.pack('<I', dt + ticks)
        if len(cs[i][1]) > 200: raise SystemExit('clockshift: the first TIME chunk would pass 200 bytes')
    out = bytearray(d[:12])
    for s, b in cs: out += bytes([s]) + struct.pack('<H', len(b)) + b
    return bytes(out)


if __name__ == '__main__':
    if len(sys.argv) != 4: sys.exit(__doc__)
    src, dst, ticks = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
    open(dst, 'wb').write(shift(open(src, 'rb').read(), ticks))
