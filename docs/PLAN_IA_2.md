# Plan de travail n° 2 — IA de Scrabble classique (2 joueurs)

Fait suite à `PLAN_IA.md` (18 branches, 2026-11-05 → 2026-11-07) et à son bilan, `BILAN_IA.md`.
Les identifiants B\*, V\*, A\* renvoient à `ANALYSE_IA.md`.

## 1. Pourquoi un nouveau plan

Le premier plan cherchait à **chercher plus** : plus de candidats, plus de plis, plus d'itérations,
un preendgame plus large, l'inférence. Tout a été mesuré, et rien n'a rapporté :
- Championship ne bat Static que d'environ 45 Elo en `ods5` (56,5 % ± 6,2) ;
- un budget ×4, 30 candidats, 4 plis, l'inférence : neutres ;
- les finales et les preendgames trouvent déjà les bons coups.

La simulation est donc limitée par **ce qu'elle mesure**, pas par la quantité de calcul. Trois
choses décident de tout, et aucune n'a été apprise pour `ods5` :

| Élément | Rôle | État en `ods5` |
|---|---|---|
| Equity statique (score + reliquat + timing) | choisit les candidats **et** joue les deux camps des simulations | reliquats de 2013 (ancien lexique), timing et repli de reliquat calibrés en anglais (B10), aucun terme de plateau |
| Évaluation en fin de simulation | `bogowin(écart + reliquat, tuiles inconnues)` | table `ods5` apprise (branche 11), mais deux variables seulement ; reliquat de milieu de partie même sac presque vide (B8) |
| Constantes de finale | `−8 − 2,61 × reliquat` | anglaises, en dur |

Le nouveau plan porte sur ces trois éléments, dans cet ordre. La recherche et la vitesse ne
reviennent qu'ensuite, quand une meilleure évaluation leur donne quelque chose à exploiter.

Les règles du premier plan restent valables : une branche par PR, `clang-format`, régression en
RelWithDebInfo + ASan, ré-enregistrement des goldens annoncé, résultat d'arène pour toute PR qui
change le jeu, et rien de propre à une langue dans le code : les données vont dans
`data/strategy/<lexique>/` et sont produites par un outil.

## 2. Vue d'ensemble

```
Chantier 0  Consolider : fusionner les 18 branches, finir les bugs ouverts
Chantier 1  Mesurer vite : arène statique, tables par joueur, arrêt séquentiel   ← tout le reste en dépend
Chantier 2  Reliquats ods5 appris par self-play, itérés jusqu'à stabilité         ← levier principal
Chantier 3  Timing, premier coup et finale appris (B10, vcplace, B8)
Chantier 4  Évaluation de fin de simulation apprise (A1)
Chantier 5  Plateau et adversaire dans l'equity statique (A8, A9)
Chantier 6  Re-tester la recherche, puis la vitesse, sur la nouvelle évaluation
```

Dépendances : 0 → 1 → 2 → 3 ; 4 peut commencer dès que 2.1 (données de self-play) existe ;
5 après 2 ; 6 en dernier.

## 3. Protocole de mesure

Chaque idée passe par des paliers, du moins cher au plus cher. Elle s'arrête au premier palier
où elle échoue.

| Palier | Mesure | Coût | Sert à |
|---|---|---|---|
| 1. Hors ligne | log-loss / score de Brier sur des parties de validation, jamais vues à l'apprentissage | secondes | trier les modèles d'évaluation avant de jouer |
| 2. Arène statique | Static-X contre Static-Y, parties appariées | minutes | reliquats, timing, terme de plateau |
| 3. Arène preendgame | `--start N`, ±1,3 point en 35 min | ~ 1 h | finale, preendgame |
| 4. Arène Championship | parties entières, ±4,25 points en 2,4 h | une nuit | **validation finale** de toute PR qui change le jeu par défaut |

Règles :
- le palier 4 est obligatoire avant de changer un défaut : un gain entre deux Static doit
  encore se voir quand Championship simule avec les nouvelles données ;
