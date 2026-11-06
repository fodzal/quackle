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

#include <QtCore>

#include <iostream>
#include <limits>
#include <algorithm>

#include <bogowinplayer.h>
#include <computerplayercollection.h>
#include <resolvent.h>
#include <datamanager.h>
#include <endgameplayer.h>
#include <endgamesolver.h>
#include <inferrer.h>
#include <game.h>
#include <gameparameters.h>
#include <lexiconparameters.h>
#include <strategyparameters.h>
#include <enumerator.h>
#include <reporter.h>
#include <sim.h>

#include <quackleio/dictimplementation.h>
#include <quackleio/flexiblealphabet.h>
#include <quackleio/froggetopt.h>
#include <quackleio/gcgio.h>
#include <quackleio/util.h>

#include "trademarkedboards.h"
#include "testharness.h"

using namespace std;
using namespace Quackle;

static Quackle::ThreadQoS
checkThreadQoS(const QString& qos)
{
	if (qos == "efficient")
		return Quackle::ThreadQoS::EnergyEfficient;
	if (qos == "balanced")
		return Quackle::ThreadQoS::Balanced;
	if (qos == "performance")
		return Quackle::ThreadQoS::Performance;

	UVcout << "Thread QoS " << QuackleIO::Util::qstringToString(qos) << " not recognized!" << endl;
	exit(1);
}

// A player that delegates its move hands these down, so setting them on the
// selected player reaches whichever player actually does the simming.
static void
applyThreadSettings(ComputerPlayer* player, const QString& threadString, const QString& qosString)
{
	if (!player || (threadString.isNull() && qosString.isNull()))
		return;

	const Quackle::ThreadQoS qos = qosString.isNull() ? player->threadQoS() : checkThreadQoS(qosString);
	const size_t count = threadString.isNull() ? Quackle::Simulator::recommendedThreadCount(qos) : threadString.toULong();

	player->setThreadCount(count, qos);
}

static ComputerPlayer*
checkPlayerName(const QString& computer)
{
	bool playerFound = false;
	const Quackle::Player &player = QUACKLE_COMPUTER_PLAYERS.playerForName(QuackleIO::Util::qstringToString(computer), playerFound);
	if (playerFound)
	{
		return player.computerPlayer();
	}
	else
	{
		UVcout << "Computer " << QuackleIO::Util::qstringToString(computer) << " not found!" << endl;
		exit(1);
	}
}

static void applyPlayerConfig(ComputerPlayer *player, const QString &config, const char *label)
{
	if (!player || config.isNull())
		return;

	ComputerParameters parameters = player->parameters();
	for (const QString &item : config.split(',', Qt::SkipEmptyParts))
	{
		const QStringList pair = item.split('=');
		bool ok = pair.size() == 2;
		const QString key = pair.value(0).trimmed();
		const double value = ok ? pair[1].trimmed().toDouble(&ok) : 0;
		if (!ok)
		{
			UVcout << "Bad config item " << QuackleIO::Util::qstringToString(item) << "; expected key=number" << endl;
			exit(1);
		}

		if (key == "seconds")
			parameters.secondsPerTurn = (int)value;
		else if (key == "iterations")
			parameters.minIterations = (int)value;
		else if (key == "maxiterations")
			parameters.maxIterations = (int)value;
		else if (key == "candidates")
			parameters.additionalCandidates = (int)value;
		else if (key == "plies")
			parameters.plies = (int)value;
		else if (key == "prune")
			parameters.prunePoints = value;
		else if (key == "timelimit")
			parameters.timeLimit = value != 0;
		else if (key == "crn")
			parameters.commonRandomNumbers = value != 0;
		else if (key == "endgame")
			parameters.endgameGenerations = (int)value;
		else if (key == "infer")
			parameters.inferenceSamples = (int)value;
		else if (key == "inferbeta")
			parameters.inferenceBeta = value;
		else if (key == "precandidates")
			parameters.preendgameCandidates = (int)value;
		else if (key == "nestedplays")
			parameters.nestedEndgamePlays = (int)value;
		else if (key == "nestedreplies")
			parameters.nestedEndgameReplies = (int)value;
		else if (key == "preinfer")
			parameters.preendgameInference = value != 0;
		else
		{
			UVcout << "Unknown config key " << QuackleIO::Util::qstringToString(key) << endl;
			exit(1);
		}
	}
	player->setParameters(parameters);

	UVcout << label << " " << player->name() << ": seconds " << parameters.secondsPerTurn << ", iterations " << parameters.minIterations
		   << ", maxiterations " << parameters.maxIterations << ", candidates " << parameters.additionalCandidates << ", plies "
		   << parameters.plies << ", prune " << parameters.prunePoints << ", timelimit " << parameters.timeLimit << ", crn "
		   << parameters.commonRandomNumbers << ", endgame " << parameters.endgameGenerations << ", infer " << parameters.inferenceSamples
		   << ", inferbeta " << parameters.inferenceBeta << ", precandidates " << parameters.preendgameCandidates << ", nestedplays "
		   << parameters.nestedEndgamePlays << ", nestedreplies " << parameters.nestedEndgameReplies << ", preinfer "
		   << parameters.preendgameInference << endl;
}

TestHarness::TestHarness()
	: m_computerPlayerToTest(0), m_computerPlayer2ToTest(0), m_quiet(false)
{
	m_gamesDir = "games";
	m_dataManager.setComputerPlayers(Quackle::ComputerPlayerCollection::fullCollection());
}

TestHarness::~TestHarness()
{
	delete m_computerPlayer2ToTest;
}

const char *usage = "Optional arguments:\n"
					"--computer=; sets the computer player (default 'Speedy Player').\n"
					"--computer2=; sets the second selfplay player (default: same as --computer).\n"
					"--config=key=value,...; overrides the first player's search settings:\n"
					"         seconds, iterations, maxiterations, candidates, plies, prune,\n"
					"         timelimit (0 = bounded by iterations only), crn (0 = independent\n"
					"         draws per candidate), endgame (solver budget in move generations,\n"
					"         0 = playout heuristic), infer (racks sampled to infer the\n"
					"         opponent's leave, 0 = off), inferbeta, precandidates (preendgame\n"
					"         candidates), nestedplays and nestedreplies (Endgame's breadth for\n"
					"         the opponent's answers in the preendgame), preinfer (1 = weight the\n"
					"         preendgame's opponent racks by their last move, at inferbeta).\n"
					"         Negative = default.\n"
					"--config2=key=value,...; the same for the second selfplay player.\n"
					"--mode=\n"
					"       'positions' (default) runs computer player all positions.\n"
					"       'report' asks computer player for report on all positions.\n"
					"       'htmlreport' asks for html report on all positions.\n"
					"       'selfplay' runs 1000 selfplay games.\n"
					"       'playability' output info for computing playability values.\n"
					"       'enumerate' lists all racks.\n"
					"       'staticleaves' output static leave values of racks in 'racks' file.\n"
					"       'randomracks' spit out random racks (forever?).\n"
					"       'leavecalc' spit out roughish values of leaves in 'leaves' file.\n"
					"       'anagram' anagrams letters supplied in --letters.\n"
					"       'cputopology' dumps detected CPU topology and thread pool sizing.\n"
					"       'sim' checks the simulator's accumulators.\n"
					"       'arena' plays --repetitions pairs of games between --computer\n"
					"               and --computer2. Both games of a pair draw the same tile\n"
					"               sequence, with seats swapped; pair k uses seed --seed + k.\n"
					"               With --arenastart=N, Static Player plays both seats until\n"
					"               at most N tiles are left in the bag, so both games of a pair\n"
					"               hand the players the same position.\n"
					"       'bench' times move generation and simulation.\n"
					"       'wintable' plays --repetitions games of --computer against --computer2\n"
					"                  and counts, for the player on turn, wins by tiles unseen\n"
					"                  and lead; test/wintable/wintable.py fits these into a\n"
					"                  bogowin table.\n"
					"       'inference' plays --repetitions Static Player games and measures, after\n"
					"                   every move, what inference learns about the mover's leave\n"
					"                   (settings from --config infer=, inferbeta=).\n"
					"       'bestmove' plays --repetitions Static Player games and checks, at every\n"
					"                  position, the best move alone against the full move list.\n"
					"       'endgame' plays --repetitions Static Player games to an empty bag and\n"
					"                 compares the endgame solver (budget from --config endgame=)\n"
					"                 with the playout heuristic, both judged by a deeper solve.\n"
					"--position=game.gcg; this option can be repeated to specify positions\n"
					"                     to test.\n"
					"--lexicon=; sets the lexicon (default 'nwl23').\n"
					"--alphabet=; sets the alphabet (default 'english').\n"
					"--seed=integer; seed the rng for reproducible sessions.\n"
					"--report; generate reports for selfplay games (default false).\n"
					"--letters; letters to anagram.\n"
					"--build; when mode is anagram, do not require that all letters be used.\n"
					"--quiet; during selfplay, print only the summary (default false).\n"
					"--repetitions=integer; the number of games for selfplay (default 1000).\n"
					"--threads=integer; simulation worker threads for the computer players.\n"
					"--qos=efficient|balanced|performance; simulation thread priority. Also sets\n"
					"      the thread count this machine advises at that priority, unless\n"
					"      --threads says otherwise.\n";

