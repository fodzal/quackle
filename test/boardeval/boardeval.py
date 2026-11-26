#!/usr/bin/env python3
"""Learn board-measure weights for the static equity from self-play data.

    boardeval.py --data ../selfplay/board0 --out ../boardeval/opp

The data must come from a harness whose --movelog has the d_* and b_* columns
(test/selfplay/selfplay_data.py). For every move followed by an opponent's move
made before the bag is empty, a least-squares fit explains what the opponent
scores with how our move changed the board measures (d_*), controlling for the
measures before our move (b_*), the tiles in the bag and our move's score. A
weight is minus the points one more unit of a measure gives the opponent.

With --target net, what we score on our own next move is subtracted from the
opponent's score: opening the board helps both players.

Writes <out>/boardeval, usable as a player's strategy=<directory>, and
<out>/report.txt, which also compares, on held-out games, the fit with and
without the d_* measures.
"""

import argparse
import csv
import gzip
import sys
import zlib
from pathlib import Path

import numpy as np

HOLDOUT = 10
BAG_BUCKETS = 10


def rows_by_game(path):
    with gzip.open(path, "rt", newline="") as f:
        game_key, game = None, []
        for row in csv.DictReader(f):
            key = (row["seed"], row["game"])
            if key != game_key:
                if game:
                    yield game
                game_key, game = key, []
            game.append(row)
        if game:
            yield game


def samples(paths, target):
    """Yields (held-out?, d measures, controls, target) per usable move."""
    for path in paths:
        for game in rows_by_game(path):
            heldout = zlib.crc32(f"{game[0]['seed']},{game[0]['game']}".encode()) % HOLDOUT == 0
            by_turn = {int(r["turn"]): r for r in game}
            d_names = [k for k in game[0] if k.startswith("d_")]
            b_names = [k for k in game[0] if k.startswith("b_")]
            for turn, row in by_turn.items():
                if turn == 0 or int(row["bag"]) == 0:
                    continue
                reply = by_turn.get(turn + 1)
                if reply is None or int(reply["bag"]) == 0:
                    continue
                y = float(reply["score"])
                if target == "net":
                    ours = by_turn.get(turn + 2)
                    if ours is None or int(ours["bag"]) == 0:
                        continue
                    y -= float(ours["score"])
                bucket = [0.0] * BAG_BUCKETS
                bucket[min(int(row["bag"]) * BAG_BUCKETS // 100, BAG_BUCKETS - 1)] = 1.0
                controls = [float(row[k]) for k in b_names] + bucket + [float(row["score"])]
                yield heldout, [float(row[k]) for k in d_names], controls, y, d_names


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", required=True, nargs="+", help="directories of chunk-*.csv.gz")
    ap.add_argument("--out", required=True)
    ap.add_argument("--target", choices=["opponent", "net"], default="opponent")
    args = ap.parse_args()

    paths = sorted(p for d in args.data for p in Path(d).glob("chunk-*.csv.gz"))
    if not paths:
        sys.exit("no chunk-*.csv.gz in " + " ".join(args.data))

    xtx = xty = None
    yy = 0.0
    n = 0
    heldout = []
    names = None
    batch_x, batch_y = [], []

    def flush():
        nonlocal xtx, xty, yy, n
        if not batch_x:
            return
        x, y = np.array(batch_x), np.array(batch_y)
        if xtx is None:
            xtx = np.zeros((x.shape[1], x.shape[1]))
            xty = np.zeros(x.shape[1])
        xtx += x.T @ x
        xty += x.T @ y
        yy += float(y @ y)
        n += len(y)
        batch_x.clear()
        batch_y.clear()

    for is_heldout, d, controls, y, d_names in samples(paths, args.target):
        if names is None:
            if not d_names:
                sys.exit("the data has no d_* columns; collect it with a harness that logs board measures")
            names = [name[2:] for name in d_names]
        if is_heldout:
            heldout.append((d + controls, y))
            continue
        batch_x.append(d + controls)
        batch_y.append(y)
        if len(batch_x) >= 50000:
            flush()
    flush()

    k = len(names)
    beta = np.linalg.lstsq(xtx, xty, rcond=None)[0]
    controls_only = np.linalg.lstsq(xtx[k:, k:], xty[k:], rcond=None)[0]
    residual = (yy - 2 * beta @ xty + beta @ xtx @ beta) / (n - len(beta))
    errors = np.sqrt(np.maximum(np.diag(np.linalg.pinv(xtx)) * residual, 0))

    hx = np.array([x for x, _ in heldout])
    hy = np.array([y for _, y in heldout])
    mse_full = float(np.mean((hy - hx @ beta) ** 2))
    mse_controls = float(np.mean((hy - hx[:, k:] @ controls_only) ** 2))

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "boardeval").write_text("".join(f"{name} {-beta[i]:.4f}\n" for i, name in enumerate(names)))

    report = [
        f"data: {' '.join(str(Path(d).resolve()) for d in args.data)} ({len(paths)} chunks), target {args.target}",
        f"training moves {n}, held-out moves {len(heldout)}",
        f"held-out mean squared error: controls only {mse_controls:.3f}, with the board changes {mse_full:.3f}",
        "points to the target per unit of change (standard error), and the weight written:",
    ]
    report += [f"  {name:14s} {beta[i]:+7.3f} ({errors[i]:.3f})  weight {-beta[i]:+.3f}" for i, name in enumerate(names)]
    text = "".join(line + "\n" for line in report)
    (out / "report.txt").write_text(text, encoding="utf-8")
    print(text, end="")


if __name__ == "__main__":
    main()
