#!/usr/bin/env python3
"""Validate the diagnostic LE VxD; --prepare marks 32-bit data executable.

Windows 3.x expects executable 32-bit VxD objects even for C data. Make this
single linker normalization explicit, then validate the DDB and locked objects.
"""
import argparse
from pathlib import Path
import struct


def inspect(path, prepare=False):
    data = bytearray(path.read_bytes())

    def need(offset, size):
        if offset < 0 or offset + size > len(data):
            raise ValueError("truncated or invalid file offset")

    def u16(offset):
        need(offset, 2)
        return struct.unpack_from("<H", data, offset)[0]

    def u32(offset):
        need(offset, 4)
        return struct.unpack_from("<I", data, offset)[0]

    need(0, 64)
    if data[:2] != b"MZ":
        raise ValueError("missing MZ header")
    le = u32(0x3c)
    need(le, 0xb0)
    if data[le:le + 4] != b"LE\x00\x00":
        raise ValueError("not a little-endian LE image")
    if u16(le + 8) != 2 or u16(le + 10) != 4:
        raise ValueError("expected i386 / Windows LE image")
    count = u32(le + 0x44)
    objtab = le + u32(le + 0x40)
    pagetab = le + u32(le + 0x48)
    pagesize = u32(le + 0x28)
    datapages = u32(le + 0x80)
    if not 0 < count < 32 or pagesize != 4096:
        raise ValueError("unexpected object count or page size")
    objects = []
    for i in range(count):
        offset = objtab + 24 * i
        need(offset, 24)
        size, base, flags, first, pages, reserved = struct.unpack_from(
            "<6I", data, offset)
        if not flags & 0x2000:
            raise ValueError("unexpected 16-bit object")
        if prepare:
            flags |= 4
            struct.pack_into("<I", data, offset + 8, flags)
        if not flags & 4 or flags & 0x10 or not flags & 0x40:
            raise ValueError("object must be executable, preloaded, non-discardable")
        objects.append((size, base, flags, first, pages))

    def object_file_offset(number, offset, length):
        if not 1 <= number <= count:
            raise ValueError("invalid object reference")
        size, _, _, first, pages = objects[number - 1]
        if offset + length > size or offset // pagesize != (offset + length - 1) // pagesize:
            raise ValueError("invalid or page-crossing structure")
        page_index = offset // pagesize
        if page_index >= pages:
            raise ValueError("structure has no backed page")
        entry = pagetab + 4 * (first - 1 + page_index)
        need(entry, 4)
        page = int.from_bytes(data[entry:entry + 3], "big")
        if data[entry + 3] != 0 or page == 0:
            raise ValueError("structure is not in a physical page")
        result = datapages + (page - 1) * pagesize + offset % pagesize
        need(result, length)
        return result

    # The first exported ordinal is the DDB (32-bit entry bundle).
    entry = le + u32(le + 0x5c)
    need(entry, 9)
    if data[entry] < 1 or data[entry + 1] != 3:
        raise ValueError("missing 32-bit DDB export at ordinal 1")
    ddb_object = u16(entry + 2)
    ddb_offset = u32(entry + 5)
    ddb = object_file_offset(ddb_object, ddb_offset, 56)
    if data[ddb + 12:ddb + 20] != b"BCM57DBG":
        raise ValueError("wrong DDB name")
    if u16(ddb + 4) != 0x030a or u16(ddb + 6) != 0:
        raise ValueError("wrong DDB version or device ID")
    if u32(ddb + 20) != 0x54004000:
        raise ValueError("unexpected initialization order")
    control = u32(ddb + 24)
    if not any(base <= control < base + size for size, base, *_ in objects):
        raise ValueError("control dispatcher is outside the image")
    if any(data[ddb + 28:ddb + 56]):
        raise ValueError("unexpected public API or service table")
    if b"\xcd\x20\x00\x00\x28\x00" not in data:
        raise ValueError("NdisGetVersion VxD call is missing")
    if prepare:
        path.write_bytes(data)
    print(f"{path}: valid BCM57DBG LE/DDB, {count} locked 32-bit objects")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    try:
        inspect(args.image, args.prepare)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"{args.image}: {error}\n")


if __name__ == "__main__":
    main()