void TestHarness::executeFromArguments()
{
	GetOpt opts;

	QString mode;
	QString computer;
	QString computer2;
	QString config;
	QString config2;
	QString seedString;
	QString repString;
	QString threadString;
	QString qosString;
	QString arenaStartString;
	bool build;
	QString letters;
	bool help;
	bool report;
	unsigned int seed = numeric_limits<unsigned int>::max();
	unsigned int reps = 1000;

	opts.addOption('c', "computer", &computer);
	opts.addOption('d', "computer2", &computer2);
	opts.addOption('g', "config", &config);
	opts.addOption('G', "config2", &config2);
	opts.addOption('a', "alphabet", &m_alphabet);
	opts.addOption('l', "lexicon", &m_lexicon);
	opts.addOption('m', "mode", &mode);
	opts.addOption('s', "seed", &seedString);
	opts.addOption('r', "repetitions", &repString);
	opts.addOption('t', "letters", &letters);
	opts.addOption('n', "threads", &threadString);
	opts.addOption('q', "qos", &qosString);
	opts.addOption('b', "arenastart", &arenaStartString);
	opts.addRepeatableOption("position", &m_positions);

	opts.addSwitch("report", &report);
	opts.addSwitch("build", &build);
	opts.addSwitch("quiet", &m_quiet);
	opts.addSwitch("help", &help);

	if (!opts.parse())
		return;

	if (help)
	{
		UVcout << usage << endl;
		return;
	}

	if (mode.isNull())
		mode = "positions";
	if (computer.isNull())
		computer = "Speedy Player";
	if (computer2.isNull())
		computer2 = computer;
	if (m_lexicon.isNull())
		m_lexicon = "nwl23";
	if (m_alphabet.isNull())
		m_alphabet = "english";
	if (!seedString.isNull())
	        seed = seedString.toUInt();
	if (!repString.isNull())
	        reps = repString.toUInt();
	if (!arenaStartString.isNull())
		m_arenaStartBag = arenaStartString.toInt();

	m_computerPlayerToTest = checkPlayerName(computer);
	// Its own instance, so the two players can be configured differently.
	m_computerPlayer2ToTest = checkPlayerName(computer2)->clone();

	startUp();

	applyThreadSettings(m_computerPlayerToTest, threadString, qosString);
	applyThreadSettings(m_computerPlayer2ToTest, threadString, qosString);
	applyPlayerConfig(m_computerPlayerToTest, config, "player A");
	applyPlayerConfig(m_computerPlayer2ToTest, config2, "player B");

	// Apply the seed before dispatching to any mode so that every lever --
	// not just selfplay -- produces reproducible results. Tile drawing (used
	// when reading positions with unknown racks, when replaying games for
	// reports, and during selfplay) all pulls from this RNG.
	if (seed != numeric_limits<unsigned int>::max())
	{
		UVcout << "using seed " << seed << endl;
		m_dataManager.seedRandomNumbers(seed);
	}

	if (mode == "positions")
		testPositions();
	else if (mode == "report")
		testReport(false);
	else if (mode == "htmlreport")
		testReport(true);
	else if (mode == "enumerate")
		enumerateAll();
	else if (mode == "staticleaves")
		staticLeaves(QString("racks"));
	else if (mode == "randomracks")
		randomRacks();
	else if (mode == "leavecalc")
		leaveCalc(QString("leaves"));
	else if (mode == "anagram")
		anagram(letters, build);
	else if (mode == "selfplay")
		selfPlayGames(seed, reps, report, false);
	else if (mode == "playability")
		selfPlayGames(seed, reps, report, true);
	else if (mode == "arena")
		arena(seed == numeric_limits<unsigned int>::max() ? 1 : seed, reps);
	else if (mode == "bench")
		bench();
	else if (mode == "wintable")
		winTable(reps);
	else if (mode == "endgame")
		endgame(reps);
	else if (mode == "bestmove")
		bestMove(reps);
	else if (mode == "inference")
		inference(reps);
	else if (mode == "worddump")
		wordDump();
	else if (mode == "bingos")
		bingos();
	else if (mode == "cputopology")
		cpuTopology();
	else if (mode == "sim")
		simulation();
}

void TestHarness::startUp()
{
	UVcout << "Starting up.";

	m_dataManager.setBackupLexicon("nwl23");
	m_dataManager.setAppDataDirectory("../data");

	QString alphabetFile = QuackleIO::Util::stdStringToQString(Quackle::AlphabetParameters::findAlphabetFile(QuackleIO::Util::qstringToStdString(m_alphabet)));
	QuackleIO::FlexibleAlphabetParameters *flexure = new QuackleIO::FlexibleAlphabetParameters;
	if (flexure->load(alphabetFile))
	{
		m_dataManager.setAlphabetParameters(flexure);
	}
	else
	{
		UVcerr << "Couldn't load alphabet " << QuackleIO::Util::qstringToString(m_alphabet) << endl;
		delete flexure;
	}

	m_dataManager.setBoardParameters(new ScrabbleBoard());

	m_dataManager.lexiconParameters()->loadDawg(Quackle::LexiconParameters::findDictionaryFile(QuackleIO::Util::qstringToStdString(m_lexicon + ".dawg")));
	UVcout << ".";

   	m_dataManager.lexiconParameters()->loadGaddag(Quackle::LexiconParameters::findDictionaryFile(QuackleIO::Util::qstringToStdString(m_lexicon + ".gaddag")));
	UVcout << ".";

	if (m_alphabet == "english")
		m_dataManager.setBackupLexicon("default_english");

	m_dataManager.strategyParameters()->initialize(QuackleIO::Util::qstringToStdString(m_lexicon));

	UVcout << endl;

	m_gamesDir = QString("games_PLAYERNAME_%1").arg(QDateTime::currentDateTime().toString("dd.MM_hh.mm.ss"));
}

void TestHarness::testFromFile(const QString &file)
{
	UVcout << "Testing game from " << QuackleIO::Util::qstringToString(file) << endl;
	Quackle::Game *game = createNewGame(file);
	if (game)
	{
		testPosition(game->currentPosition(), computerPlayerToTest());
	}

	delete game;
}

double TestHarness::leaveSim(const Rack &R, int iterations)
{
	double sum = 0.0;

	UVcout << R << endl;
	Bag B;
	B.removeLetters(R.tiles());

	int tilesToLeave = QUACKLE_DATAMANAGER->randomInteger(14, 92);

	for (int i = 0; i < iterations; i++)
	{
		Quackle::Game game;

		Quackle::PlayerList players;

		Quackle::Player compyA(m_computerPlayerToTest->name() + MARK_UV(" A"), Quackle::Player::ComputerPlayerType, 0);
		compyA.setAbbreviatedName(MARK_UV("A"));
		compyA.setComputerPlayer(m_computerPlayerToTest);
		players.push_back(compyA);

		Quackle::Player compyB(m_computerPlayer2ToTest->name() + MARK_UV(" B"), Quackle::Player::ComputerPlayerType, 1);
		compyB.setAbbreviatedName(MARK_UV("B"));
		compyB.setComputerPlayer(m_computerPlayer2ToTest);
		players.push_back(compyB);

		game.setPlayers(players);
		game.associateKnownComputerPlayers();

		game.addPosition();

		game.currentPosition().setCurrentPlayerRack(Rack(""), true);
		game.currentPosition().setOppRack(Rack(""), true);

		Bag startBag = B;
		game.currentPosition().setBag(startBag);

		game.addPosition();

		UVcout << "NEW GAME" << endl;

		while (game.currentPosition().bag().size() > tilesToLeave)
		{
			if (game.currentPosition().gameOver())
			{
				UVcout << "GAME OVER" << endl;
				break;
			}

			const Quackle::Player player(game.currentPosition().currentPlayer());
			Quackle::Move compMove(game.haveComputerPlay());
			UVcout << "with " << player.rack() << ", " << player.name() << " commits to " << compMove << endl;
		}

		game.currentPosition().setCurrentPlayerRack(R, true);
		Quackle::Move compMove(game.haveComputerPlay());
		sum += compMove.equity;
	}

	return sum / iterations;
}

void TestHarness::leaveCalc(const QString &filename)
{
	QuackleIO::GCGIO io;
	QFile file(filename);

	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		UVcout << "Could not open file " << QuackleIO::Util::qstringToString(filename) << endl;
		return;
	}

	QTextStream in(&file);

	const int iterations = 10;

	while (!in.atEnd())
	{
		QString line = in.readLine();
		Rack rack(QuackleIO::Util::encode(line));
		double value = leaveSim(rack, iterations);
		UVcout << "leavecalc: " << rack << " " << value << endl;
	}

	file.close();

}

