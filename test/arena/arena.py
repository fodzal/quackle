#!/usr/bin/env python3
"""Arena: measure whether one computer player configuration beats another.

Drives `libquackle_test --mode=arena` in several processes at once and pools
their games. A pair is two games on the same tile sequence with the seats
swapped, so most of the luck of the draw cancels; pairs, not games, are the
independent samples behind every confidence interval printed here.

Player A is --computer/--config (the reference), player B is
--computer2/--config2 (the candidate). Results are reported from B's side.

Examples:
    arena.py --pairs 200 --computer2 "Championship Player" \\
        --config2 iterations=132,maxiterations=330,timelimit=0
    arena.py --pairs 50 --computer "Championship Player" --computer2 "Championship Player" \\
        --config iterations=20,maxiterations=50,timelimit=0 \\
        --config2 iterations=40,maxiterations=100,timelimit=0

A player with a time limit does not replay exactly from its seed; pass
timelimit=0 in its config for reproducible games.
"""

import argparse
import csv
import math
import os
import re
import subprocess
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "regression"))
import run_regression as rr  # noqa: E402

GAME_LINE = re.compile(
    r"^ARENA seed (\d+) first ([AB]) scoreA (-?\d+) scoreB (-?\d+) moves (\d+) "
    r"secondsA ([\d.e+-]+) secondsB ([\d.e+-]+) resyncs (\d+)"
    r" mid ([\d.e+-]+) pre ([\d.e+-]+) end ([\d.e+-]+)$")
FIELDS = ["seed", "first", "scoreA", "scoreB", "moves", "secondsA", "secondsB", "resyncs", "mid", "pre", "end"]


def parse_games(text):
    games = []
    for line in text.splitlines():
        m = GAME_LINE.match(line.strip())
        if m:
            g = dict(zip(FIELDS, m.groups()))
            for k in ("seed", "scoreA", "scoreB", "moves", "resyncs"):
                g[k] = int(g[k])
            for k in ("secondsA", "secondsB", "mid", "pre", "end"):
                g[k] = float(g[k])
            games.append(g)
    return games


def mean_and_halfwidth(values):
    n = len(values)
    if n == 0:
        return 0.0, 0.0
    mean = sum(values) / n
    if n < 2:
        return mean, 0.0
    var = sum((v - mean) ** 2 for v in values) / (n - 1)
    return mean, 1.96 * math.sqrt(var / n)


def elo(score):
    if score <= 0 or score >= 1:
        return math.copysign(float("inf"), score - 0.5)
    return round(-400 * math.log10(1 / score - 1)) or 0.0