- arrêt séquentiel (SPRT, 1.3) dès le palier 2 : on fixe d'avance H0 = 0 Elo et H1 = +10 Elo,
  et l'arène s'arrête dès que l'une des deux est acceptée ;
- les graines des arènes de validation ne servent jamais à l'apprentissage.

---

## Chantier 0 — Consolider

1. **Fusionner les 18 branches dans `master`**, dans l'ordre de `PLAN_IA.md`. Les textes des PR
   sont prêts (`PR_IA.md`). Critère : `master` passe la régression sous ASan.
2. **Finir les branches de Philippe** : `fix/multiplayer`, `fix/endgame-passout-spread`,
   `fix/report-considered-move-endgame`.
3. **`fix/report-win`** : % de victoire faux dans le rapport d'analyse (ROADMAP A.1, A.2, A.3,
   A.7). C'est le bug signalé au départ ; il ne change pas la force, mais il fausse ce que
   l'utilisateur lit.
4. B9 (chrono vérifié entre deux candidats seulement) et B13 (plantage quand l'alphabet est
   introuvable) : petites PR à part.

## Chantier 1 — Mesurer vite

L'arène actuelle compare des joueurs qui simulent : une nuit donne ±4,5 points. Pour apprendre
des données, il faut des centaines d'essais, donc un palier beaucoup plus rapide.

1. **`ai/per-player-strategy`** — aujourd'hui, les tables de stratégie sont un singleton
   (`QUACKLE_STRATEGY_PARAMETERS`, dans `DataManager`) : les deux joueurs d'une partie ont
   forcément les mêmes reliquats. Chaque évaluateur reçoit son propre `StrategyParameters`, le
   global restant le défaut. Option de joueur `strategy=<répertoire>`.
   Critère : goldens inchangés ; Static contre Static avec deux copies de la même table donne
   des paires exactement symétriques.
2. **`test/arena-static`** — l'arène accepte deux joueurs statiques avec des tables différentes,
   et `arena.py` répartit des milliers de paires sur les 8 cœurs. Mesurer en tête de chantier le
   débit réel (paires par minute) et la précision obtenue en 10 min, et les noter ici.
   **Mesuré le 2026-11-26** (Release, 8 processus, Static contre Static sans superleaves) :
   **6 300 paires par minute**. 30 000 paires en 4 min 46 s donnent ±0,36 point de score
   (±2,5 Elo) et ±0,77 point d'écart par partie ; **en 10 min, environ ±1,7 Elo**. Pour
   comparaison, une nuit d'arène Championship donne ±4,25 points de score.
3. **`test/arena-sprt`** — arrêt séquentiel dans `arena.py` (SPRT sur les paires), pour tous
   les paliers.
