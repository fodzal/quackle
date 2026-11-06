# Bilan — IA de Scrabble classique (2 joueurs)

Récapitulatif de ce qui a été fait (2026-11-05 → 2026-11-07), de ce que les mesures ont
appris, et des étapes possibles ensuite. Le détail est dans les autres documents :
- `ANALYSE_IA.md` : le diagnostic et les identifiants (B\*, V\*, A\*, F\*) ;
- `PLAN_IA.md` : le plan par phases, le tableau d'avancement et toutes les mesures d'arène.

## 1. Point de départ

La demande portait sur quatre volets :
- rendre le moteur plus rapide, pour pouvoir agrandir la recherche ;
- vérifier trois défauts perçus :
  1. un horizon trop court et trop peu de coups explorés ;
  2. aucune inférence sur le chevalet adverse ;
  3. en fin de partie, des petits coups « à la Nigel Richards » qui ne sont pas explorés ;
- trouver d'autres améliorations ;
- trouver des bugs.

Trois contraintes :
- rien de codé en dur pour une langue : la stratégie vient de la recherche, ou de données
  apprises par lexique ;
- lexique `ods5` ;
- tout tourne sur ce portable (8 cœurs).

## 2. Méthode

Rien ne pouvait se mesurer au départ :
- le self-play en mode silencieux ne jouait aucun coup ;
- les simulations n'étaient pas reproductibles ;
- il n'y avait pas d'arène.

La première moitié du travail a donc construit l'outil de mesure :
- **parties appariées** : même suite de tuiles, sièges inversés ;
- **budgets en itérations fixes** (`timelimit=0`), pour que les parties soient reproductibles ;
- **nombres aléatoires communs** entre les candidats d'une simulation ;
- **décomposition du temps par phase** (milieu de partie, preendgame, finale) ;
- **arène à partir du preendgame** : Static joue les deux sièges jusqu'à N tuiles dans le sac,
  puis les deux joueurs testés reprennent la même position.

Sur ce poste, cela donne la précision suivante :

| Arène | Paires | Durée | Précision |
|---|---|---|---|
| Partie entière | 200 | 2,4 h | ±4,25 points de score |
| Preendgame | 400 | 35 min | ±1,3 point de score, ±0,6 à 1,5 point d'écart par partie |

## 3. Les 18 branches

Elles sont empilées dans cet ordre, toutes poussées. `docs/ai-plan` (les documents) part
directement de `master`. Les textes des PR sont prêts, chacun avec sa base et ses vérifications.

| # | Branche | Type | Apport |
|---|---|---|---|
| 1 | `fix/harness-selfplay` | bug | `--quiet` ne jouait aucun coup ; les parties étaient coupées à 50 coups ; bilan ajouté en fin de self-play |
| 2 | `test/player-config` | outil | `--config` / `--config2` : itérations, candidats, plis, élagage, `timelimit=0` |
| 3 | `test/arena` | outil | arène à parties appariées + `arena.py` (score, Elo, écart, intervalles à 95 %) |
| 4 | `ai/deterministic-sim` | bug + IA | simulations reproductibles (B12) ; nombres aléatoires communs entre candidats (B4) |
| 5 | `fix/quiet-sim` | bug | plus d'affichage de la position à chaque coup simulé |
| 6 | `fix/pass-candidate` | bug | B1 : la passe est candidate quand le sac est vide |
| 7 | `fix/strategy-loading` | bug | B5 : débordement de la table `bogowin` en français (102 tuiles) ; C.1 : écritures concurrentes dans les superleaves, et un leave absent confondu avec un leave valant 0 ; B6 : arrondi de l'écart ; B7 : `spread()` |
| 8 | `fix/bogowin-symmetry` | bug | B3 : le candidat de tête est comparé à ses challengers sur un nombre égal de tirages |
| 9 | `test/bench` | outil | `--mode bench` ; temps d'arène par phase de jeu |
| 10 | `perf/leaves` | vitesse | superleaves en table de hachage + cache par chevalet : ×1,3 |
| 11 | `tools/win-table` | données | table de victoire apprise par self-play ; table `ods5` sur 200 000 parties ; `compare.py` |
| 12 | `ai/endgame-solver` | IA + outil | solveur de finale exact (alpha-bêta, table de transposition), désactivé par défaut, sert d'étalon |
| 13 | `perf/shadow` | vitesse | `staticBestMove` explore les ancres par borne d'équité décroissante : ×1,97 en `ods5`, mêmes coups |
| 14 | `ai/inference` | IA | reliquat adverse inféré du dernier coup, utilisé par les playouts (désactivé par défaut) |
| 15 | `perf/endgame-playout` | vitesse | les playouts de `Endgame` ne coûtent plus un temps exponentiel : −8 % par coup |
| 16 | `test/arena-start` | outil | arène à partir du preendgame (`--start N`) |
| 17 | `ai/preendgame` | bug + IA | le preendgame imbriqué ignorait `timelimit=0` (paramètres non transmis) ; réglages `precandidates`, `nestedplays`, `nestedreplies` |
| 18 | `ai/preendgame-inference` | IA | le preendgame pondère les chevalets adverses par la vraisemblance du dernier coup adverse (`preinfer=1`, désactivé par défaut) ; mesure de précision en preendgame |