void TestHarness::randomRacks()
{
	for (;;)
	{
		Game game;

		Quackle::PlayerList players;

		Player compyA(m_computerPlayerToTest->name() + MARK_UV(" A"), Quackle::Player::ComputerPlayerType, 0);
		compyA.setAbbreviatedName(MARK_UV("A"));
		compyA.setComputerPlayer(m_computerPlayerToTest);
		players.push_back(compyA);

		game.setPlayers(players);

		game.addPosition();

		UVcout << game.currentPosition().currentPlayer().rack() << endl;
	}
}

void TestHarness::staticLeaves(const QString &filename)
{
	QuackleIO::GCGIO io;
	QFile file(filename);

	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		UVcout << "Could not open file " << QuackleIO::Util::qstringToString(filename) << endl;
		return;
	}

	QTextStream in(&file);

	Quackle::Game game;

	Quackle::PlayerList players;

	Quackle::Player compyA(m_computerPlayerToTest->name() + MARK_UV(" A"), Quackle::Player::ComputerPlayerType, 0);
	compyA.setAbbreviatedName(MARK_UV("A"));
	compyA.setComputerPlayer(m_computerPlayerToTest);
	players.push_back(compyA);

	Quackle::Player compyB(m_computerPlayer2ToTest->name() + MARK_UV(" B"), Quackle::Player::ComputerPlayerType, 1);
	compyB.setAbbreviatedName(MARK_UV("B"));
	compyB.setComputerPlayer(m_computerPlayer2ToTest);
	players.push_back(compyB);

	game.setPlayers(players);
	game.associateKnownComputerPlayers();

	game.addPosition();

	while (!in.atEnd())
	{
		QString line = in.readLine();
		Rack rack(QuackleIO::Util::encode(line));
		game.currentPosition().setCurrentPlayerRack(rack);
		Move move = game.currentPosition().staticBestMove();
		//Move scoredExch = game.currentPosition().scoreMove(exchZero);
		//UVcout << rack << " " << scoredExch << endl;
		UVcout << rack << " " << move << endl;
	}

	file.close();
}

void TestHarness::enumerateAll()
{
	Quackle::Bag B;
	Enumerator E(B);
	ProbableRackList racks;
	E.enumerate(&racks);
	for (ProbableRackList::iterator it = racks.begin(); it != racks.end(); ++it)
		UVcout << (*it).rack << " " << (*it).probability << endl;
}

struct PowerRack
{
    PowerRack(ProbableRack &r) : rack(r.rack),
				 stemProbability(r.probability),
				 usableTiles(0),
				 power(0) {}

    Rack   rack;
    double stemProbability; /* MSP */
    int    usableTiles; /* UT */
    double power; /* MMPR */
};

typedef vector<PowerRack> PowerRackList;

static bool powerSort(const PowerRack& r1,
		      const PowerRack& r2)
{
    return r1.power > r2.power;
}

// Looks for a single letter that acts to pluralize words based on the assumption
// that it will make a very common last letter in the lexicon.
static Letter findPluarlizer()
{
    // generate all the words in the lexicon
    Generator gen;
    int flags = Generator::AnagramRearrange | Generator::AddAnyLetters
	| Generator::ClearBlanknesses;
    WordList all = gen.anagramLetters("", flags);

    // count the frequency of occurrence of the final letter
    const int allLetters = QUACKLE_FIRST_LETTER + QUACKLE_MAXIMUM_ALPHABET_SIZE;
    int endCounts[allLetters];
    for (int j = 0; j < allLetters; j++) {
	endCounts[j] = 0;
    }
    for (WordList::iterator it = all.begin(); it != all.end(); ++it) {
	LetterString word = *it;
	Letter last = word[word.length() - 1];
 	endCounts[last]++;
    }

    // find the most common and 2nd most common final letters
    int maxEndCnt = 0;
    int maxEndLetter = QUACKLE_NULL_MARK;
    int max2EndCnt = 0;
    int max2EndLetter = QUACKLE_NULL_MARK;
    for (int j = 0; j < allLetters; j++) {
	if (endCounts[j] > maxEndCnt) {
	    max2EndCnt = maxEndCnt;
	    max2EndLetter = maxEndLetter;
	    maxEndCnt = endCounts[j];
	    maxEndLetter = j;
	} else if (endCounts[j] > max2EndCnt) {
	    max2EndCnt = endCounts[j];
	    max2EndLetter = j;
	}
    }

//     UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(maxEndLetter)
// 	   << " " << endCounts[maxEndLetter] << endl;
//     UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(max2EndLetter)
// 	   << " " << endCounts[max2EndLetter] << endl;

    // We found a pluralizer if it is much more common, otherwise this
    // language doesn't seem to have one.
    if (endCounts[maxEndLetter] > 2 * endCounts[max2EndLetter]) {
	UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(maxEndLetter)
	       << " is the pluralizer" << endl;
	return maxEndLetter;
    }
    return QUACKLE_NULL_MARK;
}

void TestHarness::bingos()
{
    Letter pluralizer = findPluarlizer();

    // enumerate all racks not considering blanks
    Quackle::Bag B;
    int blankCnt = 0;
    while (B.removeLetter(QUACKLE_BLANK_MARK)) {
	++blankCnt;
    }
    Enumerator E(B);
    ProbableRackList enumRacks;
    E.enumerate(&enumRacks, 6);

    // convert probable racks to power racks for extra fields
    PowerRackList racks;
    for (ProbableRackList::iterator it = enumRacks.begin();
	 it != enumRacks.end(); ++it) {
	racks.push_back(PowerRack(*it));
    }

    // get counts of all non-blank letters
    Letter start = QUACKLE_ALPHABET_PARAMETERS->firstLetter();
    Letter end = QUACKLE_ALPHABET_PARAMETERS->lastLetter();
    char bagCounts[QUACKLE_FIRST_LETTER + QUACKLE_MAXIMUM_ALPHABET_SIZE];
    B.letterCounts(bagCounts);

    Generator gen;
    int cnt = 0;
    for (PowerRackList::iterator it = racks.begin(); it != racks.end(); ++it) {
	LetterString letters(it->rack.tiles());

	char rackCounts[QUACKLE_FIRST_LETTER + QUACKLE_MAXIMUM_ALPHABET_SIZE];
	String::counts(letters, rackCounts);

	if (++cnt % 1000 == 0) {
	    UVcout << "stem: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters)
		   << " (" << cnt << " / " << racks.size() << ")" << endl;
	}

	// Count how many tiles combine with rack to make a bingo
	int usableTiles = 0;
	for (Letter c = start; c <= end; ++c) {
	    if (bagCounts[c] <= rackCounts[c]) {
		continue; // none left in bag to draw
	    }
	    int flags = Generator::AnagramRearrange | Generator::SingleMatch;
	    WordList anagrams = gen.anagramLetters(letters + c, flags);
	    if (anagrams.size() > 0) {
		usableTiles += bagCounts[c] - rackCounts[c]; // count all in the bag
	    }
	}

	// if we can make anything we can also do so with blanks
	if (usableTiles > 0) {
	    usableTiles += blankCnt;
	}
	it->usableTiles = usableTiles;
	if (rackCounts[pluralizer]) {
	    // Baron: "However, due to the frequent retention of the S in
	    // actual play, its attributed frequency was subsequently increased
	    // artificially 50%".  I generalized 'S' to a pluralizer for
	    // multi-language support.
	    it->stemProbability *= 1.5;
	}
	it->power = it->stemProbability * usableTiles;
    }

    sort(racks.begin(), racks.end(), powerSort);

    // Normalize max MSP to 1 (or 1.5 if it contains pluralizer) and MMPR
    double scale = 1 / racks.begin()->stemProbability;
    if (racks.begin()->rack.contains(LetterString(1, pluralizer))) {
	scale *= 1.5;
    }
    for (PowerRackList::iterator it = racks.begin(); it != racks.end(); ++it) {
	it->stemProbability *= scale;
	it->power *= scale;
    }

    // dump results
    for (PowerRackList::iterator it = racks.begin(); it != racks.end(); ++it) {
	UVcout << it->rack << " " << it->stemProbability << " "
	       << it->usableTiles << " " << it->power << endl;
    }
}

void TestHarness::testPosition(const Quackle::GamePosition &position, Quackle::ComputerPlayer *player)
{
	player->setPosition(position);

	ProbableRackList racks;
	Quackle::Bag unseenBag = position.unseenBag();

	if (unseenBag.size() <= QUACKLE_PARAMETERS->rackSize() + 3)
	{
		Enumerator enumerator(unseenBag);
		enumerator.enumerate(&racks);
		UVcout << racks.size() << " enumerations: " << endl;
		for (ProbableRackList::iterator it = racks.begin(); it != racks.end(); ++it)
			UVcout << (*it).rack << " " << (*it).probability << endl;
	}

	const int movesToShow = 10;
	UVcout << "Testing " << computerPlayerToTest()->name() << " on:" << endl;
	UVcout << position << endl;
	UVcout << "Generating moves..." << endl;

	MoveList moves = computerPlayerToTest()->moves(movesToShow);

	for (Quackle::MoveList::const_iterator it = moves.begin(); it != moves.end(); ++it)
	{
		UVcout << *it << endl;
	}
}

