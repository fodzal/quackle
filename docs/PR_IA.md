# Les PR de l'IA — effet sur le jeu et sur le temps de réflexion

Une fiche par PR, dans l'ordre de fusion. Pour chacune :
- **Effet sur le jeu** : la force de l'IA, avec les réglages par défaut sauf mention contraire ;
- **Temps** : ce que la PR ajoute ou retire au temps de réflexion par coup.

Le contexte est dans `BILAN_IA.md` ; le détail des mesures, dans `PLAN_IA.md`.

Lecture des mesures d'arène. « 50 % ± 0,5 » est le score du joueur modifié contre le joueur
d'origine, avec un intervalle à 95 %. Un intervalle qui contient 50 % veut dire « pas d'effet
mesurable ». Toutes les mesures sont faites en `ods5`, sur ce portable.

## En bref

- **Force** : aucune PR ne rend l'IA mesurablement plus forte. Aucune ne la rend plus faible non
  plus : chaque changement du jeu par défaut a été mesuré neutre, ou bien garde exactement les
  mêmes coups.
- **Vitesse** : à jeu égal, l'IA réfléchit à peu près **deux fois moins longtemps**. Pour
  Championship 33/82, le coup moyen passe d'environ 8,7 s (après la PR 10) à environ 4 s (après
  la PR 15). Les principales contributions :

  | PR | Gain |
  |---|---|
  | `perf/leaves` | ×1,3 |
  | `perf/shadow` | ×1,24 sur les simulations |
  | Table de victoire `ods5` | −14 % |
  | `perf/endgame-playout` | −8 % |

- **Interface** : les joueurs de l'interface (Championship, etc.) ont un nombre d'itérations
  fixe, borné par le temps. Avec le moteur plus rapide, ils jouent les mêmes coups, plus tôt.
- **Réglages expérimentaux** : quatre réglages sont désactivés par défaut, car ils n'apportent
  rien de mesurable et coûtent du temps : `endgame`, `infer`, `precandidates`/`nested*` et
  `preinfer`.

