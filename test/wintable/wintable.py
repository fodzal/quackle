#!/usr/bin/env python3
"""Learn a bogowin table -- P(player on turn wins | lead, tiles unseen) -- by self-play.

Runs `libquackle_test --mode=wintable` in several processes, pools their win
counts, and fits, for each number of unseen tiles, a logistic curve in the
lead, borrowing neighbouring unseen counts until there is enough data. The
result is written in the format of data/strategy/<lexicon>/bogowin.

Example:
    wintable.py --games 20000 --lexicon ods5 --alphabet french \
        --out data/strategy/ods5/bogowin
"""

import argparse
import math
import os
import re
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "regression"))
import run_regression as rr  # noqa: E402

CELL = re.compile(r"^WINTABLE (\d+) (-?\d+) (\d+) ([\d.]+)$")
MAX_LEAD = 300


def read_counts(paths):
    counts = defaultdict(lambda: [0, 0.0])
    for path in paths:
        for line in Path(path).read_text(errors="replace").splitlines():
            m = CELL.match(line.strip())
            if m:
                cell = counts[(int(m.group(1)), int(m.group(2)))]
                cell[0] += int(m.group(3))
                cell[1] += float(m.group(4))
    return counts


def fit_logistic(points, ridge=1e-3):
    """points: (x, n, wins). Returns (a, b) maximizing the binomial likelihood of sigmoid(a x + b)."""
    a = b = 0.0
    for _ in range(50):
        ga = gb = haa = hab = hbb = 0.0
        for x, n, w in points:
            p = 1 / (1 + math.exp(-max(-500.0, min(500.0, a * x + b))))
            r = w - n * p
            v = n * p * (1 - p)
            ga += r * x
            gb += r
            haa += v * x * x
            hab += v * x
            hbb += v
        ga -= ridge * a
        haa += ridge
        hbb += ridge
        det = haa * hbb - hab * hab
        if det <= 0:
            break
        da = (hbb * ga - hab * gb) / det
        db = (haa * gb - hab * ga) / det
        a += da
        b += db
        if abs(da) < 1e-9 and abs(db) < 1e-9:
            break
    return a, b


def fit_table(counts, min_positions):
    by_unseen = defaultdict(list)
    for (unseen, lead), (n, wins) in counts.items():
        by_unseen[unseen].append((lead / 100.0, n, wins))
    if not by_unseen:
        sys.exit("no WINTABLE lines found")
    max_unseen = max(by_unseen)

    table = {}
    for unseen in range(1, max_unseen + 1):
        width = 0
        while True:
            points = [p for u in range(unseen - width, unseen + width + 1) for p in by_unseen.get(u, [])]
            # The opening position always has lead 0, which says nothing about the slope.
            if sum(n for x, n, _ in points if x != 0) >= min_positions or width >= max_unseen:
                break
            width += 1
        a, b = fit_logistic(points)
        table[unseen] = (a, b, width, sum(n for _, n, _ in points))
    return table


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", help="libquackle_test, or a build dir holding test/libquackle_test")
    ap.add_argument("--games", type=int, default=10000)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--lexicon", default="ods5")
    ap.add_argument("--alphabet", default="french")
    ap.add_argument("--computer", default="Static Player")
    ap.add_argument("--computer2")
    ap.add_argument("--min-positions", type=int, default=20000,
                    help="pool neighbouring unseen counts until a fit has this many positions")
    ap.add_argument("--counts", nargs="*", help="fit these saved harness logs instead of playing")
    ap.add_argument("--out", required=True, help="bogowin file to write")
    args = ap.parse_args()

    if args.counts:
        logs = args.counts
    else:
        binary = rr.resolve_binary(args.bin)
        if not binary:
            sys.exit("libquackle_test not found; pass --bin")
        work_root = rr.setup_work_root()
        run_dir = work_root / "run"
        gaddag = rr.build_gaddag(binary, args.lexicon, args.alphabet, work_root)
        if gaddag:
            (run_dir / "lexica").mkdir(exist_ok=True)
            rr.link_file(gaddag, run_dir / "lexica" / f"{args.lexicon}.gaddag")

        jobs = max(1, min(args.jobs, args.games))
        base, extra = divmod(args.games, jobs)
        procs = []
        for j in range(jobs):
            log = work_root / f"job{j}.log"
            cmd = [str(binary), "--mode=wintable", f"--lexicon={args.lexicon}", f"--alphabet={args.alphabet}",
                   f"--seed={args.seed + j}", f"--repetitions={base + (j < extra)}", "--threads=1",
                   f"--computer={args.computer}", f"--computer2={args.computer2 or args.computer}"]
            procs.append((subprocess.Popen(cmd, cwd=run_dir, stdout=open(log, "w"), stderr=subprocess.STDOUT,
                                           env=rr.child_env(binary)), log))
        print(f"wintable: {args.games} games, {jobs} processes, logs in {work_root}")
        start = time.time()
        for p, _ in procs:
            p.wait()
        print(f"played in {time.time() - start:.0f}s")
        failed = [log for p, log in procs if p.returncode]
        if failed:
            sys.exit(f"jobs failed; see {failed}")
        logs = [log for _, log in procs]

    counts = read_counts(logs)
    table = fit_table(counts, args.min_positions)
    with open(args.out, "w", newline="\n") as f:
        for lead in range(-MAX_LEAD, MAX_LEAD + 1):
            for unseen in sorted(table):
                a, b, _, _ = table[unseen]
                p = 1 / (1 + math.exp(-max(-500.0, min(500.0, a * lead / 100.0 + b))))
                f.write(f"{lead} {unseen} {p:.6g}\n")

    print(f"positions {sum(n for n, _ in counts.values())}; wrote {args.out}")
    print("unseen  P(win|lead -50, 0, +50)  pooled+-  positions")
    for unseen in sorted(table)[::8] + [max(table)]:
        a, b, width, n = table[unseen]
        ps = [1 / (1 + math.exp(-(a * x + b))) for x in (-0.5, 0, 0.5)]
        print(f"{unseen:6d}  " + "  ".join(f"{p:.3f}" for p in ps) + f"   {width:3d}  {n:9d}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
