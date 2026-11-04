# Analyse de l'IA : défauts, bugs, pistes d'amélioration

Périmètre : Scrabble classique, **2 joueurs**. Document d'entrée pour le plan de travail
(puis les PR). Complète `ROADMAP.md` (bugs déjà connus) et `ARCHITECTURE.md` (carte du code).

Statuts : **[vu]** = constaté dans le code ; **[hyp]** = à confirmer par une mesure.
Les fonctions sont citées par nom (les numéros de ligne bougent).

Principe directeur, demandé explicitement : **aucun comportement codé en dur pour une langue.**
Tout ce qui ressemble à du savoir de joueur (garder le S, jeter le Q, pêcher un scrabble, bloquer)
doit émerger de la recherche ou de paramètres **appris par self-play** pour chaque lexique.

---

## 1. Comment l'IA décide aujourd'hui (rappel en 10 lignes)

```
sac > 2 tuiles   SmartBogowin
                   1. kibitz(13 + n) par equity statique (score + leave + timing)
                   2. on ne garde que les coups à moins de 33 pts du meilleur
                   3. chaque candidat : 132 tirages (Championship), 330 s'il peut battre le meilleur
                      tirage = chevalet adverse uniforme + pioche aléatoire,
                               on joue 2 plis (adversaire, nous) avec le joueur STATIQUE,
                               puis P(victoire) = table bogowin(écart + leave, tuiles inconnues)
                      (si sac ≤ 14 : jusqu'à la fin de partie, toujours avec le joueur statique)
                   4. tri par % de victoire
1–2 tuiles       Preendgame : candidats de SmartBogowin × chevalets adverses énumérés
sac vide         Endgame : playouts gloutons + une riposte adverse (pas de minimax)
```

---

## 2. Vos hypothèses, vérifiées

### H1. « On ne regarde que 3 coups ; cela ne dit pas qui gagnera en fin de partie » — **juste, à nuancer** [vu]

- La simulation joue bien **3 plis** (notre candidat, la réponse adverse, notre réponse ;
  `plies = 2` dans `SmartBogowin::moves`, +1 pour le candidat dans `Simulator::simulate`).
- L'IA ne compare pas un simple écart : elle le convertit en **probabilité de victoire** par la table
  `bogowin(écart + résidu, tuiles inconnues)`. Le défaut réel est que **cette table ne connaît que
  deux nombres** : l'écart et le nombre de tuiles inconnues. Elle ignore :
  - l'état du plateau (ouvert/fermé, couloirs de scrabble, cases rouges accessibles) ;
  - *quelles* tuiles restent (blancs, S, lettres chères ; le paramètre `blanks` existe mais est ignoré) ;
  - le fait que le résidu (`calculatePlayerConsideration` = valeur du leave) est la seule trace
    de notre chevalet ;
  - la langue et le nombre de tuiles (table anglaise plafonnée à 93 inconnues).
- Conséquence : à 3 plis, « +20 avec un plateau fermé » et « +20 avec un plateau grand ouvert »
  valent le même %. C'est précisément ce qui fait que l'horizon court « ne préconfigure pas qui va
  gagner ».

Remèdes (génériques) : voir A1 (évaluation de fin de playout apprise), A2 (profondeur variable),
A3 (meilleure gestion du temps et du bruit).

### H2. « L'IA devrait inférer les lettres adverses » — **juste, rien n'existe** [vu]

- Les chevalets adverses sont tirés **uniformément** dans les tuiles inconnues
  (`Simulator::randomizeOppoRacks`, commentaire `TODO -- some kind of inference engine`).
- `InferringPlayer` est un squelette : `m_parameters.inferring` ne fait qu'imprimer la position
  précédente. Le `TODO` de `SmartBogowin::moves` décrit l'algorithme prévu.
- Le seul mécanisme existant est `setPartialOppoRack`, réglé **à la main** dans le GUI.

