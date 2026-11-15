#!/usr/bin/env python3
"""Keep hit ancestry and command boundaries; drop non-causal visual births."""
import argparse
from pathlib import Path


def prune_case(lines):
    rows = [line.split() for line in lines]
    if rows and rows[0][0] == "C" and int(rows[0][2]) >= 10000:
        # Synthetic website scenarios need command inputs, not setAction.
        yield lines[0]
        return
    births = {row[1]: row for row in rows if row[0] == "B"}
    needed = {row[1] for row in rows if row[0] in ("H", "R")}
    queue = list(needed)
    while queue:
        row = births.get(queue.pop())
        if row:
            for ancestor in (row[3], row[4]):
                if ancestor not in ("0", "1", "2") and ancestor not in needed:
                    needed.add(ancestor)
                    queue.append(ancestor)
    mailbox = {}
    for line, row in zip(lines, rows):
        if row[0] == "B" and row[1] not in needed:
            continue
        if row[0] == "A":
            mailbox.pop(row[1], None)
        elif row[0] == "R":
            mailbox.pop(row[2], None)
        elif row[0] == "X":
            owner, command = row[1], row[3]
            if mailbox.get(owner) == command:
                continue
            mailbox[owner] = command
        yield line


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error("input and output must differ")
    case = []
    with args.input.open() as source, args.output.open("w") as destination:
        for line in source:
            if line.startswith("C ") and case:
                destination.writelines(prune_case(case))
                case = []
            case.append(line)
        if case:
            destination.writelines(prune_case(case))


if __name__ == "__main__":
    main()
