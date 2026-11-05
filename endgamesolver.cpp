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

#include <algorithm>
#include <climits>
#include <numeric>
#include <string>
#include <vector>

#include "endgamesolver.h"
#include "game.h"
#include "datamanager.h"
#include "gameparameters.h"
#include "generator.h"

using namespace Quackle;

namespace
{

const int Infinity = INT_MAX / 4;
const int Complete = INT_MAX;
const int MaximumDepth = 64;

const Player &opponent(const GamePosition &position)
{
	for (const auto &player : position.players())
		if (player.id() != position.currentPlayer().id())
			return player;
	return position.currentPlayer();
}

bool isScoreless(const Move &move)
{
	return move.action == Move::Pass || (move.action == Move::Place && move.effectiveScore() == 0);
}

GamePosition after(const GamePosition &position, const Move &move)
{
	GamePosition child(position);
	child.setMoveMade(move);
	child.incrementTurn();
	child.makeMove(move, true);
	return child;
}

}

uint64_t EndgameSolver::key(const GamePosition &position)
{
	uint64_t h = 14695981039346656037ull;
	auto mix = [&h](unsigned int value) { h = (h ^ value) * 1099511628211ull; };

	const Board &board = position.board();
	for (int row = 0; row < board.height(); ++row)
		for (int col = 0; col < board.width(); ++col)
			mix((unsigned char)board.letter(row, col));

	for (const auto &player : position.players())
	{
		std::string tiles(player.rack().tiles().begin(), player.rack().tiles().end());
		std::sort(tiles.begin(), tiles.end());
		mix(0x100 + (unsigned int)player.id());
		for (const char c : tiles)
			mix((unsigned char)c);
	}

	mix(0x200 + (unsigned int)position.currentPlayer().id());
	mix(0x300 + (unsigned int)position.scorelessTurnsInARow());
	return h;
}

void EndgameSolver::reset(long long maxGenerations, int maxSeconds)
{
	m_table.clear();
	m_generations = 0;
	m_maxGenerations = maxGenerations;
	m_maxSeconds = maxSeconds;
	m_stopwatch.start();
	m_aborted = false;
}

bool EndgameSolver::outOfBudget()
{
	if (m_generations >= m_maxGenerations || (m_maxSeconds > 0 && m_stopwatch.exceeded(m_maxSeconds)))
		m_aborted = true;
	return m_aborted;
}

int EndgameSolver::moveValue(const GamePosition &position, const Move &move, int depth, int alpha, int beta)
{
	const Rack left = position.currentPlayer().rack() - move;
	const int theirs = opponent(position).rack().score();
	const int score = move.effectiveScore();

	if (left.empty())
		return score + 2 * theirs;

	const int scorelessLimit = QUACKLE_PARAMETERS->numberOfScorelessTurnsThatEndsGame();
	if (scorelessLimit >= 0 && isScoreless(move) && position.scorelessTurnsInARow() + 1 >= scorelessLimit)
		return score - left.score() + theirs;

	return score - search(after(position, move), depth - 1, score - beta, score - alpha);
}