void TestHarness::anagram(const QString &letters, bool build)
{
	QuackleIO::DictImplementation dict;
	Dict::WordList list = dict.query(letters, build? Dict::Querier::NoRequireAllLetters : Dict::Querier::None);

	for (Dict::WordList::Iterator it = list.begin(); it != list.end(); ++it)
	{
		UVcout << QuackleIO::Util::qstringToString((*it).word);
		if ((*it).british)
			UVcout << "#";
		UVcout << endl;
	}
}

Quackle::Game *TestHarness::createNewGame(const QString &filename)
{
	QuackleIO::GCGIO io;
	QFile file(filename);

	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		UVcout << "Could not open gcg " << QuackleIO::Util::qstringToString(filename) << endl;
		return 0;
	}

	QTextStream in(&file);
	Quackle::Game *game = io.read(in, QuackleIO::Logania::MaintainBoardPreparation);
	file.close();

	return game;
}

void TestHarness::testPositions()
{
	UVcout << "Testing " << m_positions.size() << " positions with " << m_computerPlayerToTest->name() << "." << endl;
	for (QStringList::iterator it = m_positions.begin(); it != m_positions.end(); ++it)
		testFromFile(*it);
}

void TestHarness::testReport(bool html)
{
	UVcout << "Reporting on " << m_positions.size() << " positions with " << m_computerPlayerToTest->name() << "." << endl;
	for (QStringList::iterator it = m_positions.begin(); it != m_positions.end(); ++it)
	{
		Quackle::Game *game = createNewGame(*it);
		if (game)
		{
			if (html)
			{
				UVcout << game->currentPosition().board().htmlBoard(45) << endl;
			}
			else
			{
				UVString report;
				Quackle::Reporter::reportGame(*game, m_computerPlayerToTest, &report);
				UVcout << report << endl;
			}
		}

		delete game;
	}
}

void TestHarness::selfPlayGames(unsigned int seed, unsigned int reps, bool reports, bool playability)
{
	// Seeding is handled centrally in executeFromArguments() so that every
	// mode is reproducible; seed is unused here.
	(void)seed;

	unsigned int winsA = 0;
	unsigned int winsB = 0;
	unsigned int ties = 0;
	unsigned int unfinished = 0;
	long long totalSpread = 0;

	for (unsigned int i = 0; i < reps; i++)
	{
		const SelfPlayResult result = selfPlayGame(i, reports, playability);
		if (!result.finished)
		{
			++unfinished;
			continue;
		}

		totalSpread += result.scoreA - result.scoreB;
		if (result.scoreA > result.scoreB)
			++winsA;
		else if (result.scoreA < result.scoreB)
			++winsB;
		else
			++ties;
	}

	const unsigned int finished = winsA + winsB + ties;
	UVcout << "SELFPLAY SUMMARY games " << finished << ", A wins " << winsA << ", B wins " << winsB << ", ties " << ties << ", unfinished "
		   << unfinished << ", mean spread A-B " << (finished ? static_cast<double>(totalSpread) / finished : 0.0) << endl;
}

TestHarness::SelfPlayResult TestHarness::selfPlayGame(unsigned int gameNumber, bool reports, bool playability)
{
	SelfPlayResult result;

	Quackle::Game game;

	Quackle::PlayerList players;

	Quackle::Player compyA(m_computerPlayerToTest->name() + MARK_UV(" A"), Quackle::Player::ComputerPlayerType, 0);
	compyA.setAbbreviatedName(MARK_UV("A"));
	compyA.setComputerPlayer(m_computerPlayerToTest);
	players.push_back(compyA);

	Quackle::Player compyB(m_computerPlayer2ToTest->name() + MARK_UV(" B"), Quackle::Player::ComputerPlayerType, 1);
	compyB.setAbbreviatedName(MARK_UV("B"));
	compyB.setComputerPlayer(m_computerPlayer2ToTest);
	players.push_back(compyB);

	game.setPlayers(players);
	game.associateKnownComputerPlayers();

	game.addPosition();

	UVcout << "NEW GAME (#" << gameNumber << ")" << endl;

	QElapsedTimer time;
	time.start();

	// Six scoreless turns end any game, so this only guards against a broken engine.
	const int maxMoves = 1000;
	int i;
	for (i = 0; i < maxMoves; ++i)
	{
		if (game.currentPosition().gameOver())
		{
			result.finished = true;
			const PlayerList players = game.currentPosition().endgameAdjustedScores();
			for (PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
			{
				if (it->id() == 0)
					result.scoreA = it->score();
				else
					result.scoreB = it->score();
			}

			if (!m_quiet)
			{
				UVcout << "GAME OVER ";
				for (PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
					UVcout << it->name() << " : " << it->score() << " ";
				UVcout << endl;
			}
			break;
		}

		const Quackle::Player player(game.currentPosition().currentPlayer());

		if (playability)
		{
			game.currentPosition().kibitz(100);
			Quackle::MoveList moves = game.currentPosition().moves();
			float bestEquity = moves.front().equity - 0.0001f;
			Quackle::MoveList tops;
			for (MoveList::iterator it = moves.begin(); it != moves.end(); ++it)
			{
				if ((*it).equity >= bestEquity)
					tops.push_back(*it);
			}
			size_t numTops = tops.size();
			for (MoveList::iterator it = tops.begin(); it != tops.end(); ++it)
			{
				MoveList words = game.currentPosition().allWordsFormedBy(*it);
				for (MoveList::iterator it2 = words.begin(); it2 != words.end(); ++it2)
				{
					Rack word = (*it2).prettyTiles();
					UVcout << word << " " << numTops << endl;
				}
			}
			int toPlay = QUACKLE_DATAMANAGER->randomInteger(0, (int)numTops - 1);
			game.commitMove(tops[toPlay]);
		}
		else
		{
			Quackle::Move compMove(game.haveComputerPlay());
			if (!m_quiet)
				UVcout << "with " << player.rack() << ", " << player.name() << " commits to " << compMove << endl;
		}
	}
	result.moves = i;

	int secondsElapsed = static_cast<int>(time.elapsed() / 1000);
	if (!m_quiet) {
		UVcout << "Game " << gameNumber << " played in " << secondsElapsed
	           << " seconds with " << i << " moves" << endl;
	}

	if (!reports) {
		return result;
	}

	Quackle::StaticPlayer playah;
	UVString report;
	Quackle::Reporter::reportGame(game, &playah, &report);
	if (!m_quiet) { UVcout << report << endl; }

	QString gamesDir = m_gamesDir;
	gamesDir.replace("PLAYERNAME", QuackleIO::Util::uvStringToQString(m_computerPlayerToTest->name()));
	gamesDir.replace(" ", "_");
	QDir::current().mkdir(gamesDir);

	QString joinedCompyName = QuackleIO::Util::uvStringToQString(m_computerPlayer2ToTest->name());
	joinedCompyName.replace(" ", "_");
	QFile outFile(QString("%1/%2-game-%3.gcg").arg(gamesDir).arg(joinedCompyName).arg(gameNumber));

	if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text))
	{
		UVcout << "Could not open gcg output file" << endl;
		return result;
	}

	QuackleIO::GCGIO io;
	QTextStream out(&outFile);
	io.write(game, out);

	QFile outFileReport(QString("%1/%2-game-%3.report").arg(gamesDir).arg(joinedCompyName).arg(gameNumber));

	if (!outFileReport.open(QIODevice::WriteOnly | QIODevice::Text))
	{
		UVcout << "Could not open report output file" << endl;
		return result;
	}
	QTextStream outReport(&outFileReport);
	outReport << QuackleIO::Util::uvStringToQString(report);
	outReport << "Game played in " << secondsElapsed << " seconds." << Qt::endl;

	outFile.close();
	outFileReport.close();
	return result;
}

namespace
{
// splitmix64: portable, unlike std::shuffle, so a seed deals the same tiles on every platform.
class ArenaRng
{
public:
	explicit ArenaRng(uint64_t seed)
		: m_state(seed)
	{
	}

	size_t below(size_t n)
	{
		m_state += 0x9E3779B97F4A7C15ULL;
		uint64_t z = m_state;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
		return (size_t)((z ^ (z >> 31)) % n);
	}

	void shuffle(LongLetterString &tiles)
	{
		for (size_t i = tiles.size(); i > 1; --i)
			std::swap(tiles[i - 1], tiles[below(i)]);
	}

private:
	uint64_t m_state;
};

LongLetterString sorted(LongLetterString tiles)
{
	std::sort(tiles.begin(), tiles.end());
	return tiles;
}
}

