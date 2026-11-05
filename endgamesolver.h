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

#ifndef QUACKLE_ENDGAMESOLVER_H
#define QUACKLE_ENDGAMESOLVER_H

#include <cstdint>
#include <unordered_map>

#include "clock.h"
#include "move.h"

namespace Quackle
{

class GamePosition;

// Two-player endgame (empty bag) by alpha-beta on the final spread.
class EndgameSolver
{
public:
	struct Result
	{
		Move move = Move::createNonmove();
		// Spread change for the player on turn, from now to the end of the game.
		int value = 0;
		// False when the value rests on estimates at the depth horizon.
		bool exact = false;
		int depth = 0;
		long long generations = 0;
	};

	// The budget counts move generations, so that a search replays exactly; seconds <= 0 adds no time bound.
	Result solve(const GamePosition &position, long long maxGenerations, int maxSeconds = 0);

	// Value of playing move first, searched within the same budget.
	Result evaluate(const GamePosition &position, const Move &move, long long maxGenerations);

private:
	enum Bound : signed char { Exact, Lower, Upper };

	struct Entry
	{
		int depth;
		int value;
		Bound bound;
		int bestIndex;
	};

	int search(const GamePosition &position, int depth, int alpha, int beta);
	int moveValue(const GamePosition &position, const Move &move, int depth, int alpha, int beta);
	bool outOfBudget();
	void reset(long long maxGenerations, int maxSeconds);

	static uint64_t key(const GamePosition &position);

	std::unordered_map<uint64_t, Entry> m_table;
	long long m_generations = 0;
	long long m_maxGenerations = 0;
	int m_maxSeconds = 0;
	Stopwatch m_stopwatch;
	bool m_aborted = false;
	bool m_horizon = false;
};

}

#endif