Toutes les branches passent la régression, sous ASan à partir de la branche 8. Trois branches
ont ré-enregistré des goldens ; la PR concernée l'annonce et l'explique à chaque fois :
- branche 1 : ligne de bilan ajoutée ;
- branche 9 : nouveau format de ligne de l'arène ;
- branche 15 : décalage de la suite aléatoire, pas de changement de jeu ;
- branche 18 : ligne de mesure ajoutée au golden `inference`.

## 4. Ce que les mesures ont appris

### 4.1 Résultats

| Expérience | Résultat | Conclusion |
|---|---|---|
| Championship 132/330 contre Static, partie entière | 56,5 % ± 6,2 (≈ +45 Elo) | la simulation apporte peu en français, pour 20 s par coup |
| Budget ×4 (33/82 → 132/330) | 54,0 % ± 6,6 | pas de gain mesurable |
| Table de victoire `ods5` au lieu de la table anglaise | +0,5 ± 7,8 points de score | neutre ; gardée, car apprise et plus rapide |
| Horizon 4 plis contre 2 | 46,6 % ± 4,7 | **défaut n° 1 (horizon) non confirmé** |
| 30 candidats contre 13 | 51,75 % ± 4,25 | **défaut n° 1 (peu de coups) non confirmé** |
| Inférence (`infer=1000`) | 49,5 % ± 4,8, coup 40 % plus lent | **défaut n° 2 : l'information existe (×3 de masse sur le vrai reliquat) mais ne rapporte pas de points en milieu de partie** |
| Solveur exact contre l'heuristique de finale | même valeur sur les 47 finales prouvées | **défaut n° 3 absent en finale** |
| Championship contre Static, à partir de 2 tuiles dans le sac | +6,4 ± 1,5 points par partie | le preendgame actuel rapporte déjà |
| Preendgame à 60 candidats contre 30 | 50,12 % ± 0,55 | **défaut n° 3 absent en preendgame** |
| Finale imbriquée 40/20 contre 14/7 | 49,75 % ± 0,78 | neutre |
| Inférence en preendgame (`preinfer=1`, β = 1,5) | 50,31 % ± 0,50 ; masse sur le vrai chevalet 13,3 % → 18,1 % | **défaut n° 2 : neutre aussi là où chaque point compte** |

### 4.2 Leçons

1. **Les trois défauts perçus ne se confirment pas** dans les mesures :
   - élargir ou approfondir la recherche (plus de candidats, plus de plis, plus d'itérations,
     un preendgame plus large) ne rapporte rien de mesurable ;
   - l'IA trouve déjà les coups courts en fin de partie ;
   - l'inférence ne rapporte rien non plus, ni en milieu de partie ni en preendgame.
2. **La vitesse seule n'est pas le levier.** Le moteur est ×1,3 à ×2 plus rapide, mais un
   budget ×4 n'apporte rien. Ce qui limite la simulation, c'est ce qu'elle mesure :
   - l'évaluation en fin de playout ;
   - le joueur statique qui joue les playouts ;
   - la variance : ±16 points d'écart par paire.
3. **Une partie entière est très sensible au moindre changement de coup** : 88 paires sur 100
   ont une autre issue avec la nouvelle table de victoire. Les petits effets ne se mesurent
   que sur des positions fixées, comme le fait l'arène preendgame.
