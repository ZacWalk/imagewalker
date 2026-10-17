"""Dump the ITSF/ITSP structure of a .chm.

For comparing a generated help file against one Windows is known to accept.
Every encoding bug in chmbuild.py was found by running this over a CHM from
C:\\Windows\\Help and diffing the two directory headers against each other.

    python chmdump.py <file.chm> [full]

"full" also lists every directory entry.
"""

import struct
import sys


def decint(data, pos):
    value = 0
    while True:
        b = data[pos]
        pos += 1
        value = (value << 7) | (b & 0x7F)
        if not b & 0x80:
            return value, pos


def main(path):
    raw = open(path, "rb").read()
    print(f"=== {path}  ({len(raw):,} bytes)")
    hdr_len = struct.unpack_from("<I", raw, 0x08)[0]
    print(f"header_len={hdr_len:#x} lang={struct.unpack_from('<I', raw, 0x14)[0]:#x}")
    s0_off, s0_len = struct.unpack_from("<QQ", raw, 0x38)
    d_off, d_len = struct.unpack_from("<QQ", raw, 0x48)
    data_off = struct.unpack_from("<Q", raw, 0x58)[0] if hdr_len >= 0x60 else None
    print(f"sec0={s0_off:#x}/{s0_len:#x} dir={d_off:#x}/{d_len:#x} data={data_off:#x}")
    print("  sec0 body:", raw[s0_off:s0_off + s0_len].hex(" "))

    (chunk_size, density, depth, root, first, last, unk, nchunks, lcid) = struct.unpack_from(
        "<IIIiiiiII", raw, d_off + 0x10
    )
    print(
        f"  chunk_size={chunk_size:#x} density={density} depth={depth} root={root} "
        f"first={first} last={last} unk={unk} nchunks={nchunks} lcid={lcid:#x}"
    )

    base = d_off + 0x54
    for c in range(nchunks):
        chunk = raw[base + c * chunk_size: base + (c + 1) * chunk_size]
        tag = chunk[:4].decode("latin1")
        if tag != "PMGL":
            print(f"  chunk {c}: {tag}")
            if tag == "PMGI":
                free = struct.unpack_from("<I", chunk, 4)[0]
                pos = 0x08
                end = chunk_size - free
                while pos < end:
                    nlen, pos = decint(chunk, pos)
                    name = chunk[pos:pos + nlen].decode("utf-8", "replace")
                    pos += nlen
                    target, pos = decint(chunk, pos)
                    print(f"      PMGI {name!r} -> chunk {target}")
            continue
        free, _, prev, nxt = struct.unpack_from("<Iiii", chunk, 4)
        n = struct.unpack_from("<H", chunk, chunk_size - 2)[0]
        print(f"  chunk {c}: PMGL free={free} prev={prev} next={nxt} entries={n}")
        pos = 0x14
        starts = []
        for i in range(n):
            starts.append(pos)
            nlen, pos = decint(chunk, pos)
            name = chunk[pos:pos + nlen].decode("utf-8", "replace")
            pos += nlen
            sect, pos = decint(chunk, pos)
            off, pos = decint(chunk, pos)
            length, pos = decint(chunk, pos)
            if len(sys.argv) > 2:
                print(f"      [{i:3}] {name!r} sect={sect} off={off} len={length}")
        print(f"      entries end at {pos:#x}, free starts there, "
              f"quickref begins {chunk_size - free:#x}")
        nq = max(n - 1, 0) >> density
        qr = [
            struct.unpack_from("<H", chunk, chunk_size - 2 - 2 * (j + 1))[0]
            for j in range(nq)
        ]
        print(f"      quickref({nq}) = {qr}")
        print(f"      entry starts    = {starts[:20]}")


if __name__ == "__main__":
    main(sys.argv[1])