def summarize(games):
    by_seed = {}
    for g in games:
        by_seed.setdefault(g["seed"], []).append(g)
    pairs = [v for v in by_seed.values() if len(v) == 2]

    def score_b(g):
        return 1.0 if g["scoreB"] > g["scoreA"] else 0.5 if g["scoreB"] == g["scoreA"] else 0.0

    pair_scores = [sum(score_b(g) for g in p) / 2 for p in pairs]
    pair_spreads = [sum(g["scoreB"] - g["scoreA"] for g in p) / 2 for p in pairs]
    score, score_hw = mean_and_halfwidth(pair_scores)
    spread, spread_hw = mean_and_halfwidth(pair_spreads)

    complete = [g for p in pairs for g in p]
    moves_a = sum(g["moves"] for g in complete) / 2 or 1
    lines = [
        f"pairs {len(pairs)} ({len(complete)} games)"
        f"  B wins {sum(g['scoreB'] > g['scoreA'] for g in complete)}"
        f"  A wins {sum(g['scoreA'] > g['scoreB'] for g in complete)}"
        f"  ties {sum(g['scoreA'] == g['scoreB'] for g in complete)}",
        f"B score     {100 * score:6.2f}% +- {100 * score_hw:.2f}"
        f"   (Elo {elo(score):+.0f}, range {elo(max(score - score_hw, 1e-9)):+.0f}"
        f" .. {elo(min(score + score_hw, 1 - 1e-9)):+.0f})",
        f"B spread    {spread:+7.2f} +- {spread_hw:.2f} points per game",
        f"time/move   A {sum(g['secondsA'] for g in complete) / moves_a:.2f}s"
        f"   B {sum(g['secondsB'] for g in complete) / moves_a:.2f}s",
    ]
    total = sum(g["mid"] + g["pre"] + g["end"] for g in complete) or 1
    lines.append("time share  " + "   ".join(
        f"{name} {100 * sum(g[key] for g in complete) / total:.0f}%"
        for name, key in (("midgame", "mid"), ("preendgame", "pre"), ("endgame", "end"))))
    resyncs = sum(g["resyncs"] for g in complete)
    if resyncs:
        lines.append(f"warning: {resyncs} tile-sequence resyncs (pairs drifted apart)")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", help="libquackle_test, or a build dir holding test/libquackle_test")
    ap.add_argument("--pairs", type=int, default=100)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="processes in parallel")
    ap.add_argument("--threads", type=int, default=1, help="sim threads per process")
    ap.add_argument("--seed", type=int, default=1, help="seed of the first pair")
    ap.add_argument("--lexicon", default="ods5")
    ap.add_argument("--alphabet", default="french")
    ap.add_argument("--computer", default="Static Player")
    ap.add_argument("--computer2", default="Championship Player")
    ap.add_argument("--config")
    ap.add_argument("--config2")
    ap.add_argument("--out", help="CSV file for per-game results")
    ap.add_argument("--start", type=int, help="Static Player plays both seats until at most START tiles are in the bag")
    args = ap.parse_args()

    binary = rr.resolve_binary(args.bin)
    if not binary:
        sys.exit("libquackle_test not found; pass --bin")

    work_root = rr.setup_work_root()
    run_dir = work_root / "run"
    gaddag = rr.build_gaddag(binary, args.lexicon, args.alphabet, work_root)
    if gaddag:
        (run_dir / "lexica").mkdir(exist_ok=True)
        rr.link_file(gaddag, run_dir / "lexica" / f"{args.lexicon}.gaddag")

    jobs = max(1, min(args.jobs, args.pairs))
    base, extra = divmod(args.pairs, jobs)
    procs = []
    first = args.seed
    for j in range(jobs):
        count = base + (j < extra)
        cmd = [str(binary), "--mode=arena", f"--lexicon={args.lexicon}", f"--alphabet={args.alphabet}",
               f"--seed={first}", f"--repetitions={count}", f"--threads={args.threads}",
               f"--computer={args.computer}", f"--computer2={args.computer2}"]
        if args.config:
            cmd.append(f"--config={args.config}")
        if args.config2:
            cmd.append(f"--config2={args.config2}")
        if args.start is not None:
            cmd.append(f"--arenastart={args.start}")
        log = work_root / f"job{j}.log"
        procs.append((subprocess.Popen(cmd, cwd=run_dir, stdout=open(log, "w"), stderr=subprocess.STDOUT,
                                       env=rr.child_env(binary)), log))
        first += count

    print(f"arena: {args.pairs} pairs, {jobs} processes, {args.lexicon}/{args.alphabet}")
    print(f"  A = {args.computer} [{args.config or 'defaults'}]")
    print(f"  B = {args.computer2} [{args.config2 or 'defaults'}]")
    if args.start is not None:
        print(f"  from the first position with at most {args.start} tiles in the bag, reached by Static Player")
    print(f"  logs in {work_root}")

    start = time.time()
    done = -1
    while True:
        running = sum(p.poll() is None for p, _ in procs)
        games = [g for _, log in procs for g in parse_games(log.read_text(errors="replace"))]
        if len(games) != done:
            done = len(games)
            print(f"[{time.time() - start:6.0f}s] {done}/{2 * args.pairs} games")
        if not running:
            break
        time.sleep(5)

    failed = [(p.returncode, log) for p, log in procs if p.returncode]
    for code, log in failed:
        print(f"job exited with {code}; see {log}")

    if args.out:
        with open(args.out, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=FIELDS)
            w.writeheader()
            w.writerows(sorted(games, key=lambda g: (g["seed"], g["first"])))

    print(summarize(games))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
