"""Build the pinned ECDICT snapshot into independently compressed lookup blocks.

Only the build step downloads data. Runtime lookups read one small resource
block, without opening a connection or loading the entire dictionary into RAM.
"""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import struct
import urllib.request
import zlib

SOURCE_COMMIT = "bc015ed2e24a7abef49fc6dbbb7fe32c1dadaf8b"
SOURCE_BLOB = "c4ade63ea08cf39d9c3475e96929036d64d94c94"
BASE = f"https://raw.githubusercontent.com/skywind3000/ECDICT/{SOURCE_COMMIT}"


def build(output: Path):
    output.mkdir(parents=True, exist_ok=True)
    cache = output.parent / "dictionary-source"
    cache.mkdir(parents=True, exist_ok=True)
    source = cache / "ecdict.csv"
    if not source.exists():
        with urllib.request.urlopen(f"{BASE}/ecdict.csv", timeout=60) as response:
            source.write_bytes(response.read())
    raw = source.read_bytes()
    blob = hashlib.sha1(f"blob {len(raw)}\0".encode() + raw).hexdigest()
    if blob != SOURCE_BLOB:
        raise RuntimeError("ECDICT snapshot checksum mismatch; remove the cached CSV and retry")
    rows = {}
    for row in csv.DictReader(io.StringIO(raw.decode("utf-8-sig"))):
        word = row["word"].strip()
        if not word or not (row["translation"] or row["definition"]):
            continue
        entry = {k: row.get(column, "").replace("\\n", "\n").strip()
                 for k, column in (("p", "phonetic"), ("t", "translation"),
                                   ("d", "definition"), ("e", "exchange"))}
        entry["w"] = word
        key = word.casefold()
        if key not in rows or len(entry["t"]) > len(rows[key]["t"]):
            rows[key] = entry
    index, blocks, offset = [], [], 0
    keys = sorted(rows)
    for start in range(0, len(keys), 128):
        batch_keys = keys[start:start + 128]
        batch = [[key, rows[key]] for key in batch_keys]
        plain = json.dumps(batch, ensure_ascii=False, separators=(",", ":")).encode()
        compressed = struct.pack(">I", len(plain)) + zlib.compress(plain, 9)
        index.append([batch_keys[-1], offset, len(compressed)])
        blocks.append(compressed)
        offset += len(compressed)
    encoded_index = json.dumps(index, ensure_ascii=False, separators=(",", ":")).encode()
    destination = output / "ecdict.pdict"
    with destination.open("wb") as file:
        file.write(b"PDICT2\n\0" + struct.pack(">I", len(encoded_index)))
        file.write(encoded_index)
        for block in blocks:
            file.write(block)
    metadata = {"source": "https://github.com/skywind3000/ECDICT", "commit": SOURCE_COMMIT,
                "source_sha256": hashlib.sha256(raw).hexdigest(), "entries": len(rows),
                "format": "PDICT2", "sha256": hashlib.sha256(destination.read_bytes()).hexdigest()}
    (output / "ecdict-source.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(f"ECDICT: {len(rows):,} entries, {destination.stat().st_size:,} bytes -> {destination}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("build/deepseek-translate/data"))
    build(parser.parse_args().output)