4. **Le preendgame est le principal poste de coût** : 47 à 60 % du temps aux budgets réduits,
   parce que `timelimit=0` lui retire tout plafond. C'est aussi là que se trouve le gain mesuré
   de Championship sur Static.

## 5. Ce qui reste ouvert (connu)

- Les branches existantes de Philippe, pas encore fusionnées : `fix/endgame-passout-spread`,
  `fix/report-considered-move-endgame`, `fix/multiplayer`.
- B9 : le chrono n'est vérifié qu'entre deux candidats. Mineur, sans effet avec `timelimit=0`.
- B13 : le harnais plante quand l'alphabet est introuvable. Mineur.
- `fix/report-win` : les points A.1, A.2, A.3 et A.7 de la ROADMAP (rapport et preendgame).
- B8 et B10 : leave et timing en fin de partie calibrés en anglais. Ils doivent disparaître
  avec les données apprises (phase 5) et l'évaluation apprise (phase 7).
- Corpus de positions annotées (0.6) : pas fait. L'arène preendgame et `--mode endgame` le
  remplacent en partie.

## 6. Étapes futures possibles

Classées par rapport attendu entre gain et coût, d'après ce que les mesures ont appris.

### Inférence (phase 6) — mise en attente

- **6.a Inférence dans le preendgame** : faite (branche 18), neutre. Le vrai chevalet passe de
  13,3 % à 18 % de la masse, ce qui ne change pas assez de décisions.

Les autres pistes ne sont plus prioritaires : l'inférence, avec l'information qu'elle extrait
aujourd'hui, ne rapporte rien ni en milieu de partie ni en preendgame. Elles ne vaudraient
d'être reprises que si une version extrayait beaucoup plus d'information, ou face à un
adversaire humain, moins prévisible que Static.
- **6.b Calibration de β** (6.2) sur le self-play, et plus tard sur des parties humaines `.gcg`.
- **6.c Cumul sur plusieurs tours** (6.3) : un reliquat gardé deux tours de suite renseigne
  davantage qu'un seul coup.
- **6.d Coût** : générer les coups une seule fois à partir de toutes les tuiles inconnues,
  au lieu d'une fois par reliquat échantillonné. Aujourd'hui, l'inférence rend le coup 40 % plus
  lent.

### Fin de partie (phase 4)

- **4.3 `ai/sim-endgame`** : finir avec une vraie recherche de finale les playouts de milieu
  de partie qui vont jusqu'au bout (sac ≤ 14). C'est un gros chantier, sans indice de gain à ce
  jour.
- **Plafond de coût du preendgame** quand `timelimit=0` : mieux répartir le temps entre le
  preendgame et le milieu de partie.
- Solveur exact sur les finales à chevalet plein. Il faut une génération incrémentale ou un
  générateur beaucoup plus rapide ; aujourd'hui, une génération prend environ 2 ms.

### Évaluation et données apprises (phases 5 et 7)

Ce sont les plus prometteuses, puisque ce qui limite la simulation est ce qu'elle mesure.

- **7.1 `ai/learned-eval`** : remplacer, en fin de playout, la table de victoire et le leave
  de milieu de partie (B8) par une P(victoire) apprise : écart, tuiles inconnues, tuiles
  fortes restantes, leave, trait, ouverture du plateau.
- **5.2 `tools/leaves`** : superleaves `ods5` réapprises par self-play et itérées jusqu'à
  stabilité.
- **5.4 `ai/timing`** : heuristique de timing apprise (B10).
- **7.2 `ai/board-eval`** et **7.3 `ai/opponent-model`** : ouverture du plateau, et un
  adversaire simulé qui tient compte du score.

### Recherche en milieu de partie (phase 3)

Elle passe après, puisque l'élargissement est neutre :
- *successive halving* (A3) ;
- plis adaptatifs (A2) ;
- candidats diversifiés (A7).

### Vitesse (phase 2)

À reprendre quand un chantier en a besoin, par exemple un solveur à chevalet plein ou une
évaluation apprise plus coûteuse :
- `perf/no-copies` (V1) ;
- cross-checks incrémentaux en bitset (V4, V5) ;
- passage à l'échelle en threads (V6).

Dans le profil actuel, le générateur (gordongen/gordongoon) prend environ 42 % du temps
à lui seul.
