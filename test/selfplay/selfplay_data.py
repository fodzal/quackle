#!/usr/bin/env python3
"""Collect self-play data: one csv row per move, for learning strategy tables.

Runs `libquackle_test --mode=selfplay --movelog=...` in several processes at
once. Games come in chunks of --chunk games; chunk k is played with seed
--seed + k and written to <out>/chunk-<seed>.csv.gz. Chunks already in <out>
are skipped, so an interrupted collection resumes where it stopped.

    selfplay_data.py --bin build-release --games 200000 --out ../selfplay/gen0
    selfplay_data.py --bin build-release --games 200000 --out ../selfplay/gen1 \\
        --config strategy=../leaves/gen1

--config sets both players; they are the same --computer (Static Player by
default). The columns are listed by `libquackle_test --help` under --movelog.
Keep the data out of the repository: a few hundred thousand games take
hundreds of megabytes even compressed.
"""

import argparse
import gzip
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "regression"))
sys.path.insert(0, str(HERE.parent / "arena"))
import run_regression as rr  # noqa: E402
from arena import absolute_strategies  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", help="libquackle_test, or a build dir holding test/libquackle_test")
    ap.add_argument("--games", type=int, required=True)
    ap.add_argument("--chunk", type=int, default=2000, help="games per process run")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="processes in parallel")
    ap.add_argument("--seed", type=int, default=1000000, help="seed of the first chunk")
    ap.add_argument("--lexicon", default="ods5")
    ap.add_argument("--alphabet", default="french")
    ap.add_argument("--computer", default="Static Player")
    ap.add_argument("--config", help="settings of both players, e.g. strategy=<directory>")
    ap.add_argument("--out", required=True, help="directory for the chunks")
    args = ap.parse_args()

    config = absolute_strategies(args.config)
    binary = rr.resolve_binary(args.bin)
    if not binary:
        sys.exit("libquackle_test not found; pass --bin")

    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    chunks = [(args.seed + k, min(args.chunk, args.games - k * args.chunk))
              for k in range((args.games + args.chunk - 1) // args.chunk)]
    todo = [(seed, games) for seed, games in chunks if not (out / f"chunk-{seed}.csv.gz").exists()]

    work_root = rr.setup_work_root()
    run_dir = work_root / "run"
    gaddag = rr.build_gaddag(binary, args.lexicon, args.alphabet, work_root)
    if gaddag:
        (run_dir / "lexica").mkdir(exist_ok=True)
        rr.link_file(gaddag, run_dir / "lexica" / f"{args.lexicon}.gaddag")

    print(f"selfplay data: {args.games} games of {args.computer} [{config or 'defaults'}],"
          f" {args.lexicon}/{args.alphabet}, into {out}")
    print(f"  {len(chunks) - len(todo)} of {len(chunks)} chunks already there; logs in {work_root}", flush=True)

    def launch(seed, games):
        raw = work_root / f"chunk-{seed}.csv"
        raw.unlink(missing_ok=True)
        cmd = [str(binary), "--mode=selfplay", f"--lexicon={args.lexicon}", f"--alphabet={args.alphabet}",
               f"--seed={seed}", f"--repetitions={games}", "--quiet", f"--movelog={raw}",
               f"--computer={args.computer}", f"--computer2={args.computer}"]
        if config:
            cmd += [f"--config={config}", f"--config2={config}"]
        log = open(work_root / f"chunk-{seed}.log", "w")
        return subprocess.Popen(cmd, cwd=run_dir, stdout=log, stderr=subprocess.STDOUT, env=rr.child_env(binary)), raw

    start = time.time()
    running = {}
    finished = failed = 0
    while todo or running:
        while todo and len(running) < args.jobs:
            seed, games = todo.pop(0)
            running[seed] = launch(seed, games)
        time.sleep(1)
        for seed, (proc, raw) in list(running.items()):
            if proc.poll() is None:
                continue
            del running[seed]
            if proc.returncode:
                failed += 1
                print(f"chunk {seed} exited with {proc.returncode}; see {work_root / f'chunk-{seed}.log'}", flush=True)
                continue
            partial = out / f"chunk-{seed}.csv.gz.part"
            with open(raw, "rb") as src, gzip.open(partial, "wb", compresslevel=6) as dst:
                shutil.copyfileobj(src, dst)
            partial.rename(out / f"chunk-{seed}.csv.gz")
            raw.unlink()
            finished += 1
            elapsed = time.time() - start
            print(f"[{elapsed:6.0f}s] chunk {seed} done; {finished} this run,"
                  f" {finished * args.chunk / elapsed * 60:.0f} games per minute", flush=True)

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
