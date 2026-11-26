# Self-play data

Self-play games, recorded move by move, to learn strategy tables from (leaves first).

```
cmake --build build-release --target libquackle_test makegaddag
python3 test/selfplay/selfplay_data.py --bin build-release --games 500000 \
    --out ../quackle-data/selfplay/gen0
```

- `libquackle_test --mode=selfplay --movelog=<file>` appends one csv row per move of every
  finished game; `libquackle_test --help` lists the columns. `leave` and `value` are the
  tiles kept and their value in the mover's own tables, so a target can be built from a
  player's next move without reloading those tables.
- `selfplay_data.py` plays the games in chunks of `--chunk` games, one process per core,
  and writes chunk k, played with seed `--seed + k`, to `<out>/chunk-<seed>.csv.gz`. A
  chunk already there is skipped: rerunning the same command resumes an interrupted run,
  and a larger `--games` extends it.
- `--config` applies to both players, e.g. `strategy=<directory>` to play with a learned
  table. Default lexicon is `ods5`/`french`, default player Static Player.
- Keep the data out of the repository. On this laptop (8 cores, Release) Static Player
  plays 6 000 to 8 000 games a minute, depending on how hot it runs, and writes about
  0.6 MB of compressed data per thousand games.
