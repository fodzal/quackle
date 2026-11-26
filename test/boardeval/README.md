# Board term

Learns weights for a board term in the static equity: what a move opens for the opponent.

```
python3 test/selfplay/selfplay_data.py --bin build-release --games 200000 --out ../quackle-data/selfplay/board0
python3 test/boardeval/boardeval.py --data ../quackle-data/selfplay/board0 --out ../quackle-data/boardeval/opp
python3 test/arena/arena.py --bin build-release --pairs 20000 \
    --computer "Static Player" --computer2 "Static Player" --config2 strategy=../quackle-data/boardeval/opp
```

- The board measures (`evaluator.h`, `BoardFeature`) count the empty squares next to a tile by
  premium, and the triple and double word squares an empty line links to a tile within
  seven squares. They use the board's geometry and premiums, nothing of the language.
- `boardeval.py` fits, by least squares, what the opponent scores on its next move against
  how our move changed each measure, controlling for the measures before our move, the
  tiles in the bag and our move's score. Each weight is minus the points one more unit
  gives the opponent. `--target net` subtracts what we score on our own next move.
- A strategy directory holding a `boardeval` file (`<measure> <weight>` per line) adds
  the weighted changes to the equity of every placement while the bag is not empty:
  in the candidates, and on both sides of the simulations. Scaling the weights is a
  matter of editing the file.
- `libquackle_test --mode=bestmove --config strategy=<directory>` checks that the shadow
  bounds of the move generator still find the best move with the term in.
