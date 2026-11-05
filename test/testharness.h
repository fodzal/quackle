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

#ifndef QUACKLE_TESTHARNESS_H
#define QUACKLE_TESTHARNESS_H

#include <QStringList>

#include <datamanager.h>
#include <alphabetparameters.h>
namespace Quackle
{
	class ComputerPlayer;
	class Game;
	class GamePosition;
	class Rack;
	class GaddagNode;
}

class TestHarness
{
public:
	TestHarness();
	~TestHarness();

	// parse and execute commands specified on command line
	void executeFromArguments();

	void startUp();

	// Loads all positions and runs the computer player on them.
	void testPositions();

	// Loads all positions and spits out a report on them.
	void testReport(bool html);

	// Enumerates all racks using a full bag
	void enumerateAll();

	// Compute bingo stems ala Baron's MMPR
	void bingos();

	// Loads game from the file, and tests the final position,
	// and cleans up the game.
	void testFromFile(const QString &file);

	// Load racks from a file racks and spit out their static leave values
	void staticLeaves(const QString &file);

	// Spit out random racks.
	void randomRacks();

	// Spit out roughish leave values. Leaves come from file leaves.
	void leaveCalc(const QString &file);

	// Sim a leave.
	double leaveSim(const Quackle::Rack &R, int iterations);

	// Tests what the computer player does on this position.
	void testPosition(const Quackle::GamePosition &position, Quackle::ComputerPlayer *player);

	// Anagrams given letters.
	void anagram(const QString &letters, bool build);

	void wordDump();

	// Dumps the CPU topology the data manager detected.
	void cpuTopology();

	// Checks the simulator's accumulators. Exits nonzero on failure.
	void simulation();

	// Allocates and loads a game from the file.
	Quackle::Game *createNewGame(const QString &filename);

	struct SelfPlayResult
	{
		int scoreA = 0;
		int scoreB = 0;
		int moves = 0;
		bool finished = false;
	};

	struct ArenaResult
	{
		int scoreA = 0;
		int scoreB = 0;
		int moves = 0;
		double secondsA = 0;
		double secondsB = 0;
		// both players' thinking time, by how many tiles were left in the bag
		double secondsMidgame = 0;
		double secondsPreendgame = 0;
		double secondsEndgame = 0;
		int resyncs = 0;
	};

	// Plays pairs of games between the two computer players.
	void arena(unsigned int firstSeed, unsigned int pairs);

	// Times move generation and simulation on positions from a seeded Static game.
	void bench();

	// Prints win counts of the player on turn by tiles unseen and lead.
	void winTable(unsigned int games);
	void endgame(unsigned int games);
	void bestMove(unsigned int games);
	ArenaResult arenaGame(unsigned int seed, bool bMovesFirst);

	void selfPlayGames(unsigned int seed, unsigned int reps, bool reports, bool playability);
	SelfPlayResult selfPlayGame(unsigned int gameNumber, bool reports, bool playability);

	// Sets the positions that will be tested.
	void setPositions(const QStringList &positions)
	{
		m_positions = positions;
	}

	Quackle::ComputerPlayer *computerPlayerToTest() const
	{
		return m_computerPlayerToTest;
	}

	void setComputerPlayerToTest(Quackle::ComputerPlayer *computerPlayer)
	{
		m_computerPlayerToTest = computerPlayer;
	}

protected:
    //	void dumpGaddag(const GaddagNode *node, const LetterString &prefix);
	QStringList m_positions;
	Quackle::DataManager m_dataManager;
	Quackle::ComputerPlayer *m_computerPlayerToTest;
	Quackle::ComputerPlayer *m_computerPlayer2ToTest;
	bool m_quiet;
	QString m_gamesDir;
	QString m_lexicon;
	QString m_alphabet;
};

#endif
