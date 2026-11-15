#!/usr/bin/env python3
"""Decode a cached cleista framedata roster; never label inventory as a test pass.

Download each /framedata-data/<character>/moves.json into the input directory.
Output contains metadata only, not game archives or artwork.
"""
import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path

CHARACTERS = (
    "reimu marisa sakuya alice patchouli youmu remilia yuyuko yukari suika "
    "reisen aya komachi iku tenshi sanae cirno meiling utsuho suwako"
).split()


def decode(document):
    if document.get("format") != "framedata-dag-1":
        raise ValueError("unsupported framedata format")
    cache, visiting = {}, set()

    def value(item):
        if isinstance(item, list):
            if len(item) != 1 or type(item[0]) is not int:
                raise ValueError("invalid DAG reference")
            return node(item[0])
        return item

    def node(index):
        if index in cache:
            return cache[index]
        if index in visiting or not 0 <= index < len(document["nodes"]):
            raise ValueError("cyclic or out-of-range DAG reference")
        visiting.add(index)
        row = document["nodes"][index]
        if row[0] == -1:
            result = [value(item) for item in row[1:]]
        else:
            keys, defaults = document["schemas"][row[0]]
            if len(keys) != len(defaults) or len(row) % 2 != 1:
                raise ValueError("invalid DAG object")
            result = dict(zip(keys, defaults))
            for pos in range(1, len(row), 2):
                result[keys[row[pos]]] = value(row[pos + 1])
        visiting.remove(index)
        cache[index] = result
        return result

    return value(document["root"])


def inventory(directory):
    result = []
    for character_id, character in enumerate(CHARACTERS):
        path = directory / f"{character}.json"
        raw = path.read_bytes()
        decoded = decode(json.loads(raw))
        if decoded["key"] != character:
            raise ValueError(f"wrong character in {path}")
        actions = set()
        for move in decoded["moves"]:
            action = (move["action"], move["slug"])
            if action in actions:
                raise ValueError(f"duplicate action {character}:{action}")
            actions.add(action)
        result.append({
            "character": character, "characterId": character_id,
            "source": f"https://soku.cleista.cc/framedata-data/{character}/moves.json",
            "sha256": hashlib.sha256(raw).hexdigest(),
            "counts": dict(Counter(move["kind"] for move in decoded["moves"])),
            "moves": [{**move, "simulationStatus": "not-run",
                       "nativeStatus": "not-verified"} for move in decoded["moves"]],
        })
    return {"format": "sokumax-move-audit-1", "characters": result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = inventory(args.directory)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    for character in report["characters"]:
        print(character["character"], character["counts"])
    print("Inventory only: simulation and native coverage remain unverified.")


if __name__ == "__main__":
    main()