Tous vos exemples (échange → pêche au scrabble ou rejet du Q ; S non joué → pas de S ;
petit coup → rééquilibrage ; ouverture d'un couloir → il a la lettre qui va devant) sont des cas
particuliers d'un **seul** calcul bayésien, sans aucune règle en dur :

```
P(leave | coup observé)  ∝  P(coup observé | chevalet = leave + tuiles jouées) × P(leave)
P(coup | chevalet)       =  softmax_β( equity(coup) − equity(meilleur coup de ce chevalet) )
```

- Un chevalet avec un S aurait eu un bien meilleur coup → vraisemblance faible → le S devient
  improbable.
- Après un échange de 5 tuiles, les leaves de 2 tuiles qui justifient l'échange (bonnes
  lettres gardées) deviennent probables ; l'échange d'un Q se retrouve tout seul (le Q n'est
  plus dans le leave).
- Le paramètre β (« rationalité » de l'adversaire) se **calibre** : sur des parties d'IA
  (self-play) et sur des parties humaines (`.gcg`). Un β bas = adversaire faible, peu
  d'information tirée de ses coups.
- On peut cumuler sur plusieurs tours (le leave inféré au tour t est la base du chevalet au tour
  t+1, une fois la pioche ajoutée).

Voir A4 pour la mise en œuvre et le coût.

### H3. « En fin de partie, on n'explore pas les coups à 2 points “à la Nigel” » — **juste** [vu]

Plusieurs causes cumulées :

1. **La passe n'est jamais un candidat.** `Generator::findstaticbest` initialise `best` à la
   passe (equity −999) mais ne l'ajoute à la liste que si **aucun** autre coup n'existe. Or en
   finale, passer (quand l'adversaire est bloqué ou doit ouvrir) est parfois le seul coup gagnant.
   Vrai aussi pour l'endgame, le preendgame et les simulations. **C'est un bug** (B1).
2. **L'endgame n'est pas une recherche.** `Endgame::solve` : chaque candidat est joué puis la
   partie se termine en **glouton** (`staticBestMove` des deux côtés) ; `disappoint` teste
   au plus 160 ripostes adverses, mais la suite reste gloutonne. Un petit coup ne vaut que par la
   **suite** qu'il prépare (deux coups pour sortir, blocage, adversaire coincé avec une lettre) :
   le glouton ne trouve pas cette suite, donc le petit coup est sous-évalué.
3. **Les coups imbriqués sont très élagués** : en preendgame, l'endgame interne ne regarde que
   14 coups (7 ripostes), et 1 seul au niveau d'imbrication 2.
4. **Le choix se fait sur l'écart, pas sur la victoire** (`bestPessimistic` est un écart). Gagner
   de 1 sûrement vaut mieux que gagner de 30 « en moyenne ».
5. **Preendgame (1–2 tuiles)** : ses candidats sont ceux de `SmartBogowin`, donc **filtrés par
   l'equity statique** (top 43, à moins de 33 pts). Un coup d'une tuile qui ne vide pas le sac
   peut être éliminé avant d'être évalué. Au-delà de 2 tuiles dans le sac, plus de preendgame : la
   simulation va jusqu'au bout avec le joueur statique, qui joue mal les finales.
6. L'equity statique de fin de partie (`CatchallEvaluator::endgameResult` :
   `−8 − 2,61 × valeur du leave`) est une constante en dur, calibrée à la main.

Remède : un vrai solveur (A5) — il retrouve ces coups tout seul, aucune heuristique « Nigel ».

---

## 3. Bugs détectés (nouveaux, absents de `ROADMAP.md`)

| # | Constat | Où | Impact |
|---|---|---|---|
| B1 | **La passe n'est jamais proposée** quand un autre coup existe [vu] | `Generator::findstaticbest`, `Move::createPassMove` | Finales : coup gagnant introuvable (H3). Aussi absent de la liste du GUI. |
| B2 | *Choix voulu, pas un bug.* Les itérations sont des nombres fixes (2 et 5 it/s × `secondsPerTurn`, soit 132 / 330 pour Championship) pour borner la durée [vu] | `SmartBogowin::minIterations/maxIterations` | Un moteur plus rapide finit plus tôt sans jouer mieux : le gain de vitesse devra être réinvesti en relevant ces budgets (voir `PLAN_IA.md`, phase 2). |
| B11 | **`--mode selfplay --quiet` ne joue aucun coup** : tout le jeu est dans `if (!m_quiet)` ; parties aussi coupées à 50 coups [vu] | `TestHarness::selfPlayGame` | Bloque l'arène ; résultats de self-play silencieux sans valeur. |
| B3 | **Le 1er candidat n'est jamais simulé au maximum** (132 tirages) alors que ses challengers peuvent l'être (330) [vu] | `SmartBogowin::moves` | Comparaison biaisée : le favori statique est le plus bruité ; le seuil `1.96·35/√n` part d'une référence imprécise. |
| B4 | **Pas de nombres aléatoires communs** : chaque candidat voit des chevalets adverses et des pioches différents [vu] | `Simulator::simulateOnePosition` (tirage par playahead), candidats simulés un par un | Variance de la *différence* entre deux coups bien plus grande que nécessaire ; à budget égal, classement beaucoup plus bruité. |
| B5 | **Débordement de tableau possible** au chargement : `loadBogowin` écrit `m_bogowin[lead+300][unseen]` sans borne (tableau 601 × 94) ; boucles `while (!eof)` (dernier enregistrement relu) [vu] | `StrategyParameters::loadBogowin`, `loadSuperleaves` | Une table française (102 tuiles, > 93 inconnues) écraserait la mémoire. Bloquant pour A6. |
| B6 | `bogowin((int)lead, …)` : troncature vers 0, donc −0,9 → 0 et +0,9 → 0 [vu] | `Simulator::simulateOnePosition` | Petit biais en faveur des positions légèrement perdantes. |
| B7 | `GamePosition::spread` initialise le meilleur adversaire à 0 [vu] | `game.cpp` | Écart faux si l'adversaire a un score négatif (fin par passes depuis 0). Rare. |
| B8 | Le leave en fin de playout est évalué avec la table de **milieu de partie**, même sac vide ou presque [vu] | `calculatePlayerConsideration` dans la simulation | Résidu faux en fin de partie, là où chaque point compte. |
| B9 | Le chrono n'est vérifié qu'**entre** deux candidats [vu] | `SmartBogowin::moves` | Dépassement de temps possible ; mineur. |
| B10 | Heuristique de timing indexée par `sac − tuiles + 7` (taille de chevalet en dur) et calibrée en anglais [vu] | `CatchallEvaluator::equity` | Faux pour toute autre taille de chevalet ; mal calibré en français. |

Rappel des bugs déjà listés dans `ROADMAP.md` et toujours ouverts : A.1–A.7 (% du rapport,
preendgame), C.1 (course de données sur les superleaves), B.5 (sortie console).
Les correctifs A.8 et A.9 sont sur les branches `fix/endgame-passout-spread` et
`fix/report-considered-move-endgame`, pas encore dans `master`.

---

## 4. Vitesse

### 4.1 Prérequis
- Les budgets d'itérations sont fixes (B2) : après chaque gain de vitesse, les relever
  (choix guidé par l'arène), sinon le gain ne profite pas au jeu.
- Mesurer avant d'optimiser : profileur sur `--mode sim` et `--mode selfplay` ; un mode de
  benchmark du harnais (positions fixes, playouts/s) servira de référence pour chaque PR.
- Garde-fou : goldens de régression identiques pour toute optimisation pure.

### 4.2 Points chauds probables [hyp, à profiler]
Par pli de playout (`staticBestMove`), on paie aujourd'hui :

1. **Copies de `GamePosition`** : `Generator(const GamePosition&)` copie la position (plateau
   + liste de coups) à chaque `kibitz` ; `GamePosition::makeMove` en construit un **second**
   pour poser le coup, puis recopie le plateau ; `Game::commitCandidate` ajoute encore une
   position à l'historique ; et chaque playahead copie le `Game` complet, historique compris.
2. **Échanges** : quand le sac a ≥ 7 tuiles, `Generator::exchange` évalue les **127**
   sous-ensembles du chevalet, chacun avec un `std::map` de dédoublonnage et deux recherches de
   leave. À chaque pli.
3. **Valeur des leaves** : `std::map<LetterString,double>` (recherche en O(log n) avec
   comparaison de chaînes), appelée **deux fois** par coup (`superleave` testé puis relu), et
   `operator[]` qui insère (C.1).
4. **Génération exhaustive** alors qu'on ne veut que le meilleur coup.

### 4.3 Pistes, de la moins risquée à la plus ambitieuse
| # | Piste | Gain attendu |
|---|---|---|
| V1 | Supprimer les copies : générateur qui travaille sur une référence ; playahead sur une `GamePosition` (pas un `Game`) ; pas d'historique dans les playouts | important |
| V2 | Leaves : table indexée par multiset (hash parfait / rang combinatoire), une seule lecture, aucune insertion ; idem pour les échanges (dédoublonnage par masque de bits) | important |
| V3 | Borne supérieure par ancre (« shadow play », comme MAGPIE / wolges) : générer d'abord les ancres qui peuvent battre le meilleur courant, s'arrêter dès que la borne est sous lui | très important pour `staticBestMove` |
| V4 | Cross-checks incrémentaux (ne recalculer que les cases voisines du coup posé) | moyen |
| V5 | Structures de données : cross-checks en bitset, compteurs de lettres en tableau, pas d'allocation dans la boucle | moyen |
| V6 | Parallélisme : vérifier le passage à l'échelle des threads (file de messages, taille des paquets), éviter le faux partage | à mesurer |

---

## 5. Autres défauts de force (au-delà de H1–H3)

1. **Choix des candidats par equity statique** (top 13 + n, élagage à 33 pts) [vu]. Un coup
   défensif, un petit coup de rééquilibrage ou un échange peut être éliminé avant toute
   simulation. Aucun moyen de rattraper un bon coup statiquement mal noté.
2. **Allocation des tirages naïve** [vu] : min/max fixes, seuil avec un écart-type supposé
   (35 pts), pas d'arrêt anticipé quand un coup est clairement meilleur, pas de relance des
   candidats serrés.
3. **L'adversaire simulé est le joueur statique** [vu] : il ignore le score (ne cherche pas à
   ouvrir quand il est mené, à fermer quand il mène). Modèle d'adversaire = biais systématique.
4. **Aucune évaluation du plateau** [vu] : `sharedConsideration` vaut 0 dans `CatchallEvaluator`.
   L'equity statique (qui choisit les candidats **et** les coups de l'adversaire simulé) ignore
   la défense.
