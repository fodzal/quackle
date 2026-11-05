#!/usr/bin/env python3
"""Compare two arena.py --out CSV files played on the same seeds and players A.

With timelimit=0 everywhere a game replays exactly from its seed, so where
the two runs differ only in player B (or in data files), the per-pair
difference of B's score is caused by that change alone. That paired
difference has far less variance than either run's score.

    compare.py before.csv after.csv
"""

import csv
import math
import sys


def pair_scores(path):
    pairs = {}
    with open(path, newline="") as f:
        for g in csv.DictReader(f):
            a, b = int(g["scoreA"]), int(g["scoreB"])
            s = pairs.setdefault(int(g["seed"]), [0.0, 0.0, 0])
            s[0] += (1.0 if b > a else 0.5 if b == a else 0.0) / 2
            s[1] += (b - a) / 2
            s[2] += 1
    return {seed: s for seed, s in pairs.items() if s[2] == 2}


def mean_hw(values):
    n = len(values)
    mean = sum(values) / n
    var = sum((v - mean) ** 2 for v in values) / (n - 1) if n > 1 else 0
    return mean, 1.96 * math.sqrt(var / n)


def main():
    before, after = pair_scores(sys.argv[1]), pair_scores(sys.argv[2])
    seeds = sorted(set(before) & set(after))
    if len(seeds) < 2:
        sys.exit("fewer than two pairs in common")
    for name, i in (("score", 0), ("spread", 1)):
        b = [before[s][i] for s in seeds]
        a = [after[s][i] for s in seeds]
        d = [x - y for x, y in zip(a, b)]
        mb, ma, (md, hw) = sum(b) / len(b), sum(a) / len(a), mean_hw(d)
        scale = 100 if i == 0 else 1
        unit = "%" if i == 0 else " pts"
        print(f"B {name:6s} before {scale * mb:7.2f}{unit}  after {scale * ma:7.2f}{unit}"
              f"  change {scale * md:+7.2f} +- {scale * hw:.2f}{unit}")
    changed = sum(before[s][:2] != after[s][:2] for s in seeds)
    print(f"{len(seeds)} pairs in common, {changed} with a different outcome")


if __name__ == "__main__":
    main()
