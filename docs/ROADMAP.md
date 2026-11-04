# Feuille de route — améliorer l'IA de Scrabble classique

Objectifs : (1) corriger le % de victoire des rapports d'analyse, (2) fiabiliser les parties à
3 joueurs, (3) accélérer le moteur, (4) rendre l'IA plus forte. Voir `ARCHITECTURE.md` pour la
carte du code.

Analyse détaillée de l'IA (hypothèses vérifiées, bugs B1–B10, catalogue d'améliorations,
découpage en PR) : `ANALYSE_IA.md`. Plan de travail par phases et PR : `PLAN_IA.md`.

Statut des constats : **[vu]** = lu dans le code, certain ; **[hyp]** = hypothèse à confirmer
par un test ; **[corrigé]** = reproduit puis corrigé.

## A. Le % de victoire dans le rapport d'analyse

Le rapport (`Reporter::reportPosition`) affiche `move.win × 100`. Selon la phase de jeu, `win`
vient de trois sources différentes, chacune avec ses défauts.

1. **[vu] Preendgame affiche une autre grandeur que celle qui sert au tri.**
   `Preendgame::moves` trie par `win` (pondéré par la probabilité des chevalets) puis écrase
   `win = possibleWin` (pondéré par la `possibility`) avant de renvoyer. Le classement et le %
   affiché ne correspondent donc pas ; avec 1–2 tuiles dans le sac, l'ordre peut sembler
   contredire les %.
2. **[vu] Preendgame : coups non évalués affichés à 0 %.** Tous les candidats partent à
   `win = 0`, puis passent à 1 au moment où on les évalue. Si le chrono (`secondsPerTurn × 6`)
   coupe la boucle, les candidats restants gardent 0 %. En cas d'abort en cours de candidat,
   celui-ci garde un % partiel **surestimé** (on part de 1 et on soustrait).
3. **[vu] `highestEquity` initialisé à 0 dans le reporter.** Si toutes les equities simulées
   sont négatives (cas courant quand on est mené), aucun coup n'est marqué « best » et les
   écarts affichés sont faux.
4. **[vu] Bruit statistique.** SmartBogowin simule les candidats avec un nombre d'itérations
   différent (min 2 it/s, max 5 it/s × secondsPerTurn). À 132 itérations l'écart-type d'un % autour
   de 50 % est d'environ ±4,4 points ; un coup classé derrière peut afficher un % supérieur.
5. **[vu] Table `bogowin` hors domaine.** Une seule table (anglais, 2 joueurs, ≤ 93 inconnues)
   sert à tout, y compris le français (102 tuiles, autres valeurs). `bogopoints` utilise `+ 7` en
   dur au lieu de `rackSize()`. Si le fichier n'est pas trouvé, la table vaut 0 partout et
   tous les % de milieu de partie tombent à 0 (seules les playaheads qui finissent la partie
   comptent). De même, en français, `vcplace` (ouverture) et `syn2` sont les fichiers anglais.
6. **[vu] Fin de partie : tout est 0 / 50 / 100 %.** C'est voulu (`reallyPlayOut`), et
   l'endgame est heuristique (pas un minimax exact) : un 100 % peut être faux. Note :
   `Endgame::solve` ignore son paramètre `nestedness` ; seule celle de la position compte.
