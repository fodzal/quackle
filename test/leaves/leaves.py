#!/usr/bin/env python3
"""Learn a superleaves table from self-play data (test/selfplay/selfplay_data.py).

    leaves.py --data ../selfplay/gen0 --out ../leaves/gen1

For every move whose player moves again before the bag is empty, the target
is what that player gets on its next move: the score plus the value of the
leave it keeps then, in the tables the game was played with. A leave's value
is its mean target minus the mean target of the empty leave, which the engine
values at 0.

Leaves seen rarely are shrunk towards a linear model of the leave's tiles,
pairs of distinct tiles, duplicates and size, fitted on the same data:
value = (n * mean + k * model) / (n + k). Leaves never seen take the model.
k is chosen on held-out games (one in --holdout), on which the old table's
values are scored too.

Writes <out>/superleaves for every leave of 1 to 6 tiles, and <out>/report.txt.
<out> then works as a player's strategy=<directory>.
"""

import argparse
import csv
import gzip
import sys
import zlib
from pathlib import Path

import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import superleaves as sl  # noqa: E402

REPO = Path(__file__).resolve().parent.parent.parent
SHRINKS = [0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000]
HOLDOUT = 10


def samples(paths, alphabet):
    """Yields (leave key, target, held-out game?, old value) for every usable move."""
    for path in paths:
        with gzip.open(path, "rt", newline="") as f:
            game_key, game = None, []
            for row in csv.DictReader(f):
                key = (row["seed"], row["game"])
                if key != game_key:
                    yield from game_samples(game, alphabet)
                    game_key, game = key, []
                game.append(row)
            yield from game_samples(game, alphabet)


def game_samples(game, alphabet):
    if not game:
        return
    heldout = zlib.crc32(f"{game[0]['seed']},{game[0]['game']}".encode()) % HOLDOUT == 0
    by_player = {}
    for row in game:
        by_player.setdefault(row["player"], []).append(row)
    for rows in by_player.values():
        rows.sort(key=lambda r: int(r["turn"]))
        for row, after in zip(rows, rows[1:]):
            if int(after["bag"]) == 0:
                continue
            target = int(after["score"]) + float(after["value"])
            yield alphabet.key(row["leave"]), target, heldout, float(row["value"])


def features(key, codes):
    """Indices of the leave's features: tile counts, duplicates, distinct pairs, size."""
    counts = {}
    for c in key:
        counts[c] = counts.get(c, 0) + 1
    out = {}
    for c, n in counts.items():
        out[codes[("tile", c)]] = n
        if n >= 2:
            out[codes[("dup2", c)]] = 1
        if n >= 3:
            out[codes[("dup3", c)]] = 1
    distinct = sorted(counts)
    for i, a in enumerate(distinct):
        for b in distinct[i + 1:]:
            out[codes[("pair", a, b)]] = 1
    out[codes[("size", len(key))]] = 1
    return out


def feature_codes(alphabet):
    letters = sorted(alphabet.count)
    names = [("tile", c) for c in letters] + [("dup2", c) for c in letters] + [("dup3", c) for c in letters]
    names += [("pair", a, b) for i, a in enumerate(letters) for b in letters[i + 1:]]
    names += [("size", n) for n in range(0, 8)]
    return {name: i for i, name in enumerate(names)}


def design(keys, codes):
    x = np.zeros((len(keys), len(codes)))
    for row, key in enumerate(keys):
        for j, v in features(key, codes).items():
            x[row, j] = v
    return x


def fit_model(stats, codes, ridge):
    """Weighted least squares of each leave's mean target on its features, weighted by its count."""
    keys = list(stats)
    f = len(codes)
    xtx = np.zeros((f, f))
    xty = np.zeros(f)
    for start in range(0, len(keys), 20000):
        batch = keys[start:start + 20000]
        x = design(batch, codes)
        w = np.array([stats[k][0] for k in batch], dtype=float)
        y = np.array([stats[k][1] / stats[k][0] for k in batch])
        xtx += x.T @ (x * w[:, None])
        xty += x.T @ (w * y)
    return np.linalg.solve(xtx + ridge * np.eye(f), xty)


