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
#include <cmath>
#include <map>
#include <random>
#include <string>

#include "datamanager.h"
#include "evaluator.h"
#include "game.h"
#include "gameparameters.h"
#include "inferrer.h"

using namespace Quackle;

namespace
{

class LastMove
{
public:
	explicit LastMove(const GamePosition &position)
		: m_last(position.previousMove()), m_before(position)
	{
		m_informative = position.players().size() == 2 && m_last.action == Move::Place && !m_last.isChallengedPhoney();
		if (!m_informative)
			return;

		m_used = m_last.usedTiles();
		m_kept = QUACKLE_PARAMETERS->rackSize() - (int)m_used.length();
		m_informative = m_kept > 0;
		if (!m_informative)
			return;

		Board board(m_before.board());
		board.unmakeMove(m_last);
		m_before.setBoard(board);
		m_before.ensureBoardIsPreparedForAnalysis();
	}

	bool informative() const { return m_informative; }
	int kept() const { return m_kept; }
	const Board &boardBefore() const { return m_before.board(); }

	double likelihood(const LetterString &leave, double beta)
	{
		LetterString rack = m_used;
		rack += leave;
		m_before.setCurrentPlayerRack(Rack(rack), /* adjust bag */ false);
		const double best = m_before.staticBestMove().equity;
		const double played = QUACKLE_EVALUATOR->equity(m_before, m_last);
		return std::exp(beta * std::min(0.0, played - best));
	}

private:
	const Move m_last;
	GamePosition m_before;
	LetterString m_used;
	int m_kept = 0;
	bool m_informative = false;
};

}

WeightedRacks Inferrer::oppoLeaves(const GamePosition &position, int samples, double beta)
{
	if (samples <= 0)
		return WeightedRacks();
	LastMove last(position);
	const int kept = last.kept();
	const LongLetterString pool = position.unseenBag().tiles();
	if (!last.informative() || (int)pool.length() < kept)
		return WeightedRacks();

	const Board &board = last.boardBefore();
	// Seeded from the position, so that a position always gets the same answer.
	uint64_t seed = 14695981039346656037ull;
	for (int row = 0; row < board.height(); ++row)
		for (int col = 0; col < board.width(); ++col)
			seed = (seed ^ (unsigned char)board.letter(row, col)) * 1099511628211ull;
	std::mt19937_64 rng(seed);

	struct Leave
	{
		Rack rack;
		double likelihood;
		int draws;
	};
	// Leaves are drawn as the bag deals them, so the draw count carries the prior.
	std::map<std::string, Leave> leaves;
	std::string shuffled(pool.begin(), pool.end());
	for (int sample = 0; sample < samples; ++sample)
	{
		for (int i = 0; i < kept; ++i)
			std::swap(shuffled[i], shuffled[i + std::uniform_int_distribution<size_t>(0, shuffled.size() - 1 - i)(rng)]);
		std::string key = shuffled.substr(0, kept);
		std::sort(key.begin(), key.end());

		auto found = leaves.find(key);
		if (found == leaves.end())
		{
			LetterString leave;
			for (const char c : key)
				leave += (Letter)c;
			found = leaves.emplace(key, Leave { Rack(leave), last.likelihood(leave, beta), 0 }).first;
		}
		++found->second.draws;
	}

	WeightedRacks ret;
	for (const auto &entry : leaves)
		ret.push_back({ entry.second.rack, entry.second.draws * entry.second.likelihood });
	return ret;
}

void Inferrer::weightOppoRacks(const GamePosition &position, double beta, ProbableRackList *racks)
{
	LastMove last(position);
	if (!last.informative())
		return;
	const int kept = last.kept();

	std::map<std::string, double> likelihoods;
	double before = 0;
	double after = 0;
	for (ProbableRack &probable : *racks)
	{
		const LetterString tiles = probable.rack.tiles();
		const int size = (int)tiles.length();
		before += probable.probability;
		if (size < kept)
		{
			after += probable.probability;
			continue;
		}

		std::vector<bool> chosen(size, false);
		std::fill(chosen.begin(), chosen.begin() + kept, true);
		double sum = 0;
		int count = 0;
		do
		{
			std::string key;
			for (int i = 0; i < size; ++i)
				if (chosen[i])
					key += (char)tiles[i];
			std::sort(key.begin(), key.end());

			auto found = likelihoods.find(key);
			if (found == likelihoods.end())
			{
				LetterString leave;
				for (const char c : key)
					leave += (Letter)c;
				found = likelihoods.emplace(key, last.likelihood(leave, beta)).first;
			}
			sum += found->second;
			++count;
		} while (std::prev_permutation(chosen.begin(), chosen.end()));

		probable.probability *= sum / count;
		after += probable.probability;
	}

	if (after > 0)
		for (ProbableRack &probable : *racks)
			probable.probability *= before / after;
}
