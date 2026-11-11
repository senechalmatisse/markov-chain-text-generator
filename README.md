# Générateur de texte par chaîne de Markov

<p align="center">
<strong>Il lit Victor Hugo, Molière, Stendhal, Mérimée et Pierre Loti.</strong><br>
<strong>Puis il écrit à leur manière.</strong>
</p>

Le programme est écrit en C99, sans autre bibliothèque que la bibliothèque C standard, et il ne laisse aucune fuite mémoire : 230 tests et valgrind le vérifient, et un hook git peut les lancer avant chaque commit.

```sh
make && ./projet -k 3 -g 7 -n 8 dataset
```

<p>
> <i>On est vieille, on est prude, on est dévote, on est la tante; mais c'est qu'elle prend moins de ventre que les autres.</i></br>
> <i>Ce jour-là on pût dire: Ah! ceci est cassé.</i></br>
> <i>Ce mot fit son apparition.</i>
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
10. [Prochaine version](#prochaine-version)
11. [Pour contribuer](#pour-contribuer)


## Essayer en deux minutes

```sh
make                                   # compile ./projet (gcc, C99)
./projet dataset                       # 10 phrases inspirées du corpus
./projet -k 3 -g 7 -n 8 dataset        # ordre 3 : des phrases plus naturelles
./projet -k 5 -b 3 -g 7 dataset        # ordre 5, repli à l'ordre 3
./projet -k 3 -p Marius dataset        # chaque phrase commence par « Marius »
./projet -k 2 exemples/courriers.mbox  # une boîte mail comme corpus
make test                              # toute la suite de tests (≈ 3 min)
```

Le texte s'affiche et s'enregistre dans `texte_genere_<graine>.txt`, qui n'est jamais écrasé.

**L'ordre change tout.** À l'ordre 1, chaque mot ne dépend que du précédent ; à l'ordre k, il dépend des k précédents. Voici des extraits de l'ordre 1, avec la même graine que plus haut :

<p>
> <i>Nous quittons l'héritage et avec tristesse de mal ne voulait.</i></br>
> <i>Comme cela avait sur deux branches, chantaient toujours par une explication qui n'entrez dans les huguenots au moment.</i>
</p>

<br>

Plus l'ordre monte, plus les phrases sont cohérentes, mais moins elles sont nouvelles. Sur 2 000 phrases générées, voici la part de celles qui n'existent pas telles quelles dans le corpus :

| Ordre | 1 | 2 | 3 | 4 | 5 | 6 | 8 |
|---|---|---|---|---|---|---|---|
| Phrases inédites | 95 % | 91 % | 73 % | 39 % | 14 % | 4 % | 2 % |

**Le repli.** À l'ordre 5, 98 % des contextes de cinq mots n'ont qu'un successeur : le texte recopie alors le livre. Avec `-b 3`, dans ce cas, le mot suivant est tiré selon les trois derniers mots seulement. Les contextes suivis de plusieurs mots différents gardent l'ordre 5 :

| Options | `-k 4 -b 2` | `-k 5 -b 3` | `-k 6 -b 4` | `-k 8 -b 5` |
|---|---|---|---|---|
| Phrases inédites | 88 % | 69 % | 38 % | 14 % |
| Mots tirés à l'ordre principal | 21 % | 18 % | 17 % | 17 % |

Avec `-k 5 -b 3 -g 7`, par exemple :

<p>
> <i>Le cadet Monval, buse à figure de buse, mais bon mathématicien (terme de l'École centrale).</i></br>
> <i>Un jour enfin le bruit se répandit un matin dans une ruelle non pavée de Montreuil-sur-mer, on remarquait que d'année en année, son langage devenait plus poli, plus choisi et plus doux!</i>
</p>

<br>

**Un mot pour commencer.** Avec `-p`, chaque phrase part du mot donné, avec ou sans accents ni majuscules (`-p ete` donne « Été »). Par exemple, avec `-k 3 -g 7 -p Marius` :

<p>
> <i>Marius entendit un cliquetis de mots, traversa tout à coup il se sentit rassuré.</i></br>
> <i>Marius approcha le plus près qu'il pût et en ayant soin de ne jamais dormir sans rêver l'un de ses jurons était par la pantoufloche de la pantouflochade!</i>
</p>

<br>


**Votre propre corpus.** Donnez au programme un ou plusieurs fichiers ou répertoires de textes :

```sh
./projet mes_romans/                   # tous les fichiers d'un répertoire
./projet -l poemes/ chansons.txt       # une phrase par vers
./projet -r gutenberg/                 # les sous-répertoires aussi
./projet -t corpus_bilingue/           # garder toutes les langues
./projet -k 2 Mail/boite.mbox *.eml    # des mails
```

Les livres du projet Gutenberg peuvent être déposés tels quels : leurs en-têtes et leurs licences sont ignorés. Les mails aussi : une boîte au format mbox ou des messages `.eml` sont reconnus d'eux-mêmes, et seul le texte de l'expéditeur est lu. Sur l'exemple fourni, trois messages d'un atelier d'écriture, `-k 2 -g 4` mélange déjà deux messages :

<p>
> <i>Je ne suis pas sûr de pouvoir le lire en entier, il est encore un peu en retard: le train arrive à dix-neuf heures.
</i>
</p>

<br>

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

**3. Se replier.** Avec un ordre de repli j, le programme apprend dans la même lecture une seconde chaîne, d'ordre j. Pendant la génération, si le contexte des k derniers mots n'a qu'un successeur, ou n'a jamais été vu (après un repli, la suite de mots peut être nouvelle), le mot suivant est tiré dans la chaîne d'ordre j.

**4. Bien écrire.** Pour l'apprentissage, les mots sont réduits à des clés sans accents ni majuscules (`etait`). Le programme retient donc aussi, pour chaque paire de mots consécutifs :

- l'écriture la plus fréquente du mot (« était »), et pour les homographes   celle qui suit les deux mots précédents (« c'est la », « il est là ») ;
- ses majuscules (« Marius ») ;
- le séparateur qui le lie au mot précédent (apostrophe, trait d'union, virgule, ponctuation au fil de la phrase…) ;
- les marques de dialogue qui les séparent : tiret, guillemet ou parenthèse, ouvrant ou fermant.

Il retient enfin la ponctuation qui termine les phrases selon leur dernier mot.

**5. Choisir ce qu'on apprend.** Avant d'apprendre, le programme lit une première fois le corpus pour en reconnaître la langue majoritaire. Les passages écrits dans une autre langue sont ensuite ignorés, comme s'ils n'étaient pas dans les fichiers.

## L'architecture

```
 fichiers et répertoires du corpus
      │
      ├──► mail ........................ courriers : texte de l'expéditeur (MIME, citations, signatures)
      ▼
 itérateur de mots ............ mots {a..z}, graphies, liaisons, guillemets,
 (repertoire)                   parenthèses, tirets, ponctuation ; licences ignorées
      │
      ├──► 1re lecture : détecteur de langue ... passages à écarter (langue)
      │
      └──► 2e lecture, sans les passages écartés :
             ├──► table des mots ......... mot ↔ identifiant          (hachage)
             ├──► table des paires ....... bigramme ↔ identifiant     (hachage)
             ├──► table des n-uplets ..... état d'ordre k ≥ 3         (hachage)
             ├──► lexique ................ comment écrire un mot      (lexique)
             ├──► chaîne de Markov ....... quel mot suit              (markov)
             ├──► chaîne de repli ........ quel mot suit, à l'ordre j (markov)
             └──► chaîne de ponctuation .. comment finir la phrase    (markov)
                            │
                            ▼
                         modèle ──► phrases ──► fichier texte
                            │       (modele  ,  sauvegarde)
                            └──► modele.txt : sauvegarde et rechargement
```

| Module | Rôle | À lire pour… |
|---|---|---|
| `src/markov.h/.c` | chaîne de Markov creuse, tirage exact, générateur interne | la structure de données centrale |
| `src/hachage.h/.c` | tables des mots, des paires et des n-uplets | le hachage sans collision des mots courts |
| `src/repertoire.h/.c` | itérateur de mots, liste des fichiers, apprentissage | la lecture robuste (UTF-8, Latin-1, dialogues) |
| `src/mail.h/.c` | lecture des courriers : MIME, encodages, citations | le texte d'un mail, sans le reste |
| `src/langue.h/.c` | langue majoritaire et passages à écarter | une détection de langue par mots-outils |
| `src/lexique.h/.c` | accents, majuscules, liaisons et marques appris par vote | comment un texte devient lisible |
| `src/modele.h/.c` | modèle complet : génération, repli, sauvegarde, chargement | l'assemblage et le format de fichier |
| `src/sauvegarde.h/.c` | écriture et suppression sûres de fichiers | la gestion des erreurs du système |
| `src/texte.h`, `src/contrat.h` | types communs, macros de contrat | les conventions du projet |
| `main.c` | programme principal et ligne de commande | l'utilisation de tous les modules |
| `tests/` | 11 programmes de test et leurs outils | la manière de tout vérifier |
| `exemples/courriers.mbox` | une petite boîte mail d'exemple | essayer la lecture des mails |
| `hooks/pre-commit`, `.gitlab-ci.yml`, `Doxyfile` | hook git, intégration continue, documentation | l'outillage |

Les modules sont indépendants : `markov` ne manipule que des entiers, `hachage` que des mots ou des suites d'entiers, `mail` que des octets en mémoire, et `langue` ne lit aucun fichier. Chacun se teste seul. Les contrats complets (pré-conditions, post-conditions, complexité) sont dans les fichiers `.h`, et `make doc` en fait une documentation HTML.

## Utilisation détaillée

```
./projet [options] <corpus> [<corpus> ...]
./projet [options] <repertoire> <graine> [nb_phrases] [fichier_sortie]
./projet [options] -m <modele>
```

Un corpus est un fichier texte ou un répertoire de fichiers texte ; les mails (`.eml`, mbox) y sont reconnus d'eux-mêmes.

| Option | Rôle |
|---|---|
| `-g <graine>` | graine du générateur (défaut : l'heure) |
| `-n <nombre>` | nombre de phrases (défaut : 10) |
| `-o <fichier>` | fichier de sortie (défaut : `texte_genere_<graine>.txt`) |
| `-p <mot>` | faire commencer chaque phrase par ce mot |
| `-k <ordre>` | ordre de la chaîne, de 1 (défaut) à 8 |
| `-b <ordre>` | ordre de repli, inférieur à `-k` (défaut : 0, sans repli) |
| `-r` | lire aussi les sous-répertoires (par exemple `dataset/en`) |
| `-l` | une phrase par ligne (poèmes, chansons, listes) |
| `-t` | tout garder : ne pas écarter les passages en d'autres langues |
| `-s <fichier>` | sauvegarder le modèle appris |
| `-m <fichier>` | charger un modèle au lieu de relire le corpus |
| `-h`, `--aide` | afficher l'aide |

```sh
./projet -s modele.txt -k 5 -b 3 dataset   # apprendre une fois…
./projet -m modele.txt -n 20 -p Cosette    # …puis générer sans relire le corpus
./projet dataset 42 5 sortie.txt           # ancienne syntaxe, toujours acceptée
```

Dans l'ancienne syntaxe, le deuxième argument est un nombre : c'est la graine. Pour un répertoire dont le nom est un nombre, écrivez `./42`.

Le code de retour vaut 0 en cas de succès et 1 en cas d'erreur, avec un message clair (chemin absent, mot inconnu, ordre de repli trop grand, modèle invalide, disque plein…).

| Commande | Effet |
|---|---|
| `make` | construit `./projet` |
| `make test` | tous les tests et la vérification mémoire |
| `make test_markov` | un seul module : de même `test_hachage`, `test_repertoire`, `test_sauvegarde`, `test_lexique`, `test_langue`, `test_mail`, `test_modele` |
| `make test_programme` | lance `./projet` et vérifie les textes produits |
| `make test_defaillances` | fait échouer chaque allocation tour à tour |
| `make test_aleatoire` | octets et mails au hasard, sous AddressSanitizer |
| `make test_memoire` | valgrind sur le programme et sur chaque test |
| `make couverture` | couverture des tests (gcov) |
| `make doc` | documentation Doxygen dans `doc/html/` |
| `make installer_hook` | `make test` avant chaque commit |
| `make clean` | supprime les fichiers compilés et les rapports |

Il faut gcc et make pour compiler, **valgrind** pour `make test` et **doxygen** pour `make doc`. Si lcov est installé, `make couverture` produit aussi un rapport HTML.

## Qualité : tests, mémoire, contrats

| Vérification | Ce qui est contrôlé |
|---|---|
| 200 tests unitaires et du programme | contrats, cas limites, codes de retour, une centaine de fichiers de modèle corrompus refusés, mails mal formés |
| `test_defaillances` | 3 192 exécutions, chacune faisant échouer une allocation différente (une seule fois, ou toutes à partir de la N-ième) : erreur signalée, structure intacte, aucun bloc perdu |
| `test_aleatoire` | octets au hasard pour l'itérateur, corpus aléatoires à tous les ordres et avec toutes les options (repli compris), modèles abîmés, détecteur de langue, mails aléatoires (MIME, encodages, HTML), sous AddressSanitizer et UndefinedBehaviorSanitizer |
| `test_memoire` | valgrind sur le programme (ordres 1, 2, 4 et 5 avec repli, toutes les options, mails, sauvegarde, rechargement, erreurs) et sur chaque test ; la moindre fuite, même « still reachable », fait échouer `make test` |
| `make couverture` | 99,0 % des lignes exécutées et 87,8 % des branches prises ; les lignes restantes sont marquées `#####` dans `couverture/*.gcov` |

Ces filets ont été eux-mêmes mis à l'épreuve : des défauts injectés exprès, dont 25 dans le code de cette version, ont tous été détectés. Parmi eux : une fuite, une table modifiée malgré un échec d'allocation, un guillemet ouvert deux fois, un repli sur un contexte qui avait deux successeurs, un contexte d'accent décalé d'un mot, une citation ou une signature gardée dans un mail, une ligne « >From » d'une boîte mbox mal lue, une vérification oubliée au chargement.

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

### Repli

- **Une lecture, deux chaînes** : la chaîne de repli est apprise sur les mêmes phrases que la chaîne principale. C'est exactement celle d'un modèle de son ordre, ce que les tests vérifient pour chaque couple d'ordres.
- **Règle** : un mot est tiré à l'ordre de repli quand le contexte principal n'a qu'un successeur dans le corpus. Seuls les contextes qui ont été suivis de plusieurs mots différents gardent l'ordre principal.
- **Contexte absent** : après un repli, les k derniers mots peuvent former une suite jamais vue ; on se replie alors aussi. Les j derniers mots, eux, sont toujours un état de la chaîne de repli : c'est vrai d'un modèle appris, et vérifié au chargement. Avec `-k 5 -b 3`, 82 % des mots sont ainsi tirés à l'ordre 3 : 71 % faute de choix, 11 % faute de contexte.
- **Graine** : la chaîne de repli tire avec son propre générateur, dérivé de la graine comme celui de la chaîne de ponctuation, à l'apprentissage comme au chargement : un modèle appris et le même modèle rechargé donnent le même texte.

### Tables de hachage

- **Mots** : un mot est lu comme un nombre en base 27 (a = 1, …, z = 26). Ce code est sans collision jusqu'à 13 lettres. La position vient du hachage de Fibonacci ; les collisions sont résolues par sondage linéaire, avec un taux de remplissage d'au plus 1/2. Sur le corpus, il faut en moyenne environ 1,25 essai pour trouver un mot (1 serait parfait).
- **N-uplets** : à l'ordre k ≥ 3, un état est la suite des k derniers mots, complétée à gauche par des 0 en début de phrase. Ses éléments sont mélangés un à un dans un code de 64 bits ; deux suites de même code sont départagées en comparant leurs éléments. Chaque état coûte 4 × k octets pour ses éléments, plus au moins deux cases de 4 octets dans la table, qui reste à moitié vide.
- **Paires** : ce sont des n-uplets de taille 2. Une paire de mots consécutifs est à la fois un état de l'ordre 2 et le contexte d'écriture d'un mot ; la paire (mot d'avant, bigramme) est le contexte de ses accents.
- **Stockage** : les mots sont rangés dans des blocs de 64 Kio qui ne bougent jamais. Il n'y a donc pas une allocation par mot, et les pointeurs restent valides.

### Lecture des textes

- **Mots et graphies** : un mot est une suite de lettres, réduite à une clé sur {a, …, z}. Les lettres reconnues sont celles de Latin-1, de Latin étendu A (Ł, ř, ő, ğ, ı, ŝ…) et les lettres roumaines à virgule (ș, ț). La graphie d'origine est conservée, convertie en UTF-8 si le fichier est en Latin-1.
- **Fins de phrase** : `.`, `!`, `?`, `…`, une ligne vide ou la fin du fichier, et chaque fin de ligne avec `-l`. Le point d'une abréviation (`M. Dupont`) et celui d'un nombre (`3.5`) ne terminent pas la phrase.
- **Ponctuation au fil de la phrase** : un `!`, un `?`, des points de suspension ou un point suivis d'un mot en minuscules, sur la même ligne ou la suivante, ne terminent pas la phrase : ils deviennent une liaison, comme une virgule (« Ah! dit-il », « Qui? demanda-t-il »). Rien que dans *Les Misérables*, il y en a plus de 2 600.
- **Dialogues et parenthèses** : les guillemets français, anglais, allemands et droits sont reconnus, de même que les tirets cadratin et demi-cadratin, et les parenthèses et crochets. Les « -- » des livres du projet Gutenberg sont aussi des tirets ; quatre « - » ou plus forment un trait décoratif. Un guillemet droit ouvre s'il est collé au mot qui le suit, et ferme sinon.
- **Corpus nettoyé** : les en-têtes et licences du projet Gutenberg sont ignorés, ce qui retire 258 mots anglais du vocabulaire.
- **Ordre déterministe** : les chemins sont lus dans l'ordre donné, et les fichiers d'un répertoire dans l'ordre alphabétique. Les fichiers cachés sont ignorés.

### Lecture des mails

- **Reconnaissance** : un fichier est un mail s'il commence par un bloc d'en-têtes (`Nom: valeur`) qui contient au moins deux champs propres au courrier (`From`, `To`, `Subject`, `Date`, `Received`…), éventuellement précédé de la ligne `From ` d'une boîte mbox. Un texte qui commence par « Titre: » et « Auteur: » n'en est pas un.
- **MIME** : les parties sont découpées selon leur frontière ; dans une alternative, le texte brut est préféré au HTML ; les pièces jointes sont ignorées et un message joint est lu comme un message. Les corps sont décodés (quoted-printable, base64) et convertis en UTF-8 depuis UTF-8, ISO-8859-1, Windows-1252 ou ISO-8859-15 ; du HTML, seul le texte est gardé, entités décodées.
- **Ce qui n'est pas de l'expéditeur** : les en-têtes, les lignes citées (« > »), l'annonce d'une citation (« Le 3 mars, Paul a écrit : », même coupée en deux lignes), les citations HTML (`blockquote`), la suite d'un message transféré ou d'origine, la signature (après « -- »), les signatures PGP et les « Envoyé de mon iPhone ».
- **Messages séparés** : chaque message se termine par une ligne vide, pour que sa dernière phrase ne se poursuive pas dans le suivant. Le bilan de l'apprentissage compte les messages lus. Dans une boîte mbox, une ligne qui commençait par « From », et que la boîte a protégée en « >From », retrouve son texte au lieu de passer pour une citation.

### Un texte lisible

- **Accents** : seuls les fichiers qui contiennent des accents votent pour les accents. Sans cette règle, « etait » (5 449 fois dans `lesmiserables.txt`, écrit sans accents) l'emporterait sur « était » (1 085 fois ailleurs).
- **Majuscules** : les débuts de phrase ne votent pas, puisque leur majuscule est imposée par la position.
- **Contexte** : chaque paire (mot précédent, mot) a ses propres votes, à la majorité de Boyer et Moore (8 octets par vote). C'est ce qui donne « il a » et « va à ».
- **Homographes** : chaque suite de trois mots vote aussi. Seules ses exceptions, quand le mot d'avant change l'accent choisi par la paire, sont gardées dans le modèle : 563 sur le corpus, comme « il est là » (la paire « est la » s'écrit sans accent : « c'est la ») ou « à coup sûr » (« coup sur coup »). Sur les phrases du corpus, celles que la génération reproduit, les erreurs d'accent des homographes tombent de 1 039 à 363 sur 67 631 (de 98,5 % à 99,5 % de justes). Sur un texte jamais lu, le gain est presque nul (94,2 % dans les deux cas) : ces contextes sont trop rares pour se généraliser.
- **Liaisons** : le séparateur le plus fréquent entre deux mots donne les élisions, les traits d'union, les abréviations, les virgules et la ponctuation au fil de la phrase. Une liste fixe de lettres élidées aurait transformé « M. Madeleine » (948 occurrences) en « M'Madeleine ».
- **Dialogues et parenthèses** : la liaison et les marques votent ensemble, rangées dans un entier de 16 bits. Un mot qui ouvre le plus souvent une réplique garde son tiret. À l'écriture, une marque n'est fermée que si elle est ouverte, guillemets et parenthèses restent bien imbriqués, et tout ce qui est ouvert est refermé en fin de phrase.
- **Où placer la ponctuation** : un `!`, un `?`, des points de suspension ou un point d'abréviation se placent avant le guillemet ou la parenthèse qui se ferme (« «Victoire!» et », « (etc.) »), une virgule après (« «Viens», dit-il »), comme presque partout dans le corpus. En fin de phrase, une citation qui commence par une majuscule garde sa ponctuation à l'intérieur (« «Viens ici!» »), une remarque en minuscules la laisse dehors (« (en riant). »).
- **Ponctuation finale** : elle est tirée selon le dernier mot par une autre chaîne de Markov. Avec `-l`, une fin de vers sans ponctuation est apprise telle quelle.

### Filtre de langue

- **Profils** : pour chacune des dix-neuf langues (français, anglais, espagnol, allemand, italien, portugais, néerlandais, latin, catalan, suédois, danois, norvégien, finnois, polonais, tchèque, hongrois, roumain, turc, espéranto), une liste de 67 à 112 mots-outils : articles, pronoms, prépositions. Dans les romans du corpus, ils forment plus de la moitié des mots. Ajouter une langue revient à ajouter son nom et sa liste dans `src/langue.c`.
- **Langues proches** : danois et norvégien, espagnol et catalan, polonais et tchèque se distinguent par les mots qu'ils ne partagent pas (« mig », « af », « nu » en danois, « meg », « av », « nå » en norvégien). Les mots qui sont aussi des interjections ou des lettres isolées d'autres langues sont exclus : le « Hi, hi, hi » de Nicole et la leçon des voyelles du *Bourgeois gentilhomme* passaient sinon pour du catalan.
- **Passages** : les phrases sont groupées en passages d'au moins 40 mots, car une phrase seule est trop courte pour être jugée. La langue majoritaire est celle qui compte le plus de mots-outils dans tout le corpus.
- **Décision** : un passage est écarté si une autre langue y domine. Il l'est aussi si moins de 7 mots sur 10 sont des mots connus, c'est-à-dire vus surtout dans des passages sûrs. Cette seconde règle écarte un vocabulaire bilingue (« éveil, m. awakening »), qui n'a presque pas de mots-outils. Elle garde une réplique courte ou une liste de noms propres.
- **Résultat sur `dataset`** : 50 679 mots sont écartés sur 903 194, et l'ajout des douze nouvelles langues n'a rien changé. Ce sont les notes et le vocabulaire anglais de `contes.txt`, les chansons espagnoles et italiennes du *Bourgeois gentilhomme* et des listes de mots rares d'`orthographe1990.txt`. Rien n'est écarté chez Hugo, Stendhal, Gide ou Pierre Loti. Le filtre coûte une seconde lecture du corpus.

### Premier mot

`-p` accepte un mot avec ou sans accents ni majuscules. Si ce mot commence des phrases du corpus, la génération part de là. Sinon, elle part de son contexte le plus fréquent, et le mot reçoit une majuscule.

### Modèle sauvegardé

Le format est un fichier texte, décrit dans `modele.h` et écrit avec le module `sauvegarde`. Au chargement, tout est vérifié : syntaxe, bornes, doublons, cohérence des graphies, des contextes, des états, des transitions des deux chaînes et de la ponctuation. Un fichier modifié est refusé, jamais accepté à moitié.

## Mesures

Sur le corpus `dataset` (5 Mo, 903 194 mots dont 852 515 gardés par le filtre de langue, 36 425 mots différents) :

| Ordre | États | Transitions | Apprentissage + génération | Mémoire | Modèle sauvegardé | Rechargement + génération |
|---|---|---|---|---|---|---|
| 1 | 36 426 | 306 690 | 0,88 s | 42 Mio | 7,7 Mio | 0,16 s |
| 2 | 294 573 | 618 242 | 0,93 s | 61 Mio | 11 Mio | 0,20 s |
| 3 | 589 078 | 751 119 | 1,16 s | 107 Mio | 20 Mio | 0,45 s |
| 8 | 755 531 | 802 291 | 1,14 s | 121 Mio | 36 Mio | 0,61 s |
| 5, repli 3 | 743 532 + 589 078 | 798 000 + 751 119 | 1,57 s | 165 Mio | 42 Mio | 0,85 s |

Les temps sont des temps de calcul (le meilleur de sept essais), la mémoire est la mémoire résidente maximale du programme. À l'ordre 8, chaque état n'a en moyenne que 1,06 successeur : c'est pourquoi le texte y recopie le corpus.

## Limites connues

- **Langues** : le filtre connaît dix-neuf langues, toutes écrites en alphabet latin. Pour un corpus écrit dans une autre, la langue n'est pas reconnue et rien n'est écarté. En finnois, en turc et dans les langues slaves, où les mots-outils se fondent souvent dans les mots (suffixes, déclinaisons), les profils ne couvrent qu'un tiers environ du texte, contre plus de la moitié en français : dans un corpus mêlé, l'une de ces langues peut être jugée minoritaire face à une langue mieux couverte, même avec plus de mots.
- **Langues proches** : à la fin d'un texte danois au milieu de textes norvégiens, un dernier passage trop court pour contenir les mots qui les distinguent peut être gardé.
- **Passages mixtes** : dans les notes de `contes.txt`, quelques passages mêlent citations françaises et explications anglaises, et le français y domine. Ils sont gardés : environ 850 mots, dont quelques bribes d'anglais peuvent reparaître (« Footnote »).
- **Listes de mots rares** : les listes d'`orthographe1990.txt` sont écartées, bien qu'en français. Sans mots-outils ni mots connus, rien ne les distingue d'une langue étrangère.
- **Mots qui ne diffèrent que par les accents** : ils partagent une même clé (« égare » / « égaré », « ou » / « où »). Si leur contexte n'apparaît que dans des fichiers sans accents, c'est l'écriture la plus fréquente du mot qui est utilisée.
- **Mails** : les paramètres MIME découpés ou encodés (RFC 2231) ne sont pas lus ; une pièce jointe insérée dans le texte même (uuencode, d'avant MIME) est lue comme du texte. Dans une boîte mbox, une ligne `From ` non protégée qui suit une ligne vide commence un nouveau message, comme dans tout lecteur mbox, et la boîte est lue entièrement en mémoire. Les adresses et les liens écrits dans un message sont lus comme des mots.
- **Repli** : il gagne en nouveauté ce qu'il perd en cohérence. À l'ordre 5, quatre mots sur cinq sont tirés à l'ordre 3, et le texte ressemble alors davantage à l'ordre 3 qu'à l'ordre 5.
- **Plateforme** : `make test_defaillances` utilise l'option `--wrap` de l'éditeur de liens. Le ld de GNU, utilisé sous Linux par gcc et clang, la connaît ; celui de macOS ne la connaît pas.

## Prochaine version

Le projet est conçu pour être exploré et prolongé. Quelques pistes, non encore réalisées :

- **Un accent qui regarde aussi le mot suivant** : « de la » ou « de là », « la » ou « là » se décident surtout par ce qui suit (une virgule, un nom). Écrire chaque phrase une fois tous ses mots tirés permettrait de faire voter le mot suivant.
- **Un repli plus fin** : se replier d'un ordre à la fois (5, puis 4, puis 3), ou dès que le contexte a moins de deux ou trois successeurs, et mesurer le compromis entre nouveauté et cohérence sur un texte mis de côté.
- **D'autres formats** : lire directement un livre EPUB ou une page HTML, avec le code qui extrait déjà le texte des mails HTML.
- **D'autres alphabets** : grec ou cyrillique, en translittérant les lettres comme le sont déjà celles d'Europe centrale.
- **Des contraintes** : choisir la longueur des phrases, le mot qui les termine, ou faire rimer les vers avec `-l`.

## Pour contribuer :

1. Lancez `make installer_hook`.
2. Écrivez le contrat dans le `.h` avant le code.
3. Ajoutez les tests correspondants.
4. Vérifiez que `make test` reste vert.

Les conventions sont simples : du C99 avec POSIX, sans accents dans le code, et une erreur d'environnement toujours signalée, jamais une violation de contrat.