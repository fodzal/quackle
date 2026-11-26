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

#include "board.h"
#include "boardparameters.h"
#include "datamanager.h"
#include "game.h"
#include "evaluator.h"
#include "strategyparameters.h"
#include "catchall.h"

#include <vector>

using namespace Quackle;

namespace
{

// Leave values of one rack, indexed by the multiset of tiles a move uses
// (mixed radix over the rack's distinct letters), filled on demand.
struct LeaveCache
{
	const ScorePlusLeaveEvaluator *evaluator = nullptr;
	const StrategyParameters *strategy = nullptr;
	unsigned int dataVersion = 0;
	LetterString rack;
	int distinct = 0;
	Letter letters[LETTER_STRING_MAXIMUM_LENGTH];
	int counts[LETTER_STRING_MAXIMUM_LENGTH];
	int weights[LETTER_STRING_MAXIMUM_LENGTH];
	int fullIndex = 0;
	std::vector<double> values;
	std::vector<bool> known;

	bool reset(const ScorePlusLeaveEvaluator *newEvaluator, const StrategyParameters *newStrategy, unsigned int newDataVersion,
		const LetterString &newRack)
	{
		const int maximumSubsets = 4096;

		evaluator = newEvaluator;
		strategy = newStrategy;
		dataVersion = newDataVersion;
		rack = newRack;
		distinct = 0;
		for (const Letter letter : rack)
		{
			int i = 0;
			while (i < distinct && letters[i] != letter)
				++i;
			if (i == distinct)
			{
				letters[distinct] = letter;
				counts[distinct++] = 0;
			}
			++counts[i];
		}

		int subsets = 1;
		fullIndex = 0;
		for (int i = 0; i < distinct; ++i)
		{
			weights[i] = subsets;
			fullIndex += counts[i] * subsets;
			subsets *= counts[i] + 1;
			if (subsets > maximumSubsets)
			{
				evaluator = nullptr;
				return false;
			}
		}

		values.assign(subsets, 0);
		known.assign(subsets, false);
		return true;
	}

	// -1 when the move uses tiles the rack does not hold.
	int leaveIndex(const Move &move) const
	{
		int index = fullIndex;
		if (move.isChallengedPhoney() || move.action == Move::BlindExchange)
			return index;

		int used[LETTER_STRING_MAXIMUM_LENGTH] = {};
		for (const Letter tile : move.tiles())
		{
			Letter letter;
			if (tile == QUACKLE_BLANK_MARK || QUACKLE_ALPHABET_PARAMETERS->isBlankLetter(tile))
				letter = QUACKLE_BLANK_MARK;
			else if (QUACKLE_ALPHABET_PARAMETERS->isPlainLetter(tile))
				letter = tile;
			else
				continue;

			int i = 0;
			while (i < distinct && letters[i] != letter)
				++i;
			if (i == distinct || ++used[i] > counts[i])
				return -1;
			index -= weights[i];
		}
		return index;
	}
};

thread_local LeaveCache leaveCache;

}

namespace
{

const int reach = 7;

// The new tiles of a placement, read over the board.
struct Overlay
{
	const Board &board;
	const Move *move;
	int length = 0;

	Overlay(const Board &board, const Move *move)
		: board(board), move(move)
	{
		if (move)
			length = (int)move->tiles().length();
	}

	bool isNew(int row, int col) const
	{
		if (!move)
			return false;
		const int along = move->horizontal ? col - move->startcol : row - move->startrow;
		const int across = move->horizontal ? row - move->startrow : col - move->startcol;
		return across == 0 && along >= 0 && along < length && !Move::isAlreadyOnBoard(move->tiles()[along]);
	}

	bool onBoard(int row, int col) const { return row >= 0 && col >= 0 && row < board.height() && col < board.width(); }
	bool occupied(int row, int col) const { return board.letter(row, col) != QUACKLE_NULL_MARK || isNew(row, col); }