5. **Données de stratégie anglaises en français** (déjà noté `ROADMAP.md` A.5) : `bogowin`,
   `syn2`, `vcplace`, timing, constantes d'endgame. Les `superleaves` `ods5` existent mais datent
   d'un ancien lexique.
6. **Pas d'arène de mesure** : on ne peut pas savoir si une idée aide (déjà noté `ROADMAP.md` E).

### Inventaire de ce qui est « en dur » et devrait être appris
| Constante | Où | Remplacement générique |
|---|---|---|
| `heuristicArray[13]` (timing) | `CatchallEvaluator::equity` | régression sur self-play, par lexique |
| `−8 − 2,61 × leave` (finale) | `CatchallEvaluator::endgameResult` | solveur (A5) ou régression |
| `vcvalues[8][8]`, seuil `−5,5`, synergie `1,5×` | `ScorePlusLeaveEvaluator::leaveValue` | superleaves complètes par lexique (le repli devient inutile) |
| table `bogowin` (anglais) | `data/strategy/default` | évaluation apprise (A1/A6) |
| `33` pts, `13` candidats, `35` (écart-type), `2`/`5` it/s | `SmartBogowin` | élagage statistique (A3) |
| `2` tuiles max pour le preendgame | `Preendgame::maximumTilesInBagToEngage` | A5 |

