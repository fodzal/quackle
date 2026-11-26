# Learned leaves

Learns a `superleaves` table from self-play data (`test/selfplay/`), then iterates.

```
python3 test/selfplay/selfplay_data.py --bin build-release --games 500000 --out ../quackle-data/selfplay/gen0
python3 test/leaves/leaves.py --data ../quackle-data/selfplay/gen0 --out ../quackle-data/leaves/gen1
python3 test/arena/arena.py --bin build-release --pairs 100000 --sprt 0,10 \
    --computer "Static Player" --computer2 "Static Player" --config2 strategy=../quackle-data/leaves/gen1
```

Generation k+1 is learned from games played with generation k (`--config
strategy=../quackle-data/leaves/genk` in `selfplay_data.py`); generation 0 is the
repository's table. Stop when two successive generations are neutral in the static arena.

## Method

- For each move whose player moves again before the bag is empty, the target is what that
  player gets on that next move: its score plus the value, in the tables the game was
  played with, of the leave it keeps then.
- A leave's value is its mean target minus the mean target of the empty leave, since the
  engine values the empty leave at 0.
- Leaves seen rarely are shrunk towards a linear model fitted on the same data (counts of
  each tile, duplicates, pairs of distinct tiles, leave size):
  `value = (n * mean + k * model) / (n + k)`. Leaves never seen take the model. The model
  stands in for the hand-tuned fallback (`worths`, `syn2`, vowel/consonant table), which
  is no longer reached since every leave of 1 to 6 tiles gets a value.
- `k` is chosen on held-out games (one in ten, picked by a hash of the game). The report
  also scores the old table on them, which is the first, offline, test of a new table.

`superleaves.py` reads and writes the binary format directly; writing the repository's
table back reproduces it byte for byte.
