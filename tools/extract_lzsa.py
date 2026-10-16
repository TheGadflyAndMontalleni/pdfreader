#!/usr/bin/env python3
"""Extract the "LzSA" archive SumatraPDF embeds in its installer's resources.

Container format, from the producer side (src/shared/tools/MakeLZSA.cpp):

    header : magic u32 = 0x41537a4c ("LzSA"), fileCount u32
    per file (24 bytes + name + NUL):
        headerSize      u32   == 25 + len(name)
        compressedSize  u32
        uncompressedSize u32
        fileCrc32       u32
        mtimeLow        u32
        mtimeHigh       u32
    then: headerCrc32 u32
    then: the concatenated per-file blobs

Despite the magic, the payload is LZMA, not LZSA: each blob starts with
1 + LZMA_PROPS_SIZE (= 6) bytes -- a props-length byte, the five LZMA1 property
bytes, then a raw LZMA1 stream.

Usage:
    python extract_lzsa.py <installer.exe> <output-dir> [--list]
"""
from __future__ import annotations

import argparse
import binascii
import lzma
import os
import struct
import sys

MAGIC = 0x41537A4C  # "LzSA"
ENTRY_HEADER = 24
LZMA_PROPS_SIZE = 5
# First byte of an entry whose payload is copied verbatim (MakeLZSA.cpp).
STORED_MARKER = 0xFF


def read_u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def lzma_props_to_filter(props: bytes) -> dict:
    d = props[0]
    lc = d % 9
    d //= 9
    lp = d % 5
    pb = d // 5
    dict_size = read_u32(props, 1)
    return {
        "id": lzma.FILTER_LZMA1,
        "lc": lc,
        "lp": lp,
        "pb": pb,
        "dict_size": max(dict_size, 4096),
    }


def decode_blob(blob: bytes, uncompressed_size: int) -> bytes:
    """Decode one entry payload.

    Layout, per MakeLZSA.cpp:
        stored : 0xFF || raw bytes
        lzma   : bcjFlag(1) || lzmaProps(5) || raw LZMA1 stream
                 bcjFlag == 1 means an x86 BCJ filter was applied before LZMA,
                 so it must be undone while decoding.
    """
    if not blob:
        raise ValueError("empty payload")

    if blob[0] == STORED_MARKER:
        return blob[1:1 + uncompressed_size]

    if len(blob) < 1 + LZMA_PROPS_SIZE:
        raise ValueError("too short for an LZMA header")

    bcj_flag = blob[0]
    props = blob[1:1 + LZMA_PROPS_SIZE]
    stream = blob[1 + LZMA_PROPS_SIZE:]

    base = lzma_props_to_filter(props)
    if bcj_flag:
        # liblzma takes the chain in encoding order; it reverses it internally.
        filters = [{"id": lzma.FILTER_X86}, base]
    else:
        filters = [base]

    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=filters)
    return dec.decompress(stream)


def parse_archive(blob: bytes, base_offset: int):
    """Return (entries, header_len) or (None, reason)."""
    if len(blob) < 12:
        return None, "too short"
    if read_u32(blob, 0) != MAGIC:
        return None, "bad magic"

    file_count = read_u32(blob, 4)
    if file_count == 0 or file_count > 4096:
        return None, f"implausible file count {file_count}"

    entries = []
    off = 8
    for _ in range(file_count):
        if off + ENTRY_HEADER > len(blob):
            return None, "truncated entry header"
        header_size = read_u32(blob, off)
        compressed_size = read_u32(blob, off + 4)
        uncompressed_size = read_u32(blob, off + 8)
        crc = read_u32(blob, off + 12)
        # mtime low/high at off+16 and off+20 are not needed for extraction.

        name_len = header_size - (ENTRY_HEADER + 1)
        if name_len <= 0 or name_len > 4096:
            return None, f"implausible name length {name_len}"
        name_start = off + ENTRY_HEADER
        name_end = name_start + name_len
        if name_end + 1 > len(blob):
            return None, "truncated name"
        raw_name = blob[name_start:name_end]
        name = raw_name.decode("utf-8", errors="replace")

        entries.append({
            "name": name,
            "header_size": header_size,
            "compressed_size": compressed_size,
            "uncompressed_size": uncompressed_size,
            "crc32": crc,
        })
        off = name_end + 1  # + NUL

    header_crc_stored = read_u32(blob, off)
    header_crc_calc = binascii.crc32(blob[:off]) & 0xFFFFFFFF
    # `off` is relative to the slice we were handed, so rebase it onto the file.
    data_off = base_offset + off + 4
    return {
        "entries": entries,
        "data_off": data_off,
        "header_crc_ok": header_crc_stored == header_crc_calc,
        "base_offset": base_offset,
        "data_bytes": sum(e["compressed_size"] for e in entries),
    }, None


def extract(installer_path: str, out_dir: str, list_only: bool) -> int:
    with open(installer_path, "rb") as fh:
        image = fh.read()

    # Locate every embedded archive by its magic rather than trusting
    # hand-derived resource offsets.
    archives = []
    start = 0
    while True:
        idx = image.find(struct.pack("<I", MAGIC), start)
        if idx < 0:
            break
        parsed, reason = parse_archive(image[idx:], idx)
        if parsed:
            archives.append(parsed)
            print(f"[+] archive at 0x{idx:X}: {len(parsed['entries'])} file(s), "
                  f"header CRC {'ok' if parsed['header_crc_ok'] else 'MISMATCH'}")
        else:
            print(f"[-] magic at 0x{idx:X} rejected: {reason}")
        start = idx + 4

    if not archives:
        print("no archive found")
        return 1

    total_ok = total_bad = 0
    for ai, arch in enumerate(archives):
        off = arch["data_off"]
        for entry in arch["entries"]:
            name = entry["name"]
            comp = entry["compressed_size"]
            blob = image[off:off + comp]
            off += comp

            if len(blob) != comp:
                print(f"    ! {name}: truncated ({len(blob)}/{comp})")
                total_bad += 1
                continue

            if list_only:
                print(f"    {entry['uncompressed_size']:>10,}  {name}")
                continue

            if comp < 2:
                print(f"    ! {name}: payload too small")
                total_bad += 1
                continue

            try:
                data = decode_blob(blob, entry["uncompressed_size"])
            except (lzma.LZMAError, ValueError) as exc:
                print(f"    ! {name}: decode failed: {exc}")
                total_bad += 1
                continue

            crc = binascii.crc32(data) & 0xFFFFFFFF
            ok_size = len(data) == entry["uncompressed_size"]
            ok_crc = crc == entry["crc32"]
            flag = "ok " if (ok_size and ok_crc) else "BAD"
            if ok_size and ok_crc:
                total_ok += 1
            else:
                total_bad += 1
            print(f"    {flag} {len(data):>10,}  crc={'ok' if ok_crc else 'MISMATCH'}  {name}")

            if ok_size and ok_crc:
                dest = os.path.join(out_dir, name.replace("\\", os.sep))
                os.makedirs(os.path.dirname(dest) or out_dir, exist_ok=True)
                with open(dest, "wb") as out:
                    out.write(data)

    if not list_only:
        print(f"\nextracted {total_ok} file(s), {total_bad} failed -> {out_dir}")
    return 0 if total_bad == 0 else 2


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("installer")
    ap.add_argument("out_dir")
    ap.add_argument("--list", action="store_true", help="list entries without writing")
    args = ap.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)
    return extract(args.installer, args.out_dir, args.list)


if __name__ == "__main__":
    sys.exit(main())
