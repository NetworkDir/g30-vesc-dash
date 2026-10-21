#!/usr/bin/env python3
"""Keep only the bytes of an Intel HEX file within [start, end).

usage: hexfilter.py in.hex start end out.hex   (start/end accept 0x hex)
"""
import importlib.util, os, sys

_spec = importlib.util.spec_from_file_location(
    "mergehex", os.path.join(os.path.dirname(__file__), "mergehex.py"))
mergehex = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mergehex)


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 1
    src, start, end, out = sys.argv[1:]
    start, end = int(start, 0), int(end, 0)
    data = {a: b for a, b in mergehex.read_hex(src).items() if start <= a < end}
    mergehex.write_hex(out, data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