TestHarness::ArenaResult TestHarness::arenaGame(unsigned int seed, bool bMovesFirst)
{
	ArenaResult result;
	ArenaRng rng(seed);

	Quackle::Player compyA(m_computerPlayerToTest->name() + MARK_UV(" A"), Quackle::Player::ComputerPlayerType, 0);
	compyA.setAbbreviatedName(MARK_UV("A"));
	Quackle::Player compyB(m_computerPlayer2ToTest->name() + MARK_UV(" B"), Quackle::Player::ComputerPlayerType, 1);
	compyB.setAbbreviatedName(MARK_UV("B"));

	Quackle::PlayerList players;
	if (bMovesFirst)
	{
		players.push_back(compyB);
		players.push_back(compyA);
	}
	else
	{
		players.push_back(compyA);
		players.push_back(compyB);
	}

	// setPlayers numbers players in turn order.
	const int aId = bMovesFirst ? 1 : 0;

	Quackle::Game game;
	game.setPlayers(players);
	game.addPosition();

	// The bag in drawing order: both games of a pair deal from the same sequence.
	LongLetterString sequence = Bag().tiles();
	rng.shuffle(sequence);

	const int rackSize = QUACKLE_PARAMETERS->rackSize();
	vector<int> ids;
	for (const auto &player : game.currentPosition().players())
		ids.push_back(player.id());
	for (int id : ids)
	{
		LetterString rack;
		for (int i = 0; i < rackSize; ++i)
			rack += sequence[i];
		sequence.erase(0, rackSize);
		game.currentPosition().setPlayerRack(id, Rack(rack), /* adjust bag */ true);
	}

	ComputerPlayer *opener = m_arenaStartBag >= 0 ? checkPlayerName("Static Player") : nullptr;

	const int maxMoves = 1000;
	for (result.moves = 0; result.moves < maxMoves && !game.currentPosition().gameOver(); ++result.moves)
	{
		const int moverId = game.currentPosition().currentPlayer().id();
		const int bagSize = game.currentPosition().bag().size();
		const bool opening = opener && bagSize > m_arenaStartBag;
		ComputerPlayer *player = opening ? opener : moverId == aId ? m_computerPlayerToTest : m_computerPlayer2ToTest;

		QElapsedTimer timer;
		timer.start();
		player->setPosition(game.currentPosition());
		const Move move = player->move();
		const double seconds = timer.elapsed() / 1000.0;
		if (!opening)
		{
			(moverId == aId ? result.secondsA : result.secondsB) += seconds;
			(bagSize == 0 ? result.secondsEndgame : bagSize <= 2 ? result.secondsPreendgame : result.secondsMidgame) += seconds;
		}

		// Only now, so that the player never sees the order of the bag.
		LetterString order;
		for (int i = std::min<int>(rackSize, (int)sequence.size()) - 1; i >= 0; --i)
			order += sequence[i];
		game.currentPosition().setDrawingOrder(order);
		game.commitMove(move);

		const GamePosition &position = game.currentPosition();
		for (const auto &it : position.players())
			if (it.id() == moverId)
				sequence.erase(0, std::min(sequence.size(), (size_t)it.drawnLetters().tiles().length()));

		if (move.action == Move::Exchange)
			for (Letter letter : move.usedTiles())
				sequence.insert(rng.below(sequence.size() + 1), 1, letter);

		if (sorted(sequence) != sorted(position.bag().tiles()))
		{
			++result.resyncs;
			sequence = position.bag().tiles();
			rng.shuffle(sequence);
		}
	}

	for (const auto &it : game.currentPosition().endgameAdjustedScores())
	{
		if (it.id() == aId)
			result.scoreA = it.score();
		else
			result.scoreB = it.score();
	}

	return result;
}

void TestHarness::arena(unsigned int firstSeed, unsigned int pairs)
{
	unsigned int games = 0;
	unsigned int winsA = 0;
	unsigned int winsB = 0;
	unsigned int ties = 0;
	double pairScoreSum = 0;
	double pairScoreSquares = 0;
	double pairSpreadSum = 0;
	double pairSpreadSquares = 0;

	for (unsigned int pair = 0; pair < pairs; ++pair)
	{
		const unsigned int seed = firstSeed + pair;
		double pairScore = 0;
		double pairSpread = 0;

		for (int seat = 0; seat < 2; ++seat)
		{
			const ArenaResult r = arenaGame(seed, seat == 1);
			const double score = r.scoreA > r.scoreB ? 1 : (r.scoreA == r.scoreB ? 0.5 : 0);
			pairScore += score / 2;
			pairSpread += (r.scoreA - r.scoreB) / 2.0;
			++games;
			winsA += r.scoreA > r.scoreB;
			winsB += r.scoreA < r.scoreB;
			ties += r.scoreA == r.scoreB;

			UVcout << "ARENA seed " << seed << " first " << (seat ? "B" : "A") << " scoreA " << r.scoreA << " scoreB " << r.scoreB
				   << " moves " << r.moves << " secondsA " << r.secondsA << " secondsB " << r.secondsB << " resyncs " << r.resyncs
				   << " mid " << r.secondsMidgame << " pre " << r.secondsPreendgame << " end " << r.secondsEndgame << endl;
		}

		pairScoreSum += pairScore;
		pairScoreSquares += pairScore * pairScore;
		pairSpreadSum += pairSpread;
		pairSpreadSquares += pairSpread * pairSpread;
	}

	// Pairs, not games, are the independent samples.
	auto halfWidth = [pairs](double sum, double squares) {
		if (pairs < 2)
			return 0.0;
		const double mean = sum / pairs;
		const double variance = (squares - pairs * mean * mean) / (pairs - 1);
		return 1.96 * sqrt(std::max(0.0, variance) / pairs);
	};

	const double n = pairs ? pairs : 1;
	UVcout << "ARENA SUMMARY pairs " << pairs << ", games " << games << ", A wins " << winsA << ", B wins " << winsB << ", ties " << ties
		   << ", A score " << 100 * pairScoreSum / n << "% +- " << 100 * halfWidth(pairScoreSum, pairScoreSquares) << ", mean spread A-B "
		   << pairSpreadSum / n << " +- " << halfWidth(pairSpreadSum, pairSpreadSquares) << endl;
}

void TestHarness::bench()
{
	ComputerPlayer *staticPlayer = checkPlayerName("Static Player");

	vector<GamePosition> positions;
	{
		Quackle::PlayerList players;
		players.push_back(Quackle::Player(MARK_UV("A"), Quackle::Player::ComputerPlayerType, 0));
		players.push_back(Quackle::Player(MARK_UV("B"), Quackle::Player::ComputerPlayerType, 1));
		Quackle::Game game;
		game.setPlayers(players);
		game.addPosition();
		while (!game.currentPosition().gameOver())
		{
			positions.push_back(game.currentPosition());
			game.haveComputerPlay(staticPlayer);
		}
	}

	vector<const GamePosition *> midgame;
	for (const auto &position : positions)
		if (position.bag().size() > 2 * QUACKLE_PARAMETERS->rackSize() && !position.board().isEmpty())
			midgame.push_back(&position);
	if (midgame.empty())
	{
		UVcout << "BENCH no midgame position" << endl;
		return;
	}

	UVcout << "BENCH positions " << positions.size() << ", midgame " << midgame.size() << endl;

	auto report = [](const char *what, long long count, const char *unit, double seconds) {
		UVcout << "BENCH " << what << ": " << count << " " << unit << " in " << seconds << " s, " << (seconds > 0 ? count / seconds : 0)
			   << " " << unit << "/s" << endl;
	};

	const int rounds = 20;
	QElapsedTimer timer;

	timer.start();
	long long calls = 0;
	for (int round = 0; round < rounds; ++round)
		for (const GamePosition *position : midgame)
		{
			GamePosition copy(*position);
			copy.staticBestMove();
			++calls;
		}
	report("staticBestMove", calls, "calls", timer.elapsed() / 1000.0);

	timer.start();
	calls = 0;
	for (int round = 0; round < rounds; ++round)
		for (const GamePosition *position : midgame)
		{
			GamePosition copy(*position);
			copy.kibitz(15);
			++calls;
		}
	report("kibitz(15)", calls, "calls", timer.elapsed() / 1000.0);

	// One sim thread: the rate per core, which is what the arena's one-thread processes get.
	const int candidates = 10;
	const int iterations = 50;
	const size_t simPositions = std::min<size_t>(midgame.size(), 4);
	double simSeconds = 0;
	long long playaheads = 0;
	for (size_t i = 0; i < simPositions; ++i)
	{
		GamePosition position(*midgame[i * midgame.size() / simPositions]);
		position.kibitz(candidates);

		Simulator simulator;
		simulator.setThreadCount(1);
		simulator.setPosition(position);
		simulator.setIncludedMoves(position.moves());

		timer.start();
		simulator.simulate(2, iterations);
		simSeconds += timer.elapsed() / 1000.0;
		playaheads += (long long)position.moves().size() * iterations;
	}
	report("2-ply playaheads", playaheads, "playaheads", simSeconds);
}