4. **Adversaire de référence** — garder, pour le palier 4, une version figée de Championship
   (avec les données actuelles) en plus de Static. Static est très prévisible, ce qui
   avantage ou pénalise certaines idées (l'inférence, par exemple).
   **Fait** : Championship 33/82, `timelimit=0`, avec les tables `ods5` figées à `2cb0782` par
   `test/arena/freeze.py`. Seules les données sont figées : les deux joueurs tournent dans le
   même binaire, donc un changement du moteur atteint aussi la référence.

## Chantier 2 — Reliquats `ods5` appris (levier principal)

Le reliquat entre deux fois dans chaque décision : dans le choix des candidats et dans les
deux camps de chaque simulation. Les valeurs actuelles datent de 2013, pour un ancien lexique,
et le repli (`vcvalues`, synergies `syn2`, seuils) est calibré en anglais.

1. **`tools/selfplay-data`** — `--mode selfplay` écrit un enregistrement par coup : chevalet,
   coup, reliquat, score du coup, points marqués au coup suivant, tuiles dans le sac, écart,
   issue de la partie. Format CSV compressé, **hors du dépôt**. Il sert aussi aux chantiers 3
   et 4.
2. **`tools/leaves`** — valeur d'un reliquat R = moyenne, sur les coups qui laissent R, de ce
   que le joueur marque au coup suivant (score + reliquat suivant), moins la moyenne sur tous
   les coups. Les reliquats rares (5 ou 6 tuiles) sont lissés par un modèle
   « tuiles + paires + doublons » ajusté sur les mêmes données : ce modèle remplace aussi le
   repli codé en dur. Un script Python produit `superleaves`, encodé avec l'outil existant
   (`encodeleaves`).
3. **Itération** — on rejoue le self-play avec la génération k pour apprendre la génération k+1,
   jusqu'à ce que deux générations successives soient neutres en arène statique (SPRT).
4. **Variante par phase de jeu** — une valeur de reliquat qui dépend aussi du nombre de tuiles
   dans le sac, par tranches. C'est générique (aucune langue), et c'est la forme naturelle du
   timing (chantier 3). À garder si l'arène statique la préfère à la table unique.

Validation :
- palier 2 : Static (nouvelles valeurs) contre Static (valeurs de 2013) ;
- palier 4 : Championship (nouvelles valeurs) contre Championship (valeurs de 2013).

Les nouvelles valeurs deviennent le défaut seulement si le palier 4 est favorable.

## Chantier 3 — Timing, premier coup et finale appris

1. **`ai/timing`** (B10) — `heuristicArray[13]`, indexé par `sac − tuiles + 7`, devient une table
   par lexique, apprise sur les données de 2.1 (ou absorbée par 2.4 si la variante par phase est
   retenue). L'index ne suppose plus 7 tuiles au chevalet.
2. **`ai/opening`** — `ods5` n'a pas de `vcplace` : le placement du premier coup n'est pas évalué
   en français. Apprendre la table par self-play (même méthode que 2.2, sur le premier coup
   seulement), ou la remplacer par une simulation courte du premier coup.
3. **`ai/endgame-constants`** — `−8 − 2,61 × reliquat` (sac vide, coup qui ne termine pas) devient
   une régression apprise : sur les finales résolues par le solveur exact (branche 12), et sur
   les finales du self-play. Palier 3.

## Chantier 4 — Évaluation de fin de simulation apprise (A1)

La table de victoire ne connaît que l'écart et le nombre de tuiles inconnues. Elle ne sait pas
si le plateau est ouvert, quelles tuiles restent, ni ce que vaut notre chevalet en fin de partie
(B8).

1. **Données** — à partir de 2.1, des positions à l'horizon de la simulation, avec : écart,
   tuiles inconnues, blancs et lettres chères inconnus, notre reliquat (valeur et taille), le
   trait, et des mesures génériques d'ouverture du plateau (lignes où un scrabble est possible,
   cases « mot compte triple » accessibles). Étiquette : issue de la partie.
2. **Modèle** — régression logistique par tranche de tuiles inconnues, la généralisation directe
   de `tools/win-table`. Réseau léger seulement si le palier 1 montre un gain net sur la
   régression. Coefficients dans `data/strategy/ods5/`.
3. **`ai/learned-eval`** — `SmartBogowin` utilise ce modèle à la place de `bogowin` (option
   `eval=learned`, désactivée par défaut).
   Critère d'entrée : log-loss meilleure que `bogowin` au palier 1.
   Critère de sortie : palier 4 favorable.
   Mesurer aussi le coût par coup, puisque les mesures de plateau ne sont pas gratuites.

## Chantier 5 — Plateau et adversaire dans l'equity statique

