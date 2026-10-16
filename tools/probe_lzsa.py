#!/usr/bin/env python3
"""Probe the entry payload layout by brute force, using the stored CRC32."""
import binascii
import lzma
import struct
import sys

sys.path.insert(0, r"D:\harness_test\pdfreader\tools")
from extract_lzsa import parse_archive, read_u32, STORED_MARKER  # noqa: E402

INSTALLER = r"D:\Users\RimeSun\Downloads\SumatraPDF-3.6.1-64-install.exe"


def props_to_filter(props: bytes) -> dict:
    d = props[0]
    lc = d % 9
    d //= 9
    lp = d % 5
    pb = d // 5
    ds = read_u32(props, 1)
    return {"id": lzma.FILTER_LZMA1, "lc": lc, "lp": lp, "pb": pb,
            "dict_size": max(ds, 4096)}


def try_decode(stream: bytes, props: bytes, bcj: bool):
    base = props_to_filter(props)
    filters = ([{"id": lzma.FILTER_X86}, base] if bcj else [base])
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=filters)
    return dec.decompress(stream)


def main() -> int:
    image = open(INSTALLER, "rb").read()
    arch = None
    start = 8
    while True:
        idx = image.find(struct.pack("<I", 0x41537A4C), start)
        if idx < 0:
            break
        p, _ = parse_archive(image[idx:], idx)
        if p and len(p["entries"]) == 49:
            arch = p
            break
        start = idx + 4
    if not arch:
        print("archive with 49 entries not found")
        return 1

    off = arch["data_off"]
    blobs = []
    for e in arch["entries"]:
        blobs.append((e, image[off:off + e["compressed_size"]]))
        off += e["compressed_size"]

    print("=== first 4 entries: header bytes ===")
    for e, blob in blobs[:4]:
        head = " ".join(f"{b:02x}" for b in blob[:10])
        print(f"  {e['name'][:40]:<42} comp={e['compressed_size']:>7,} "
              f"uncomp={e['uncompressed_size']:>7,}  head={head}")

    variants = {
        "flag@0 props@1 stream@6, no-bcj": lambda b: try_decode(b[6:], b[1:6], False),
        "flag@0 props@1 stream@6, bcj":    lambda b: try_decode(b[6:], b[1:6], True),
        "props@0 stream@5, no-bcj":        lambda b: try_decode(b[5:], b[0:5], False),
        "len@0 props@1 stream@6, no-bcj":  lambda b: try_decode(b[6:], b[1:6], False),
    }

    print("\n=== which layout reproduces size + CRC32? ===")
    for label, fn in variants.items():
        good = 0
        first_err = ""
        for e, blob in blobs[:12]:
            if not blob or blob[0] == STORED_MARKER:
                continue
            try:
                data = fn(blob)
            except Exception as exc:  # noqa: BLE001
                if not first_err:
                    first_err = f"{type(exc).__name__}: {exc}"
                continue
            size_ok = len(data) == e["uncompressed_size"]
            crc_ok = (binascii.crc32(data) & 0xFFFFFFFF) == e["crc32"]
            if size_ok and crc_ok:
                good += 1
        print(f"  {label:<34} matches={good}/12   first error: {first_err or '-'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