| # | PR | Effet sur le jeu | Temps par coup |
|---|---|---|---|
| 1 | `fix/harness-selfplay` | aucun (outil de test) | — |
| 2 | `test/player-config` | aucun par défaut | — |
| 3 | `test/arena` | aucun (outil de mesure) | — |
| 4 | `ai/deterministic-sim` | comparaison des candidats plus juste ; 53,5 % ± 7,4, non significatif | inchangé |
| 5 | `fix/quiet-sim` | aucun | un peu moins (plus d'affichage) |
| 6 | `fix/pass-candidate` | la passe devient possible en finale | négligeable |
| 7 | `fix/strategy-loading` | corrections (table française, leaves) ; non mesuré seul | inchangé |
| 8 | `fix/bogowin-symmetry` | comparaison des candidats plus juste ; non mesuré seul | un peu plus (non mesuré) |
| 9 | `test/bench` | aucun (outil de mesure) | — |
| 10 | `perf/leaves` | aucun, mêmes coups | **≈ −23 %** (×1,3) |
| 11 | `tools/win-table` | neutre (+0,5 ± 7,8 points de score) | **−14 %** (19,8 → 17,1 s) |
| 12 | `ai/endgame-solver` | aucun par défaut ; l'heuristique de finale était déjà juste | — (si activé : +, borné) |
| 13 | `perf/shadow` | aucun, mêmes coups | **−20 %** sur les simulations |
| 14 | `ai/inference` | aucun par défaut ; activé : neutre (49,5 % ± 4,8) | — (si activé : +40 %) |
| 15 | `perf/endgame-playout` | aucun, mêmes coups | **−8 %** (finale −16 %) |
| 16 | `test/arena-start` | aucun (outil de mesure) | — |
| 17 | `ai/preendgame` | bug corrigé ; réglages neutres | — (si élargi : ×1,5 à ×3,3) |
| 18 | `ai/preendgame-inference` | aucun par défaut ; activé : neutre (50,3 % ± 0,5) | — (si activé : ≈ 0) |

---

## 1. `fix/harness-selfplay`
Le mode self-play du harnais de test ne jouait aucun coup avec `--quiet`, et coupait les parties
à 50 coups. Il joue maintenant des parties complètes et affiche un bilan : victoires, nuls, écart.

- **Effet sur le jeu** : aucun, c'est un outil de test.
- **Temps** : —

## 2. `test/player-config`
Chaque réglage de recherche d'un joueur peut maintenant être changé en ligne de commande :
itérations, candidats, plis, élagage. `timelimit=0` retire en plus la limite de temps : seul le
nombre d'itérations borne la recherche, et les parties deviennent reproductibles.

- **Effet sur le jeu** : aucun par défaut.
- **Temps** : —

## 3. `test/arena`
C'est l'arène : deux joueurs s'affrontent sur des paires de parties, avec la même suite de tuiles
et les sièges inversés, ce qui annule l'essentiel de la chance. Un script répartit les paires sur
les 8 cœurs et calcule le score, l'Elo et l'écart, avec leurs intervalles.

- **Effet sur le jeu** : aucun, c'est l'outil de mesure de toutes les PR suivantes.
- **Temps** : —

## 4. `ai/deterministic-sim`
Les simulations tiraient leurs graines au hasard du système, si bien qu'aucune partie ne se
rejouait. Chaque simulation a maintenant sa graine. Par défaut, tous les candidats sont simulés
avec les mêmes tirages (« nombres aléatoires communs ») : on compare les coups, pas la chance de
chacun.

- **Effet sur le jeu** : la comparaison des candidats est plus juste. Arène, avec contre sans
  tirages communs : 53,5 % ± 7,4 sur 64 paires, tendance favorable mais non significative.
- **Temps** : inchangé (≈ 5,4 s par coup des deux côtés).

## 5. `fix/quiet-sim`
La simulation affichait toute la position à chaque coup, ce qui noyait la sortie.

- **Effet sur le jeu** : aucun.
- **Temps** : un peu moins, puisqu'il n'y a plus d'affichage (non mesuré).

## 6. `fix/pass-candidate`
En finale, la passe ne faisait jamais partie des coups envisagés. C'est pourtant parfois le
seul coup gagnant, quand tout autre coup ouvre une sortie à l'adversaire. Elle est maintenant
proposée dès que le sac est vide.

- **Effet sur le jeu** : la passe devient possible en finale. Les joueurs statiques ne changent
  pas.
- **Temps** : négligeable (un candidat de plus).

## 7. `fix/strategy-loading`
Corrections dans le chargement des tables de stratégie :
- **table de victoire en français** : elle débordait de son tableau, puisque le jeu français a
  102 tuiles ;
- **leaves** : des écritures concurrentes dans la table, et un leave absent confondu avec un
  leave qui vaut 0 ;
- **écart** : il était arrondi au point inférieur avant la lecture de la table de victoire ;
- **`spread()`** : un cas faux.

- **Effet sur le jeu** : corrections, non mesurées seules.
- **Temps** : inchangé.

## 8. `fix/bogowin-symmetry`
Le candidat en tête était simulé moins souvent que les challengers qu'on prolongeait, puis
comparé à eux. Il est maintenant prolongé en même temps, ce qui donne des comparaisons à
échantillons égaux.

- **Effet sur le jeu** : la comparaison est plus juste ; non mesuré seul.
- **Temps** : un peu plus, seulement quand un challenger est prolongé (non mesuré).

## 9. `test/bench`
Ajoute un mode de chronométrage du moteur (meilleur coup statique, kibitz, simulations), et le
découpage du temps d'arène par phase de jeu : milieu de partie, preendgame, finale.

- **Effet sur le jeu** : aucun, c'est un outil de mesure.
- **Temps** : —

## 10. `perf/leaves`
La valeur des leaves (environ un million d'entrées) passe d'un arbre trié à une table de
hachage, et chaque chevalet garde un cache de ses leaves.

- **Effet sur le jeu** : aucun, les coups sont identiques.
- **Temps** : **×1,3** sur les simulations et le meilleur coup statique, soit environ −23 % par
  coup.

## 11. `tools/win-table`
Ajoute un outil qui apprend la table de victoire (probabilité de gagner selon l'écart et le
nombre de tuiles inconnues) par self-play, et une table apprise pour `ods5` sur 200 000 parties.
Elle remplace la table anglaise.

- **Effet sur le jeu** : neutre, +0,5 ± 7,8 points de score contre Static, sur les mêmes
  parties. La table est gardée parce qu'elle est apprise et non codée en dur.
- **Temps** : **−14 %** (19,8 s → 17,1 s par coup pour Championship au budget complet).

## 12. `ai/endgame-solver`
Ajoute un solveur exact de finale (sac vide), désactivé par défaut (`endgame=N`). Il a servi
d'étalon : sur les 47 finales qu'il a résolues, l'heuristique actuelle jouait déjà un coup de
même valeur.

- **Effet sur le jeu** : aucun par défaut.
- **Temps** : — par défaut ; s'il est activé, il est borné par un budget de calcul.

## 13. `perf/shadow`
Pour trouver le meilleur coup statique, le générateur borne d'abord ce que peut rapporter chaque
emplacement (ancre). Il les explore ensuite du plus prometteur au moins prometteur, et s'arrête
dès qu'aucun ne peut plus battre le meilleur coup trouvé. Il n'explore ainsi que 37 % des ancres.

- **Effet sur le jeu** : aucun, les coups sont identiques (vérifié sur 3 740 positions).
- **Temps** : meilleur coup statique **×1,97**, simulations ×1,24, soit environ −20 % par coup en
  milieu de partie.

## 14. `ai/inference`
L'IA déduit du dernier coup adverse les tuiles que l'adversaire a probablement gardées, et en
tient compte dans ses simulations (`infer=N`, désactivé par défaut). La masse de probabilité sur
le vrai reliquat est multipliée par 3.

- **Effet sur le jeu** : aucun par défaut ; activée, elle est neutre (49,5 % ± 4,8).
- **Temps** : — par défaut ; **+40 %** si elle est activée.

## 15. `perf/endgame-playout`
L'heuristique de finale rejouait toute la fin de partie à chaque niveau de sa recherche, pour un
coût exponentiel. Elle ne le fait plus qu'une fois.

- **Effet sur le jeu** : aucun, les coups sont identiques (72 finales).
- **Temps** : **−8 %** par coup pour Championship 33/82, et −16 % sur les finales.

## 16. `test/arena-start`
L'arène peut démarrer chaque paire à N tuiles dans le sac : Static joue jusque-là, puis les deux
joueurs reprennent la même position. Pour comparer des fins de partie, la précision passe à ±1,3 point
de score en 35 min, contre ±4,25 en 2,4 h pour une arène de parties entières.

- **Effet sur le jeu** : aucun, c'est un outil de mesure.
- **Temps** : —

## 17. `ai/preendgame`
**Correction d'un bug** : le preendgame imbriqué (2 tuiles dans le sac) ne recevait pas les
réglages du joueur ; il tournait donc avec les réglages par défaut. Ajoute aussi des réglages de
largeur : `precandidates`, `nestedplays`, `nestedreplies`.

- **Effet sur le jeu** : le bug est corrigé. Élargir la recherche est neutre :
  - 60 candidats au lieu de 30 : 50,1 % ± 0,55 ;
  - finale imbriquée 40/20 au lieu de 14/7 : 49,75 % ± 0,78.
- **Temps** : inchangé par défaut. Élargi, ×3,3 (60 candidats) ou ×1,5 (finale 40/20) sur le
  preendgame.

## 18. `ai/preendgame-inference`
En preendgame, les chevalets adverses possibles sont pondérés par la vraisemblance du dernier
coup adverse (`preinfer=1`, désactivé par défaut). Le vrai chevalet reçoit alors 18 % de la
probabilité au lieu de 13 %.

- **Effet sur le jeu** : aucun par défaut ; activé, il est neutre (50,3 % ± 0,5).
- **Temps** : aucun surcoût mesurable (1,37 s contre 1,36 s par coup).

## `docs/ai-plan`
Les documents : architecture, feuille de route, analyse, plan, bilan et ce récapitulatif.

- **Effet sur le jeu** : aucun.
- **Temps** : —