1. **`ai/board-eval`** (A8) — `sharedConsideration` vaut 0 : l'equity statique ignore la défense.
   Ajouter un terme appris sur les données de 2.1 : points marqués par l'adversaire au coup
   suivant, selon ce que notre coup ouvre (mêmes mesures de plateau qu'en 4.1). Ce terme agit à
   trois endroits : il change les candidats, l'adversaire simulé et notre propre jeu simulé.
   Paliers 2 puis 4.
2. **`ai/opponent-model`** (A9) — l'adversaire simulé tient compte du score : il ouvre quand il
   est mené, il ferme quand il mène. Seulement après 5.1 et 4.3.

## Chantier 6 — Recherche et vitesse, sur la nouvelle évaluation

Les mesures du premier plan valent pour l'évaluation actuelle. Avec une meilleure évaluation,
des simulations plus longues ou plus larges peuvent devenir rentables. On rejoue donc les
expériences neutres, avec les mêmes réglages et en palier 4 :
- 4 plis contre 2 ;
- 30 candidats contre 13 ;
- budget ×4 ;
- `infer=1000`.

Ensuite, selon les résultats :
- *successive halving* (A3) et plis adaptatifs (A2), si l'élargissement devient positif ;
- vitesse, si un chantier en a besoin : `perf/no-copies` (V1), cross-checks incrémentaux
  (V4, V5), passage à l'échelle en threads (V6). Le générateur prend encore ≈ 42 % du temps.

## 4. Ce qui n'est pas prioritaire

- Inférence sur plusieurs tours et calibration de β (6.2, 6.3 du premier plan) : l'information
  existe déjà mais ne change pas assez de décisions. À reprendre face à un adversaire humain.
- Solveur exact à chevalet plein et `ai/sim-endgame` : aucune trace de gain, coût élevé.
- Élargir le preendgame : mesuré neutre deux fois.

## 5. Avancement

| # | Branche | Chantier | État | Résultat |
|---|---|---|---|---|
| 0.1 | les 18 branches de `PLAN_IA.md` | 0 | prêtes, pile linéaire sur `master` | régression 30/30 sous ASan au sommet (`ai/preendgame-inference`) |
| 19 | `fix/endgame-passout-spread` | 0 | rebasée | mode du harnais renommé `passout` (`endgame` est pris par le solveur) |
| 20 | `fix/report-considered-move-endgame` | 0 | rebasée | `report-boys2` ré-enregistré : un écart d'equity statique (42 → 44), dû aux corrections de leaves de la branche 7 |
| 21 | `fix/multiplayer` | 0 | rebasée | tirages par playahead de la pile et duels multijoueurs fusionnés ; interpolation `double` de la table gardée ; goldens 2 joueurs identiques ; goldens 3 et 4 joueurs : seule la ligne `SELFPLAY SUMMARY` s'ajoute |
| 22 | `fix/report-win` | 0 | prête | A.1, A.2, A.3, A.7 ; `report-logan` et `report-boys2` ré-enregistrés (format des %, et « best » quand toutes les equities sont négatives) |
| 23 | `fix/sim-deadline` | 0 | prête | B9 ; sans effet avec `timelimit=0`, donc pas d'arène possible |
| 24 | `fix/missing-alphabet` | 0 | prête | B13 ; sortie 1 avec un message |
| 25 | `ai/per-player-strategy` | 1.1 | prête | goldens inchangés (38/38 sous ASan) ; Static contre Static avec une copie de la table `ods5` sur un côté ou sur les deux : les mêmes 400 paires coup pour coup, toutes 1-1, écart 0 ; idem avec Championship 10/20 (32 paires, 2 threads) |
| 26 | `test/arena-static` | 1.2 | prête | 6 300 paires par minute ; ±1,7 Elo en 10 min ; sans superleaves : −29 Elo (−32..−27), −12,2 ± 0,8 points |
| 27 | `test/arena-sprt` | 1.3 | prête | SPRT 0/+10 simulé : 4 % de H1 à 0 Elo, 97 % à +10 ; en arène, sans superleaves : décidé en 600 paires (H0) et, sièges inversés, en 1 000 paires (H1) |
| 28 | `test/arena-reference` | 1.4 | prête | Static sur les tables figées : les mêmes 400 paires que sur celles du dépôt |

Sommet de la pile (`fix/missing-alphabet`) : régression 38/38 sous ASan (RelWithDebInfo), le 2026-11-26.