void TestHarness::winTable(unsigned int games)
{
	const int maxLead = 300;
	const int leads = 2 * maxLead + 1;
	map<int, vector<pair<long long, double>>> cells;

	for (unsigned int g = 0; g < games; ++g)
	{
		Quackle::PlayerList players;
		Quackle::Player a(MARK_UV("A"), Quackle::Player::ComputerPlayerType, 0);
		a.setComputerPlayer(m_computerPlayerToTest);
		players.push_back(a);
		Quackle::Player b(MARK_UV("B"), Quackle::Player::ComputerPlayerType, 1);
		b.setComputerPlayer(m_computerPlayer2ToTest);
		players.push_back(b);

		Quackle::Game game;
		game.setPlayers(players);
		game.associateKnownComputerPlayers();
		game.addPosition();

		struct Sample
		{
			int player;
			int unseen;
			int lead;
		};
		vector<Sample> samples;

		for (int moves = 0; moves < 1000 && !game.currentPosition().gameOver(); ++moves)
		{
			const GamePosition &position = game.currentPosition();
			const int player = position.currentPlayer().id();
			int unseen = position.bag().size();
			for (const auto &other : position.players())
				if (other.id() != player)
					unseen += other.rack().tiles().length();
			samples.push_back({ player, unseen, std::max(-maxLead, std::min(maxLead, position.spread(player))) });
			game.haveComputerPlay(player == 0 ? m_computerPlayerToTest : m_computerPlayer2ToTest);
		}

		if (!game.currentPosition().gameOver())
			continue;

		int score[2] = { 0, 0 };
		for (const auto &player : game.currentPosition().endgameAdjustedScores())
			score[player.id()] = player.score();

		for (const Sample &sample : samples)
		{
			const int mine = score[sample.player];
			const int theirs = score[1 - sample.player];
			auto &row = cells[sample.unseen];
			if (row.empty())
				row.assign(leads, { 0, 0.0 });
			auto &cell = row[sample.lead + maxLead];
			++cell.first;
			cell.second += mine > theirs ? 1 : (mine == theirs ? 0.5 : 0);
		}
	}

	for (const auto &row : cells)
		for (int i = 0; i < leads; ++i)
			if (row.second[i].first)
				UVcout << "WINTABLE " << row.first << " " << i - maxLead << " " << row.second[i].first << " " << row.second[i].second
					   << endl;
	UVcout << "WINTABLE SUMMARY games " << games << endl;
}

void TestHarness::endgame(unsigned int games)
{
	ComputerPlayer *staticPlayer = checkPlayerName("Static Player");
	const long long generations = m_computerPlayerToTest && m_computerPlayerToTest->parameters().endgameGenerations > 0
		? m_computerPlayerToTest->parameters().endgameGenerations
		: 20000;
	const long long judgeGenerations = 10 * generations;

	int positions = 0;
	int proven = 0;
	int changed = 0;
	int judged = 0;
	int heuristicBetter = 0;
	int lineMismatches = 0;
	long long gain = 0;
	double secondsSolver = 0;
	double secondsHeuristic = 0;

	for (unsigned int g = 0; g < games; ++g)
	{
		Quackle::PlayerList players;
		players.push_back(Quackle::Player(MARK_UV("A"), Quackle::Player::ComputerPlayerType, 0));
		players.push_back(Quackle::Player(MARK_UV("B"), Quackle::Player::ComputerPlayerType, 1));
		Quackle::Game game;
		game.setPlayers(players);
		game.addPosition();
		while (!game.currentPosition().gameOver() && !game.currentPosition().bag().empty())
			game.haveComputerPlay(staticPlayer);

		for (int ply = 0; !game.currentPosition().gameOver(); ++ply)
		{
			const GamePosition position = game.currentPosition();
			int theirTiles = 0;
			for (const auto &player : position.players())
				if (player.id() != position.currentPlayer().id())
					theirTiles = (int)player.rack().tiles().length();
			++positions;

			QElapsedTimer timer;
			timer.start();
			EndgameSolver solver;
			const EndgameSolver::Result solved = solver.solve(position, generations);
			secondsSolver += timer.elapsed() / 1000.0;

			timer.start();
			EndgamePlayer heuristic;
			ComputerParameters parameters = heuristic.parameters();
			parameters.endgameGenerations = 0;
			heuristic.setParameters(parameters);
			heuristic.setPosition(position);
			const Move heuristicMove = heuristic.move();
			secondsHeuristic += timer.elapsed() / 1000.0;

			UVcout << "ENDGAME game " << g << " ply " << ply << " tiles " << position.currentPlayer().rack().tiles().length() << "+"
				   << theirTiles << " solver " << solved.move << " value " << solved.value << (solved.exact ? " exact" : " estimate")
				   << " depth " << solved.depth << " generations " << solved.generations << " heuristic " << heuristicMove << endl;

			proven += solved.exact;
			if (solved.exact)
			{
				// Both sides playing proven moves must realize the proven value under the game's own scoring.
				const int me = position.currentPlayer().id();
				Quackle::Game line(game);
				bool lineProven = true;
				while (lineProven && !line.currentPosition().gameOver())
				{
					EndgameSolver step;
					const EndgameSolver::Result next = step.solve(line.currentPosition(), generations);
					lineProven = next.exact;
					if (lineProven)
						line.commitMove(next.move);
				}
				if (lineProven)
				{
					int spread = 0;
					for (const auto &player : line.currentPosition().endgameAdjustedScores())
						spread += player.id() == me ? player.score() : -player.score();
					if (spread - position.spread(me) != solved.value)
					{
						++lineMismatches;
						UVcout << "ENDGAME   MISMATCH proven line ends at " << spread - position.spread(me) << endl;
					}
				}
			}
			if (solved.exact && !(solved.move == heuristicMove))
			{
				++changed;
				EndgameSolver judge;
				const EndgameSolver::Result value = judge.evaluate(position, heuristicMove, judgeGenerations);
				UVcout << "ENDGAME   heuristic move value " << value.value << (value.exact ? " exact" : " estimate") << endl;
				if (value.exact)
				{
					++judged;
					gain += solved.value - value.value;
					heuristicBetter += value.value > solved.value;
				}
			}

			game.haveComputerPlay(staticPlayer);
		}
	}

	UVcout << "ENDGAME SUMMARY games " << games << ", positions " << positions << ", proven " << proven << ", solver differs " << changed
		   << ", judged " << judged << ", total gain " << gain << ", heuristic better " << heuristicBetter << ", line mismatches "
		   << lineMismatches << endl;
	UVcout << "ENDGAME TIME secondsSolver " << secondsSolver << " secondsHeuristic " << secondsHeuristic << endl;
}

void TestHarness::bestMove(unsigned int games)
{
	ComputerPlayer *staticPlayer = checkPlayerName("Static Player");
	int positions = 0;
	int mismatches = 0;

	for (unsigned int g = 0; g < games; ++g)
	{
		Quackle::PlayerList players;
		players.push_back(Quackle::Player(MARK_UV("A"), Quackle::Player::ComputerPlayerType, 0));
		players.push_back(Quackle::Player(MARK_UV("B"), Quackle::Player::ComputerPlayerType, 1));
		Quackle::Game game;
		game.setPlayers(players);
		game.addPosition();
		for (int moves = 0; moves < 1000 && !game.currentPosition().gameOver(); ++moves)
		{
			GamePosition best(game.currentPosition());
			GamePosition all(game.currentPosition());
			const Move alone = best.staticBestMove();
			all.kibitz(1000000);
			++positions;
			const Move &head = all.moves().empty() ? alone : all.moves().front();
			// The full list keeps one of the two directions of a one-tile play, not necessarily the one equityComparator ranks first.
			if (all.moves().empty() || alone.equity != head.equity || alone.score != head.score || !(alone.usedTiles() == head.usedTiles()))
			{
				++mismatches;
				UVcout << "BESTMOVE MISMATCH game " << g << " move " << moves << ": alone " << alone << ", list " << head << endl;
			}
			game.haveComputerPlay(staticPlayer);
		}
	}

	UVcout << "BESTMOVE SUMMARY games " << games << ", positions " << positions << ", mismatches " << mismatches << endl;
}

