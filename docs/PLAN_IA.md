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

## Avancement

Branches poussées sur `origin`, **empilées** dans cet ordre (chacune part de la
précédente ; les fusionner dans l'ordre). `docs/ai-plan` est à part, sur `master`.

| # | Branche | Contenu | Vérification |
|---|---|---|---|
| 1 | `fix/harness-selfplay` | 0.2 — B11 : `--quiet` jouait 0 coup ; parties complètes ; bilan | 25/25 ; goldens selfplay/playability ré-enregistrés (ligne de bilan ajoutée, rien d'autre) |
| 2 | `test/player-config` | 0.3 — `--config` / `--config2` (itérations, candidats, plis, élagage, `timelimit=0`) | 25/25, goldens inchangés |
| 3 | `test/arena` | 0.4 — `--mode arena` + `test/arena/arena.py` | 26/26 (nouveau golden `arena-speedy-vs-static`) ; Static contre Static : parties exactement symétriques |
| 4 | `ai/deterministic-sim` | B4 + reproductibilité : graine par playahead, nombres aléatoires communs (`crn`) | 26/26 ; résultat identique avec 1 et 4 threads |
| 5 | `fix/quiet-sim` | ROADMAP B.5 : plus de dump de position à chaque coup | — |
| 6 | `fix/pass-candidate` | B1 : la passe est candidate quand le sac est vide | 26/26 ; **pas encore de position de test** (viendra avec 0.6) |
| 7 | `fix/strategy-loading` | B5, B6 (écart interpolé), B7, ROADMAP C.1 | 26/26, goldens inchangés |
| 8 | `fix/bogowin-symmetry` | B3 : référence et challenger comparés au même nombre de tirages | 26/26 ; smoke test ASan Championship |
| 9 | `test/bench` | 0.5 — `--mode bench` ; temps de l'arène par phase (milieu, preendgame, finale) | 26/26 ; golden arène ré-enregistré (format de ligne seulement) |
| 10 | `perf/leaves` | V2 — superleaves en table de hachage ; cache par chevalet des valeurs de leave | 26/26, goldens inchangés ; playaheads ×1,29–1,33 |
| 11 | `tools/win-table` | 5.3 — `--mode wintable` + `test/wintable/wintable.py` (ajustement logistique par nombre de tuiles inconnues) ; table `ods5` apprise sur 200 000 parties Static ; `test/arena/compare.py` (comparaison appariée de deux arènes) | 27/27 (nouveau golden `wintable-static`), autres goldens inchangés |
| 12 | `ai/endgame-solver` | 4.1 (partiel) — solveur exact (alpha-bêta, approfondissement itératif, table de transposition, budget en générations) ; `endgame=N` ; `--mode endgame` | 28/28 (nouveau golden `endgame-solver`), autres goldens inchangés ; lignes prouvées rejouées sous les règles du moteur : 0 écart |
| 13 | `perf/shadow` | V3 (partiel) — `staticBestMove` : ancres explorées par borne d'équité décroissante, arrêt sous le meilleur coup ; `--mode bestmove` | 29/29 (nouveau golden `bestmove`), autres goldens inchangés ; 0 écart sur 3 740 positions ; 37 % (`ods5`) / 44 % (`nwl23`) des ancres explorées ; `staticBestMove` ×1,97 / ×1,27, playaheads ×1,24 / ×1,33 |
| 14 | `ai/inference` | 6.1 — reliquat adverse inféré du dernier coup (vraisemblance exp(β·(équité jouée − meilleure équité)), a priori = tirages) ; chaque playout tire son chevalet partiel adverse selon ces poids ; `infer=N`, `inferbeta=` ; `--mode inference` | 30/30 (nouveau golden `inference`), autres goldens inchangés ; `ods5` : 0,29 bit par tuile du reliquat (β = 0,5), masse sur le vrai reliquat ×3 par rapport à l'a priori ; arène : neutre (49,5 % ± 4,8), coup 40 % plus lent |
| 15 | `perf/endgame-playout` | `Endgame` : les recherches internes des playouts ne rejouent plus elles-mêmes la partie jusqu'au bout (coût exponentiel en nombre de coups restants) | 30/30, ASan 30/30 ; mêmes coups sur 72 positions de finale ; 3 goldens selfplay ré-enregistrés **uniquement** pour le décalage de la suite aléatoire (chaque `Endgame` construit tire un chevalet factice ; la partie 0 est identique coup pour coup) ; finale −16 %, coup Championship 33/82 −8 % |
| 16 | `test/arena-start` | arène à partir du preendgame : `--start N` (Static joue les deux sièges jusqu'à N tuiles dans le sac, même position pour les deux parties d'une paire) | 30/30, goldens inchangés ; Static contre Static : paires symétriques ; ±1,3 point de score sur 400 paires en 35 min |

Non traités pour l'instant : B9 (chrono vérifié entre candidats seulement), branches
existantes `fix/endgame-passout-spread`, `fix/report-considered-move-endgame`,
`fix/multiplayer` (à fusionner d'abord ; `fix/pass-candidate` rend la correction A.8 plus
utile, car les playouts de finale peuvent désormais contenir des passes).

### Mesures (Release, `ods5`, 8 processus × 1 thread)

| Date | A | B | Paires | Score de B | Écart B−A | s/coup |
|---|---|---|---|---|---|---|
| 2026-11-05 | Static | Championship 132/330 | 8 | 75,0 % ± 18,5 | +19 ± 33 | B 12,7 |
| 2026-11-05 | Championship 20/50 `crn=0` | idem `crn=1` | 64 | 53,5 % ± 7,4 | +7,4 ± 15,6 | ≈ 5,4 |
| 2026-11-05 | Static | Championship 132/330 (graine 5000) | 100 | **56,5 % ± 6,2** (Elo +45, +2..+90) | +6,7 ± 16,2 | B 19,8 |
| 2026-11-05 | Championship 33/82 | Championship 132/330 (graine 6000, branche 10) | 100 | **54,0 % ± 6,6** (Elo +28, −18..+75) | +8,5 ± 13,9 | A 8,7 / B 19,7 |
| 2026-11-05 | Static | Championship 132/330, table `ods5` (graine 5000, branche 11) | 100 | **57,0 % ± 6,1** (Elo +49) ; apparié : +0,5 ± 7,8 | +15,7 ± 14,4 ; apparié : +8,9 ± 16,6 | B 17,1 |
| 2026-11-06 | Championship 33/82 (13 candidats) | idem, 30 candidats (graine 8000, branche 14) | 200 | 51,75 % ± 4,25 (Elo +12, −17..+42) | −4,3 ± 9,7 | A 4,8 / B 5,3 |
| 2026-11-06 | Championship 33/82 | idem, `infer=1000` (graine 9000, branche 14) | 200 | 49,5 % ± 4,8 (Elo −3, −37..+30) | −2,4 ± 10,0 | A 4,4 / B 6,1 |
| 2026-11-06 | Static | Championship 33/82, **à partir de 2 tuiles dans le sac** (graine 20000, branche 16) | 400 | **53,25 % ± 1,27** (Elo +23) | **+6,4 ± 1,5** | B 1,5 |

**Arène preendgame (branche 16).** Les effets cherchés sont plus petits que le bruit d'une partie entière, et le preendgame ne pèse que 2 à 3 coups par partie. En faisant jouer Static jusqu'à 2 tuiles dans le sac, puis les deux joueurs à partir de cette même position (sièges inversés), on mesure la fin de partie seule : ±1,3 point de score et ±1,5 point d'écart sur 400 paires en 35 min, contre ±4,25 en 2,4 h pour une arène complète. Championship y gagne +6,4 points par partie sur Static. C'est désormais l'outil de la phase 4 (preendgame, défaut n° 3).

**Inférence (branche 14).** Tirer le reliquat adverse selon la vraisemblance du dernier coup (β = 0,5, 1 000 tirages) ne change pas le résultat : 49,5 % ± 4,8, écart −2,4 ± 10,0, pour un coup 40 % plus lent. L'information existe (×3 de masse sur le vrai reliquat) mais ne se traduit pas en points à ce niveau de jeu : un adversaire Championship joue près du coup statique, et 3 tuiles sur 7 ne pèsent pas assez sur la suite. Inférence gardée désactivée par défaut ; la calibration (6.2) et le cumul sur plusieurs tours (6.3) passent après le preendgame.

**Candidats (branche 14).** Simuler 30 candidats au lieu de 13 (même budget d'itérations par candidat) ne change pas le résultat de façon mesurable : 51,75 % ± 4,25, écart −4,3 ± 9,7. Le meilleur coup se trouve donc presque toujours déjà parmi les 13 premiers coups statiques ; élargir la liste (A7) ou répartir le budget autrement (A3, successive halving) n'est pas prioritaire. Le preendgame prend 54 % du temps à ce budget, contre 45 % pour le milieu de partie : c'est désormais le premier poste de coût (4.2).

**Horizon (branche 11, 4 plis).** Rejouée sur les graines de la référence : 61,0 % ± 6,3
contre Static (Elo +78), soit +4,0 ± 8,8 points par rapport à 2 plis (non significatif, mais
meilleur résultat obtenu) ; coût 23,4 s/coup au lieu de 17,1. Le face-à-face direct à budget réduit
(33/82, 200 paires, graine 7000) **ne le confirme pas** : 4 plis contre 2 plis = 46,6 % ± 4,7
(Elo −23, −56..+9) ; avec peu d'itérations, des playouts plus longs sont plus bruités.
L'horizon seul n'est donc pas le levier (H1 non confirmé en l'état). Préendgame = 47 % du temps
à ce budget : son coût fixe (sans plafond, `timelimit=0`) domine dès que les itérations baissent. Ajustement : les expériences
de recherche se font désormais à 33/82 itérations — le budget ×4 n'apportant rien de mesurable,
cela permet 4 fois plus de paires pour le même temps.

**Finale (branche 12).** Le solveur exact, comparé à l'heuristique actuelle (`Endgame` :
top 320 coups statiques, playouts gloutons, 160 réponses adverses) sur toutes les positions à
sac vide de 30 parties Static en `ods5` : sur 69 positions, il en prouve 47 avec 20 000
générations, et **dans les 47 l'heuristique joue un coup de même valeur exacte** (0 point
perdu). Les positions non prouvées sont les premiers coups de finale avec un chevalet plein
(7 contre 3 à 7 tuiles) : avec ≈ 2 ms par génération, le générateur actuel ne permet pas de
les résoudre. Conséquences :
- le solveur reste **désactivé par défaut** (`endgame=0`) : sans gain mesuré, il ne ferait
  que coûter du temps ; il sert d'outil de mesure et d'étiquetage (corpus 0.6) ;
- le défaut n° 3 (coups « à la Nigel ») n'apparaît pas en fin de finale ; s'il existe, c'est
  au premier coup de finale ou en preendgame — le preendgame imbriqué utilise l'heuristique
  avec 14/7 candidats seulement, ce qui en fait le prochain suspect (4.2) ;
- un solveur sur les finales à chevalet plein exige un générateur bien plus rapide (V3),
  ou une génération incrémentale (coups adverses mis à jour autour des tuiles posées).

**Table de victoire `ods5` (branche 11).** Rejouée sur les mêmes graines que la référence,
la table apprise ne change pas le résultat de façon mesurable (+0,5 point de score, +9 points
d'écart, intervalles larges ; 88 paires sur 100 ont une issue différente, ce qui montre
combien une partie d'arène est sensible au moindre changement de coup). Elle est gardée :
elle est apprise et non codée en dur, et le preendgame tombe de 33 % à 22 % du temps (coup
moyen 17,1 s au lieu de 19,8 s). La table anglaise n'expliquait donc pas le faible gain de la
simulation. Prochaine expérience : l'horizon (4 plis contre 2, mêmes graines), sans code.

**Budget ×4 (branche 10).** Quadrupler les itérations n'apporte pas de gain mesurable sur 100
paires. Le temps se partage 67 % milieu de partie / 33 % preendgame / 0 % finale ; à budget ¼,
le coup moyen prend encore 8,7 s, surtout à cause du preendgame (sans plafond avec
`timelimit=0`). Conséquences :
- la vitesse brute (phase 2) n'est pas le levier principal tant que l'évaluation en fin de
  playout et la structure de recherche restent les mêmes ; on la garde, mais après les
  chantiers qui changent *ce que* la simulation mesure ;
- priorité aux données apprises (5.3 table de victoire, puis leaves), à l'horizon et à la
  sélection des candidats (phase 3), et au preendgame (phase 4, coût et qualité).

**Référence (branche 8).** À plein budget, Championship ne bat Static que de ≈ 45 Elo en
français, pour 20 s par coup ; le 75 % sur 8 paires était du bruit. La simulation actuelle
apporte donc peu. Pistes, à départager avec le corpus et l'arène :
- la table `bogowin` utilisée en `ods5` est celle de `data/strategy/default`, apprise en anglais
  (100 tuiles, autre distribution) ; seuls les superleaves sont propres à `ods5` ;
- 2 plis et 13 candidats : l'horizon court (H1) et la sélection statique des candidats ;
- variance : ± 16 points d'écart par paire, même au plein budget.
Les 3 dernières parties ont pris ≈ 1 600 s de plus que les autres : des preendgames sans
plafond de temps (`timelimit=0`), ce que la décomposition par phase de la branche 9 mesurera.

### Bench (Release, 1 thread, `--seed=1`, 14 positions de milieu de partie × 20 tours)

| Date | Lexique | `staticBestMove`/s | `kibitz(15)`/s | playaheads 2 plis/s |
|---|---|---|---|---|
| 2026-11-05 | `ods5` | 720 | 394 | 545 |
| 2026-11-05 | `nwl23` | 1 739 | 986 | 395 |

Un playahead de 2 plis coûte 1,8 ms en `ods5` et 2,5 ms en `nwl23`, alors qu'une génération
complète coûte 1,4 ms et 0,6 ms : en anglais, l'essentiel du coût d'un playahead est hors du
générateur (copies de position, tirages, évaluation). Cela confirme V1 (`perf/no-copies`)
comme première PR de vitesse, sous réserve du profilage.

Débit observé : 128 parties Championship 20/50 contre lui-même en 44 min. À plein budget
(132/330), compter ≈ 35 paires/heure, soit ≈ 300 paires par nuit : intervalle ≈ ± 4,5 points
de score. Une amélioration de moins de ~3 points de % de victoire ne sera donc pas
mesurable en une nuit ; le corpus de positions (0.6) reste nécessaire pour les effets fins.

### Ajustements du plan

- Ajouté **`ai/deterministic-sim`** en phase 0 (au lieu de la phase 3) : les simulations
  n'étaient pas reproductibles (graines `random_device` dans les threads), ce qui empêchait
  de rejouer une partie d'arène. L'item 3.1 est fait.
- 0.5 (bench) doit **décomposer le temps par phase** (milieu de partie, preendgame,
  finale) : avec `timelimit=0`, le preendgame n'a plus de plafond de temps et pèse lourd.
- 0.6 (corpus) : extraire les positions des parties de l'arène (finales où passer gagne,
  preendgames), plutôt que de les construire à la main — une finale exige les 100 tuiles.
- Après la référence (56,5 %) : **avancer `tools/win-table` (5.3)** — une table de victoire
  apprise sur `ods5` est peu coûteuse (self-play Static) et peut expliquer une partie du
  faible gain de la simulation en français. À tester dans l'arène dès la phase 2 terminée.
- **0.6 (corpus) reporté à la phase 4** : étiqueter une finale (« passer est le seul gain »)
  exige un solveur exact, qui est justement l'objet de la phase 4. La phase 2 passe avant.
- **Profilage (phase 2.1)**, paire d'arène Championship 20/50 en `ods5`, échantillonneur
  maison (pas de profileur VS dans les BuildTools) : 85 % du temps dans `staticBestMove` des
  playouts ; `leaveValue` + `Rack - Move` ≈ 24 % (`std::map` de ~10⁶ superleaves) ; générateur
  (gordongen/goon) ≈ 50 % ; copies de `GamePosition` < 5 %. V1 (`perf/no-copies`) descend donc
  en fin de phase 2 ; V2 (`perf/leaves`) passe en premier, puis le générateur (V3).
  Après `perf/leaves` : équité 14 %, générateur ≈ 56 % en propre.
- Le bench est bruité sur ce portable (cœurs P/LPE, chauffe) : comparer deux binaires en
  alternance, épinglés sur un cœur, meilleur de N passes.
- Build Windows : voir `ARCHITECTURE.md` §10 (suivi des en-têtes cassé sous MSVC en
  français sans `chcp 65001`).

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
