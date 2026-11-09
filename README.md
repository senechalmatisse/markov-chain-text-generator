# Générateur de texte par chaîne de Markov

**Il lit Victor Hugo, Molière, Stendhal, Mérimée et Pierre Loti, puis il écrit à leur manière.** Le programme est écrit en C99, sans autre bibliothèque que la bibliothèque C standard, et il ne laisse aucune fuite mémoire : plus de cent tests et valgrind le vérifient, et un hook git peut les lancer avant chaque commit.

```sh
make && ./projet -k 2 -g 7 -n 8 dataset
```

> Tu te trompes, Cosette, répondit Marius.
> Il était plus l'homme; l'heure indiquée, et j'aurai soin d'assujettir le verrou.
> Vous n'avez pas plus prier trop qu'aimer trop, et ce jardin est joli!

Aucune de ces phrases n'existe dans les livres : le programme les a inventées. Il a seulement appris quel mot suit quel autre, et comment chaque mot s'écrit. Ces trois phrases font partie des huit que produit la commande ci-dessus : avec la même graine (`-g 7`), vous obtiendrez les mêmes.

---

## Sommaire

1. [Pourquoi ce projet vaut le détour](#pourquoi-ce-projet-vaut-le-détour)
2. [Essayer en deux minutes](#essayer-en-deux-minutes)
3. [Comment ça marche](#comment-ça-marche)
4. [L'architecture](#larchitecture)
5. [Utilisation détaillée](#utilisation-détaillée)
6. [Qualité : tests, mémoire, contrats](#qualité--tests-mémoire-contrats)
7. [Choix de conception](#choix-de-conception)
8. [Mesures](#mesures)
9. [Limites connues](#limites-connues)
10. [Pour aller plus loin](#pour-aller-plus-loin)

---

## Pourquoi ce projet vaut le détour

- **Il tient en mémoire ce qu'une approche naïve ne pourrait pas stocker.** Le corpus contient 43 853 mots différents. Une matrice de transitions pleine pèserait 15,4 Go ; la représentation creuse utilisée ici tient en 35 Mio.
- **Il écrit un français lisible**, avec accents, majuscules, élisions et traits d'union : « c'était », « peut-être », « dit-il ». Il retrouve les accents même si le plus gros livre du corpus en est privé, et il distingue « il a » de « va à », ou « il m'a » de « M. Madeleine ».
- **Il est reproductible.** La même graine redonne le même texte : le générateur aléatoire est celui du projet, pas celui de la bibliothèque C, et les fichiers sont lus dans l'ordre alphabétique. Un modèle appris se sauvegarde, puis se recharge en 0,16 s au lieu de 0,43 s d'apprentissage, pour un texte identique.
- **Il est vérifié sous tous les angles** :
  - 134 tests ;
  - 1 188 exécutions où une allocation mémoire échoue volontairement ;
  - des dizaines de milliers de fichiers aléatoires lus sous
    AddressSanitizer ;
  - valgrind sur le programme et sur chaque test ;
  - 98,7 % des lignes exécutées par les tests.
- **Il est construit par contrat** : chaque fonction déclare ses pré-conditions et ses post-conditions, et les tests vérifient qu'une violation arrête bien le programme.

## Essayer en deux minutes

```sh
make                                 # compile ./projet (gcc, C99)
./projet dataset                     # 10 phrases inspirées du corpus
./projet -k 2 -g 7 -n 8 dataset      # ordre 2 : des phrases plus naturelles
./projet -k 1 -g 7 -n 8 dataset      # comparez avec l'ordre 1
make test                            # toute la suite de tests (≈ 75 s)
```

Le texte s'affiche et s'enregistre dans `texte_genere_<graine>.txt`, qui n'est jamais écrasé. Pour faire écrire le programme à partir de vos propres textes, mettez-les dans un répertoire : `./projet mon_corpus`.

L'ordre change tout. Voici des extraits de l'ordre 1, avec la même graine que plus haut :

> Il était fini.
> Moi dans la, la conscience droite et le jour au couchant.
> Marius, que médecin.

À l'ordre 1, chaque mot ne dépend que du précédent. À l'ordre 2, il dépend des deux précédents, et les phrases gagnent nettement en cohérence.

## Comment ça marche

Tout repose sur une idée simple : **le mot suivant ne dépend que du (ou des) mot(s) précédent(s)**. C'est une chaîne de Markov.

**1. Apprendre.** Le programme lit chaque phrase et compte quel mot suit quel autre. Le mot vide (l'état 0) marque le début et la fin des phrases. Sur le corpus « Le chat dort. Le chien dort. » :

```
0     → le      2 fois    probabilité 1
le    → chat    1 fois    probabilité 1/2
le    → chien   1 fois    probabilité 1/2
chat  → dort    1 fois    probabilité 1
chien → dort    1 fois    probabilité 1
dort  → 0       2 fois    probabilité 1      (fin de phrase)
```

**2. Générer.** Il part de l'état 0, tire chaque mot suivant selon ces probabilités, et s'arrête quand il revient à 0. Un parcours possible est `0 → le → chien → dort → 0`, ce qui donne « Le chien dort. ». Appris sur les 850 000 mots du corpus, ces tirages produisent des phrases inédites.

**3. Bien écrire.** Pour l'apprentissage, les mots sont réduits à des clés sans accents ni majuscules (`etait`). Le programme retient donc aussi, pour chaque paire de mots consécutifs :

- l'écriture la plus fréquente du mot (« était ») ;
- ses majuscules (« Marius ») ;
- le séparateur qui le lie au mot précédent (apostrophe, trait d'union,
  virgule…).

Il retient enfin la ponctuation qui termine les phrases selon leur dernier mot.

## L'architecture

```
 dataset/*.txt
      │
      ▼
 itérateur de mots ............ mots {a..z}, graphies, liaisons, ponctuation
 (repertoire)                   en-têtes et licences Gutenberg ignorés
      │
      ├──► table des mots ......... mot ↔ identifiant         (hachage)
      ├──► table des paires ....... bigramme ↔ identifiant    (hachage)
      ├──► lexique ................ comment écrire chaque mot (lexique)
      ├──► chaîne de Markov ....... quel mot suit             (markov)
      └──► chaîne de ponctuation .. comment finir la phrase   (markov)
                       │
                       ▼
                    modèle ──► phrases ──► fichier texte    (modele, sauvegarde)
                       │
                       └──► modele.txt : sauvegarde et rechargement
```

| Module | Rôle | À lire pour… |
|---|---|---|
| `src/markov.h/.c` | chaîne de Markov creuse, tirage exact, générateur interne | la structure de données centrale |
| `src/hachage.h/.c` | table des mots et table des paires | le hachage sans collision des mots courts |
| `src/repertoire.h/.c` | itérateur de mots, liste des fichiers, apprentissage | la lecture robuste (UTF-8, Latin-1) |
| `src/lexique.h/.c` | accents, majuscules et liaisons appris par vote | comment un texte devient lisible |
| `src/modele.h/.c` | modèle complet : génération, sauvegarde, chargement | l'assemblage et le format de fichier |
| `src/sauvegarde.h/.c` | écriture et suppression sûres de fichiers | la gestion des erreurs du système |
| `src/texte.h`, `src/contrat.h` | types communs, macros de contrat | les conventions du projet |
| `main.c` | programme principal et ligne de commande | l'utilisation de tous les modules |
| `tests/` | 9 programmes de test et leurs outils | la manière de tout vérifier |
| `hooks/pre-commit`, `.gitlab-ci.yml`, `Doxyfile` | hook git, intégration continue, documentation | l'outillage |

Les modules sont indépendants : `markov` ne manipule que des entiers et `hachage` que des mots ou des paires. Chacun se teste seul. Les contrats complets (pré-conditions, post-conditions, complexité) sont dans les fichiers `.h`, et `make doc` en fait une documentation HTML.

## Utilisation détaillée

```
./projet [options] <repertoire> [graine] [nb_phrases] [fichier_sortie]
./projet [options] -m <modele>
```

| Option | Rôle |
|---|---|
| `-g <graine>` | graine du générateur (défaut : l'heure) |
| `-n <nombre>` | nombre de phrases (défaut : 10) |
| `-o <fichier>` | fichier de sortie (défaut : `texte_genere_<graine>.txt`) |
| `-k <1\|2>` | ordre de la chaîne (défaut : 1) |
| `-r` | lire aussi les sous-répertoires (par exemple `dataset/en`) |
| `-s <fichier>` | sauvegarder le modèle appris |
| `-m <fichier>` | charger un modèle au lieu de relire le corpus |
| `-h`, `--aide` | afficher l'aide |

```sh
./projet -s modele.txt -k 2 dataset    # apprendre une fois…
./projet -m modele.txt -n 20           # …puis générer sans relire le corpus
./projet dataset 42 5 sortie.txt       # ancienne syntaxe, toujours acceptée
```

Le code de retour vaut 0 en cas de succès et 1 en cas d'erreur, avec un message clair (répertoire absent, modèle invalide, disque plein…).

| Commande | Effet |
|---|---|
| `make` | construit `./projet` |
| `make test` | tous les tests et la vérification mémoire |
| `make test_markov` | un seul module : de même `test_hachage`, `test_repertoire`, `test_sauvegarde`, `test_lexique`, `test_modele` |
| `make test_programme` | lance `./projet` et vérifie les textes produits |
| `make test_defaillances` | fait échouer chaque allocation tour à tour |
| `make test_aleatoire` | octets au hasard, sous AddressSanitizer |
| `make test_memoire` | valgrind sur le programme et sur chaque test |
| `make couverture` | couverture des tests (gcov) |
| `make doc` | documentation Doxygen dans `doc/html/` |
| `make installer_hook` | `make test` avant chaque commit |
| `make clean` | supprime les fichiers compilés et les rapports |

Il faut gcc et make pour compiler, **valgrind** pour `make test` et **doxygen** pour `make doc`. Si lcov est installé, `make couverture` produit aussi un rapport HTML.

## Qualité : tests, mémoire, contrats

| Vérification | Ce qui est contrôlé |
|---|---|
| 119 tests unitaires et du programme | contrats, cas limites, codes de retour, environ 40 fichiers de modèle corrompus refusés |
| `test_defaillances` | 1 188 exécutions, chacune faisant échouer une allocation différente (une seule fois, ou toutes à partir de la N-ième) : erreur signalée, structure intacte, aucun bloc perdu |
| `test_aleatoire` | octets au hasard pour l'itérateur, corpus aléatoires, modèles abîmés, sous AddressSanitizer et UndefinedBehaviorSanitizer |
| `test_memoire` | valgrind sur le programme (ordres 1 et 2, sauvegarde, rechargement, erreurs) et sur chaque test ; la moindre fuite, même « still reachable », fait échouer `make test` |
| `make couverture` | 98,7 % des lignes et 85,9 % des branches exécutées ; les lignes restantes sont marquées `#####` dans `couverture/*.gcov` |

Ces filets ont été eux-mêmes mis à l'épreuve : deux défauts injectés exprès ont bien été détectés (une fuite dans le chargement, une structure modifiée malgré un échec d'allocation).

**Programmation par contrat.** Chaque fonction publique documente ses `@pre` et `@post`, et les macros de `src/contrat.h` les vérifient à l'exécution. Les erreurs d'environnement (fichier absent, mémoire épuisée, disque plein…) ne sont pas des violations de contrat : elles passent par la valeur de retour et `errno`. Chaque violation est testée dans un processus fils, qui doit être arrêté par `SIGABRT`.

**Hook git et intégration continue.**

- Après `make installer_hook`, chaque commit lance `make test` sur une copie du contenu indexé, c'est-à-dire exactement ce qui sera validé. Un commit qui fuit est refusé ; `git commit --no-verify` permet de passer outre.
- La CI GitLab compile, lance `make test`, puis produit la couverture (pourcentage affiché par GitLab) et la documentation.

## Choix de conception

### Chaîne de Markov

- **Matrice creuse** : seules les transitions de poids non nul sont stockées.
- **Apprentissage** : chaque état a une petite table de hachage de ses successeurs, ce qui permet d'augmenter un poids en O(1) en moyenne.
- **Finalisation** : tableau trié de poids cumulés. Probabilité et déplacement se font en O(log k), et le tirage est exact (entiers, sans biais).
- **Générateur pseudo-aléatoire propre** (SplitMix64), plutôt que `rand()`, qui varie d'une bibliothèque C à l'autre.

### Tables de hachage

- **Mots** : un mot est lu comme un nombre en base 27 (a = 1, …, z = 26). Ce code est sans collision jusqu'à 13 lettres. La position vient du hachage de Fibonacci ; les collisions sont résolues par sondage linéaire, avec un taux de remplissage d'au plus 1/2. Sur le corpus, il faut en moyenne environ 1,25 essai pour trouver un mot (1 serait parfait).
- **Paires** : code `premier × 2³² + second`, mêmes techniques. Une paire de mots consécutifs est à la fois un état de l'ordre 2 et le contexte d'écriture d'un mot.
- **Stockage** : les mots sont rangés dans des blocs de 64 Kio qui ne bougent jamais. Il n'y a donc pas une allocation par mot, et les pointeurs restent valides.

### Lecture des textes

- **Mots et graphies** : un mot est une suite de lettres, réduite à une clé sur {a, …, z}. Sa graphie d'origine est conservée, convertie en UTF-8 si le fichier est en Latin-1.
- **Fins de phrase** : `.`, `!`, `?`, `…`, une ligne vide ou la fin du fichier. Le point d'une abréviation (`M. Dupont`) et celui d'un nombre (`3.5`) ne terminent pas la phrase.
- **Corpus nettoyé** : les en-têtes et licences du projet Gutenberg sont ignorés, ce qui retire 258 mots anglais du vocabulaire.
- **Ordre déterministe** : les fichiers sont lus dans l'ordre alphabétique, et les fichiers cachés sont ignorés.

### Un texte lisible

- **Accents** : seuls les fichiers qui contiennent des accents votent pour les accents. Sans cette règle, « etait » (5 449 fois dans `lesmiserables.txt`, écrit sans accents) l'emporterait sur « était » (1 085 fois ailleurs).
- **Majuscules** : les débuts de phrase ne votent pas, puisque leur majuscule est imposée par la position.
- **Contexte** : chaque paire (mot précédent, mot) a ses propres votes, à la majorité de Boyer et Moore (8 octets par vote). C'est ce qui donne « il a » et « va à ».
- **Liaisons** : le séparateur le plus fréquent entre deux mots donne les élisions, les traits d'union, les abréviations et les virgules. Une liste fixe de lettres élidées aurait transformé « M. Madeleine » (948 occurrences) en « M'Madeleine ».
- **Ponctuation finale** : elle est tirée selon le dernier mot par une seconde chaîne de Markov.

### Modèle sauvegardé

Le format est un fichier texte, décrit dans `modele.h` et écrit avec le module `sauvegarde`. Au chargement, tout est vérifié : syntaxe, bornes, doublons, cohérence des graphies, des transitions et de la ponctuation. Un fichier modifié est refusé, jamais accepté à moitié.

## Mesures

Sur le corpus `dataset` (5 Mo, 43 853 mots différents) :

| | Ordre 1 | Ordre 2 |
|---|---|---|
| États / transitions | 43 854 / 344 252 | 327 756 / 668 304 |
| Apprentissage + génération | 0,43 s | 0,55 s |
| Mémoire maximale (valgrind massif) | 35 Mio | 50 Mio |
| Modèle sauvegardé | 8 Mio | 11 Mio |
| Chargement du modèle + génération | 0,16 s | 0,22 s |

## Limites connues

- **Phrases anglaises** : `contes.txt` est une édition scolaire annotée. À partir de la ligne 11 107, elle contient des notes et un vocabulaire en anglais, qui font partie du livre et non de la licence. Le texte généré en reprend donc parfois des bribes (« Shyness. », « adj. »). Couper ce fichier à la ligne 11 107 les supprimerait.
- **Mots qui ne diffèrent que par les accents** : ils partagent une même clé (« égare » / « égaré »). Si leur contexte n'apparaît que dans des fichiers sans accents, c'est l'écriture la plus fréquente du mot qui est utilisée.
- **Plateforme** : `make test_defaillances` utilise l'option `--wrap` de l'éditeur de liens. Le ld de GNU, utilisé sous Linux par gcc et clang, la connaît ; celui de macOS ne la connaît pas.

## Pour aller plus loin

Le projet est conçu pour être exploré et prolongé. Quelques pistes, non encore réalisées :

- **Votre propre corpus** : des poèmes, des chansons, vos mails ou les œuvres complètes d'un auteur du projet Gutenberg. Il suffit d'un répertoire de fichiers texte.
- **Un ordre quelconque** : l'ordre 2 montre que `markov` n'a pas à changer. Généraliser la table des paires à des n-uplets ouvrirait les ordres 3 et plus.
- **Guillemets et dialogues** : la ponctuation interne se limite aujourd'hui aux virgules, points-virgules et deux-points.
- **Détection de langue** : écarter automatiquement les passages qui ne sont pas dans la langue majoritaire du corpus.
- **Choisir le premier mot** : faire partir la génération d'un mot donné.

Pour contribuer :

1. Lancez `make installer_hook`.
2. Écrivez le contrat dans le `.h` avant le code.
3. Ajoutez les tests correspondants.
4. Vérifiez que `make test` reste vert.

Les conventions sont simples : du C99 avec POSIX, sans accents dans le code, et une erreur d'environnement toujours signalée, jamais une violation de contrat.