---

## 6. Catalogue des améliorations

Chaque entrée : ce que c'est, pourquoi, dépendances. Coût : S (jours), M (1–2 semaines),
L (plus). Gain : estimation à confirmer dans l'arène.

### Fondations (sans elles, on avance à l'aveugle)
- **F1. Arène** (M) — mode harnais : K parties entre deux configurations, seeds appariées,
  sièges alternés, sacs identiques par paire ; sortie : % de victoire, écart moyen, intervalle
  de confiance, temps/coup. Configurations à budget d'**itérations** fixe (déterministes) en plus
  du budget en temps.
- **F2. Benchmark de vitesse** (S) — positions figées, playouts/s et coups générés/s.
- **F3. Corpus de positions** (S) — positions de test commentées (finales à passe, blocages,
  « coup à 2 points », échanges), avec la bonne réponse connue ; sert à vérifier H3 et A5.

### Corrections qui changent le jeu
- **A0. Bugs B1–B10** (S chacun) — en particulier B1 (passe), B2 (budget en temps), B3, B5.

### Recherche en milieu de partie
- **A3. Allocation statistique des tirages** (M) — nombres aléatoires communs (B4 : même
  graine pour l'itération i de chaque candidat) + élagage successif (successive halving / UCB
  sur l'écart au meilleur) + arrêt quand le meilleur est significativement devant. Même temps,
  beaucoup plus de candidats, classement fiable. Dépend de F1, B2.
- **A7. Candidats plus variés** (S) — en plus du top statique : top par score, meilleurs
  leaves, meilleurs échanges, coups qui ferment/ouvrent. Utile seulement avec A3.
- **A2. Profondeur adaptative** (M) — plus de plis quand le sac se vide ou quand les candidats
  sont serrés ; coût maîtrisé grâce à la vitesse (V1–V3).

### Évaluation
- **A1. Évaluation de fin de playout apprise** (L) — remplace `bogowin(écart, inconnues)` par
  P(victoire | écart, tuiles inconnues, tuiles fortes restantes, leave, ouverture du plateau, trait).
  Entraînée par self-play pour chaque lexique (régression logistique d'abord, petit réseau
  ensuite). Répond directement à H1. Dépend de F1.
- **A6. Données par lexique** (M) — pipeline self-play qui produit `superleaves`, la table
  (ou le modèle) de victoire et le timing pour n'importe quel lexique ; ODS récent si disponible.
  Dépend de B5.
- **A8. Évaluation statique du plateau** (M) — terme appris pour `sharedConsideration`
  (couloirs de scrabble, cases rouges ouvertes) : meilleurs candidats et adversaire simulé plus
  réaliste.

### Information imparfaite
- **A4. Inférence du chevalet adverse** (M→L) — §H2. Étapes : (1) vraisemblance sur le
  dernier coup adverse, tirage pondéré (rejet ou importance) dans `randomizeOppoRacks` ;
  (2) calibration de β sur self-play et `.gcg` humains ; (3) cumul sur plusieurs tours ;
  (4) mesure dans l'arène. Le coût principal est de générer les coups pour des milliers de
  chevalets candidats : d'où l'intérêt de V3 et de l'idée du `TODO` (générer depuis le « gros
  chevalet » du sac).
