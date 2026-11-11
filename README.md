# Générateur de texte par chaîne de Markov

<p align="center">
<strong>Il lit Victor Hugo, Molière, Stendhal, Mérimée et Pierre Loti.</strong><br>
<strong>Puis il écrit à leur manière.</strong>
</p>

Le programme est écrit en C99, sans autre bibliothèque que la bibliothèque C standard, et il ne laisse aucune fuite mémoire : 190 tests et valgrind le vérifient, et un hook git peut les lancer avant chaque commit.

```sh
make && ./projet -k 3 -g 7 -n 8 dataset
```

<p>
> <i>Jean Valjean était morte, et avait été fait colonel à Waterloo.</i></br>
> <i>D'abord ces gens-là avec leurs fariboles.</i></br>
> <i>Sa sœur touchait une rente viagère de cinq cents francs, et sortez d'ici</i>
</p>

Aucune de ces phrases n'existe dans les livres : le programme les a inventées. Il a seulement appris quels mots suivent quels autres, et comment chaque mot s'écrit. Ces trois phrases font partie des huit que produit la commande ci-dessus : avec la même graine (`-g 7`), vous obtiendrez les mêmes.


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
10. [Pour contribuer](#pour-contribuer)


## Essayer en deux minutes

```sh
make                                 # compile ./projet (gcc, C99)
./projet dataset                     # 10 phrases inspirées du corpus
./projet -k 3 -g 7 -n 8 dataset      # ordre 3 : des phrases plus naturelles
./projet -k 1 -g 7 -n 8 dataset      # comparez avec l'ordre 1
./projet -k 3 -p Marius dataset      # chaque phrase commence par « Marius »
make test                            # toute la suite de tests (≈ 2 min)
```

Le texte s'affiche et s'enregistre dans `texte_genere_<graine>.txt`, qui n'est jamais écrasé.

**L'ordre change tout.** À l'ordre 1, chaque mot ne dépend que du précédent ; à l'ordre k, il dépend des k précédents. Voici des extraits de l'ordre 1, avec la même graine que plus haut :

<p>
> <i>Ce je la voiture.</i></br>
> <i>Les bêtes féroces sont habillés et nous décrivit plusieurs chandelles éclairaient le répète que vous asseoir sur un ogre de la, dans toute une nuit.</i>
</p>

<br>

Plus l'ordre monte, plus les phrases sont cohérentes, mais moins elles sont nouvelles. Sur 2 000 phrases générées, voici la part de celles qui n'existent pas telles quelles dans le corpus :

| Ordre | 1 | 2 | 3 | 4 | 5 | 6 | 8 |
|---|---|---|---|---|---|---|---|
| Phrases inédites | 93 % | 87 % | 68 % | 36 % | 13 % | 5 % | 1 % |

Les ordres 2 et 3 donnent le meilleur compromis.

**Un mot pour commencer.** Avec `-p`, chaque phrase part du mot donné, avec ou sans accents ni majuscules (`-p ete` donne « Été »). Par exemple, avec `-k 3 -g 7 -p Marius` :

<p>
> <i>Marius attendait dans une anxiété que tout accroissait.</i></br>
> <i>Marius était une prunelle franche, et il lui a dit: «Restez assis, monsieur Dollinger!»</i>
</p>

<br>


**Votre propre corpus.** Donnez au programme un ou plusieurs fichiers ou répertoires de textes :

```sh
./projet mes_romans/                   # tous les fichiers d'un répertoire
./projet -l poemes/ chansons.txt       # une phrase par vers
./projet -r gutenberg/                 # les sous-répertoires aussi
./projet -t corpus_bilingue/           # garder toutes les langues
```

Les livres du projet Gutenberg peuvent être déposés tels quels : leurs en-têtes et leurs licences sont ignorés.

## Comment ça marche

Tout repose sur une idée simple, celle des chaînes de Markov : **le mot suivant ne dépend que du ou des mots précédents**.

**1. Apprendre.** Le programme lit chaque phrase et compte quel mot suit quel autre. Le mot vide (l'état 0) marque le début et la fin des phrases. Sur le corpus « Le chat dort. Le chien dort. » :

```
0     → le      2 fois    probabilité 1
le    → chat    1 fois    probabilité 1/2
le    → chien   1 fois    probabilité 1/2
chat  → dort    1 fois    probabilité 1
chien → dort    1 fois    probabilité 1
dort  → 0       2 fois    probabilité 1      (fin de phrase)
```

À l'ordre k, un état n'est plus un mot mais la suite des k derniers mots de la phrase : à l'ordre 2, « le chat » mène à « chat dort ».

**2. Générer.** Il part de l'état 0, tire chaque mot suivant selon ces probabilités, et s'arrête quand il revient à 0. Un parcours possible est `0 → le → chien → dort → 0`, ce qui donne « Le chien dort. ». Appris sur les 850 000 mots du corpus, ces tirages produisent des phrases inédites.

**3. Bien écrire.** Pour l'apprentissage, les mots sont réduits à des clés sans accents ni majuscules (`etait`). Le programme retient donc aussi, pour chaque paire de mots consécutifs :

- l'écriture la plus fréquente du mot (« était ») ;
- ses majuscules (« Marius ») ;
- le séparateur qui le lie au mot précédent (apostrophe, trait d'union, virgule…) ;
- les marques de dialogue qui les séparent : tiret, guillemet ouvrant ou fermant.

Il retient enfin la ponctuation qui termine les phrases selon leur dernier mot.

**4. Choisir ce qu'on apprend.** Avant d'apprendre, le programme lit une première fois le corpus pour en reconnaître la langue majoritaire. Les passages écrits dans une autre langue sont ensuite ignorés, comme s'ils n'étaient pas dans les fichiers.

## L'architecture

```
 fichiers et répertoires du corpus
      │
      ▼
 itérateur de mots ............ mots {a..z}, graphies, liaisons, guillemets,
 (repertoire)                   tirets, ponctuation ; licences ignorées
      │
      ├──► 1re lecture : détecteur de langue ... passages à écarter (langue)
      │
      └──► 2e lecture, sans les passages écartés :
             ├──► table des mots ......... mot ↔ identifiant        (hachage)
             ├──► table des paires ....... bigramme ↔ identifiant   (hachage)
             ├──► table des n-uplets ..... état d'ordre k ≥ 3       (hachage)
             ├──► lexique ................ comment écrire un mot    (lexique)
             ├──► chaîne de Markov ....... quel mot suit            (markov)
             └──► chaîne de ponctuation .. comment finir la phrase  (markov)
                            │
                            ▼
                         modèle ──► phrases ──► fichier texte       (modele, sauvegarde)
                            │
                            └──► modele.txt : sauvegarde et rechargement
```

| Module | Rôle | À lire pour… |
|---|---|---|
| `src/markov.h/.c` | chaîne de Markov creuse, tirage exact, générateur interne | la structure de données centrale |
| `src/hachage.h/.c` | tables des mots, des paires et des n-uplets | le hachage sans collision des mots courts |
| `src/repertoire.h/.c` | itérateur de mots, liste des fichiers, apprentissage | la lecture robuste (UTF-8, Latin-1, dialogues) |
| `src/langue.h/.c` | langue majoritaire et passages à écarter | une détection de langue par mots-outils |
| `src/lexique.h/.c` | accents, majuscules, liaisons et marques appris par vote | comment un texte devient lisible |
| `src/modele.h/.c` | modèle complet : génération, sauvegarde, chargement | l'assemblage et le format de fichier |
| `src/sauvegarde.h/.c` | écriture et suppression sûres de fichiers | la gestion des erreurs du système |
| `src/texte.h`, `src/contrat.h` | types communs, macros de contrat | les conventions du projet |
| `main.c` | programme principal et ligne de commande | l'utilisation de tous les modules |
| `tests/` | 10 programmes de test et leurs outils | la manière de tout vérifier |
| `hooks/pre-commit`, `.gitlab-ci.yml`, `Doxyfile` | hook git, intégration continue, documentation | l'outillage |

Les modules sont indépendants : `markov` ne manipule que des entiers, `hachage` que des mots ou des suites d'entiers, et `langue` ne lit aucun fichier. Chacun se teste seul. Les contrats complets (pré-conditions, post-conditions, complexité) sont dans les fichiers `.h`, et `make doc` en fait une documentation HTML.

## Utilisation détaillée

```
./projet [options] <corpus> [<corpus> ...]
./projet [options] <repertoire> <graine> [nb_phrases] [fichier_sortie]
./projet [options] -m <modele>
```

Un corpus est un fichier texte ou un répertoire de fichiers texte.

| Option | Rôle |
|---|---|
| `-g <graine>` | graine du générateur (défaut : l'heure) |
| `-n <nombre>` | nombre de phrases (défaut : 10) |
| `-o <fichier>` | fichier de sortie (défaut : `texte_genere_<graine>.txt`) |
| `-p <mot>` | faire commencer chaque phrase par ce mot |
| `-k <ordre>` | ordre de la chaîne, de 1 (défaut) à 8 |
| `-r` | lire aussi les sous-répertoires (par exemple `dataset/en`) |
| `-l` | une phrase par ligne (poèmes, chansons, listes) |
| `-t` | tout garder : ne pas écarter les passages en d'autres langues |
| `-s <fichier>` | sauvegarder le modèle appris |
| `-m <fichier>` | charger un modèle au lieu de relire le corpus |
| `-h`, `--aide` | afficher l'aide |

```sh
./projet -s modele.txt -k 3 dataset      # apprendre une fois…
./projet -m modele.txt -n 20 -p Cosette  # …puis générer sans relire le corpus
./projet dataset 42 5 sortie.txt         # ancienne syntaxe, toujours acceptée
```

Dans l'ancienne syntaxe, le deuxième argument est un nombre : c'est la graine. Pour un répertoire dont le nom est un nombre, écrivez `./42`.
Le code de retour vaut 0 en cas de succès et 1 en cas d'erreur, avec un message clair (chemin absent, mot inconnu, modèle invalide, disque plein…).

| Commande | Effet |
|---|---|
| `make` | construit `./projet` |
| `make test` | tous les tests et la vérification mémoire |
| `make test_markov` | un seul module : de même `test_hachage`, `test_repertoire`, `test_sauvegarde`, `test_lexique`, `test_langue`, `test_modele` |
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
| 165 tests unitaires et du programme | contrats, cas limites, codes de retour, une soixantaine de fichiers de modèle corrompus refusés |
| `test_defaillances` | 2 356 exécutions, chacune faisant échouer une allocation différente (une seule fois, ou toutes à partir de la N-ième) : erreur signalée, structure intacte, aucun bloc perdu |
| `test_aleatoire` | octets au hasard pour l'itérateur, corpus aléatoires à tous les ordres et avec toutes les options, modèles abîmés, détecteur de langue, sous AddressSanitizer et UndefinedBehaviorSanitizer |
| `test_memoire` | valgrind sur le programme (ordres 1, 2 et 4, toutes les options, sauvegarde, rechargement, erreurs) et sur chaque test ; la moindre fuite, même « still reachable », fait échouer `make test` |
| `make couverture` | 98,8 % des lignes et 86,6 % des branches exécutées ; les lignes restantes sont marquées `#####` dans `couverture/*.gcov` |

Ces filets ont été eux-mêmes mis à l'épreuve : des défauts injectés exprès ont bien été détectés. Parmi eux : une fuite, une table modifiée malgré un échec d'allocation, un guillemet ouvert deux fois, un mauvais point de départ pour `-p`, une vérification oubliée au chargement.

**Programmation par contrat.** Chaque fonction publique documente ses `@pre` et `@post`, et les macros de `src/contrat.h` les vérifient à l'exécution. Les erreurs d'environnement (fichier absent, mémoire épuisée, disque plein…) ne sont pas des violations de contrat : elles passent par la valeur de retour et `errno`. Chaque violation est testée dans un processus fils, qui doit être arrêté par `SIGABRT`.

**Hook git et intégration continue.**

- Après `make installer_hook`, chaque commit lance `make test` sur une copie du contenu indexé, c'est-à-dire exactement ce qui sera validé. Un commit qui fuit est refusé ; `git commit --no-verify` permet de passer outre.
- La CI GitLab compile, lance `make test`, puis produit la couverture (pourcentage affiché par GitLab) et la documentation.

## Choix de conception

### Chaîne de Markov

- **Matrice creuse** : seules les transitions de poids non nul sont stockées. La structure d'exemple `src/bbergezg`, à matrice pleine, n'a donc pas été retenue.
- **Apprentissage** : chaque état a une petite table de hachage de ses successeurs, ce qui permet d'augmenter un poids en O(1) en moyenne.
- **Finalisation** : tableau trié de poids cumulés. Probabilité et déplacement se font en O(log k), et le tirage est exact (entiers, sans biais).
- **Générateur pseudo-aléatoire propre** (SplitMix64), plutôt que `rand()`, qui varie d'une bibliothèque C à l'autre.
- **Ordre quelconque** : `markov` ne connaît que des numéros d'états. Monter en ordre ne demande donc que de numéroter des suites de mots : il n'a pas été modifié.

### Tables de hachage

- **Mots** : un mot est lu comme un nombre en base 27 (a = 1, …, z = 26). Ce code est sans collision jusqu'à 13 lettres. La position vient du hachage de Fibonacci ; les collisions sont résolues par sondage linéaire, avec un taux de remplissage d'au plus 1/2. Sur le corpus, il faut en moyenne environ 1,25 essai pour trouver un mot (1 serait parfait).
- **N-uplets** : à l'ordre k ≥ 3, un état est la suite des k derniers mots, complétée à gauche par des 0 en début de phrase. Ses éléments sont mélangés un à un dans un code de 64 bits ; deux suites de même code sont départagées en comparant leurs éléments. Chaque état coûte 4 × k octets pour ses éléments, plus au moins deux cases de 4 octets dans la table, qui reste à moitié vide.
- **Paires** : ce sont des n-uplets de taille 2. Une paire de mots consécutifs est à la fois un état de l'ordre 2 et le contexte d'écriture d'un mot.
- **Stockage** : les mots sont rangés dans des blocs de 64 Kio qui ne bougent jamais. Il n'y a donc pas une allocation par mot, et les pointeurs restent valides.

### Lecture des textes

- **Mots et graphies** : un mot est une suite de lettres, réduite à une clé sur {a, …, z}. Sa graphie d'origine est conservée, convertie en UTF-8 si le fichier est en Latin-1.
- **Fins de phrase** : `.`, `!`, `?`, `…`, une ligne vide ou la fin du fichier, et chaque fin de ligne avec `-l`. Le point d'une abréviation (`M. Dupont`) et celui d'un nombre (`3.5`) ne terminent pas la phrase.
- **Dialogues** : les guillemets français, anglais, allemands et droits sont reconnus, de même que les tirets cadratin et demi-cadratin. Les « -- » des livres du projet Gutenberg sont aussi des tirets ; quatre « - » ou plus forment un trait décoratif. Un guillemet droit ouvre s'il est collé au mot qui le suit, et ferme sinon.
- **Corpus nettoyé** : les en-têtes et licences du projet Gutenberg sont ignorés, ce qui retire 258 mots anglais du vocabulaire.
- **Ordre déterministe** : les chemins sont lus dans l'ordre donné, et les fichiers d'un répertoire dans l'ordre alphabétique. Les fichiers cachés sont ignorés.

### Un texte lisible

- **Accents** : seuls les fichiers qui contiennent des accents votent pour les accents. Sans cette règle, « etait » (5 449 fois dans `lesmiserables.txt`, écrit sans accents) l'emporterait sur « était » (1 085 fois ailleurs).
- **Majuscules** : les débuts de phrase ne votent pas, puisque leur majuscule est imposée par la position.
- **Contexte** : chaque paire (mot précédent, mot) a ses propres votes, à la majorité de Boyer et Moore (8 octets par vote). C'est ce qui donne « il a » et « va à ».
- **Liaisons** : le séparateur le plus fréquent entre deux mots donne les élisions, les traits d'union, les abréviations et les virgules. Une liste fixe de lettres élidées aurait transformé « M. Madeleine » (948 occurrences) en « M'Madeleine ».
- **Dialogues** : la liaison et les marques de dialogue votent ensemble, rangées dans un seul octet. Un mot qui ouvre le plus souvent une réplique garde son tiret. À l'écriture, un guillemet n'est ouvert que s'il est fermé, et tout guillemet ouvert est refermé en fin de phrase.
- **Ponctuation finale** : elle est tirée selon le dernier mot par une seconde chaîne de Markov. Avec `-l`, une fin de vers sans ponctuation est apprise telle quelle.

### Filtre de langue

- **Profils** : pour chacune des sept langues (français, anglais, espagnol, allemand, italien, portugais, néerlandais), une liste de 72 à 112 mots-outils : articles, pronoms, prépositions. Dans les romans du corpus, ils forment plus de la moitié des mots.
- **Passages** : les phrases sont groupées en passages d'au moins 40 mots, car une phrase seule est trop courte pour être jugée. La langue majoritaire est celle qui compte le plus de mots-outils dans tout le corpus.
- **Décision** : un passage est écarté si une autre langue y domine. Il l'est aussi si moins de 7 mots sur 10 sont des mots connus, c'est-à-dire vus surtout dans des passages sûrs. Cette seconde règle écarte un vocabulaire bilingue (« éveil, m. awakening »), qui n'a presque pas de mots-outils. Elle garde une réplique courte ou une liste de noms propres.
- **Résultat sur `dataset`** : 50 248 mots sont écartés sur 903 194. Ce sont les notes et le vocabulaire anglais de `contes.txt`, les chansons espagnoles et italiennes du *Bourgeois gentilhomme* et des listes de mots rares d'`orthographe1990.txt`. Rien n'est écarté chez Hugo, Stendhal, Gide ou Pierre Loti ; dans les contes, seules les deux dernières phrases, voisines des notes, le sont. Le filtre coûte une seconde lecture du corpus, soit 0,1 s.

### Premier mot

`-p` accepte un mot avec ou sans accents ni majuscules. Si ce mot commence des phrases du corpus, la génération part de là. Sinon, elle part de son contexte le plus fréquent, et le mot reçoit une majuscule.

### Modèle sauvegardé

Le format est un fichier texte, décrit dans `modele.h` et écrit avec le module `sauvegarde`. Sa version 2 ajoute l'ordre, les marques de dialogue et les états d'ordre k : un modèle de l'ancienne version doit être réappris. Au chargement, tout est vérifié : syntaxe, bornes, doublons, cohérence des graphies, des états, des transitions et de la ponctuation. Un fichier modifié est refusé, jamais accepté à moitié.

## Mesures

Sur le corpus `dataset` (5 Mo, 903 194 mots dont 852 946 gardés par le filtre de langue, 36 511 mots différents) :

| Ordre | États | Transitions | Apprentissage + génération | Mémoire | Modèle sauvegardé | Rechargement + génération |
|---|---|---|---|---|---|---|
| 1 | 36 512 | 305 658 | 0,52 s | 38 Mio | 7,7 Mio | 0,14 s |
| 2 | 293 061 | 615 454 | 0,61 s | 57 Mio | 11 Mio | 0,19 s |
| 3 | 584 630 | 747 493 | 0,76 s | 95 Mio | 20 Mio | 0,37 s |
| 8 | 748 974 | 798 201 | 0,73 s | 113 Mio | 35 Mio | 0,55 s |

La mémoire est la mémoire résidente maximale du programme. À l'ordre 8, chaque état n'a en moyenne que 1,07 successeur : c'est pourquoi le texte y recopie le corpus.

## Limites connues

- **Langues** : le filtre ne connaît que sept langues. Pour un corpus écrit dans une autre, la langue n'est pas reconnue et rien n'est écarté.
- **Passages mixtes** : dans les notes de `contes.txt`, quelques passages mêlent citations françaises et explications anglaises, et le français y domine. Ils sont gardés : environ 900 mots, dont quelques bribes d'anglais peuvent reparaître.
- **Listes de mots rares** : les listes d'`orthographe1990.txt` sont écartées, bien qu'en français. Sans mots-outils ni mots connus, rien ne les distingue d'une langue étrangère.
- **Mots qui ne diffèrent que par les accents** : ils partagent une même clé (« égare » / « égaré », « vit » / « vît »). Si leur contexte n'apparaît que dans des fichiers sans accents, c'est l'écriture la plus fréquente du mot qui est utilisée.
- **Mails** : un mail exporté en texte se lit comme un autre texte, mais ses en-têtes et ses citations ne sont pas reconnus, ni le format MIME.
- **Plateforme** : `make test_defaillances` utilise l'option `--wrap` de l'éditeur de liens. Le ld de GNU, utilisé sous Linux par gcc et clang, la connaît ; celui de macOS ne la connaît pas.

## Pour contribuer

1. Lancez `make installer_hook`.
2. Écrivez le contrat dans le `.h` avant le code.
3. Ajoutez les tests correspondants.
4. Vérifiez que `make test` reste vert.

Les conventions sont simples : du C99 avec POSIX, sans accents dans le code, et une erreur d'environnement toujours signalée, jamais une violation de contrat.