void TestHarness::inference(unsigned int games)
{
	ComputerPlayer *staticPlayer = checkPlayerName("Static Player");
	const ComputerParameters parameters = m_computerPlayerToTest ? m_computerPlayerToTest->parameters() : ComputerParameters();
	const int samples = parameters.inferenceSamples > 0 ? parameters.inferenceSamples : 1000;
	const double beta = parameters.inferenceBeta;

	int positions = 0;
	int tiles = 0;
	double bits = 0;
	double trueMassPosterior = 0;
	double trueMassPrior = 0;
	int preendgames = 0;
	double preendgamePosterior = 0;
	double preendgamePrior = 0;

	// Expected count of each letter in the leave, as a fraction of the leave.
	auto marginals = [](const WeightedRacks &leaves) {
		map<Letter, double> share;
		double total = 0;
		for (const auto &leave : leaves)
		{
			total += leave.second;
			for (const Letter letter : leave.first.tiles())
				share[letter] += leave.second / leave.first.tiles().length();
		}
		for (auto &entry : share)
			entry.second /= total;
		return share;
	};
	auto mass = [](const WeightedRacks &leaves, const Rack &rack) {
		double total = 0;
		double match = 0;
		for (const auto &leave : leaves)
		{
			total += leave.second;
			if (String::alphabetize(leave.first.tiles()) == String::alphabetize(rack.tiles()))
				match += leave.second;
		}
		return total > 0 ? match / total : 0;
	};

	for (unsigned int g = 0; g < games; ++g)
	{
		Quackle::PlayerList players;
		players.push_back(Quackle::Player(MARK_UV("A"), Quackle::Player::ComputerPlayerType, 0));
		players.push_back(Quackle::Player(MARK_UV("B"), Quackle::Player::ComputerPlayerType, 1));
		Quackle::Game game;
		game.setPlayers(players);
		game.addPosition();
		for (int moves = 0; moves < 1000 && !game.currentPosition().gameOver(); ++moves)
		{
			const Rack moverRack = game.currentPosition().currentPlayer().rack();
			game.haveComputerPlay(staticPlayer);
			const GamePosition &after = game.currentPosition();
			if (after.gameOver() || after.bag().empty())
				continue;

			if (after.bag().size() <= 2)
			{
				const LetterString moverTiles = String::alphabetize(after.nextPlayer()->rack().tiles());
				ProbableRackList racks;
				Bag unseen = after.unseenBag();
				Enumerator(unseen).enumerate(&racks);
				ProbableRackList weighted = racks;
				Inferrer::weightOppoRacks(after, beta, &weighted);
				for (size_t i = 0; i < racks.size(); ++i)
					if (String::alphabetize(racks[i].rack.tiles()) == moverTiles)
					{
						preendgamePrior += racks[i].probability;
						preendgamePosterior += weighted[i].probability;
					}
				++preendgames;
			}

			const WeightedRacks posterior = Inferrer::oppoLeaves(after, samples, beta);
			if (posterior.empty())
				continue;
			const WeightedRacks prior = Inferrer::oppoLeaves(after, samples, 0);
			const Rack leave = moverRack - after.previousMove();

			map<Letter, double> posteriorShare = marginals(posterior);
			map<Letter, double> priorShare = marginals(prior);
			for (const Letter letter : leave.tiles())
			{
				bits += log2(std::max(1e-6, posteriorShare[letter]) / std::max(1e-6, priorShare[letter]));
				++tiles;
			}
			trueMassPosterior += mass(posterior, leave);
			trueMassPrior += mass(prior, leave);
			++positions;
		}
	}

	const double n = positions ? positions : 1;
	UVcout << "INFERENCE SUMMARY games " << games << ", samples " << samples << ", beta " << beta << ", positions " << positions
		   << ", bits per leave tile " << (tiles ? bits / tiles : 0) << ", mass on the true leave " << trueMassPosterior / n << " (prior "
		   << trueMassPrior / n << ")" << endl;
	UVcout << "INFERENCE PREENDGAME positions " << preendgames << ", mass on the true rack "
		   << (preendgames ? preendgamePosterior / preendgames : 0) << " (prior " << (preendgames ? preendgamePrior / preendgames : 0)
		   << ")" << endl;
}

static void dumpGaddag(const GaddagNode *node, const LetterString &prefix)
{
    for (const GaddagNode* child = node->firstChild(); child; child = child->nextSibling()) {
	Letter childLetter = child->letter();
	LetterString newPrefix(prefix);
	newPrefix += childLetter;

	if (child->isTerminal()) {
	    UVcout << "wordDump: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(newPrefix) << endl;
	}
	if (child->firstChild()) {
	    dumpGaddag(child, newPrefix);
	}
    }
}

void TestHarness::wordDump()
{
    if (QUACKLE_LEXICON_PARAMETERS->hasGaddag()) {
	dumpGaddag(QUACKLE_LEXICON_PARAMETERS->gaddagRoot(),
		      LetterString());
    } else if (QUACKLE_LEXICON_PARAMETERS->hasSomething()) {
	// Enumerate straight from the dawg so the full word list is available
	// even when no gaddag has been built yet -- e.g. to feed makegaddag.
	Generator gen;
	int flags = Generator::AnagramRearrange | Generator::AddAnyLetters
	    | Generator::ClearBlanknesses;
	WordList all = gen.anagramLetters("", flags);
	for (WordList::const_iterator it = all.begin(); it != all.end(); ++it)
	    UVcout << "wordDump: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(*it) << endl;
    } else {
	UVcout << "wordDump: no lexicon" << endl;
    }
}

void TestHarness::cpuTopology()
{
	const Quackle::CPUTopology &topology = m_dataManager.cpuTopology();

	if (topology.usedGenericFallback())
	{
		UVcout << "WARNING: generic fallback used -- no platform-specific CPU probe was available, "
			   << "or the one for this platform failed. Only the logical core count is known." << endl;
	}

	UVcout << topology.description();
}

namespace
{

int simFailures = 0;

// Pinned so the goldens do not depend on the recording machine's core count.
// Chunk size follows the thread count, and so does how much of a batch an
// abort can still interrupt.
const size_t simThreadCount = 2;

void check(bool condition, const char *what)
{
	UVcout << (condition ? "PASS" : "FAIL") << " " << what << endl;
	if (!condition)
		++simFailures;
}

Quackle::AveragedValue sampled(std::initializer_list<double> values)
{
	Quackle::AveragedValue value;
	for (double v : values)
		value.incorporateValue(v);
	return value;
}

Quackle::Level leveled(std::initializer_list<std::initializer_list<double>> statistics)
{
	Quackle::Level level;
	for (const auto &samples : statistics)
	{
		Quackle::PositionStatistics position;
		position.score = sampled(samples);
		level.statistics.push_back(position);
	}
	return level;
}

void checkLevelListIncorporation()
{
	Quackle::LevelList tall;
	tall.push_back(leveled({ { 20 }, { 30 } }));
	tall.push_back(leveled({ { 40 } }));
	tall.push_back(leveled({}));

	Quackle::LevelList shallow;
	shallow.push_back(leveled({ { 10 } }));

	Quackle::LevelList grown(shallow);
	grown.incorporate(tall);

	check(grown.size() == 3, "merging a taller list grows the level count");
	check(grown[0].statistics.size() == 2, "merging a wider level grows the statistic count");
	check(grown[0].statistics[0].score.incorporatedValues() == 2, "overlapping statistics sum their sample counts");
	check(grown[0].statistics[0].score.valueSum() == 30, "overlapping statistics sum their values");
	check(grown[0].statistics[1].score.incorporatedValues() == 1, "statistics past the destination's width are added");
	check(grown[1].statistics[0].score.valueSum() == 40, "levels past the destination's depth are added");
	check(grown[2].statistics.empty(), "an empty source level adds nothing");

	Quackle::LevelList kept(tall);
	kept.incorporate(shallow);

	check(kept.size() == 3, "merging a shallower list does not truncate");
	check(kept[1].statistics[0].score.valueSum() == 40, "merging a shallower list leaves deeper levels intact");
	check(kept[0].statistics[0].score.incorporatedValues() == 2, "a shallower source still merges where it overlaps");
}

void checkAccumulators(const Quackle::GamePosition &position)
{
	Quackle::Simulator simulator;
	simulator.setThreadCount(simThreadCount);
	simulator.setPosition(position);

	const int plies = 2;
	const int iterations = 8;
	simulator.simulate(plies, iterations);

	check(simulator.iterations() == iterations, "the simulator counts the iterations it ran");

	// Every candidate must come out of a batch with the same number of
	// samples everywhere, or calculateEquity() compares unlike averages.
	bool evenlySampled = !simulator.simmedMoves().empty();
	bool candidateSampled = evenlySampled;
	for (const auto &it : simulator.simmedMoves())
	{
		if (it.residual.incorporatedValues() != iterations || it.gameSpread.incorporatedValues() != iterations
			|| it.wins.incorporatedValues() != iterations)
			evenlySampled = false;

		// level one, player one is the candidate itself; always played
		if (it.levels.empty() || it.levels[0].statistics.empty() || it.levels[0].statistics[0].score.incorporatedValues() != iterations
			|| it.levels[0].statistics[0].bingos.incorporatedValues() != iterations)
			candidateSampled = false;
	}

	check(evenlySampled, "every candidate accumulates one residual, spread and win per iteration");
	check(candidateSampled, "every candidate accumulates one level statistic sample per iteration");

	simulator.resetNumbers();

	bool reset = simulator.iterations() == 0;
	for (const auto &it : simulator.simmedMoves())
		if (it.residual.hasValues() || it.gameSpread.hasValues() || it.wins.hasValues() || !it.levels.empty())
			reset = false;

	check(reset, "resetNumbers clears every accumulator");
}

void checkChunkSizing()
{
	check(Quackle::Simulator::iterationsPerChunk(8, 1, 100) > 1, "a lone candidate batches iterations to fill the pool");
	check(Quackle::Simulator::iterationsPerChunk(8, 200, 100) == 1, "a candidate list wider than the pool batches one iteration");
	check(Quackle::Simulator::iterationsPerChunk(8, 1, 3) == 3, "a chunk never exceeds the iterations remaining");
	check(Quackle::Simulator::iterationsPerChunk(0, 4, 10) == 1, "a chunk is at least one iteration");
	check(Quackle::Simulator::iterationsPerChunk(8, 0, 10) == 1, "an empty candidate list still yields a chunk");
}

struct LogShape
{
	bool readable = false;
	bool wellFormed = false;
	bool nested = false;
	QList<int> indices;
	QList<int> playaheads;

