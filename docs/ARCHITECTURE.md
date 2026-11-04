# Carte du code — Quackle (fork fodzal)

Référence de travail pour les développements sur l'IA. Les numéros de ligne dérivent vite :
on cite les fonctions, à retrouver par recherche.

## 1. Les couches

```
quacker/        GUI Qt (Quackle.exe)            ── dépend de ──┐
quackleio/      I/O Qt : dictionnaires, .gcg, StreamingReporter │
test/           libquackle_test (harnais --mode) + régression   │
racine *.cpp    libquackle : le moteur, sans Qt  <──────────────┘
makegaddag/ makeminidawg/ encodeleaves/ gaddagize/   outils de génération de données
data/           alphabets, lexiques (.dawg), stratégie (leaves, bogowin…)
```

Tout ce qui touche à la force de l'IA est dans **libquackle** (racine du repo). Le GUI et
`quackleio` ne font qu'appeler le moteur.

## 2. Modèle de données

| Fichier | Rôle |
|---|---|
| `alphabetparameters.*` | `Letter` = octet ; `LetterString` = mot interne. Blanc = lettre décalée (`clearBlankness`). Conversion user ↔ interne via `QUACKLE_ALPHABET_PARAMETERS->userVisible/encode`. |
| `fixedstring.h` | Chaîne de taille fixe utilisée pour `LetterString` (pas d'allocation). |
| `board.*`, `boardparameters.*` | Plateau + cases bonus ; `Board::toString`. Les cross-checks vivent dans `Generator`. |
| `rack.*`, `bag.*` | Chevalet, sac ; `Bag::someShuffledTiles`, `unseenBag`. |
| `move.*` | `Move` : `action` (Place/Exchange/Pass/…), `score`, `equity`, **`win` (0..1)**, `possibleWin`, `isBingo`. `MoveList::sort(…, Equity|Win|Score…)`. |
| `player.*`, `playerlist.*` | Joueur humain/IA, score, chevalet, `ComputerPlayer*` associé. |
| `game.*` | `GamePosition` (plateau + joueurs + sac + coups candidats) et `Game` (historique de positions). Points clés : `kibitz(n)`, `staticBestMove()`, `spread(playerId)` (= mon score − **meilleur** adversaire), `setOppRack` (⚠ premier adversaire seulement), `calculatePlayerConsideration` / `calculateSharedConsideration`, `endgameAdjustedScores`, `deadwood`. |

### Paramètres globaux (singletons)

`DataManager::self()` porte l'état global, exposé par macros :
`QUACKLE_ALPHABET_PARAMETERS`, `QUACKLE_LEXICON_PARAMETERS` (dawg/gaddag),
`QUACKLE_STRATEGY_PARAMETERS` (leaves, bogowin, worths, vcplace), `QUACKLE_PARAMETERS`
(taille chevalet, etc.), `QUACKLE_EVALUATOR`, `QUACKLE_COMPUTER_PLAYERS`.
Ces objets sont lus par **tous les threads de simulation** : toute écriture paresseuse dedans
est une course de données (voir ROADMAP §C).

## 3. Génération de coups (`generator.cpp`, ~2150 lignes)

- `Generator::kibitz(n, flags)` → `findstaticbest()` puis tri par equity et top-n.
  Avec `n == 1`, `setrecordall(false)` : on ne garde que le meilleur (chemin rapide utilisé
  par la simulation).
- `findstaticbest` : plateau vide → `anagram()` ; sinon `gordongenerate()` si gaddag
  présent (algorithme de Gordon), sinon `generate()` sur dawg (plus lent, mêmes résultats).
  Puis `exchange()` énumère les échanges.
- Chaque coup trouvé passe par `Generator::equity()` → `QUACKLE_EVALUATOR`.
- Le gaddag n'est pas versionné dans `data/lexica` : il se construit avec `makegaddag`
  (le script de régression le fait tout seul et le met en cache).

## 4. Évaluation statique (equity)

`evaluator.*` + `catchall.*`. L'évaluateur par défaut est `CatchallEvaluator` :

- **Plateau vide** : score + leave + `vcPlace(start, length, voyelles/consonnes)` (fichier
  `vcplace`, anglais uniquement).
- **Sac non vide** : score + leave + heuristique de timing en dur
  (`heuristicArray[13]`, indexé par `sac − tuiles jouées + 7`) — calibrée sur l'anglais.
- **Sac vide** : `move.score + endgameResult` (2 × deadwood adverse si on sort, sinon
  `-8 − 2.61 × valeur restante`).

`ScorePlusLeaveEvaluator::leaveValue` : lookup dans `superleaves`
(`std::map<LetterString,double>`) ; si absent **ou égal à 0**, repli sur
`worths` + `syn2` + équilibre voyelles/consonnes.

## 5. Les joueurs IA

Enregistrés dans `ComputerPlayerCollection::fullCollection()`.

| Nom affiché | Classe | id | Comportement |
|---|---|---|---|
| Static Player | `StaticPlayer` | 1 | meilleur coup statique |
| Speedy Player | `EndgamePlayer` | 2 | statique, puis solveur d'endgame quand le sac est vide. **Seul joueur déterministe → seul testé en régression.** |
| Championship Player | `TorontoPlayer : Resolvent` | 2006 | 66 s/coup |
| Twenty Second Championship | `TwentySecondPlayer` | 5209 | 20 s |
| Five Minute Championship | `FiveMinutePlayer` | 5208 | 300 s |
| (interne) | `SmartBogowin` | 110 | simulation Monte Carlo, choix par % de victoire |
| (interne) | `Preendgame` | 202 | 1–2 tuiles dans le sac |
| (désactivé) | `InferringPlayer` | 2012 | inférence de chevalet — jamais implémentée |

`Resolvent::moves` aiguille selon le sac :

```
sac vide           → EndgamePlayer → Endgame::solve / Endgame::moves
1..2 tuiles        → Preendgame
sinon              → SmartBogowin  (sim 2 plis, ou jusqu'à la fin si sac ≤ 2×chevalet)
```

### SmartBogowin (`bogowinplayer.cpp`) — le cœur de l'IA « milieu de partie »

1. `kibitz(13 + n)` candidats statiques, `pruneTo(33 pts, …)`, coups « considérés » en tête.
2. Simule le 1er candidat `minIterations` fois (= 2 it/s × secondsPerTurn).
3. Pour chaque candidat suivant : `minIterations` ; s'il peut battre le meilleur
   (`bogopoints + 1.96·35/√n > best`), on monte à `maxIterations` (5 it/s × s).
4. Arrêt au chrono. Tri final par **win**.

`bogopoints` convertit un % de victoire en points d'avance équivalents via la table
`bogowin` (recherche linéaire sur −300..300).

## 6. Le simulateur (`sim.*`)

- `Simulator::simulate(plies, iterations)` : pousse des itérations par paquets
  (`iterationsPerChunk`) dans `m_sendQueue` ; un pool de threads exécute
  `simulateOnePosition` ; `collectChunk` réintègre les résultats dans l'ordre.
  Une itération = une « playahead » par coup inclus.
- `simulateOnePosition` : copie le `Game`, tire les chevalets adverses
  (`randomizeOppoRacks`, avec `partialOppoRack`) et l'ordre de pioche, puis joue
  `levelCount` niveaux × `playerCount` joueurs (+ `decimalTurns` plis restants).
  Chaque adversaire joue `staticBestMove()` (→ un appel complet au générateur par pli :
  **le point chaud CPU**).
- Résidu en fin de playahead : `calculatePlayerConsideration` (leave) et, au tout dernier
  pli, `calculateSharedConsideration`.
- **Victoire** : partie finie → 1 / 0.5 / 0 selon `spread`. Sinon
  `bogowin(spread + residual, sac + taille chevalet)` (ou `1 − bogowin(…)` si c'est
  l'adversaire qui a la main).
- `SimmedMove::calculateEquity` = Σ score du joueur qui ouvre chaque niveau
  − Σ scores de **tous** les autres joueurs + résidu.
- `Simulator::moves(prune, byWin)` remplit `move.equity` et `move.win` depuis les
  moyennes accumulées.
- Journal XML optionnel (`setLogfile`) ; sa structure est vérifiée par `--mode sim`.

## 7. Fin de partie

- `endgame.cpp` : **pas un minimax exact**. Heuristique optimiste/pessimiste par coup,
  `disappoint()` (meilleure riposte adverse sur quelques coups), puis `reallyPlayOut()`
  qui joue la partie jusqu'au bout en appelant récursivement `Endgame::solve`.
  `win` = 1/0.5/0 selon le spread final ; `equity` = variation de spread.
- `preendgame.cpp` : énumère les chevalets adverses possibles (`Enumerator`, pondérés par
  `probability` et `possibility`), joue chaque candidat puis demande à un `Resolvent`
  imbriqué (`nestedness`) la réponse adverse. Limite : `secondsPerTurn × 6`.

## 8. Rapports d'analyse

- `Reporter::reportPosition` (`reporter.cpp`) : pour chaque position de l'historique,
  `setPosition`, `considerMove(coup joué)`, `moves(10)`, puis affiche
  écart à « best » (equity), position, mot, score, **win × 100 %**, leave.
  Le coup joué hors top-10 est marqué « urp ».
- `StreamingReporter::reportGame` (`quackleio/streamingreporter.cpp`) : boucle sur
  l'historique. C'est ce qu'appelle le menu GUI « Generate report as… »
  (`TopLevel::reportAs` dans `quacker/quacker.cpp`), avec un **clone** du joueur choisi.
- `GraphicalReporter` (HTML) : toujours avec `StaticPlayer`, donc pas de %.
- GUI : `quacker/movebox.cpp` affiche aussi `win` (`formatWinPercentage`).

## 9. Données de stratégie

`DataManager::findDataFile("strategy", lexique, fichier)` cherche, dans l'ordre :
dossier utilisateur puis `data/strategy/<lexique>/`, `csw/` (lexiques csw*), le
*backup lexicon*, puis `default/`. Dans le GUI (`quacker/settings.cpp`), le backup vaut
`default_english` dès que l'alphabet est exactement A–Z — **c'est le cas du français** : une
partie en `ods5` utilise donc les `vcplace` et `syn2` anglais.

| Dossier | Contenu |
|---|---|
| `default/` | `bogowin`, `worths` |
| `default_english/` | `superleaves`, `syn2`, `vcplace`, `worths` |
| `csw/` | `superleaves` |
| `ods5/` | `superleaves`, `worths` (français) |

⚠ Il n'existe qu'une **seule** table `bogowin`, tirée de parties anglaises à 2 joueurs, plafonnée
à 93 tuiles inconnues. Elle sert aussi pour le français et les parties à 3.

Lexiques fournis : `nwl23`, `csw24`, `ods5` (français, ancien), `osps`, etc. (`data/lexica`).

## 10. Build et tests (rappel de `AGENTS.md`)

- Configurer `quacker/` (qui inclut tout). Sur Windows : Qt 6.10.3 dans
  `C:\dev\qt\6.10.3\msvc2022_64`, MSVC + Ninja.
- **Ce poste (2026-11-05)** : CMake 4.5 (`C:\Program Files\CMake\bin`), VS 2026 Build Tools
  (MSVC 14.50, `vcvars64.bat` sous `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build`),
  Qt 6.10.3, Ninja, Python 3.13. Build de test : `build-asan` (RelWithDebInfo + ASan).
  Exécuter les binaires avec `C:\dev\qt\6.10.3\msvc2022_64\bin` dans le PATH.
  Régression complète sur `master` : 25/25, aucune erreur ASan, environ 6 min.
- Harnais : `test/libquackle_test --mode=<positions|report|selfplay|sim|leavecalc|staticleaves|…>`
  `--computer="Championship Player" --seed=N --threads=N --position=…`.
  Ne pas utiliser `quackletest` (legacy).
- Régression : `python test/regression/run_regression.py` (suites `common`,
  `nwl23-english`, `csw24-english`), en Release/RelWithDebInfo, ASan si possible.
  Ne jamais ré-enregistrer les goldens pour faire passer un test.
- `selfplay` ne fait que des parties **à 2 joueurs**, plafonnées à 50 coups.
- Positions de test : `test/positions/*.gcg` (dont `multiplayer-crash.gcg`, 4 joueurs,
  non branchée en régression).
- `clang-format` sur tout fichier modifié ; commentaires minimalistes (voir `AGENTS.md`).
