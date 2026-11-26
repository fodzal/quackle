#!/usr/bin/env python3
"""Freeze the strategy tables of a lexicon, as of a git revision, into a directory.

    freeze.py --rev 2cb0782 --lexicon ods5 --out ../arena-ref/ods5-2cb0782

The directory then gives a reference player its tables through
--config strategy=<directory>, whatever the repository's data becomes later.
Every table the harness would resolve for the lexicon is written, falling back
the way DataManager::findDataFile does; a table found nowhere is written empty,
which loads as no table, so that a file added to the lexicon later cannot leak in.
"""

import argparse
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
TABLES = ["syn2", "worths", "vcplace", "bogowin", "superleaves"]


def git_blob(rev, path):
    proc = subprocess.run(["git", "-C", str(REPO), "show", f"{rev}:{path}"], capture_output=True)
    return proc.stdout if proc.returncode == 0 else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rev", required=True, help="git revision to take the tables from")
    ap.add_argument("--lexicon", default="ods5")
    ap.add_argument("--alphabet", default="french")
    ap.add_argument("--out", required=True, help="directory to write; must not exist yet")
    args = ap.parse_args()

    out = Path(args.out)
    if out.exists():
        sys.exit(f"{out} already exists")

    commit = subprocess.run(["git", "-C", str(REPO), "rev-parse", "--short", f"{args.rev}^{{commit}}"],
                            capture_output=True, text=True)
    if commit.returncode != 0:
        sys.exit(f"unknown revision {args.rev}")

    # The harness's backup lexicon (TestHarness::startUp).
    backup = "default_english" if args.alphabet == "english" else "nwl23"
    lexica = [args.lexicon] + (["csw"] if args.lexicon.startswith("csw") else []) + [backup, "default"]

    out.mkdir(parents=True)
    sources = []
    for table in TABLES:
        for lexicon in lexica:
            path = f"data/strategy/{lexicon}/{table}"
            blob = git_blob(args.rev, path)
            if blob is not None:
                (out / table).write_bytes(blob)
                sources.append(f"{table}: {path}")
                break
        else:
            (out / table).write_bytes(b"")
            sources.append(f"{table}: none (empty)")

    (out / "FROZEN").write_text(f"lexicon {args.lexicon}, revision {commit.stdout.strip()}\n" + "\n".join(sources) + "\n")
    print((out / "FROZEN").read_text(), end="")


if __name__ == "__main__":
    main()
