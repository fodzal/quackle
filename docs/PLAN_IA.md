# Plan de travail — IA de Scrabble classique (2 joueurs)

Suite de `ANALYSE_IA.md` (diagnostic, identifiants B*, V*, A*, F*). Ici : l'ordre, le contenu
de chaque PR et le critère qui dit qu'elle est finie.

Règles communes à toutes les PR :
- une branche par PR, PR vers `master` du fork ;
- `clang-format` sur les fichiers touchés, commentaires selon `AGENTS.md` ;
- régression (`run_regression.py`) en RelWithDebInfo + ASan ; goldens identiques pour toute
  PR « perf/ » ou « fix/ » sans effet sur le jeu ; sinon ré-enregistrement **annoncé** dans la PR ;
- toute PR qui change le jeu publie un **résultat d'arène** contre la référence (phase 0) ;
- rien de spécifique à une langue dans le code : ce qui dépend du lexique va dans
  `data/strategy/<lexique>/`, produit par un outil.

## Vue d'ensemble

```
Phase 0  Socle : build Windows, arène, benchmark, corpus          ← tout le reste en dépend
Phase 1  Bugs (petites PR locales)                                ← dès que l'arène tourne
Phase 2  Vitesse (profiler puis optimiser)  ─┐
Phase 3  Recherche milieu de partie         ─┤ 2 et 4 en parallèle possible
Phase 4  Fin de partie (solveur, preendgame) ┘
Phase 5  Données apprises par lexique (français)
Phase 6  Inférence du chevalet adverse
Phase 7  Évaluation apprise (fin de playout, plateau), adversaire simulé
```

Dépendances fortes : 0 → tout ; 2 → hausse des budgets d'itérations ; 3 (nombres aléatoires
communs) → mesures fines des phases 5–7 ; 5 (pipeline self-play) → 6 (calibrage) et 7.

---

## Phase 0 — Socle de mesure

### 0.1 Environnement de build Windows (pas de PR)
Qt 6.10.3 est installé (`C:\dev\qt\6.10.3\msvc2022_64`) ; il manque CMake dans le PATH
(celui de Visual Studio 2022 convient). Objectif : `libquackle_test` + `makegaddag` en
RelWithDebInfo + ASan, puis `run_regression.py` vert sur `master`. Noter la procédure dans
`ARCHITECTURE.md` §10.

### 0.2 PR `fix/harness-selfplay`
Corriger ce qui rend `--mode selfplay` inutilisable pour mesurer :
- **avec `--quiet`, aucun coup n'est joué** (`TestHarness::selfPlayGame` : tout le jeu est
  dans `if (!m_quiet)`) ;
- parties coupées à 50 coups (`playahead = 50`) au lieu d'aller jusqu'au bout ;
- le joueur A commence toujours ;
- aucun bilan (victoires, écart).

Critère : `--mode selfplay --quiet` joue des parties complètes, résultat identique avec ou sans
`--quiet` pour une même graine.