def predict(keys, codes, beta):
    out = np.empty(len(keys))
    for start in range(0, len(keys), 50000):
        batch = keys[start:start + 50000]
        out[start:start + len(batch)] = design(batch, codes) @ beta
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", required=True, nargs="+", help="directories of chunk-*.csv.gz")
    ap.add_argument("--out", required=True)
    ap.add_argument("--alphabet", default="french")
    ap.add_argument("--ridge", type=float, default=1.0, help="ridge penalty of the smoothing model")
    ap.add_argument("--max-size", type=int, default=6)
    args = ap.parse_args()

    alphabet = sl.Alphabet(REPO / "data" / "alphabets" / f"{args.alphabet}.quackle_alphabet")
    paths = sorted(p for d in args.data for p in Path(d).glob("chunk-*.csv.gz"))
    if not paths:
        sys.exit("no chunk-*.csv.gz in " + " ".join(args.data))

    # Per leave: count, sum and sum of squares of targets on training games.
    stats = {}
    heldout = []
    for key, target, is_heldout, old in samples(paths, alphabet):
        if is_heldout:
            heldout.append((key, target, old))
            continue
        s = stats.get(key)
        if s is None:
            stats[key] = [1, target, target * target]
        else:
            s[0] += 1
            s[1] += target
            s[2] += target * target
    train = sum(s[0] for s in stats.values())
    if () not in stats:
        sys.exit("no move kept an empty leave; not enough data")
    anchor = stats[()][1] / stats[()][0]

    # Targets relative to the empty leave from here on.
    for s in stats.values():
        s[2] += -2 * anchor * s[1] + s[0] * anchor * anchor
        s[1] -= s[0] * anchor

    codes = feature_codes(alphabet)
    beta = fit_model(stats, codes, args.ridge)

    seen = list(stats)
    model_seen = dict(zip(seen, predict(seen, codes, beta)))

    def shrunk(key, k, model_value):
        s = stats.get(key)
        if s is None:
            return model_value
        return (s[1] + k * model_value) / (s[0] + k)

    heldout_keys = sorted({key for key, _, _ in heldout})
    model_heldout = dict(zip(heldout_keys, predict(heldout_keys, codes, beta)))
    targets = np.array([t - anchor for _, t, _ in heldout])

    def mse(predictions):
        return float(np.mean((targets - np.array(predictions)) ** 2))

    scores = {
        "zero": mse([0.0] * len(heldout)),
        "old table": mse([old for _, _, old in heldout]),
        "model only": mse([model_heldout[key] for key, _, _ in heldout]),
    }
    shrink_scores = {k: mse([shrunk(key, k, model_heldout[key]) for key, _, _ in heldout]) for k in SHRINKS}
    best_k = min(shrink_scores, key=shrink_scores.get)

    leaves = alphabet.all_leaves(args.max_size)
    model_all = predict(leaves, codes, beta)
    values = [shrunk(key, best_k, m) for key, m in zip(leaves, model_all)]

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    sl.write(out / "superleaves", zip(leaves, values))

    by_size = {}
    for key, v in zip(leaves, values):
        by_size.setdefault(len(key), []).append(v)
    best_single = sorted(((v, alphabet.leave_text(k)) for k, v in zip(leaves, values) if len(k) == 1), reverse=True)

    report = [
        f"data: {' '.join(str(Path(d).resolve()) for d in args.data)} ({len(paths)} chunks)",
        f"training samples {train}, distinct leaves seen {len(stats)} of {len(leaves)}; held-out samples {len(heldout)}",
        f"empty leave: next-move target {anchor:.3f} over {stats[()][0]} samples",
        f"held-out mean squared error, target relative to the empty leave (variance {float(np.var(targets)):.2f}):",
    ]
    report += [f"  {name:12s} {value:9.3f}" for name, value in scores.items()]
    report += [f"  shrink k={k:<6g} {v:9.3f}{'  <- chosen' if k == best_k else ''}" for k, v in shrink_scores.items()]
    report.append("mean value by leave size: " + ", ".join(
        f"{n}: {sum(v) / len(v):+.2f}" for n, v in sorted(by_size.items())))
    report.append("single tiles: " + " ".join(f"{t} {v:+.1f}" for v, t in best_single))
    (out / "report.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n".join(report))


if __name__ == "__main__":
    main()
