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

#include <iostream>

#include "endgameplayer.h"
#include "endgamesolver.h"

// #define DEBUG_COMPUTERPLAYER

using namespace Quackle;

EndgamePlayer::EndgamePlayer()
{
	m_name = MARK_UV("Speedy Player");
	m_id = 2;
}

EndgamePlayer::~EndgamePlayer() {}

Move EndgamePlayer::move()
{
	return moves(1).back();
}

MoveList EndgamePlayer::moves(int nmoves)
{
	if (currentPosition().bag().size() > 0)
	{
#ifdef DEBUG_ENDGAME
		UVcout << "EndgamePlayer returning statically evaluated play." << endl;
#endif
		m_simulator.currentPosition().kibitz(nmoves);
#ifdef DEBUG_ENDGAME
		UVcout << "EndgamePlayer returns " << m_simulator.currentPosition().moves() << endl;
#endif
		return m_simulator.currentPosition().moves();
	}

	if (nmoves == 1 && m_parameters.endgameGenerations > 0 && currentPosition().nestedness() == 0
		&& currentPosition().players().size() == 2)
	{
		EndgameSolver solver;
		const EndgameSolver::Result result
			= solver.solve(currentPosition(), m_parameters.endgameGenerations, m_parameters.timeLimit ? m_parameters.secondsPerTurn : 0);
		if (result.exact)
		{
			Move solution = result.move;
			const int spread = currentPosition().spread(currentPosition().currentPlayer().id()) + result.value;
			solution.equity = result.value;
			solution.win = spread > 0 ? 1 : (spread == 0 ? 0.5 : 0);
			MoveList ret;
			ret.push_back(solution);
			return ret;
		}
	}

	m_endgame.setPosition(currentPosition());

	if (nmoves > 1)
		return m_endgame.moves(nmoves);

#ifdef DEBUG_ENDGAME
	UVcout << "EndgamePlayer solving endgame from position:" << endl;
	UVcout << currentPosition() << endl;
#endif

	MoveList ret;
	Move solution = m_endgame.solve(currentPosition().nestedness());

	ret.push_back(solution);
#ifdef DEBUG_ENDGAME
	UVcout << "EndgamePlayer returns solved endgame:" << ret << endl;
#endif

	return ret;
}

void EndgamePlayer::setDispatch(ComputerDispatch *dispatch)
{
	ComputerPlayer::setDispatch(dispatch);
	m_endgame.setDispatch(dispatch);
}
