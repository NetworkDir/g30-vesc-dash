#!/usr/bin/env python3
"""Merge Intel HEX files (e.g. SoftDevice + application) into one file.

usage: mergehex.py in1.hex in2.hex [...] out.hex
Fails if two inputs write different data to the same address.
"""

import sys


def read_hex(path):
    data = {}
    base = 0
    with open(path) as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            if line[0] != ':':
                raise ValueError(f"{path}:{lineno}: not an Intel HEX record")
            raw = bytes.fromhex(line[1:])
            if (sum(raw) & 0xFF) != 0:
                raise ValueError(f"{path}:{lineno}: bad checksum")
            count, addr, rtype = raw[0], (raw[1] << 8) | raw[2], raw[3]
            payload = raw[4:4 + count]
            if rtype == 0x00:
                for i, b in enumerate(payload):
                    data[base + addr + i] = b
            elif rtype == 0x01:
                break
            elif rtype == 0x02:
                base = ((payload[0] << 8) | payload[1]) << 4
            elif rtype == 0x04:
                base = ((payload[0] << 8) | payload[1]) << 16
            elif rtype in (0x03, 0x05):
                pass  # start address records, not needed for flashing
            else:
                raise ValueError(f"{path}:{lineno}: unknown record type {rtype}")
    return data


def record(rtype, addr, payload):
    raw = bytes([len(payload), (addr >> 8) & 0xFF, addr & 0xFF, rtype]) + payload
    return ":" + (raw + bytes([(-sum(raw)) & 0xFF])).hex().upper()


def write_hex(path, data):
    lines = []
    upper = None
    addrs = sorted(data)
    i = 0
    while i < len(addrs):
        start = addrs[i]
        chunk = bytearray([data[start]])
        i += 1
        while (i < len(addrs) and addrs[i] == start + len(chunk) and len(chunk) < 16
               and (addrs[i] & 0xFFFF) != 0):
            chunk.append(data[addrs[i]])
            i += 1
        if (start >> 16) != upper:
            upper = start >> 16
            lines.append(record(0x04, 0, bytes([(upper >> 8) & 0xFF, upper & 0xFF])))
        lines.append(record(0x00, start & 0xFFFF, bytes(chunk)))
    lines.append(":00000001FF")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    merged = {}
    for path in sys.argv[1:-1]:
        for addr, b in read_hex(path).items():
            if addr in merged and merged[addr] != b:
                print(f"overlap at 0x{addr:08X} ({path})", file=sys.stderr)
                return 1
            merged[addr] = b
    write_hex(sys.argv[-1], merged)
    return 0


if __name__ == "__main__":
    sys.exit(main())
