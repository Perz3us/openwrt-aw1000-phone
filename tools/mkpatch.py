#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026 perz3us
"""Convert a ProSLIC patch C source into the aw1000-fxs binary patch format.

usage: mkpatch.py PATCH.c OUT.bin
       mkpatch.py --check OUT.bin
"""
import re
import struct
import sys

MAGIC = b"FXSP"


def array(src, name):
    m = re.search(r"\b%s\s*\[\]\s*=\s*\{(.*?)\};" % name, src, re.S)
    if not m:
        raise SystemExit("missing array %s" % name)
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    return [int(v.rstrip("uUlL"), 0) for v in re.findall(r"0x[0-9A-Fa-f]+[uUlL]*|\b\d+[uUlL]*\b", body)]


def convert(src_path, out_path):
    src = open(src_path, encoding="latin-1").read()
    m = re.search(r"Patch ID\s*=\s*(0x[0-9A-Fa-f]+)", src)
    if not m:
        raise SystemExit("missing Patch ID")
    serial = int(m.group(1), 16)
    data = array(src, "patchData")
    entries = array(src, "patchEntries")
    ps_addr = array(src, "patchSupportAddr")
    ps_data = array(src, "patchSupportData")
    if len(entries) != 16:
        raise SystemExit("expected 16 patch entries, got %d" % len(entries))
    while ps_addr and ps_addr[-1] == 0:
        ps_addr.pop()
    ps_data = ps_data[:len(ps_addr)]
    if len(ps_data) != len(ps_addr):
        raise SystemExit("support address/data length mismatch")

    out = bytearray(MAGIC)
    out += struct.pack("<II", serial, len(data))
    out += struct.pack("<%dI" % len(data), *data)
    out += struct.pack("<16H", *entries)
    out += struct.pack("<I", len(ps_addr))
    out += struct.pack("<%dH" % len(ps_addr), *ps_addr)
    out += struct.pack("<%dI" % len(ps_data), *ps_data)
    open(out_path, "wb").write(out)
    return serial, len(data), len(ps_addr)


def check(path):
    b = open(path, "rb").read()
    if b[:4] != MAGIC:
        raise SystemExit("bad magic")
    serial, n_data = struct.unpack_from("<II", b, 4)
    off = 12 + 4 * n_data + 32
    (n_ps,) = struct.unpack_from("<I", b, off)
    return serial, n_data, n_ps


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--check":
        s, n, p = check(sys.argv[2])
    elif len(sys.argv) == 3:
        s, n, p = convert(sys.argv[1], sys.argv[2])
    else:
        raise SystemExit(__doc__)
    print("serial 0x%08x data %d entries 16 support %d" % (s, n, p))


if __name__ == "__main__":
    main()