- **A9. Adversaire simulé sensible au score** (M) — l'adversaire simulé joue un peu plus
  « pour gagner » (ex. sa propre mini-simulation, ou une equity qui tient compte de l'écart).
  À tester seulement après A3/A1.

### Fin de partie
- **A5. Vrai solveur de finale** (L) — alpha-bêta en approfondissement itératif, table de
  transposition (Zobrist), ordre des coups par equity, **passe incluse**, objectif = victoire
  puis écart. Retrouve seul les coups « à la Nigel ». Puis **preendgame étendu** à 1–6 tuiles
  dans le sac : énumération des tirages + solveur, candidats non filtrés par l'equity statique.

---

## 7. Proposition de découpage en PR

Remplacée par le plan détaillé : `PLAN_IA.md`. Liste initiale conservée pour mémoire.

Ordre conseillé : mesurer → corriger → accélérer → renforcer.

1. `fix/pass-candidate` — B1 (+ position de test de finale où passer est le seul gain).
2. `fix/strategy-loading` — B5, B6, C.1 de la roadmap (superleaves en lecture seule).
3. `fix/bogowin-iterations` — B2, B3, B9 (budget en temps, symétrie des candidats).
4. `test/arena` — F1, F2, F3.
5. `perf/no-copies` — V1. 6. `perf/leaves-exchanges` — V2. 7. `perf/shadow-play` — V3.
8. `ai/crn-successive-halving` — B4 + A3 (+ A7).
9. `ai/endgame-solver` — A5 (finale), puis `ai/preendgame` (extension).
10. `ai/french-data` — A6.
11. `ai/inference` — A4.
12. `ai/learned-eval` — A1, puis A8, A2, A9.

Chaque PR : position(s) de test ou cas de régression, goldens ré-enregistrés seulement pour un
changement de comportement voulu (et annoncé), mesure dans l'arène pour toute PR « ai/ ».