7. **[vu] Mise en forme.** `precision(4)` + `showpoint` donne `5.000%`, `50.00%`, `100.0%`.
8. **[corrigé] Fin de partie par passes : vainqueur inversé.** Quand la partie se termine par
   6 tours sans score (chevalets bloqués), `incrementTurn` applique déjà la pénalité
   (−chevalet de chacun). Les trois boucles de `endgame.cpp` rappelaient ensuite
   `adjustScoresToFinishGame()`, qui remplaçait cette pénalité par un bonus de sortie
   (+2× chevalets adverses) au profit du dernier joueur à passer : écart faux, et le % passait
   de 100 % à 0 % (ou l'inverse). Reproduit et vérifié par `--mode endgame`.
9. **[corrigé] Coup joué affiché à 0 % en fin de partie.** Le rapport demande l'évaluation du
   coup joué (`considerMove`), mais `EndgamePlayer` (sac vide) et `Preendgame` coupaient leur
   liste au top 10. Hors top 10 (fréquent en finale, où beaucoup de coups sont à 100 %), le
   coup était ajouté sans évaluation : 0 % et écart calculé sur le score brut. Ils renvoient
   désormais le coup considéré évalué, comme `SmartBogowin`. Golden : `report-boys2`
   (tour 11, `5B CAGE` : 0 % → 100 %).

**Prochaine étape :** obtenir un `.gcg` où le % est manifestement faux, noter la ligne du
rapport et le joueur IA utilisé, puis en faire une position de test dans `test/positions/`.
Corriger 1, 2 et 3 en priorité ; ce sont des correctifs locaux.

## B. Parties à N joueurs

Branche `fix/multiplayer`. À 2 joueurs, le comportement est inchangé (goldens identiques).

1. **[corrigé] Equity simulée = mon score − somme des scores de tous les adversaires.**
   `SimmedMove::calculateEquity` et le résidu soustraient désormais l'adversaire *moyen*
   (division par `opponentCount` = N − 1).
2. **[corrigé] Victoire = avance sur le meilleur adversaire, via une table 2 joueurs.** Hors fin
   de partie, P(victoire) = produit des duels contre chaque adversaire (table `bogowin`, avec le
   résidu propre à chaque paire et le bon joueur au trait dans chaque duel). Un duel n'a que 2/N
   des tours restants : inconnues = `sac × 2/N + chevalet`. Fin de partie : égalité en tête à k
   joueurs = 1/k. Hypothèse d'indépendance des duels : approximation, à valider.
3. **[corrigé] Tirage des chevalets adverses faux à 3+.** `randomizeOppoRacks` donnait la
   tuile partielle (`partialOppoRack`) à chaque adversaire, et `setPlayerRack` ne sait reprendre
   des tuiles que dans le sac : un adversaire recevait des tuiles encore sur le chevalet d'un
   autre → tuiles dupliquées et sac incohérent (« PROBLEM PROBLEM » avec `DEBUG_BAG`). Les
   chevalets adverses sont maintenant vidés dans le sac avant le tirage ; à sac vide, chacun
   garde son nombre de tuiles.
4. **[corrigé] Preendgame / Endgame à 3+ = information parfaite.** À plus de 2 joueurs,
   `Resolvent` délègue toujours à `SmartBogowin`, qui à sac vide simule jusqu'à la fin au lieu
   d'appeler l'endgame ; `EndgamePlayer` (« Speedy Player ») revient à l'évaluation statique.
   `GamePosition::setOppRack` et l'`Enumerator` restent 2 joueurs, mais ne sont plus atteints.
5. **[vu] Bruit stdout.** `SmartBogowin::moves` écrit la position entière sur `UVcout` à chaque
   coup.
6. **[corrigé] Couverture.** Le harnais accepte `--players=N` en self-play ; suites : self-play
   3 et 4 joueurs (nwl23, csw24), `positions` et `report` sur `multiplayer-crash.gcg`, et des
   contrôles multijoueurs dans `--mode sim`.

**Prochaine étape :** lancer les suites sous ASan ; mesurer en self-play à 3 (Championship vs
Static) si le produit des duels bat l'ancien calcul ; envisager un P(victoire) estimé
directement par des playouts plus longs plutôt que par la table.

## C. Correction / sûreté des threads

1. **[vu] Course de données probable sur les superleaves.** `StrategyParameters::superleave`
   fait `m_superleaves[leave]` sur un `std::map` partagé : une clé absente **insère**, depuis
   plusieurs threads de simulation en parallèle. Correctif : `find()` en lecture seule (et ne plus
   confondre « valeur 0 » et « absent »).
2. **[hyp]** D'autres écritures paresseuses dans les singletons `QUACKLE_*` : à chercher sous TSan
   (macOS/Linux ; MSVC ne propose que ASan).

## D. Performance

Mesurer d'abord : profiler `--mode sim` et `--mode selfplay --computer="Championship Player"`
(Windows : VS Profiler ; Linux : `perf`).

Points chauds attendus :

- **Générateur appelé à chaque pli de chaque playahead** (`staticBestMove`). Pistes : borne
  supérieure par ancre (« shadow play », comme MAGPIE/wolges) pour ne générer que ce qui peut
  battre le meilleur courant ; cross-checks incrémentaux.
- **Leave lookup** dans un `std::map<LetterString,double>` → table indexée par multiset de
  lettres (hash parfait), ou KLV à la wolges.
- **Copie du `Game` complet par playahead** (`Game game = constants.game`) : historique inclus.
  Une copie de `GamePosition` suffit sans doute.
- `UVcout` dans `SmartBogowin::moves` (voir B.5).
- Équivalence à vérifier : `findstaticbest` sans gaddag est bien plus lent ; toujours générer le
  gaddag.

Garde-fou : les goldens de régression doivent rester identiques pour toute optimisation pure.

## E. Force de l'IA

Prérequis indispensable : **une arène de mesure**. Un mode harnais qui joue K parties entre deux
configurations (seeds appariées, sièges alternés) et donne taux de victoire, spread moyen et
intervalle de confiance. Sans elle, impossible de dire si une idée aide.

Idées, de la moins chère à la plus chère :

1. **Données françaises.** Recalculer `superleaves` et une table `bogowin` pour le lexique
   utilisé (ODS récent si disponible ; `ods5` est ancien) par self-play — les modes `leavecalc` /
   `staticleaves` et `encodeleaves/` existent. Recalibrer l'heuristique de timing de `catchall`.
2. **Allocation des itérations.** Remplacer le schéma min/max fixe par un élagage statistique
   (successive halving / UCB sur la différence au meilleur) : plus d'itérations sur les coups
   serrés, plus de candidats explorés au même temps.
3. **Plus de candidats** une fois le moteur accéléré (13 + n actuellement, élagage à 33 pts).
4. **Inférence** du leave adverse à partir de son dernier coup (`InferringPlayer` est un stub ;
   le `TODO` dans `SmartBogowin::moves` décrit l'algorithme).
5. **Vrai solveur d'endgame** (alpha-bêta + approfondissement itératif, table de transposition),
   et preendgame étendu à plus de tuiles dans le sac.
6. Évaluation apprise (réseau léger sur leave + plateau) — seulement une fois l'arène en place.

## F. Méthode de travail

- Une branche par sujet (`fix/preendgame-win`, `perf/leave-lookup`, …), PR vers `master` du fork.
- Chaque correctif de bug arrive avec une position de test ou un cas de régression.
- Goldens : ne ré-enregistrer que pour un changement de comportement voulu, et le dire.
- Ordre suggéré : A (bugs locaux) → C.1 → arène (E-prérequis) → B → D → E.