int EndgameSolver::search(const GamePosition &position, int depth, int alpha, int beta)
{
	if (depth == 0)
	{
		m_horizon = true;
		return opponent(position).rack().score() - position.currentPlayer().rack().score();
	}

	const uint64_t positionKey = key(position);
	int hintIndex = -1;
	const auto found = m_table.find(positionKey);
	if (found != m_table.end())
	{
		const Entry &entry = found->second;
		hintIndex = entry.bestIndex;
		if (entry.depth >= depth)
		{
			const bool usable
				= entry.bound == Exact || (entry.bound == Lower && entry.value >= beta) || (entry.bound == Upper && entry.value <= alpha);
			if (usable)
			{
				if (entry.depth != Complete)
					m_horizon = true;
				return entry.value;
			}
		}
	}

	if (outOfBudget())
		return 0;

	Generator generator(position);
	generator.kibitz(INT_MAX / 2, Generator::CannotExchange);
	++m_generations;
	const MoveList &moves = generator.kibitzList();
	if (moves.empty())
		return 0;

	const int theirs = opponent(position).rack().score();
	const Rack &rack = position.currentPlayer().rack();
	std::vector<int> order(moves.size());
	std::vector<int> priority(moves.size());
	for (size_t i = 0; i < moves.size(); ++i)
	{
		priority[i] = moves[i].effectiveScore() + ((rack - moves[i]).empty() ? 1000 + 2 * theirs : 0);
		if ((int)i == hintIndex)
			priority[i] = Infinity;
	}
	std::iota(order.begin(), order.end(), 0);
	std::stable_sort(order.begin(), order.end(), [&priority](int a, int b) { return priority[a] > priority[b]; });

	const bool horizonAbove = m_horizon;
	m_horizon = false;

	const int originalAlpha = alpha;
	int best = -Infinity;
	int bestIndex = order.front();
	for (const int i : order)
	{
		const int value = moveValue(position, moves[i], depth, alpha, beta);
		if (m_aborted)
			return 0;
		if (value > best)
		{
			best = value;
			bestIndex = i;
		}
		alpha = std::max(alpha, value);
		if (alpha >= beta)
			break;
	}

	const Bound bound = best <= originalAlpha ? Upper : (best >= beta ? Lower : Exact);
	m_table[positionKey] = { m_horizon ? depth : Complete, best, bound, bestIndex };
	m_horizon = m_horizon || horizonAbove;
	return best;
}

EndgameSolver::Result EndgameSolver::solve(const GamePosition &position, long long maxGenerations, int maxSeconds)
{
	reset(maxGenerations, maxSeconds);
	Result result;

	Generator generator(position);
	generator.kibitz(INT_MAX / 2, Generator::CannotExchange);
	++m_generations;
	const MoveList moves = generator.kibitzList();
	if (moves.empty())
		return result;

	std::vector<int> order(moves.size());
	std::iota(order.begin(), order.end(), 0);
	{
		const int theirs = opponent(position).rack().score();
		const Rack &rack = position.currentPlayer().rack();
		std::vector<int> priority(moves.size());
		for (size_t i = 0; i < moves.size(); ++i)
			priority[i] = moves[i].effectiveScore() + ((rack - moves[i]).empty() ? 1000 + 2 * theirs : 0);
		std::stable_sort(order.begin(), order.end(), [&priority](int a, int b) { return priority[a] > priority[b]; });
	}

	result.move = moves[order.front()];
	for (int depth = 1; depth <= MaximumDepth; ++depth)
	{
		m_horizon = false;
		int alpha = -Infinity;
		int bestIndex = -1;
		bool iterationHorizon = false;

		for (size_t n = 0; n < order.size(); ++n)
		{
			m_horizon = false;
			const int value = moveValue(position, moves[order[n]], depth, alpha, Infinity);
			if (m_aborted)
				break;
			iterationHorizon = iterationHorizon || m_horizon;
			if (bestIndex < 0 || value > alpha)
			{
				alpha = value;
				bestIndex = (int)n;
			}
		}

		// A partial iteration still improves on the last one, provided the previous best was searched again.
		if (bestIndex >= 0)
		{
			result.move = moves[order[bestIndex]];
			result.value = alpha;
			result.depth = depth;
			result.exact = !m_aborted && !iterationHorizon;
			std::rotate(order.begin(), order.begin() + bestIndex, order.begin() + bestIndex + 1);
		}

		if (m_aborted || result.exact)
			break;
	}

	result.generations = m_generations;
	return result;
}

EndgameSolver::Result EndgameSolver::evaluate(const GamePosition &position, const Move &move, long long maxGenerations)
{
	reset(maxGenerations, 0);
	Result result;
	result.move = move;

	for (int depth = 1; depth <= MaximumDepth; ++depth)
	{
		m_horizon = false;
		const int value = moveValue(position, move, depth, -Infinity, Infinity);
		if (m_aborted)
			break;
		result.value = value;
		result.depth = depth;
		result.exact = !m_horizon;
		if (result.exact)
			break;
	}

	result.generations = m_generations;
	return result;
}