	bool isAnchor(int row, int col) const
	{
		if (occupied(row, col))
			return false;
		static const int steps[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
		for (const auto &step : steps)
		{
			const int r = row + step[0], c = col + step[1];
			if (onBoard(r, c) && occupied(r, c))
				return true;
		}
		return false;
	}

	bool isReached(int row, int col) const
	{
		if (occupied(row, col))
			return false;
		static const int steps[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
		for (const auto &step : steps)
			for (int k = 1; k <= reach; ++k)
			{
				const int r = row + k * step[0], c = col + k * step[1];
				if (!onBoard(r, c))
					break;
				if (occupied(r, c))
					return true;
			}
		return false;
	}
};

int anchorFeature(int row, int col)
{
	const int word = QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
	const int letter = QUACKLE_BOARD_PARAMETERS->letterMultiplier(row, col);
	if (word >= 3)
		return AnchorTripleWord;
	if (word == 2)
		return AnchorDoubleWord;
	if (letter >= 3)
		return AnchorTripleLetter;
	if (letter == 2)
		return AnchorDoubleLetter;
	return AnchorPlain;
}

int reachFeature(int row, int col)
{
	const int word = QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
	if (word >= 3)
		return ReachTripleWord;
	if (word == 2)
		return ReachDoubleWord;
	return -1;
}

struct ReachSquare
{
	int row, col, feature;
};

// Board parameters can change between games, so the list is rebuilt when the board does.
const std::vector<ReachSquare> &reachSquares()
{
	thread_local const BoardParameters *parameters = nullptr;
	thread_local std::vector<ReachSquare> squares;
	if (parameters != QUACKLE_BOARD_PARAMETERS)
	{
		parameters = QUACKLE_BOARD_PARAMETERS;
		squares.clear();
		for (int row = 0; row < parameters->height(); ++row)
			for (int col = 0; col < parameters->width(); ++col)
			{
				const int feature = reachFeature(row, col);
				if (feature >= 0)
					squares.push_back({ row, col, feature });
			}
	}
	return squares;
}

void measure(const Overlay &overlay, double features[BoardFeatureCount])
{
	for (int i = 0; i < BoardFeatureCount; ++i)
		features[i] = 0;
	for (int row = 0; row < overlay.board.height(); ++row)
		for (int col = 0; col < overlay.board.width(); ++col)
		{
			if (overlay.isAnchor(row, col))
				++features[anchorFeature(row, col)];
			const int feature = reachFeature(row, col);
			if (feature >= 0 && overlay.isReached(row, col))
				++features[feature];
		}
}

}

const char *Quackle::boardFeatureName(int feature)
{
	static const char *names[BoardFeatureCount]
		= { "anchor_tw", "anchor_dw", "anchor_tl", "anchor_dl", "anchor_plain", "reach_tw", "reach_dw" };
	return feature >= 0 && feature < BoardFeatureCount ? names[feature] : "";
}

void Quackle::boardFeatures(const Board &board, double features[BoardFeatureCount])
{
	measure(Overlay(board, nullptr), features);
}

void Quackle::boardFeatureChanges(const Board &board, const Move &move, double changes[BoardFeatureCount])
{
	for (int i = 0; i < BoardFeatureCount; ++i)
		changes[i] = 0;
	if (move.action != Move::Place || move.tiles().empty())
		return;

	const Overlay before(board, nullptr);
	const Overlay after(board, &move);
	const int length = (int)move.tiles().length();
	const int rowStep = move.horizontal ? 0 : 1;
	const int colStep = move.horizontal ? 1 : 0;

	// Anchors change only on the new tiles and their neighbours.
	const int firstRow = move.startrow - 1, lastRow = move.startrow + rowStep * (length - 1) + 1;
	const int firstCol = move.startcol - 1, lastCol = move.startcol + colStep * (length - 1) + 1;
	for (int row = firstRow; row <= lastRow; ++row)
		for (int col = firstCol; col <= lastCol; ++col)
		{
			if (!before.onBoard(row, col))
				continue;
			// Diagonal corners of the box touch no new tile.
			if ((row == firstRow || row == lastRow) && (col == firstCol || col == lastCol))
				continue;
			const int change = (int)after.isAnchor(row, col) - (int)before.isAnchor(row, col);
			if (change)
				changes[anchorFeature(row, col)] += change;
		}

	// Reach changes only for premium squares sharing a line with a new tile.
	for (const ReachSquare &square : reachSquares())
	{
		const int row = square.row, col = square.col;
		const bool sameLine = move.horizontal ? (row == move.startrow || (col >= move.startcol && col < move.startcol + length))
											  : (col == move.startcol || (row >= move.startrow && row < move.startrow + length));
		if (sameLine)
			changes[square.feature] += (int)after.isReached(row, col) - (int)before.isReached(row, col);
	}
}

double Evaluator::equity(const GamePosition &position, const Move &move) const
{
	(void)position;
	return move.effectiveScore();
}

double Evaluator::playerConsideration(const GamePosition &position, const Move &move) const
{
	(void)position;
	(void)move;
	return 0;
}

double Evaluator::sharedConsideration(const GamePosition &position, const Move &move) const
{
	(void)position;
	(void)move;
	return 0;
}

double Evaluator::leaveValue(const LetterString &leave, const StrategyParameters &strategy) const
{
	(void)leave;
	(void)strategy;
	return 0;
}

////////////

double ScorePlusLeaveEvaluator::equity(const GamePosition &position, const Move &move) const
{
	return playerConsideration(position, move) + sharedConsideration(position, move) + move.effectiveScore();
}

double ScorePlusLeaveEvaluator::playerConsideration(const GamePosition &position, const Move &move) const
{
	const Rack &rack = position.currentPlayer().rack();
	const StrategyParameters &strategy = position.strategy();
	const unsigned int dataVersion = QUACKLE_DATAMANAGER->evaluationDataVersion();
	if ((leaveCache.evaluator != this || leaveCache.strategy != &strategy || leaveCache.dataVersion != dataVersion
			|| !(leaveCache.rack == rack.tiles()))
		&& !leaveCache.reset(this, &strategy, dataVersion, rack.tiles()))
		return leaveValue((rack - move).tiles(), strategy);

	const int index = leaveCache.leaveIndex(move);
	if (index < 0)
		return leaveValue((rack - move).tiles(), strategy);

	if (!leaveCache.known[index])
	{
		leaveCache.values[index] = leaveValue((rack - move).tiles(), strategy);
		leaveCache.known[index] = true;
	}
	return leaveCache.values[index];
}

double ScorePlusLeaveEvaluator::sharedConsideration(const GamePosition &position, const Move &move) const
{
	return boardTerm(position, move);
}

double Quackle::boardTerm(const GamePosition &position, const Move &move)
{
	const StrategyParameters &strategy = position.strategy();
	if (!strategy.hasBoardEval() || move.action != Move::Place || position.bag().empty() || position.board().isEmpty())
		return 0;

	double changes[BoardFeatureCount];
	boardFeatureChanges(position.board(), move, changes);
	double term = 0;
	for (int i = 0; i < BoardFeatureCount; ++i)
		term += strategy.boardWeights()[i] * changes[i];
	return term;
}

double Quackle::boardTermLandingBound(const Board &board, const StrategyParameters &strategy, int row, int col)
{
	if (!strategy.hasBoardEval())
		return 0;

	const Overlay overlay(board, nullptr);
	const std::vector<double> &weights = strategy.boardWeights();
	double bound = 0;
	if (overlay.isAnchor(row, col))
		bound += std::max(0.0, -weights[anchorFeature(row, col)]);
	const int reached = reachFeature(row, col);
	if (reached >= 0 && overlay.isReached(row, col))
		bound += std::max(0.0, -weights[reached]);
	return bound;
}

double Quackle::boardTermRiseBound(const StrategyParameters &strategy, int placed)
{
	if (!strategy.hasBoardEval())
		return 0;

	int tripleWords = 0;
	int doubleWords = 0;
	for (int row = 0; row < QUACKLE_BOARD_PARAMETERS->height(); ++row)
		for (int col = 0; col < QUACKLE_BOARD_PARAMETERS->width(); ++col)
		{
			const int word = QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
			tripleWords += word >= 3;
			doubleWords += word == 2;
		}

	double bound = 0;
	for (int i = 0; i < BoardFeatureCount; ++i)
	{
		const double rise = i == ReachTripleWord ? tripleWords : i == ReachDoubleWord ? doubleWords : 2 * placed + 2;
		bound += std::max(0.0, strategy.boardWeights()[i] * rise);
	}
	return bound;
}

double ScorePlusLeaveEvaluator::leaveValue(const LetterString &leave, const StrategyParameters &strategy) const
{
	LetterString alphabetized = String::alphabetize(leave);

	double superleaveValue;
	if (strategy.hasSuperleaves() && strategy.superleave(alphabetized, &superleaveValue))
		return superleaveValue;

	double value = 0;

	if (!leave.empty())
	{
		double synergy = 0;
		LetterString uniqleave;

		if (strategy.hasWorths())
			for (const auto &leaveIt : leave)
				value += strategy.tileWorth(leaveIt);

		if (strategy.hasSyn2())
			for (unsigned int i = 0; i < alphabetized.length() - 1; ++i)
				if (alphabetized[i] == alphabetized[i + 1])
					value += strategy.syn2(alphabetized[i], alphabetized[i]);

		uniqleave += alphabetized[0];
		for (unsigned int i = 1; i < alphabetized.length(); ++i)
			if (uniqleave[uniqleave.length() - 1] != alphabetized[i])
				uniqleave += alphabetized[i];

		if (uniqleave.length() >= 2 && strategy.hasSyn2())
		{
			for (unsigned int i = 0; i < uniqleave.length() - 1; ++i)
				for (unsigned int j = i + 1; j < uniqleave.length(); ++j)
					synergy += strategy.syn2(uniqleave[i], uniqleave[j]);

			// TODO handle the Q

			bool holding_bad_tile = false;
			for (unsigned int i = 0; i < uniqleave.length(); ++i)
			{
				if (strategy.tileWorth(uniqleave[i]) < -5.5)
				{
					holding_bad_tile = true;
				}
			}

			if ((synergy > 3.0) && !holding_bad_tile)
			{
				synergy += 1.5 * (synergy - 3.0);
			}

			value += synergy;
		}
	}

	int vowels = 0;
	int cons = 0;

	for (const auto &leaveIt : leave)
	{
		if (leaveIt != QUACKLE_BLANK_MARK)
		{
			if (QUACKLE_ALPHABET_PARAMETERS->isVowel(leaveIt))
				vowels++;
			else
				cons++;
		}
	}

	const float vcvalues[8][8] = {
		{ 0.0, 0.0, -1.0, -2.5, -5.0, -8.5, -13.5, -18.5 },
		{ -1.0, 0.0, 0.5, 0.0, -1.5, -5.0, -10.0, 0.0 },
		{ -3.5, -1.0, 0.5, 1.5, -1.5, -3.0, 0.0, 0.0 },
		{ -7.0, -3.5, -0.5, 2.5, 0.0, 0.0, 0.0, 0.0 },
		{ -10.0, -6.5, -3.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
		{ -13.5, -11.5, -8.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
		{ -18.5, -16.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
		{ -23.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
	};

#ifdef DEBUG_BOARD
	UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(leave) << " has " << vowels << " vowels, " << cons << " cons.  value of "
		   << vcvalues[vowels][cons] << endl;
#endif

	value += vcvalues[vowels][cons];

	if (value < -40)
		value = -40;

#ifdef DEBUG_BOARD
	UVcout << "leave " << QUACKLE_ALPHABET_PARAMETERS->userVisible(leave) << " worth " << value << " uniq "
		   << QUACKLE_ALPHABET_PARAMETERS->userVisible(uniqleave) << " synergy " << synergy << endl;
#endif

	return value;
}
