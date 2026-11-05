# Arena

Measures whether one computer-player configuration plays better than another.

```
cmake --build build-release --target libquackle_test makegaddag
python3 test/arena/arena.py --bin build-release --pairs 200 \
    --computer "Championship Player" --config  iterations=132,maxiterations=330,timelimit=0 \
    --computer2 "Championship Player" --config2 iterations=264,maxiterations=660,timelimit=0
```

- `libquackle_test --mode=arena` plays `--repetitions` *pairs*. Both games of a pair
  deal the same tile sequence, with the seats swapped, which cancels most of the luck
  of the draw. Pair k uses seed `--seed + k`.
- `arena.py` splits the pairs over `--jobs` processes (default: one per core, each with
  `--threads 1`), pools the games and reports from player B's side: score with a 95 %
  interval, the Elo equivalent, mean spread per game, and time per move. `--out` writes
  one CSV row per game.
- Player A is `--computer`/`--config`, the reference; B is `--computer2`/`--config2`,
  the candidate. `--config` takes the keys listed by `libquackle_test --help`; use
  `timelimit=0` so that iteration budgets, not the clock, bound the search.
- Default lexicon is `ods5`/`french`. The gaddag is built and cached the same way as
  for the regression suites.
- Use a Release build: an ASan or Debug build is several times slower.

The drawing order is imposed only after the player has chosen its move, so a player
never sees the order of the bag.