### 0.3 PR `test/player-config`
Pouvoir décrire un joueur IA autrement que par son nom, pour comparer deux réglages du même
code : `--computer="Championship Player" --config=iterations=50,maxiterations=120,candidates=14,plies=2,prune=33,seconds=0`
(et l'équivalent `--config2` pour le second joueur).
- `ComputerParameters` gagne ces champs ; `SmartBogowin` les lit au lieu de ses constantes.
- **Budget en itérations, sans limite de temps** quand `seconds=0` : seule façon d'obtenir des
  parties reproductibles, et ce que l'on veut pour comparer des algorithmes. Le budget en temps
  reste pour le GUI.
- Valeurs par défaut = comportement actuel → goldens inchangés.

### 0.4 PR `test/arena` — l'arène (F1)
Nouveau mode `--mode arena` (ou script Python autour de `selfplay`, si plus simple) :
- **Parties appariées** : pour chaque graine, deux parties avec le même sac, sièges inversés.
  La chance du tirage s'annule en grande partie ; c'est ce qui rend l'arène utilisable avec
  quelques centaines de paires au lieu de milliers de parties.
- **Parallélisme par processus** : N processus × 1 thread de simulation chacun, graines
  disjointes ; un script agrège. Plus efficace et plus reproductible que des threads.
- **Sortie** : CSV par partie (graine, siège, scores, nombre de coups, temps/coup) + bilan :
  % de victoire, écart moyen, intervalle de confiance à 95 %, Elo équivalent.
- **Arrêt séquentiel (SPRT)** : on s'arrête dès que « B est meilleur de ≥ x % » ou « B n'est pas
  meilleur » est établi. Ordre de grandeur : ±2 points de % de victoire demandent ~2 400 parties
  non appariées ; l'appariement et l'écart moyen (plus sensible) réduisent ce chiffre.
- **Lexique** : `ods5` par défaut (`--lexicon=ods5 --alphabet=french`) ; nwl23 en complément
  pour les PR qui ne touchent pas aux données.
- **Joueurs forts** : la validation se fait en itérations fixes, au moins au niveau de
  Championship (voir « Décisions »). Un palier « rapide » (20 itérations) sert seulement à
  écarter les idées clairement mauvaises avant d'y consacrer une nuit.

Critère : un « A contre A » donne 50 % ± l'intervalle annoncé ; « Championship contre Static »
donne un écart net et stable d'une exécution à l'autre.

### 0.5 PR `test/bench` (F2)
`--mode bench` : positions figées (`test/positions/`), mesure `staticBestMove`/s, playouts/s,
temps d'une simulation complète. Sortie lisible par un script pour comparer deux binaires.
Sert de juge de paix pour toute la phase 2.

### 0.6 PR `test/position-corpus` (F3)
Positions annotées avec la bonne réponse, chargées par un mode `--mode puzzles` qui dit si l'IA
la trouve :
- finale où **passer** est le seul gain ; finale « adversaire bloqué » ; coup d'une tuile qui
  gagne ; preendgame où ne pas vider le sac gagne ;
- milieu de partie : échange évident, rééquilibrage.
Sources : parties réelles (`.gcg`), positions construites à la main. Le score au corpus est
suivi de PR en PR (complément de l'arène, beaucoup plus rapide).

### 0.7 Référence
Résultats de référence enregistrés dans `docs/` (bench + arène + corpus sur `master`), pour que
chaque PR se compare à un point fixe.

---

## Phase 1 — Bugs

PR courtes et indépendantes, dans cet ordre :

| PR | Contenu | Vérification |
|---|---|---|
| fusion des branches existantes | `fix/endgame-passout-spread` (A.8), `fix/report-considered-move-endgame` (A.9), `fix/multiplayer` | goldens déjà mis à jour dans ces branches |
| `fix/pass-candidate` | B1 : la passe fait partie des candidats quand l'échange est interdit ; equity évaluée par l'évaluateur, pas −999 | corpus « passer gagne » ; arène (effet attendu surtout en finale) |
| `fix/strategy-loading` | B5 (bornes et `eof` au chargement), C.1 (superleaves en lecture seule, 0 ≠ absent), B6 (arrondi de l'écart), B7 (`spread`) | ASan ; goldens (B6 et C.1 peuvent les changer : annoncer) |
| `fix/bogowin-symmetry` | B3 (1er candidat simulé comme les autres), B9 (chrono vérifié pendant la simulation) | arène |
| `fix/report-win` | ROADMAP A.1, A.2, A.3, A.7 (rapport et preendgame) | positions de test du rapport |
| `fix/quiet-sim` | ROADMAP B.5 (plus d'impression de position à chaque coup) | — |

B8 (leave évalué comme en milieu de partie en fin de playout) et B10 (timing en dur) ne sont pas
traités ici : ils disparaissent avec les phases 5 et 7.

---

## Phase 2 — Vitesse

1. **Profilage** (pas de PR) : VS Profiler sur `--mode bench` et une simulation complète.
   Le résultat ordonne les PR suivantes ; l'ordre ci-dessous est celui attendu.
2. `perf/no-copies` (V1) — playouts sur une `GamePosition` sans historique ; générateur sur
   référence ; `makeMove` sans second générateur.
3. `perf/leaves` (V2) — table de leaves indexée (rang du multiset), une lecture par coup ;
   échanges dédoublonnés par masque de bits.
4. `perf/shadow-play` (V3) — pour `kibitz(1)` : bornes supérieures par ancre, génération dans
   l'ordre décroissant des bornes, arrêt quand la borne passe sous le meilleur trouvé.
5. `perf/crosschecks` (V4, V5) — cross-checks incrémentaux en bitset.
6. `perf/threads` (V6) — passage à l'échelle mesuré de 1 à N threads.

Critère de chaque PR : goldens **identiques**, gain mesuré par `--mode bench`.

**Puis : réinvestir le gain.** Une fois la vitesse acquise, tracer dans l'arène la courbe
« force en fonction du nombre d'itérations » (et du nombre de candidats, de plis) à temps de
réflexion constant. On choisit les nouveaux budgets par défaut sur cette courbe, au lieu de les
multiplier à l'aveugle. PR `ai/budgets`.

---

## Phase 3 — Recherche en milieu de partie

1. `ai/common-random-numbers` (B4) — l'itération i de chaque candidat utilise la même graine
   (même chevalet adverse, même pioche, dans la mesure où les tuiles le permettent). Variance de
   la différence entre coups mesurée avant/après dans un mode de diagnostic.
2. `ai/successive-halving` (A3) — remplace min/max fixes : tous les candidats reçoivent un petit
   lot, on élimine la moitié la moins bonne, on double le lot, etc. ; arrêt anticipé quand le
   meilleur est significativement devant. Budget total inchangé ⇒ plus de candidats examinés.
3. `ai/candidates` (A7) — candidats diversifiés (score, leave, échanges, coups courts) en plus
   du top statique ; seuil de 33 pts supprimé (la phase 2 de l'élagage s'en charge).
4. `ai/adaptive-plies` (A2) — profondeur selon la phase de jeu et l'écart entre candidats.

Chaque étape : arène pleine, SPRT.

---

## Phase 4 — Fin de partie

1. `ai/endgame-solver` (A5) — alpha-bêta en approfondissement itératif, table de transposition
   (Zobrist sur plateau + chevalets), ordre des coups par equity, passe incluse, objectif
   lexicographique (victoire, puis écart). Budget en temps avec meilleure réponse disponible à
   tout moment. Remplace `Endgame::solve` ; l'ancien reste accessible pour comparaison le temps
   de la validation.
   Critères : corpus de finales résolu ; sur des finales aléatoires, jamais pire que l'ancien
   en écart ; temps borné.
2. `ai/preendgame` — 1 à 6 tuiles dans le sac : pour chaque candidat, énumération (ou
   échantillonnage pondéré au-delà de 2–3 tuiles) des tirages et du chevalet adverse, réponse
   adverse par le solveur. Candidats non filtrés par l'equity statique (tous les coups courts
   inclus).
3. `ai/sim-endgame` — dans les simulations qui vont jusqu'à la fin (sac ≤ 14), finir avec une
   version rapide du solveur (profondeur limitée) au lieu du joueur statique.

---

## Phase 5 — Données apprises par lexique

Lexique : **`ods5`** (décision du 2026-11-05).

1. `tools/selfplay-data` — le self-play écrit, pour chaque coup, les éléments nécessaires à
   l'apprentissage (leave, écart, tuiles inconnues, issue de la partie). Volumineux : format
   binaire ou CSV compressé, hors du dépôt.
2. `tools/leaves` (A6) — superleaves pour toute taille de leave, par lexique, avec la méthode
   existante (`leavecalc`/`staticleaves`/`encodeleaves`) automatisée et itérée (on rejoue avec
   les nouvelles valeurs jusqu'à stabilité).
3. `tools/win-table` (A6) — table de victoire par lexique et par nombre de tuiles du jeu
   (102 en français), à la place de la table anglaise. Dépend de B5.
4. `ai/timing` — l'heuristique de timing et les constantes de finale deviennent des données
   apprises (B10).

Validation : arène en français, nouvelles données contre données anglaises actuelles.

---

## Phase 6 — Inférence du chevalet adverse (A4)

1. `ai/inference-core` — pour le dernier coup adverse, vraisemblance de chaque leave possible
   (§H2 de l'analyse) ; tirage pondéré des chevalets adverses dans `randomizeOppoRacks`.
   Optimisation clé : générer les coups une seule fois depuis le « gros chevalet » des tuiles
   inconnues (idée du `TODO` de `SmartBogowin::moves`).
2. `ai/inference-calibration` — β ajusté sur le self-play (adversaire = IA) et sur un corpus de
   parties humaines `.gcg` (adversaire humain). Le GUI pourra exposer le choix.
3. `ai/inference-history` — cumul sur plusieurs tours.

Validation : (a) précision — sur des parties connues, le vrai leave a-t-il une probabilité
plus haute qu'au hasard ? ; (b) arène contre la version sans inférence.

---

## Phase 7 — Évaluation apprise

1. `ai/learned-eval` (A1) — P(victoire) en fin de playout à partir de : écart, tuiles inconnues,
   tuiles fortes restantes, leave, trait, mesures d'ouverture du plateau. Régression logistique
   d'abord (interprétable, rapide), réseau léger ensuite si l'arène le justifie. Remplace la
   table de victoire et corrige B8.
2. `ai/board-eval` (A8) — terme appris d'ouverture du plateau dans l'equity statique.
3. `ai/opponent-model` (A9) — adversaire simulé qui tient compte du score.

---

## Décisions (2026-11-05)

1. **Lexique français : `ods5`** (déjà dans le dépôt, avec `superleaves` et `worths`).
2. **Temps de réflexion dans le GUI** : celui du joueur choisi (Twenty Second, Championship,
   Five Minute). Les gains de vitesse servent à relever le nombre d'itérations que chaque joueur
   fait dans son temps.
3. **Arène = joueurs forts** : budget **en itérations fixes** (sans limite de temps) au moins
   égal à celui de Championship (132 / 330 itérations, 14 candidats), plus élevé une fois le
   moteur accéléré. Le palier « rapide » ne sert qu'à éliminer les idées clairement mauvaises.
4. **Machine : ce poste** — Intel Core Ultra 7 258V, 8 cœurs (4 performants + 4 basse
   consommation), 32 Go, portable. Conséquences :
   - peu de parties fortes par nuit : l'arène doit tirer le maximum de chaque partie (parties
     appariées, écart moyen comme métrique principale, arrêt séquentiel) ;
   - 8 processus × 1 thread, avec des cœurs inégaux : le budget en itérations (pas en temps)
     rend les résultats indépendants du cœur sur lequel tombe chaque partie ;
   - portable : secteur branché, profil d'alimentation « performances », veille désactivée
     pendant les longues séries ; le temps par coup est relevé pour repérer le bridage thermique ;
   - le corpus de positions (0.6) est le test rapide de tous les jours ; l'arène tourne la nuit ;
   - la durée réelle d'une partie forte sera mesurée par `--mode bench` (0.5), et le nombre de
     paires visé par validation fixé à ce moment-là.
