/*
 *  Quackle -- Crossword game artificial intelligence and analysis tool
 *  Copyright (C) 2005-2019 Jason Katz-Brown, John O'Laughlin, and John Fultz.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QUACKLE_EVALUATOR_H
#define QUACKLE_EVALUATOR_H

#include "alphabetparameters.h"

namespace Quackle
{

class Board;
class GamePosition;
class Move;
class StrategyParameters;

// Measures of how open a board is: empty squares next to a tile, by premium (triple word, double word,
// triple letter, double letter, none), then triple and double word squares that an empty line links to a
// tile within reach of one rack.
enum BoardFeature {
	AnchorTripleWord,
	AnchorDoubleWord,
	AnchorTripleLetter,
	AnchorDoubleLetter,
	AnchorPlain,
	ReachTripleWord,
	ReachDoubleWord,
	BoardFeatureCount
};

const char *boardFeatureName(int feature);

// The board's measures.
void boardFeatures(const Board &board, double features[BoardFeatureCount]);

// How much the move changes them; zero for anything but a placement.
void boardFeatureChanges(const Board &board, const Move &move, double changes[BoardFeatureCount]);

// The position's strategy's weights times the move's changes; zero without weights, or with the bag or the board empty.
double boardTerm(const GamePosition &position, const Move &move);

// Bound boardTerm for a placement: a measure falls only on squares where a tile lands, so the falls are bounded per
// square, and the rises per number of tiles placed.
double boardTermLandingBound(const Board &board, const StrategyParameters &strategy, int row, int col);
double boardTermRiseBound(const StrategyParameters &strategy, int placed);

class Evaluator
{
public:
	virtual ~Evaluator() {};

	// Default implementations of these return zero for everything but
	// equity, which returns the score of the move.

	// Return equity of move based on static evaluation
	// suitable for equity field of move. Rack must be alphabetized.
	virtual double equity(const GamePosition &position, const Move &move) const;

	virtual double playerConsideration(const GamePosition &position, const Move &move) const;
	virtual double sharedConsideration(const GamePosition &position, const Move &move) const;

	virtual double leaveValue(const LetterString &leave, const StrategyParameters &strategy) const;
};

class ScorePlusLeaveEvaluator : public Evaluator
{
public:
	virtual ~ScorePlusLeaveEvaluator() {};

	// Evaluator that always returns a score+leave equity
	virtual double equity(const GamePosition &position, const Move &move) const;

	virtual double playerConsideration(const GamePosition &position, const Move &move) const;
	virtual double sharedConsideration(const GamePosition &position, const Move &move) const;

	virtual double leaveValue(const LetterString &leave, const StrategyParameters &strategy) const;
};

}

#endif
