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

#ifndef QUACKLE_INFERRER_H
#define QUACKLE_INFERRER_H

#include <utility>
#include <vector>

#include "enumerator.h"
#include "rack.h"

namespace Quackle
{

class GamePosition;

typedef std::vector<std::pair<Rack, double>> WeightedRacks;

namespace Inferrer
{

// Leaves the opponent may have kept with the move that led to position, weighted by how close that move
// comes to the static best play from each rack: weight exp(beta * (equity of the move - best equity)).
// Empty when the last move tells nothing, such as a pass, an exchange or a play of a whole rack.
WeightedRacks oppoLeaves(const GamePosition &position, int samples, double beta);

// Multiplies the probability of each of the opponent's possible racks by the mean likelihood of their last
// move over the leaves the rack could hold, then rescales to the original total.
void weightOppoRacks(const GamePosition &position, double beta, ProbableRackList *racks);
}

}

#endif