	bool indicesRunTo(int iterations) const
	{
		if (indices.size() != iterations)
			return false;
		for (int i = 0; i < indices.size(); ++i)
			if (indices[i] != i + 1)
				return false;
		return true;
	}

	bool everyIterationHas(int candidates) const
	{
		if (playaheads.isEmpty() || candidates <= 0)
			return false;
		for (int count : playaheads)
			if (count != candidates)
				return false;
		return true;
	}
};

LogShape readLogShape(const QString &path)
{
	LogShape shape;

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return shape;

	shape.readable = true;
	shape.nested = true;

	QXmlStreamReader xml(&file);
	QStringList open;

	while (!xml.atEnd())
	{
		xml.readNext();

		if (xml.isStartElement())
		{
			const QString name = xml.name().toString();
			const QString parent = open.isEmpty() ? QString() : open.last();

			if (name == "iteration")
			{
				shape.nested = shape.nested && parent == "simulation";
				shape.indices.append(xml.attributes().value("index").toInt());
				shape.playaheads.append(0);
			}
			else if (name == "playahead")
			{
				shape.nested = shape.nested && parent == "iteration";
				if (!shape.playaheads.isEmpty())
					++shape.playaheads.last();
			}
			else if (name == "ply")
			{
				shape.nested = shape.nested && parent == "playahead";
			}

			open.append(name);
		}
		else if (xml.isEndElement() && !open.isEmpty())
		{
			open.removeLast();
		}
	}

	shape.wellFormed = !xml.hasError();

	file.close();
	QFile::remove(path);

	return shape;
}

// The log embeds playahead values, which the workers' unseeded rngs make
// nondeterministic, so only its structure can be asserted.
void checkLog(const Quackle::GamePosition &position)
{
	const QString logPath("sim.log");
	QFile::remove(logPath);

	const int iterations = 3;
	int candidates = 0;

	{
		Quackle::Simulator simulator;
		simulator.setThreadCount(simThreadCount);
		simulator.setLogfile(logPath.toStdString(), false);
		simulator.setPosition(position);

		for (const auto &it : simulator.simmedMoves())
			if (it.includeInSimulation())
				++candidates;

		simulator.simulate(2, iterations);
		simulator.closeLogfile();
	}

	const LogShape shape = readLogShape(logPath);

	check(shape.readable, "the simulation log was written");
	check(shape.wellFormed, "the simulation log is well-formed xml");
	check(shape.nested, "plies nest in playaheads nest in iterations");
	check(shape.indices.size() == iterations, "the log holds one iteration element per iteration");
	check(shape.indicesRunTo(iterations), "iteration indices run 1..n in order");
	check(shape.everyIterationHas(candidates), "every iteration logs one playahead per candidate");
}

int narrowToOneCandidate(Quackle::Simulator &simulator)
{
	Quackle::MoveList one;
	if (!simulator.simmedMoves().empty())
		one.push_back(simulator.simmedMoves().front().move);
	simulator.setIncludedMoves(one);

	int included = 0;
	for (const auto &it : simulator.simmedMoves())
		if (it.includeInSimulation())
			++included;
	return included;
}

bool sampledExactly(const Quackle::Simulator &simulator, int iterations)
{
	bool any = false;
	for (const auto &it : simulator.simmedMoves())
	{
		if (!it.includeInSimulation())
			continue;
		any = true;

		if (it.residual.incorporatedValues() != iterations || it.gameSpread.incorporatedValues() != iterations
			|| it.wins.incorporatedValues() != iterations)
			return false;

		if (iterations > 0
			&& (it.levels.empty() || it.levels[0].statistics.empty()
				|| it.levels[0].statistics[0].score.incorporatedValues() != iterations))
			return false;
	}
	return any;
}

// SmartBogowin's shape: one candidate, many iterations. Nothing else in this
// mode leaves the pool with so little to chew on per iteration.
void checkSingleCandidateBatch(const Quackle::GamePosition &position)
{
	const QString logPath("sim.log");
	QFile::remove(logPath);

	// deliberately not a round number, so a batch that rounds its work up or
	// down runs a different count than asked
	const int iterations = 21;
	int included = 0;

	{
		Quackle::Simulator simulator;
		simulator.setThreadCount(simThreadCount);
		simulator.setLogfile(logPath.toStdString(), false);
		simulator.setPosition(position);
		included = narrowToOneCandidate(simulator);

		// otherwise this runs one iteration at a time and proves nothing
		check(Quackle::Simulator::iterationsPerChunk(simulator.threadCount(), included, iterations) > 1,
			"one candidate puts more than one iteration in flight");

		simulator.simulate(2, iterations);

		check(included == 1, "the batch narrowed to a single candidate");
		check(simulator.iterations() == iterations, "a single-candidate batch runs exactly the iterations asked");
		check(sampledExactly(simulator, iterations), "a single-candidate batch samples every iteration");

		simulator.closeLogfile();
	}

	const LogShape shape = readLogShape(logPath);

	check(shape.wellFormed, "the single-candidate log is well-formed xml");
	check(shape.indicesRunTo(iterations), "the single-candidate log holds every iteration in order");
	check(shape.everyIterationHas(included), "the single-candidate log holds one playahead per iteration");
}

class AbortAfter : public Quackle::ComputerDispatch
{
public:
	explicit AbortAfter(int calls)
		: m_remaining(calls)
	{
	}

	virtual bool shouldAbort() { return m_remaining-- <= 0; }
	virtual void signalFractionDone(double) {}

private:
	int m_remaining;
};

void checkAbort(const Quackle::GamePosition &position)
{
	const int iterations = 20;

	{
		Quackle::Simulator simulator;
		simulator.setThreadCount(simThreadCount);
		simulator.setPosition(position);
		AbortAfter immediately(0);
		simulator.setDispatch(&immediately);
		simulator.simulate(2, iterations);

		check(simulator.iterations() == 0, "aborting before the first iteration runs none");
		check(sampledExactly(simulator, 0), "aborting before the first iteration accumulates nothing");
	}

	Quackle::Simulator simulator;
	simulator.setThreadCount(simThreadCount);
	simulator.setPosition(position);
	AbortAfter partway(1);
	simulator.setDispatch(&partway);
	simulator.simulate(2, iterations);

	const int ran = simulator.iterations();
	check(ran > 0 && ran < iterations, "aborting partway stops short of the requested iterations");
	// An abort must not leave candidates at different sample counts, or
	// calculateEquity() compares averages over unlike populations.
	check(sampledExactly(simulator, ran), "an aborted batch leaves every candidate evenly sampled");

	// Returning with replies still in flight would let this next batch pop
	// them and count another iteration's samples as its own.
	simulator.setDispatch(0);
	simulator.resetNumbers();
	const int after = 4;
	simulator.simulate(2, after);

	check(simulator.iterations() == after, "a batch after an abort runs its own iterations");
	check(sampledExactly(simulator, after), "an abort leaves no replies behind for the next batch");
}
}

void TestHarness::simulation()
{
	checkLevelListIncorporation();
	checkChunkSizing();

	Quackle::Game game;
	Quackle::PlayerList players;

	Quackle::Player compyA(m_computerPlayerToTest->name() + MARK_UV(" A"), Quackle::Player::ComputerPlayerType, 0);
	compyA.setAbbreviatedName(MARK_UV("A"));
	compyA.setComputerPlayer(m_computerPlayerToTest);
	players.push_back(compyA);

	Quackle::Player compyB(m_computerPlayer2ToTest->name() + MARK_UV(" B"), Quackle::Player::ComputerPlayerType, 1);
	compyB.setAbbreviatedName(MARK_UV("B"));
	compyB.setComputerPlayer(m_computerPlayer2ToTest);
	players.push_back(compyB);

	game.setPlayers(players);
	game.associateKnownComputerPlayers();
	game.addPosition();
	game.currentPosition().kibitz(4);

	checkAccumulators(game.currentPosition());
	checkLog(game.currentPosition());
	checkSingleCandidateBatch(game.currentPosition());
	checkAbort(game.currentPosition());

	if (simFailures > 0)
	{
		UVcout << simFailures << " simulator check(s) failed" << endl;
		exit(1);
	}

	UVcout << "all simulator checks passed" << endl;
}
