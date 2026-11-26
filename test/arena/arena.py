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


def absolute_strategies(config):
    """The harness runs in a scratch directory, so strategy=<dir> must not stay relative."""
    if not config:
        return config
    items = []
    for item in config.split(","):
        key, sep, value = item.partition("=")
        if key.strip() == "strategy":
            path = Path(value.strip()).resolve()
            if not path.is_dir():
                sys.exit(f"strategy directory {path} not found")
            item = f"strategy={path}"
        items.append(item)
    return ",".join(items)


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


def complete_pairs(games):
    by_seed = {}
    for g in games:
        by_seed.setdefault(g["seed"], []).append(g)
    return [v for v in by_seed.values() if len(v) == 2]


def score_b(g):
    return 1.0 if g["scoreB"] > g["scoreA"] else 0.5 if g["scoreB"] == g["scoreA"] else 0.0


def pair_score(pair):
    return sum(score_b(g) for g in pair) / 2


def expected_score(elo_difference):
    return 1 / (1 + 10 ** (-elo_difference / 400))


class Sprt:
    """Sequential test of H0: B is elo0 stronger than A, against H1: elo1 stronger.

    Each pair's mean score is one sample; the log-likelihood ratio uses the
    normal approximation of the generalized SPRT, with the pairs' own variance.
    """

    MINIMUM_PAIRS = 20

    def __init__(self, elo0, elo1, alpha, beta):
        self.elo0, self.elo1 = elo0, elo1
        self.s0, self.s1 = expected_score(elo0), expected_score(elo1)
        self.lower = math.log(beta / (1 - alpha))
        self.upper = math.log((1 - beta) / alpha)

    def llr(self, scores):
        n = len(scores)
        if n < 2:
            return 0.0
        mean = sum(scores) / n
        variance = max(sum((x - mean) ** 2 for x in scores) / (n - 1), 1e-9)
        return n * (self.s1 - self.s0) * (2 * mean - self.s0 - self.s1) / (2 * variance)

    def decision(self, scores):
        if len(scores) < self.MINIMUM_PAIRS:
            return None
        llr = self.llr(scores)
        if llr >= self.upper:
            return "H1"
        if llr <= self.lower:
            return "H0"
        return None

    def report(self, scores):
        verdict = {"H1": f"H1 accepted: B is at least {self.elo1:+g} Elo stronger",
                   "H0": f"H0 accepted: B is not {self.elo1:+g} Elo stronger (at most {self.elo0:+g})",
                   None: "no decision"}[self.decision(scores)]
        return (f"SPRT        elo0 {self.elo0:+g} elo1 {self.elo1:+g}: LLR {self.llr(scores):+.2f}"
                f" in [{self.lower:.2f}, {self.upper:.2f}] after {len(scores)} pairs; {verdict}")


def summarize(games):
    pairs = complete_pairs(games)
    pair_scores = [pair_score(p) for p in pairs]
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
    ap.add_argument("--sprt", metavar="ELO0,ELO1",
                    help="stop as soon as a sequential test accepts H0 (B is ELO0 stronger) or H1 (ELO1 stronger); "
                         "--pairs is then the most pairs played")
    ap.add_argument("--alpha", type=float, default=0.05, help="SPRT: chance of accepting H1 when H0 holds")
    ap.add_argument("--beta", type=float, default=0.05, help="SPRT: chance of accepting H0 when H1 holds")
    args = ap.parse_args()

    sprt = None
    if args.sprt:
        try:
            elo0, elo1 = (float(x) for x in args.sprt.split(","))
        except ValueError:
            sys.exit("--sprt takes ELO0,ELO1, e.g. 0,10")
        if elo1 <= elo0:
            sys.exit("--sprt needs ELO1 > ELO0")
        sprt = Sprt(elo0, elo1, args.alpha, args.beta)

    args.config = absolute_strategies(args.config)
    args.config2 = absolute_strategies(args.config2)

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
    if sprt:
        print(f"  SPRT elo0 {sprt.elo0:+g} elo1 {sprt.elo1:+g}, alpha {args.alpha}, beta {args.beta}; at most {args.pairs} pairs")
    print(f"  logs in {work_root}")

    start = time.time()
    done = -1
    terminated = set()
    while True:
        running = sum(p.poll() is None for p, _ in procs)
        games = [g for _, log in procs for g in parse_games(log.read_text(errors="replace"))]
        if len(games) != done:
            done = len(games)
            line = f"[{time.time() - start:6.0f}s] {done}/{2 * args.pairs} games"
            if sprt:
                line += f", LLR {sprt.llr([pair_score(p) for p in complete_pairs(games)]):+.2f}"
            print(line)
        if not running:
            break
        if sprt and sprt.decision([pair_score(p) for p in complete_pairs(games)]):
            for j, (p, _) in enumerate(procs):
                if p.poll() is None:
                    p.terminate()
                    terminated.add(j)
            for p, _ in procs:
                p.wait()
            games = [g for _, log in procs for g in parse_games(log.read_text(errors="replace"))]
            break
        time.sleep(5)

    failed = [(p.returncode, log) for j, (p, log) in enumerate(procs) if p.returncode and j not in terminated]
    for code, log in failed:
        print(f"job exited with {code}; see {log}")

    if args.out:
        with open(args.out, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=FIELDS)
            w.writeheader()
            w.writerows(sorted(games, key=lambda g: (g["seed"], g["first"])))

    print(summarize(games))
    if sprt:
        print(sprt.report([pair_score(p) for p in complete_pairs(games)]))
    elapsed = time.time() - start
    print(f"throughput  {len(games) / 2 / elapsed * 60:.0f} pairs per minute ({elapsed:.0f}s)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
