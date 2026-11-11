/**
Fichier test_repertoire.c

Tests unitaires du module repertoire (cf. src/repertoire.h) : iterateur des
mots d'un fichier (mots, graphies, liaisons, marques de dialogue), liste des
fichiers d'un corpus et construction d'une chaine de Markov (ordre 1 a
REPERTOIRE_ORDRE_MAX, filtre de langue). Les fichiers sont crees dans des
repertoires temporaires.
*/
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include "outils_tests.h"
#include "../src/repertoire.h"
#include "../src/mail.h"

/* ======================================================================== */
/*                                 Utilitaires                              */
/* ======================================================================== */

/**
 * Ecrit taille octets dans un fichier temporaire et verifie que l'iterateur
 * renvoie exactement les mots attendus. attendu contient les mots separes
 * par des espaces ; un '|' colle a la fin d'un mot indique que ce mot doit
 * terminer une phrase. Exemple : "le chat dort| il pleut|".
 */
static void verifier_lecture(const char *contenu, size_t taille, const char *attendu)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "texte.txt");
  iterateur_mots_t *it;
  const char *p = attendu;
  const char *mot;
  char *mot_attendu = malloc(strlen(attendu) + 1u);
  size_t longueur;
  bool fin_attendue;
  bool fin = true;

  assert(mot_attendu != NULL);
  ecrire_octets(chemin, contenu, taille);
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  while (*p != '\0') {
    while (*p == ' ') {
      p++;
    }
    if (*p != '\0') {
      longueur = 0u;
      while (p[longueur] != ' ' && p[longueur] != '\0') {
        longueur++;
      }
      memcpy(mot_attendu, p, longueur);
      mot_attendu[longueur] = '\0';
      p += longueur;
      fin_attendue = (mot_attendu[longueur - 1u] == '|');
      if (fin_attendue) {
        mot_attendu[longueur - 1u] = '\0';
      }
      mot = iterateur_mots_suivant(it, &fin);
      if (mot == NULL || strcmp(mot, mot_attendu) != 0
          || fin != fin_attendue) {
        fprintf(stderr, "\nattendu \"%s\"%s, obtenu \"%s\"%s\n", mot_attendu,
                fin_attendue ? " (fin de phrase)" : "",
                mot == NULL ? "(NULL)" : mot, fin ? " (fin de phrase)" : "");
      }
      assert(mot != NULL);
      assert(strcmp(mot, mot_attendu) == 0);
      assert(fin == fin_attendue);
    }
  }
  /* plus de mot, et la fonction renvoie ensuite toujours NULL */
  assert(iterateur_mots_suivant(it, &fin) == NULL);
  assert(!fin);
  assert(iterateur_mots_suivant(it, NULL) == NULL);
  assert(iterateur_mots_erreur(it) == 0);
  iterateur_mots_detruire(&it);
  assert(it == NULL);
  free(mot_attendu);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

/** Variante de verifier_lecture pour un contenu sans octet nul. */
static void verifier_texte(const char *contenu, const char *attendu)
{
  verifier_lecture(contenu, strlen(contenu), attendu);
}

/** Description complete attendue d'un mot lu par l'iterateur. */
typedef struct {
  const char *mot;
  const char *graphie;
  bool fin;
  liaison_t liaison;
  ponctuation_t ponctuation;
} lecture_t;

/** Verifie mot, graphie, fin de phrase, liaison et ponctuation. */
static void verifier_details(const char *contenu, const lecture_t *attendus,
                             size_t nb, bool accentue)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "texte.txt");
  iterateur_mots_t *it;
  const char *mot;
  bool fin;
  size_t i;

  ecrire_fichier(chemin, contenu);
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  assert(iterateur_mots_accentue(it) == accentue);
  for (i = 0u; i < nb; i++) {
    mot = iterateur_mots_suivant(it, &fin);
    if (mot == NULL || strcmp(mot, attendus[i].mot) != 0
        || strcmp(iterateur_mots_graphie(it), attendus[i].graphie) != 0
        || fin != attendus[i].fin
        || iterateur_mots_liaison(it) != attendus[i].liaison
        || iterateur_mots_ponctuation(it) != attendus[i].ponctuation) {
      fprintf(stderr, "\nmot %u : attendu \"%s\" (%s, fin %d, liaison %d, "
              "ponctuation %d)\n", (unsigned int)i, attendus[i].mot,
              attendus[i].graphie, attendus[i].fin, attendus[i].liaison,
              attendus[i].ponctuation);
      if (mot != NULL) {
        fprintf(stderr, "        obtenu \"%s\" (%s, fin %d, liaison %d, "
                "ponctuation %d)\n", mot, iterateur_mots_graphie(it), fin,
                iterateur_mots_liaison(it), iterateur_mots_ponctuation(it));
      }
    }
    assert(mot != NULL && strcmp(mot, attendus[i].mot) == 0);
    assert(strcmp(iterateur_mots_graphie(it), attendus[i].graphie) == 0);
    assert(fin == attendus[i].fin);
    assert(iterateur_mots_liaison(it) == attendus[i].liaison);
    assert(iterateur_mots_ponctuation(it) == attendus[i].ponctuation);
  }
  assert(iterateur_mots_suivant(it, NULL) == NULL);
  iterateur_mots_detruire(&it);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

/** Mot attendu et marques de dialogue attendues devant lui. */
typedef struct {
  const char *mot;
  unsigned int marques;
} marque_t;

/** Verifie les mots d'un texte et les marques placees devant chacun. */
static void verifier_marques(const char *contenu, const marque_t *attendus, size_t nb)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "texte.txt");
  iterateur_mots_t *it;
  const char *mot;
  size_t i;

  ecrire_fichier(chemin, contenu);
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  for (i = 0u; i < nb; i++) {
    mot = iterateur_mots_suivant(it, NULL);
    if (mot == NULL || strcmp(mot, attendus[i].mot) != 0
        || iterateur_mots_marques(it) != attendus[i].marques) {
      fprintf(stderr, "\n\"%s\", mot %u : attendu \"%s\" (marques %u)",
              contenu, (unsigned int)i, attendus[i].mot, attendus[i].marques);
      if (mot != NULL) {
        fprintf(stderr, ", obtenu \"%s\" (marques %u)", mot, iterateur_mots_marques(it));
      }
      fprintf(stderr, "\n");
    }
    assert(mot != NULL && strcmp(mot, attendus[i].mot) == 0);
    assert(iterateur_mots_marques(it) == attendus[i].marques);
  }
  assert(iterateur_mots_suivant(it, NULL) == NULL);
  iterateur_mots_detruire(&it);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

/** Nombre d'elements d'un tableau. */
#define NB(tableau) (sizeof(tableau) / sizeof((tableau)[0]))

/* Abreviations des marques. */
#define AUCUNE MARQUE_AUCUNE
#define FERME MARQUE_FERME_GUILLEMET
#define TIRET MARQUE_TIRET
#define OUVRE MARQUE_OUVRE_GUILLEMET
#define OUVRE_P MARQUE_OUVRE_PARENTHESE
#define FERME_P MARQUE_FERME_PARENTHESE

/* Guillemets et tirets en UTF-8. */
#define GO "\xC2\xAB"          /* guillemet francais ouvrant */
#define GF "\xC2\xBB"          /* guillemet francais fermant */
#define CADRATIN "\xE2\x80\x94"

/* Textes courants, pour le filtre de langue. */
static const char *const TEXTE_FRANCAIS =
  "Le chat dort sur le toit de la maison. Il regarde les oiseaux qui "
  "passent dans le ciel. La petite fille ouvre la porte et appelle son "
  "chat. Elle ne veut pas qu'il reste dehors pendant la nuit. Le chat "
  "descend du toit et vient pres d'elle. Ils rentrent tous les deux dans "
  "la cuisine. La mere a prepare une soupe pour le repas du soir. Le pere "
  "lit un livre au coin du feu. Tout est calme dans la maison et la nuit "
  "tombe sur le village.\n";

static const char *const TEXTE_ANGLAIS =
  "The old man walked to the river with his dog. He had not seen the water "
  "for many years. It was cold and the wind was strong. She said that they "
  "would come back in the morning. There was a small house on the hill and "
  "it was empty. We have been here for a long time but nobody came.\n";

/** Identifiant d'un mot dont on sait qu'il est dans la table. */
static unsigned int id(const hachage_t *mots, const char *mot)
{
  unsigned int identifiant = hachage_identifiant(mots, mot);

  assert(identifiant != HACHAGE_ABSENT);
  return identifiant;
}


/* ======================================================================== */
/*                          Iterateur : les mots                            */
/* ======================================================================== */

static void test_iterateur_phrases_simples(void)
{
  verifier_texte("Le chat dort. Le chien mange !", "le chat dort| le chien mange|");
  verifier_texte("Quoi ?! Non !!! Si.", "quoi| non| si|");
}

static void test_iterateur_ponctuation_retiree(void)
{
  verifier_texte("Bonjour, monde ; c'est-a-dire (voila) : \"fin\"",
                 "bonjour monde c est a dire voila fin|");
  verifier_texte("_Ne jugez point._ [1] {x} <y> * # @ & %", "ne jugez point| x y|");
  verifier_texte("... debut", "debut|");
}

static void test_iterateur_majuscules(void)
{
  verifier_texte("BONJOUR Monde eT TOI", "bonjour monde et toi|");
}

static void test_iterateur_accents_utf8(void)
{
  /* "Ete a l'ecole, OEuvre CA naif." avec accents, apostrophe typographique
     et ligature, en UTF-8 */
  verifier_texte("\xC3\x89t\xC3\xA9 \xC3\xA0 l\xE2\x80\x99\xC3\xA9" "cole, "
                 "\xC5\x92uvre \xC3\x87" "A na\xC3\xAF" "f.",
                 "ete a l ecole oeuvre ca naif|");
  /* ligature ae, eszett, guillemets francais */
  verifier_texte("c\xC3\xA6sar \xC2\xAB stra\xC3\x9F" "e \xC2\xBB", "caesar strasse|");
}

static void test_iterateur_latin1(void)
{
  /* meme texte encode en Latin-1 (ISO-8859-1) */
  verifier_texte("\xC9t\xE9 \xE0 l'\xE9" "cole, \xC7" "A na\xEF" "f.",
                 "ete a l ecole ca naif|");
  /* octet Latin-1 suivi d'un octet de continuation isole */
  verifier_texte("caf\xE9\x80x", "cafe x|");
}

static void test_iterateur_bom_et_crlf(void)
{
  verifier_texte("\xEF\xBB\xBFTitre\r\n\r\nPremier texte\r\nsuite.",
                 "titre| premier texte suite|");
}

static void test_iterateur_mot_tres_long(void)
{
  const size_t longueur = 100000u;
  char *contenu = malloc(longueur + sizeof(" a"));
  char *attendu = malloc(longueur + sizeof(" a|"));

  assert(contenu != NULL && attendu != NULL);
  memset(contenu, 'Z', longueur);
  strcpy(contenu + longueur, " a");
  memset(attendu, 'z', longueur);
  strcpy(attendu + longueur, " a|");
  verifier_lecture(contenu, strlen(contenu), attendu);
  free(contenu);
  free(attendu);
}


/* ======================================================================== */
/*                       Iterateur : les fins de phrase                     */
/* ======================================================================== */

static void test_iterateur_fin_de_fichier(void)
{
  /* le dernier mot du fichier termine la phrase, meme sans ponctuation */
  verifier_texte("Je compte les jours\nSur mes doigts", "je compte les jours sur mes doigts|");
  verifier_texte("Fin\n\n\n", "fin|");
}

static void test_iterateur_lignes_vides(void)
{
  /* strophes de chanson : seule la ligne vide termine la phrase */
  verifier_texte("I want you\nI want you so bad\n\nShe's so heavy\n",
                 "i want you i want you so bad| she s so heavy|");
  /* ligne ne contenant que des blancs */
  verifier_texte("Titre\n  \t \nTexte", "titre| texte|");
  /* une ligne de tirets n'est pas une ligne vide */
  verifier_texte("Avant\n--\nApres", "avant apres|");
}

static void test_iterateur_abreviations(void)
{
  verifier_texte("M. Dupont parle a J.-J. Rousseau. MM. Durand et "
                 "Martin viennent.",
                 "m dupont parle a j j rousseau| mm durand et martin "
                 "viennent|");
  /* une minuscule isolee suivie d'un point termine bien la phrase */
  verifier_texte("Il a dit a. Puis b", "il a dit a| puis b|");
}

static void test_iterateur_nombres(void)
{
  verifier_texte("Il a 3.5 ans et 2 chats. En 1815. Fin", "il a ans et chats| en| fin|");
}

static void test_iterateur_points_de_suspension(void)
{
  verifier_texte("Attends\xE2\x80\xA6 bon", "attends| bon|");
  verifier_texte("She's so...", "she s so|");
  /* suivis d'une minuscule, ils restent dans la phrase */
  verifier_texte("Attends\xE2\x80\xA6 bon", "attends bon|");
}

static void test_iterateur_ponctuation_interne(void)
{
  const lecture_t attendus[] = {
    { "ah", "Ah", false, LIAISON_EXCLAMATION, PONCTUATION_AUCUNE },
    { "dit", "dit", false, LIAISON_TRAIT_UNION, PONCTUATION_AUCUNE },
    { "il", "il", true, LIAISON_ESPACE, PONCTUATION_POINT },
    { "qui", "Qui", false, LIAISON_INTERROGATION, PONCTUATION_AUCUNE },
    { "demanda", "demanda", false, LIAISON_TRAIT_UNION, PONCTUATION_AUCUNE },
    { "t", "t", false, LIAISON_TRAIT_UNION, PONCTUATION_AUCUNE },
    { "elle", "elle", true, LIAISON_ESPACE, PONCTUATION_POINT },
    { "il", "Il", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "hesita", "h\xC3\xA9sita", false, LIAISON_SUSPENSION, PONCTUATION_AUCUNE },
    { "puis", "puis", false, LIAISON_SUSPENSION, PONCTUATION_AUCUNE },
    { "partit", "partit", true, LIAISON_ESPACE, PONCTUATION_POINT },
    { "etc", "Etc", false, LIAISON_ABREVIATION, PONCTUATION_AUCUNE },
    { "et", "et", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "la", "la", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "suite", "suite", false, LIAISON_EXCLAMATION, PONCTUATION_AUCUNE },
    { "fin", "fin", true, LIAISON_ESPACE, PONCTUATION_EXCLAMATION },
    { "mais", "mais", true, LIAISON_ESPACE, PONCTUATION_AUCUNE }
  };
  const marque_t citation[] = {
    { "viens", OUVRE }, { "dit", FERME }, { "il", AUCUNE }
  };
  const lecture_t citation_details[] = {
    { "viens", "Viens", false, LIAISON_EXCLAMATION, PONCTUATION_AUCUNE },
    { "dit", "dit", false, LIAISON_TRAIT_UNION, PONCTUATION_AUCUNE },
    { "il", "il", true, LIAISON_ESPACE, PONCTUATION_POINT }
  };

  /* ponctuation suivie d'une minuscule (meme a la ligne suivante) : la
     phrase continue ; suivie d'une majuscule ou d'une ligne vide : elle
     finit */
  verifier_details("Ah! dit-il. Qui? demanda-t-elle. Il h\xC3\xA9sita... "
                   "puis\xE2\x80\xA6 partit. Etc. et la suite!\nfin!\n\n"
                   "mais", attendus, NB(attendus), true);
  /* la citation se referme apres le point d'exclamation */
  verifier_marques("\xC2\xABViens!\xC2\xBB dit-il.", citation, NB(citation));
  verifier_details("\xC2\xABViens!\xC2\xBB dit-il.", citation_details, NB(citation_details), true);
  /* "M. Dupont" et "3.5" sont inchanges ; "?!" donne une interrogation */
  verifier_texte("Quoi?! non. M. Dupont a 3.5 ans", "quoi non| m dupont a ans|");
}

static void test_iterateur_parentheses(void)
{
  const marque_t parentheses[] = {
    { "il", AUCUNE }, { "sortit", AUCUNE }, { "sans", OUVRE_P },
    { "bruit", AUCUNE }, { "puis", FERME_P }, { "enfin", OUVRE_P }, { "revint", FERME_P }
  };
  const marque_t phrase_entre_parentheses[] = {
    { "il", OUVRE_P }, { "sortit", AUCUNE }, { "puis", FERME_P }
  };
  const marque_t imbrication[] = {
    { "il", AUCUNE }, { "dit", AUCUNE }, { "oui", OUVRE_P | OUVRE }, { "fin", FERME | FERME_P }
  };
  const lecture_t virgule[] = {
    { "sans", "sans", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "bruit", "bruit", false, LIAISON_VIRGULE, PONCTUATION_AUCUNE },
    { "puis", "puis", true, LIAISON_ESPACE, PONCTUATION_POINT }
  };

  verifier_marques("Il sortit (sans bruit), puis [enfin] revint.", parentheses, NB(parentheses));
  /* apres la ponctuation finale, la parenthese fermante est rangee devant
     le mot suivant (comme un guillemet) */
  verifier_marques("(Il sortit.) Puis", phrase_entre_parentheses, NB(phrase_entre_parentheses));
  verifier_marques("Il dit (\xC2\xABoui\xC2\xBB) fin", imbrication, NB(imbrication));
  verifier_details("(sans bruit), puis.", virgule, NB(virgule), false);
}

static void test_iterateur_lettres_etendues(void)
{
  const lecture_t attendus[] = {
    { "lodz", "\xC5\x81\xC3\xB3" "d\xC5\xBA", false, LIAISON_ESPACE,
      PONCTUATION_AUCUNE },
    { "sie", "si\xC4\x99", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "rec", "\xC5\x98" "E\xC4\x8C", false, LIAISON_ESPACE,
      PONCTUATION_AUCUNE },
    { "ilik", "\xC4\xB1l\xC4\xB1k", false, LIAISON_ESPACE,
      PONCTUATION_AUCUNE },
    { "istanbul", "\xC4\xB0stanbul", false, LIAISON_ESPACE,
      PONCTUATION_AUCUNE },
    { "stiinta", "\xC8\x98tiin\xC8\x9B\xC4\x83", false, LIAISON_ESPACE,
      PONCTUATION_AUCUNE },
    { "osz", "\xC5\x91sz", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "cu", "\xC4\x89u", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "ijs", "\xC4\xB3s", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "oeuvre", "\xC5\x93uvre", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "ls", "l\xC5\xBF", true, LIAISON_ESPACE, PONCTUATION_POINT }
  };

  /* polonais, tcheque, turc, roumain, hongrois, esperanto, neerlandais,
     francais, s long */
  verifier_details("\xC5\x81\xC3\xB3" "d\xC5\xBA si\xC4\x99 \xC5\x98" "E\xC4\x8C "
                   "\xC4\xB1l\xC4\xB1k \xC4\xB0stanbul "
                   "\xC8\x98tiin\xC8\x9B\xC4\x83 \xC5\x91sz \xC4\x89u "
                   "\xC4\xB3s \xC5\x93uvre l\xC5\xBF.", attendus,
                   NB(attendus), true);
  /* un caractere hors de ces plages (U+0180) separe toujours les mots */
  verifier_texte("ab\xC6\x80" "cd", "ab cd|");
}

/** Nombre de messages lus par l'iterateur dans un fichier de contenu
    donne (et graphie du premier mot, si premier != NULL). */
static size_t nb_messages_lus(const char *contenu, const char *premier)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "courrier.eml");
  iterateur_mots_t *it;
  size_t nb;

  ecrire_fichier(chemin, contenu);
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  nb = iterateur_mots_nb_messages(it);
  if (premier != NULL) {
    assert(iterateur_mots_suivant(it, NULL) != NULL);
    assert(strcmp(iterateur_mots_graphie(it), premier) == 0);
  }
  iterateur_mots_detruire(&it);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
  return nb;
}

static void test_iterateur_courrier(void)
{
  /* un message : seul le texte de l'expediteur est lu, decode */
  verifier_texte("From: Jean <jean@example.com>\nTo: paul@example.com\n"
                 "Subject: Rendez-vous\nContent-Type: text/plain; "
                 "charset=iso-8859-1\nContent-Transfer-Encoding: "
                 "quoted-printable\n\nRendez-vous =E0 midi.\n\n"
                 "Le 3 mars, Paul a =E9crit :\n> Quand ?\n-- \nJean\n",
                 "rendez vous a midi|");
  assert(nb_messages_lus("From: a\nTo: b\n\nCaf\xC3\xA9 noir.\n", "Caf\xC3\xA9") == 1u);
  /* boite mbox : la derniere phrase d'un message ne se poursuit pas dans le suivant */
  verifier_texte("From jean@example.com Mon Mar  3 10:00:00 2024\n"
                 "From: Jean\nTo: Paul\n\nBonjour Paul\n\n"
                 "From paul@example.com Mon Mar  3 11:00:00 2024\n"
                 "From: Paul\nTo: Jean\n\nSalut Jean\n",
                 "bonjour paul| salut jean|");
  assert(nb_messages_lus("From a Mon\nFrom: a\nTo: b\n\nUn.\n\n"
                         "From b Tue\nFrom: b\nTo: a\n\nDeux.\n", "Un") == 2u);
  /* courrier sans texte, texte ordinaire */
  verifier_texte("From: a\nTo: b\nContent-Type: image/png\n\nxyz\n", "");
  assert(nb_messages_lus("From: a\nTo: b\nContent-Type: image/png\n\n", NULL) == 1u);
  assert(nb_messages_lus("Le chat dort.\n", "Le") == 0u);
  assert(nb_messages_lus("Titre: Contes\nAuteur: Perrault\n\nIl etait.\n", "Titre") == 0u);
}

static void test_iterateur_grand_courrier(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "boite.mbox");
  char *contenu = malloc(3u * MAIL_TAILLE_DETECTION + 256u);
  iterateur_mots_t *it;
  size_t nb_mots = 0u;
  size_t i;

  /* un courrier plus grand que la fenetre de detection est lu en entier */
  assert(contenu != NULL);
  strcpy(contenu, "From: a\nTo: b\n\n");
  for (i = 0u; strlen(contenu) < 3u * MAIL_TAILLE_DETECTION; i++) {
    strcat(contenu, "mot ");
  }
  strcat(contenu, "fin.\n");
  ecrire_fichier(chemin, contenu);
  it = iterateur_mots_creer(chemin);
  assert(it != NULL && iterateur_mots_nb_messages(it) == 1u);
  while (iterateur_mots_suivant(it, NULL) != NULL) {
    nb_mots++;
  }
  assert(nb_mots == i + 1u && iterateur_mots_erreur(it) == 0);
  iterateur_mots_detruire(&it);
  free(contenu);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

static void test_iterateur_sans_mot(void)
{
  verifier_texte("", "");
  verifier_texte(" ... !!! 1234 \n\n -- ", "");
  verifier_lecture("\0\0\0", 3u, "");
}


/* ======================================================================== */
/*            Iterateur : graphies, liaisons, ponctuation, accents          */
/* ======================================================================== */

static void test_iterateur_graphies(void)
{
  /* "Ete a L'Ecole, OEUVRE ca" avec accents, ligature et majuscules */
  const lecture_t attendus[] = {
    { "ete", "\xC3\x89t\xC3\xA9", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "a", "\xC3\xA0", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "l", "L", false, LIAISON_APOSTROPHE, PONCTUATION_AUCUNE },
    { "ecole", "\xC3\x89" "cole", false, LIAISON_VIRGULE, PONCTUATION_AUCUNE },
    { "oeuvre", "\xC5\x92UVRE", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "ca", "\xC3\xA7" "a", true, LIAISON_ESPACE, PONCTUATION_AUCUNE }
  };

  verifier_details("\xC3\x89t\xC3\xA9 \xC3\xA0 L'\xC3\x89" "cole, "
                   "\xC5\x92UVRE \xC3\xA7" "a", attendus, 6u, true);
}

static void test_iterateur_graphie_latin1(void)
{
  /* un texte Latin-1 donne des graphies en UTF-8 */
  const lecture_t attendus[] = {
    { "ete", "\xC3\x89t\xC3\xA9", true, LIAISON_ESPACE, PONCTUATION_POINT }
  };

  verifier_details("\xC9t\xE9.", attendus, 1u, true);
}

static void test_iterateur_liaisons(void)
{
  /* "C'est peut-etre, dit-il; oui: M. Dupont << bien >> bon ' va-et-vient."
     (apostrophe typographique, trait d'union insecable) */
  const lecture_t attendus[] = {
    { "c", "C", false, LIAISON_APOSTROPHE, PONCTUATION_AUCUNE },
    { "est", "est", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "peut", "peut", false, LIAISON_TRAIT_UNION, PONCTUATION_AUCUNE },
    { "etre", "\xC3\xAAtre", false, LIAISON_VIRGULE, PONCTUATION_AUCUNE },
    { "dit", "dit", false, LIAISON_TRAIT_UNION, PONCTUATION_AUCUNE },
    { "il", "il", false, LIAISON_POINT_VIRGULE, PONCTUATION_AUCUNE },
    { "oui", "oui", false, LIAISON_DEUX_POINTS, PONCTUATION_AUCUNE },
    { "m", "M", false, LIAISON_ABREVIATION, PONCTUATION_AUCUNE },
    { "dupont", "Dupont", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "bien", "bien", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "bon", "bon", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "va", "va", false, LIAISON_TRAIT_UNION, PONCTUATION_AUCUNE },
    { "et", "et", false, LIAISON_TRAIT_UNION, PONCTUATION_AUCUNE },
    { "vient", "vient", true, LIAISON_ESPACE, PONCTUATION_POINT }
  };

  verifier_details("C\xE2\x80\x99" "est peut-\xC3\xAAtre, dit-il; oui: "
                   "M. Dupont \xC2\xAB bien \xC2\xBB bon ' va\xE2\x80\x91"
                   "et-vient.", attendus, 14u, true);
}

static void test_iterateur_ponctuations(void)
{
  const lecture_t attendus[] = {
    { "quoi", "Quoi", true, LIAISON_ESPACE, PONCTUATION_INTERROGATION },
    { "non", "Non", true, LIAISON_ESPACE, PONCTUATION_EXCLAMATION },
    { "bon", "Bon", true, LIAISON_ESPACE, PONCTUATION_SUSPENSION },
    { "attends", "Attends", true, LIAISON_ESPACE, PONCTUATION_SUSPENSION },
    { "fin", "Fin", true, LIAISON_ESPACE, PONCTUATION_POINT },
    { "titre", "Titre", true, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "suite", "Suite", true, LIAISON_ESPACE, PONCTUATION_AUCUNE }
  };

  verifier_details("Quoi ?! Non !!! Bon... Attends\xE2\x80\xA6 Fin. "
                   "Titre\n\nSuite", attendus, 7u, true);
}

static void test_iterateur_accentue(void)
{
  const lecture_t ascii[] = {
    { "etait", "etait", true, LIAISON_ESPACE, PONCTUATION_AUCUNE }
  };

  verifier_details("etait", ascii, 1u, false);
  /* des accents seulement dans la licence ignoree ne comptent pas */
  verifier_details("etait\n*** END OF THE PROJECT GUTENBERG EBOOK X ***\n"
                   "\xC3\xA9t\xC3\xA9\n", ascii, 1u, false);
  /* ni dans l'en-tete ignore */
  verifier_details("\xC3\xA9t\xC3\xA9\n*** START OF THE PROJECT GUTENBERG "
                   "EBOOK X ***\netait", ascii, 1u, false);
}

static void test_iterateur_gutenberg(void)
{
  verifier_texte("Avant.\n*** START OF THE PROJECT GUTENBERG EBOOK X ***\n"
                 "Texte utile.\n*** END OF THE PROJECT GUTENBERG EBOOK X ***\n"
                 "License words.", "texte utile|");
  verifier_texte("Texte.\nEnd of the Project Gutenberg EBook of X\nlicense",
                 "texte|");
  verifier_texte("Texte.\nEnd of Project Gutenberg's X, by Y\n\n*** END OF "
                 "THIS PROJECT GUTENBERG EBOOK X ***\nlicence", "texte|");
  /* casse et espaces en debut de ligne indifferents */
  verifier_texte("Texte.\n   ***end of this project gutenberg ebook\nfin",
                 "texte|");
  /* pas de marque : tout est lu */
  verifier_texte("Texte.\n*** END OF THE WORLD ***\nfin",
                 "texte| end of the world fin|");
  verifier_texte("Le texte *** END OF THE PROJECT GUTENBERG",
                 "le texte end of the project gutenberg|");
}

static void test_graphie_valide(void)
{
  assert(repertoire_graphie_valide("\xC3\x89t\xC3\xA9", "ete"));
  assert(repertoire_graphie_valide("ETE", "ete"));
  assert(repertoire_graphie_valide("\xC5\x93uvre", "oeuvre"));
  assert(repertoire_graphie_valide("stra\xC3\x9F" "e", "strasse"));
  assert(repertoire_graphie_valide("\xC3\x86sop", "aesop"));
  assert(!repertoire_graphie_valide("\xC3\x89t\xC3\xA9", "et"));
  assert(!repertoire_graphie_valide("\xC3\x89t", "ete"));
  assert(!repertoire_graphie_valide("l'eau", "leau"));
  assert(!repertoire_graphie_valide("", ""));
  assert(!repertoire_graphie_valide("\xC3", "a"));
}


/* ======================================================================== */
/*                           Iterateur : les erreurs                        */
/* ======================================================================== */

static void test_iterateur_fichier_inexistant(void)
{
  errno = 0;
  assert(iterateur_mots_creer("/tmp/fichier_qui_n_existe_pas.txt") == NULL);
  assert(errno == ENOENT);
}

static void test_iterateur_repertoire(void)
{
  char *repertoire = creer_repertoire_temporaire();

  errno = 0;
  assert(iterateur_mots_creer(repertoire) == NULL);
  assert(errno == EISDIR);
  detruire_repertoire_temporaire(repertoire);
}

static void test_iterateur_detruire_en_cours(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "texte.txt");
  iterateur_mots_t *it;

  ecrire_fichier(chemin, "un deux trois quatre");
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  assert(strcmp(iterateur_mots_suivant(it, NULL), "un") == 0);
  /* destruction avant la fin du fichier : pas de fuite */
  iterateur_mots_detruire(&it);
  assert(it == NULL);
  iterateur_mots_detruire(&it);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}


/* ======================================================================== */
/*                 Iterateur : guillemets, tirets, lignes                   */
/* ======================================================================== */

static void test_iterateur_guillemets(void)
{
  /* guillemets francais : ouvrant devant "viens", fermant devant "puis" */
  const marque_t francais[] = {
    { "il", AUCUNE }, { "dit", AUCUNE }, { "viens", OUVRE }, { "ici", AUCUNE },
    { "puis", FERME }, { "il", AUCUNE }, { "sortit", AUCUNE }
  };
  const marque_t droits[] = {
    { "bonjour", OUVRE }, { "dit", FERME }, { "elle", AUCUNE },
    { "au", OUVRE }, { "revoir", AUCUNE }
  };
  const marque_t anglais[] = {
    { "hello", OUVRE }, { "he", FERME }, { "said", AUCUNE }
  };
  const marque_t allemands[] = {
    { "hallo", OUVRE }, { "sagte", FERME }, { "er", AUCUNE }
  };
  const marque_t suedois[] = {
    { "hej", OUVRE }, { "sa", FERME }, { "han", AUCUNE }
  };
  const marque_t deux_marques[] = { { "oui", OUVRE }, { "non", FERME | OUVRE } };
  const marque_t mot_cite[] = {
    { "le", AUCUNE }, { "mot", AUCUNE }, { "respectable", OUVRE }, { "car", FERME }
  };

  verifier_marques("Il dit: " GO "Viens ici!" GF " Puis il sortit.", francais, NB(francais));
  /* avec les espaces insecables de la typographie francaise */
  verifier_marques("Il dit : " GO "\xC2\xA0Viens ici !\xC2\xA0" GF
                   " Puis il sortit.", francais, NB(francais));
  /* guillemet droit : ouvrant s'il est colle a ce qui le suit */
  verifier_marques("\"Bonjour\", dit-elle. \"Au revoir.\"", droits, NB(droits));
  verifier_marques("\xE2\x80\x9CHello,\xE2\x80\x9D he said.", anglais, NB(anglais));
  /* en allemand U+201C ferme, en suedois U+201D ouvre : la position decide */
  verifier_marques("\xE2\x80\x9EHallo\xE2\x80\x9C, sagte er.", allemands, NB(allemands));
  verifier_marques("\xE2\x80\x9DHej\xE2\x80\x9D, sa han.", suedois, NB(suedois));
  verifier_marques(GO "Oui" GF " " GO "Non" GF, deux_marques, NB(deux_marques));
  verifier_marques("le mot \"respectable\"; car", mot_cite, NB(mot_cite));
}

static void test_iterateur_tirets(void)
{
  const marque_t replique[] = {
    { "oui", TIRET }, { "dit", AUCUNE }, { "il", AUCUNE }
  };
  const marque_t un_mot[] = { { "oui", TIRET } };
  const marque_t deux_repliques[] = { { "oui", TIRET }, { "non", TIRET } };
  const marque_t incise[] = {
    { "un", AUCUNE }, { "jour", AUCUNE }, { "il", TIRET },
    { "pleuvait", AUCUNE }, { "il", TIRET }, { "sortit", AUCUNE }
  };
  const marque_t incise_virgule[] = {
    { "oui", AUCUNE }, { "dit", TIRET }, { "il", AUCUNE }, { "je", TIRET }, { "viens", AUCUNE }
  };
  const marque_t isole[] = { { "mot", AUCUNE }, { "mot", TIRET } };
  const marque_t colle[] = {
    { "mot", AUCUNE }, { "mot", AUCUNE }, { "peut", AUCUNE }, { "etre", AUCUNE }
  };
  const marque_t traits[] = { { "a", AUCUNE }, { "b", AUCUNE }, { "c", TIRET } };
  const marque_t phrases[] = { { "fin", AUCUNE }, { "non", TIRET } };
  const marque_t ferme_et_tiret[] = {
    { "viens", OUVRE }, { "ici", AUCUNE }, { "non", FERME | TIRET }
  };
  const marque_t elision[] = { { "l", AUCUNE }, { "eau", AUCUNE } };
  /* la liaison ne change pas : virgule puis tiret, trait d'union */
  const lecture_t liaisons[] = {
    { "oui", "Oui", false, LIAISON_VIRGULE, PONCTUATION_AUCUNE },
    { "dit", "dit", false, LIAISON_TRAIT_UNION, PONCTUATION_AUCUNE },
    { "il", "il", true, LIAISON_ESPACE, PONCTUATION_POINT }
  };

  /* tirets de dialogue : "--" des livres du projet Gutenberg, cadratin,
     demi-cadratin, '-' seul en debut de ligne */
  verifier_marques("--Oui, dit-il.", replique, NB(replique));
  verifier_marques(CADRATIN " Oui, dit-il.", replique, NB(replique));
  verifier_marques("\xE2\x80\x93Oui.", un_mot, NB(un_mot));
  verifier_marques("\xE2\x80\x95 Oui.", un_mot, NB(un_mot));
  verifier_marques("- Oui.\n- Non.", deux_repliques, NB(deux_repliques));
  verifier_marques("-Oui.\n  -Non.", deux_repliques, NB(deux_repliques));
  verifier_marques("---Oui.", un_mot, NB(un_mot));
  /* tirets d'incise, au milieu d'une phrase */
  verifier_marques("Un jour--il pleuvait--il sortit.", incise, NB(incise));
  verifier_marques("Oui, --dit-il, --je viens.", incise_virgule, NB(incise_virgule));
  verifier_marques("mot - mot", isole, NB(isole));
  /* un '-' colle au mot qui le precede n'est pas un tiret ; ni le trait
     d'union ; ni un trait decoratif de quatre '-' ou plus */
  verifier_marques("mot- mot, peut-etre", colle, NB(colle));
  verifier_marques("a ---- b --- c", traits, NB(traits));
  verifier_marques("a ----------- b ---c", traits, NB(traits));
  /* apres une fin de phrase, les marques sont celles de la phrase suivante */
  verifier_marques("Fin.\n\n--Non", phrases, NB(phrases));
  verifier_marques(GO "Viens ici!" GF " --Non", ferme_et_tiret, NB(ferme_et_tiret));
  verifier_marques("l'eau", elision, NB(elision));
  verifier_details("Oui, --dit-il.", liaisons, NB(liaisons), false);
}

/** Mot attendu en lecture "une phrase par ligne". */
typedef struct {
  const char *mot;
  bool fin;
  ponctuation_t ponctuation;
} vers_t;

static void test_iterateur_phrase_par_ligne(void)
{
  const char *const contenu = "Sur le pont\r\nd'Avignon,\n\nOn y danse. On y\n"
                              "danse";
  const vers_t par_ligne[] = {
    { "sur", false, PONCTUATION_AUCUNE }, { "le", false, PONCTUATION_AUCUNE },
    { "pont", true, PONCTUATION_AUCUNE }, { "d", false, PONCTUATION_AUCUNE },
    { "avignon", true, PONCTUATION_AUCUNE },
    { "on", false, PONCTUATION_AUCUNE }, { "y", false, PONCTUATION_AUCUNE },
    { "danse", true, PONCTUATION_POINT }, { "on", false, PONCTUATION_AUCUNE },
    { "y", true, PONCTUATION_AUCUNE }, { "danse", true, PONCTUATION_AUCUNE }
  };
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "chanson.txt");
  iterateur_mots_t *it;
  const char *mot;
  bool fin;
  size_t i;

  ecrire_fichier(chemin, contenu);
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  iterateur_mots_phrase_par_ligne(it, true);
  for (i = 0u; i < NB(par_ligne); i++) {
    mot = iterateur_mots_suivant(it, &fin);
    assert(mot != NULL && strcmp(mot, par_ligne[i].mot) == 0);
    assert(fin == par_ligne[i].fin);
    assert(iterateur_mots_ponctuation(it) == par_ligne[i].ponctuation);
  }
  assert(iterateur_mots_suivant(it, NULL) == NULL);
  iterateur_mots_detruire(&it);
  /* on peut revenir a la lecture habituelle avant le premier mot : seules
     la ligne vide, la ponctuation et la fin du fichier finissent une phrase */
  it = iterateur_mots_creer(chemin);
  iterateur_mots_phrase_par_ligne(it, true);
  iterateur_mots_phrase_par_ligne(it, false);
  iterateur_mots_detruire(&it);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
  verifier_texte(contenu, "sur le pont d avignon| on y danse| on y danse|");
}

static void test_cle(void)
{
  char *cle;

  cle = repertoire_cle("\xC3\x89t\xC3\xA9");                 /* Ete accentue */
  assert(cle != NULL && strcmp(cle, "ete") == 0);
  free(cle);
  cle = repertoire_cle("\xC5\x92uvre");                      /* OE lie */
  assert(cle != NULL && strcmp(cle, "oeuvre") == 0);
  free(cle);
  cle = repertoire_cle("stra\xC3\x9F" "e");                  /* eszett */
  assert(cle != NULL && strcmp(cle, "strasse") == 0);
  free(cle);
  cle = repertoire_cle("MARIUS");
  assert(cle != NULL && strcmp(cle, "marius") == 0);
  free(cle);
  cle = repertoire_cle("a");
  assert(cle != NULL && strcmp(cle, "a") == 0);
  free(cle);
  cle = repertoire_cle("\xC5\x81\xC3\xB3" "d\xC5\xBA");      /* polonais */
  assert(cle != NULL && strcmp(cle, "lodz") == 0);
  free(cle);
  cle = repertoire_cle("\xC8\x98tiin\xC8\x9B\xC4\x83");   /* roumain */
  assert(cle != NULL && strcmp(cle, "stiinta") == 0);
  free(cle);
  /* ce qui n'est pas un mot seul : EINVAL */
  errno = 0;
  assert(repertoire_cle("") == NULL && errno == EINVAL);
  errno = 0;
  assert(repertoire_cle("l'eau") == NULL && errno == EINVAL);
  errno = 0;
  assert(repertoire_cle("deux mots") == NULL && errno == EINVAL);
  errno = 0;
  assert(repertoire_cle("abc1") == NULL && errno == EINVAL);
  errno = 0;
  assert(repertoire_cle("\xC3") == NULL && errno == EINVAL);  /* UTF-8 coupe */
}


/* ======================================================================== */
/*                     Chaine de Markov d'un repertoire                     */
/* ======================================================================== */

static void test_markov_poids(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  markov_t *m;
  unsigned int le;
  unsigned int chat;
  unsigned int dort;
  unsigned int chien;
  unsigned int mange;

  creer_fichier_dans(repertoire, "a.txt", "Le chat dort. Le chien dort.");
  creer_fichier_dans(repertoire, "b.txt", "Le chat mange.");
  m = repertoire_creer_markov(repertoire, 1u, mots);
  assert(m != NULL);
  assert(markov_est_finalisee(m));
  assert(markov_nb_etats(m) == 6u);
  assert(hachage_nb_mots(mots) == 6u);
  assert(strcmp(hachage_mot(mots, REPERTOIRE_DEBUT_PHRASE), "") == 0);

  le = id(mots, "le");
  chat = id(mots, "chat");
  dort = id(mots, "dort");
  chien = id(mots, "chien");
  mange = id(mots, "mange");
  /* identifiants dans l'ordre d'apparition */
  assert(le == 1u && chat == 2u && dort == 3u && chien == 4u && mange == 5u);

  assert(markov_poids(m, REPERTOIRE_DEBUT_PHRASE, le) == 3u);
  assert(markov_nb_voisins(m, REPERTOIRE_DEBUT_PHRASE) == 1u);
  assert(markov_poids(m, le, chat) == 2u);
  assert(markov_poids(m, le, chien) == 1u);
  assert(markov_poids(m, chat, dort) == 1u);
  assert(markov_poids(m, chat, mange) == 1u);
  assert(markov_poids(m, chien, dort) == 1u);
  assert(markov_poids(m, dort, REPERTOIRE_DEBUT_PHRASE) == 2u);
  assert(markov_poids(m, mange, REPERTOIRE_DEBUT_PHRASE) == 1u);
  assert(markov_poids(m, dort, le) == 0u);
  assert(markov_poids_total(m, le) == 3u);

  assert(ecart(markov_probabilite(m, le, chat), 2.0 / 3.0) < 1e-12);
  assert(ecart(markov_probabilite(m, le, chien), 1.0 / 3.0) < 1e-12);
  assert(markov_probabilite(m, REPERTOIRE_DEBUT_PHRASE, le) == 1.0);

  markov_detruire(&m);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_markov_fichiers_independants(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  markov_t *m;

  /* le premier fichier se termine sans ponctuation : son dernier mot ne
     doit pas etre relie au premier mot du fichier suivant */
  creer_fichier_dans(repertoire, "1.txt", "Fin du premier");
  creer_fichier_dans(repertoire, "2.txt", "Debut du second.");
  m = repertoire_creer_markov(repertoire, 1u, mots);
  assert(m != NULL);
  assert(markov_poids(m, id(mots, "premier"), id(mots, "debut")) == 0u);
  assert(markov_poids(m, id(mots, "premier"), REPERTOIRE_DEBUT_PHRASE) == 1u);
  assert(markov_poids(m, REPERTOIRE_DEBUT_PHRASE, id(mots, "debut")) == 1u);
  /* "du" est suivi de "premier" puis de "second" */
  assert(markov_nb_voisins(m, id(mots, "du")) == 2u);
  markov_detruire(&m);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_markov_ordre_des_fichiers(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  markov_t *m;

  /* cree dans le desordre : lu dans l'ordre alphabetique des noms */
  creer_fichier_dans(repertoire, "c.txt", "Gamma.");
  creer_fichier_dans(repertoire, "a.txt", "Alpha.");
  creer_fichier_dans(repertoire, "b.txt", "Beta.");
  m = repertoire_creer_markov(repertoire, 1u, mots);
  assert(m != NULL);
  assert(strcmp(hachage_mot(mots, 1u), "alpha") == 0);
  assert(strcmp(hachage_mot(mots, 2u), "beta") == 0);
  assert(strcmp(hachage_mot(mots, 3u), "gamma") == 0);
  markov_detruire(&m);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_markov_fichiers_ignores(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *sous_repertoire = chemin_dans(repertoire, "sous");
  char *lien = chemin_dans(repertoire, "lien_casse.txt");
  hachage_t *mots = hachage_creer(0u);
  markov_t *m;

  creer_fichier_dans(repertoire, "texte.txt", "Visible.");
  creer_fichier_dans(repertoire, ".cache", "Cache.");
  assert(mkdir(sous_repertoire, 0700) == 0);
  creer_fichier_dans(sous_repertoire, "texte.txt", "Souterrain.");
  assert(symlink("/tmp/cible_inexistante_markov", lien) == 0);
  m = repertoire_creer_markov(repertoire, 1u, mots);
  assert(m != NULL);
  assert(hachage_nb_mots(mots) == 2u);
  assert(hachage_contient(mots, "visible"));
  assert(!hachage_contient(mots, "cache"));
  assert(!hachage_contient(mots, "souterrain"));
  markov_detruire(&m);
  hachage_detruire(&mots);
  free(sous_repertoire);
  free(lien);
  detruire_repertoire_temporaire(repertoire);
}

static void test_markov_repertoire_vide(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  markov_t *m;

  m = repertoire_creer_markov(repertoire, 1u, mots);
  assert(m != NULL);
  assert(markov_nb_etats(m) == 1u);
  assert(hachage_nb_mots(mots) == 1u);
  assert(markov_nb_voisins(m, REPERTOIRE_DEBUT_PHRASE) == 0u);
  markov_detruire(&m);
  hachage_detruire(&mots);

  /* fichiers sans aucun mot */
  mots = hachage_creer(0u);
  creer_fichier_dans(repertoire, "vide.txt", "");
  creer_fichier_dans(repertoire, "ponctuation.txt", "... !!! 42");
  m = repertoire_creer_markov(repertoire, 1u, mots);
  assert(m != NULL);
  assert(markov_nb_etats(m) == 1u);
  assert(markov_nb_voisins(m, REPERTOIRE_DEBUT_PHRASE) == 0u);
  markov_detruire(&m);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_markov_erreurs(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *fichier = chemin_dans(repertoire, "texte.txt");
  char *sous_fichier = chemin_dans(repertoire, "texte.txt/sous");
  char *inexistant = chemin_dans(repertoire, "inexistant");
  hachage_t *mots = hachage_creer(0u);

  creer_fichier_dans(repertoire, "texte.txt", "Un texte.");
  /* repertoire inexistant : NULL, ENOENT et table inchangee */
  errno = 0;
  assert(repertoire_creer_markov(inexistant, 1u, mots) == NULL);
  assert(errno == ENOENT);
  assert(hachage_nb_mots(mots) == 0u);
  /* un composant du chemin est un fichier */
  errno = 0;
  assert(repertoire_creer_markov(sous_fichier, 1u, mots) == NULL);
  assert(errno == ENOTDIR);
  assert(hachage_nb_mots(mots) == 0u);
  /* fichier illisible (sans objet pour root, qui peut tout lire) */
  if (geteuid() != 0) {
    assert(chmod(fichier, 0000) == 0);
    errno = 0;
    assert(repertoire_creer_markov(repertoire, 1u, mots) == NULL);
    assert(errno == EACCES);
    assert(chmod(fichier, 0600) == 0);
  }
  hachage_detruire(&mots);
  free(fichier);
  free(sous_fichier);
  free(inexistant);
  detruire_repertoire_temporaire(repertoire);
}

static void test_markov_corpus_reel(void)
{
  /* corpus du projet (si les tests sont lances depuis la racine) */
  const char *corpus = "dataset/en";
  struct stat informations;
  hachage_t *mots;
  markov_t *m;
  unsigned int e;
  unsigned int etat = REPERTOIRE_DEBUT_PHRASE;
  unsigned int nb_phrases = 0u;
  unsigned int i;

  if (stat(corpus, &informations) == 0 && S_ISDIR(informations.st_mode)) {
    mots = hachage_creer(0u);
    m = repertoire_creer_markov(corpus, 2024u, mots);
    assert(m != NULL);
    assert(markov_nb_etats(m) == hachage_nb_mots(mots));
    assert(markov_nb_voisins(m, REPERTOIRE_DEBUT_PHRASE) > 0u);
    assert(hachage_contient(mots, "want") && hachage_contient(mots, "you"));
    /* tout mot a au moins un voisin : on peut toujours se deplacer */
    for (e = 1u; e < markov_nb_etats(m); e++) {
      assert(markov_nb_voisins(m, e) > 0u);
      assert(hachage_mot_valide(hachage_mot(mots, e)));
      assert(hachage_mot(mots, e)[0] != '\0');
    }
    for (i = 0u; i < 20000u; i++) {
      etat = markov_deplacer(m, etat);
      nb_phrases += (etat == REPERTOIRE_DEBUT_PHRASE) ? 1u : 0u;
    }
    assert(nb_phrases > 0u);
    markov_detruire(&m);
    hachage_detruire(&mots);
  }
}

static void test_markov_meme_graine_meme_texte(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots_a = hachage_creer(0u);
  hachage_t *mots_b = hachage_creer(0u);
  markov_t *a;
  markov_t *b;
  unsigned int ea = REPERTOIRE_DEBUT_PHRASE;
  unsigned int eb = REPERTOIRE_DEBUT_PHRASE;
  unsigned int i;

  creer_fichier_dans(repertoire, "a.txt",
                     "Le chat dort. Le chien dort. Le chat mange. Un chat "
                     "mange le chien. Le chien mange un chat.");
  a = repertoire_creer_markov(repertoire, 99u, mots_a);
  b = repertoire_creer_markov(repertoire, 99u, mots_b);
  assert(a != NULL && b != NULL);
  for (i = 0u; i < 1000u; i++) {
    ea = markov_deplacer(a, ea);
    eb = markov_deplacer(b, eb);
    assert(strcmp(hachage_mot(mots_a, ea), hachage_mot(mots_b, eb)) == 0);
  }
  markov_detruire(&a);
  markov_detruire(&b);
  hachage_detruire(&mots_a);
  hachage_detruire(&mots_b);
  detruire_repertoire_temporaire(repertoire);
}


/* ======================================================================== */
/*                        Liste des fichiers                                */
/* ======================================================================== */

/** Verifie la liste des fichiers (chemins relatifs au repertoire). */
static void verifier_liste(const char *repertoire, bool recursif,
                           const char *const *attendus, size_t nb)
{
  size_t nb_fichiers = 99u;
  char **fichiers = repertoire_lister_fichiers(repertoire, recursif, &nb_fichiers);
  char *chemin;
  size_t i;

  assert(fichiers != NULL);
  assert(nb_fichiers == nb);
  for (i = 0u; i < nb; i++) {
    chemin = chemin_dans(repertoire, attendus[i]);
    assert(strcmp(fichiers[i], chemin) == 0);
    free(chemin);
  }
  repertoire_liberer_fichiers(&fichiers, nb_fichiers);
  assert(fichiers == NULL);
}

static void test_lister_fichiers(void)
{
  char *racine = creer_repertoire_temporaire();
  char *sous = chemin_dans(racine, "sous");
  char *profond = chemin_dans(sous, "profond");
  char *cache = chemin_dans(sous, ".cache");
  char *cache_racine = chemin_dans(racine, ".rep_cache");
  char *lien_rep = chemin_dans(racine, "lien_rep");
  char *lien_fichier = chemin_dans(racine, "lien_fichier");
  char *cible = chemin_dans(racine, "a.txt");
  char *vide = chemin_dans(cache_racine, "vide");
  const char *const directs[] = { "a.txt", "b.txt", "lien_fichier" };
  const char *const tous[] = { "a.txt", "b.txt", "lien_fichier", "sous/c.txt", "sous/profond/e.txt" };
  size_t nb = 99u;
  char **fichiers;

  assert(mkdir(sous, 0700) == 0 && mkdir(profond, 0700) == 0);
  assert(mkdir(cache, 0700) == 0 && mkdir(cache_racine, 0700) == 0);
  assert(mkdir(vide, 0700) == 0);
  creer_fichier_dans(racine, "b.txt", "b");
  creer_fichier_dans(racine, "a.txt", "a");
  creer_fichier_dans(racine, ".cache.txt", "x");
  creer_fichier_dans(sous, "c.txt", "c");
  creer_fichier_dans(profond, "e.txt", "e");
  creer_fichier_dans(cache, "d.txt", "d");
  creer_fichier_dans(cache_racine, "f.txt", "f");
  assert(symlink(sous, lien_rep) == 0);       /* lien vers un repertoire */
  assert(symlink(cible, lien_fichier) == 0);  /* lien vers un fichier */

  verifier_liste(racine, false, directs, 3u);
  verifier_liste(racine, true, tous, 5u);
  /* repertoire vide : tableau vide mais non NULL */
  verifier_liste(vide, true, tous, 0u);
  errno = 0;
  assert(repertoire_lister_fichiers(cible, false, &nb) == NULL);
  assert(errno == ENOTDIR);
  errno = 0;
  assert(repertoire_lister_fichiers("/tmp/repertoire_absent_markov", true, &nb) == NULL);
  assert(errno == ENOENT);
  /* sous-repertoire illisible : genant seulement en mode recursif */
  if (geteuid() != 0) {
    assert(chmod(sous, 0000) == 0);
    verifier_liste(racine, false, directs, 3u);
    errno = 0;
    fichiers = repertoire_lister_fichiers(racine, true, &nb);
    assert(fichiers == NULL && errno == EACCES);
    assert(chmod(sous, 0700) == 0);
  }
  free(sous);
  free(profond);
  free(cache);
  free(cache_racine);
  free(lien_rep);
  free(lien_fichier);
  free(cible);
  free(vide);
  detruire_repertoire_temporaire(racine);
}


static void test_lister_profondeur_maximale(void)
{
  char *racine = creer_repertoire_temporaire();
  char *chemin = malloc(strlen(racine) + 2u * 80u + 16u);
  size_t nb = 99u;
  char **fichiers;
  unsigned int niveau;

  /* 70 niveaux de sous-repertoires, un fichier tout au fond */
  assert(chemin != NULL);
  strcpy(chemin, racine);
  for (niveau = 0u; niveau < 70u; niveau++) {
    strcat(chemin, "/d");
    assert(mkdir(chemin, 0700) == 0);
  }
  creer_fichier_dans(chemin, "fond.txt", "Profond.");
  errno = 0;
  fichiers = repertoire_lister_fichiers(racine, true, &nb);
  assert(fichiers == NULL && errno == ELOOP);
  /* sans recursivite, la profondeur n'importe pas */
  fichiers = repertoire_lister_fichiers(racine, false, &nb);
  assert(fichiers != NULL && nb == 0u);
  repertoire_liberer_fichiers(&fichiers, nb);
  free(chemin);
  detruire_repertoire_temporaire(racine);
}


/* ======================================================================== */
/*              Apprentissage : ordre 2, lexique, ponctuation               */
/* ======================================================================== */

static void test_apprendre_ordre_2(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  hachage_paires_t *bigrammes = hachage_paires_creer(0u);
  repertoire_options_t options = { 2u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };
  markov_t *m;
  unsigned int le;
  unsigned int chat;
  unsigned int chien;
  unsigned int dort;

  options.bigrammes = bigrammes;
  creer_fichier_dans(repertoire, "a.txt", "Le chat dort. Le chien dort.");
  m = repertoire_apprendre(repertoire, 3u, mots, &options);
  assert(m != NULL && markov_est_finalisee(m));
  le = id(mots, "le");
  chat = id(mots, "chat");
  chien = id(mots, "chien");
  dort = id(mots, "dort");
  /* etats = paires : (0,0) (0,le) (le,chat) (chat,dort) (le,chien) (chien,dort) */
  assert(hachage_paires_nb(bigrammes) == 6u);
  assert(markov_nb_etats(m) == 6u);
  assert(hachage_paires_identifiant(bigrammes, 0u, 0u) == 0u);
  assert(markov_poids(m, 0u, hachage_paires_identifiant(bigrammes, 0u, le)) == 2u);
  assert(markov_poids(m, hachage_paires_identifiant(bigrammes, 0u, le),
                      hachage_paires_identifiant(bigrammes, le, chat)) == 1u);
  assert(markov_poids(m, hachage_paires_identifiant(bigrammes, le, chien),
                      hachage_paires_identifiant(bigrammes, chien, dort)) == 1u);
  assert(markov_poids(m, hachage_paires_identifiant(bigrammes, chat, dort), 0u) == 1u);
  assert(markov_nb_voisins(m, hachage_paires_identifiant(bigrammes, 0u, le)) == 2u);
  markov_detruire(&m);
  hachage_paires_detruire(&bigrammes);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_apprendre_lexique_ponctuation(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  hachage_paires_t *bigrammes = hachage_paires_creer(0u);
  lexique_t *lexique = lexique_creer();
  markov_t *ponctuation = markov_creer(5u);
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };
  markov_t *m;
  unsigned int oui;
  unsigned int non;
  unsigned int titre;

  options.bigrammes = bigrammes;
  options.lexique = lexique;
  options.ponctuation = ponctuation;
  creer_fichier_dans(repertoire, "a.txt", "Oui ? Oui ! Non. Titre\n\nC'\xC3\xA9tait oui.");
  m = repertoire_apprendre(repertoire, 3u, mots, &options);
  assert(m != NULL);
  oui = id(mots, "oui");
  non = id(mots, "non");
  titre = id(mots, "titre");
  /* ponctuation finale selon le dernier mot (fin sans ponctuation = point) */
  assert(markov_est_finalisee(ponctuation));
  assert(markov_nb_etats(ponctuation)
         == REPERTOIRE_NB_FINS + hachage_nb_mots(mots));
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + oui, PONCTUATION_INTERROGATION) == 1u);
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + oui, PONCTUATION_EXCLAMATION) == 1u);
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + oui, PONCTUATION_POINT) == 1u);
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + non, PONCTUATION_POINT) == 1u);
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + titre, PONCTUATION_POINT) == 1u);
  /* lexique synchronise avec les mots et les paires */
  assert(lexique_est_finalise(lexique));
  assert(lexique_nb_mots(lexique) == hachage_nb_mots(mots));
  assert(lexique_nb_bigrammes(lexique) == hachage_paires_nb(bigrammes));
  assert(strcmp(lexique_variante(lexique, id(mots, "etait"),
                                 lexique_variante_mot(lexique, id(mots, "etait"))),
                "\xC3\xA9tait") == 0);
  assert(lexique_liaison(lexique, hachage_paires_identifiant(bigrammes, id(mots, "c"), id(mots, "etait")))
         == LIAISON_APOSTROPHE);
  markov_detruire(&m);
  markov_detruire(&ponctuation);
  lexique_detruire(&lexique);
  hachage_paires_detruire(&bigrammes);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_apprendre_recursif(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *sous = chemin_dans(repertoire, "sous");
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 1u, true, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };
  markov_t *m;

  assert(mkdir(sous, 0700) == 0);
  creer_fichier_dans(repertoire, "a.txt", "Surface.");
  creer_fichier_dans(sous, "b.txt", "Profondeur.");
  m = repertoire_apprendre(repertoire, 3u, mots, &options);
  assert(m != NULL);
  assert(hachage_contient(mots, "surface"));
  assert(hachage_contient(mots, "profondeur"));
  markov_detruire(&m);
  hachage_detruire(&mots);
  free(sous);
  detruire_repertoire_temporaire(repertoire);
}


/* ======================================================================== */
/*           Corpus, ordre quelconque, marques, lignes, langues             */
/* ======================================================================== */

static void test_lister_corpus(void)
{
  char *racine = creer_repertoire_temporaire();
  char *sous = chemin_dans(racine, "sous");
  char *a = chemin_dans(racine, "a.txt");
  char *b = chemin_dans(racine, "b.txt");
  char *c = chemin_dans(sous, "c.txt");
  char *tube = chemin_dans(racine, "tube");
  char *absent = chemin_dans(racine, "absent.txt");
  char *sous_fichier = chemin_dans(racine, "a.txt/x");
  const char *chemins[3];
  char **fichiers;
  size_t nb = 99u;

  assert(mkdir(sous, 0700) == 0);
  ecrire_fichier(a, "a");
  ecrire_fichier(b, "b");
  ecrire_fichier(c, "c");
  /* un fichier, un repertoire, puis le meme fichier : l'ordre donne est
     garde, les fichiers du repertoire sont tries, rien n'est dedouble */
  chemins[0] = b;
  chemins[1] = racine;
  chemins[2] = b;
  fichiers = repertoire_lister_corpus(chemins, 3u, false, &nb);
  assert(fichiers != NULL && nb == 4u);
  assert(strcmp(fichiers[0], b) == 0);
  assert(strcmp(fichiers[1], a) == 0);
  assert(strcmp(fichiers[2], b) == 0);
  assert(strcmp(fichiers[3], b) == 0);
  repertoire_liberer_fichiers(&fichiers, nb);
  assert(fichiers == NULL);
  /* recursif : les sous-repertoires du repertoire sont lus aussi */
  fichiers = repertoire_lister_corpus(chemins, 2u, true, &nb);
  assert(fichiers != NULL && nb == 4u);
  assert(strcmp(fichiers[0], b) == 0);
  assert(strcmp(fichiers[1], a) == 0);
  assert(strcmp(fichiers[2], b) == 0);
  assert(strcmp(fichiers[3], c) == 0);
  repertoire_liberer_fichiers(&fichiers, nb);
  /* un seul fichier ; un repertoire sans fichier (tableau vide non NULL) */
  chemins[0] = c;
  fichiers = repertoire_lister_corpus(chemins, 1u, false, &nb);
  assert(fichiers != NULL && nb == 1u && strcmp(fichiers[0], c) == 0);
  repertoire_liberer_fichiers(&fichiers, nb);
  assert(unlink(c) == 0);
  chemins[0] = sous;
  fichiers = repertoire_lister_corpus(chemins, 1u, true, &nb);
  assert(fichiers != NULL && nb == 0u);
  repertoire_liberer_fichiers(&fichiers, nb);
  /* erreurs : chemin absent, composant qui n'est pas un repertoire, tube */
  chemins[0] = a;
  chemins[1] = absent;
  errno = 0;
  assert(repertoire_lister_corpus(chemins, 2u, false, &nb) == NULL);
  assert(errno == ENOENT);
  chemins[1] = sous_fichier;
  errno = 0;
  assert(repertoire_lister_corpus(chemins, 2u, false, &nb) == NULL);
  assert(errno == ENOTDIR);
  assert(mkfifo(tube, 0600) == 0);
  chemins[1] = tube;
  errno = 0;
  assert(repertoire_lister_corpus(chemins, 2u, false, &nb) == NULL);
  assert(errno == EINVAL);
  free(sous);
  free(a);
  free(b);
  free(c);
  free(tube);
  free(absent);
  free(sous_fichier);
  detruire_repertoire_temporaire(racine);
}

static void test_apprendre_corpus(void)
{
  /* corpus = un fichier seul, puis un fichier et un repertoire */
  char *repertoire = creer_repertoire_temporaire();
  char *ailleurs = creer_repertoire_temporaire();
  char *fichier = chemin_dans(ailleurs, "lettre.txt");
  const char *chemins[2];
  hachage_t *mots = hachage_creer(0u);
  repertoire_bilan_t bilan = { 9u, 9u, 9u, 9u, 9u, 9u, LANGUE_FRANCAIS };
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL, false,
                                   false, NULL, 0u, NULL, NULL };
  markov_t *m;

  options.bilan = &bilan;
  ecrire_fichier(fichier, "Cher ami. Viens vite!");
  creer_fichier_dans(repertoire, "a.txt", "Le chat dort.");
  creer_fichier_dans(repertoire, "b.txt", "Le chien dort. Il ronfle");
  /* un fichier donne a la place d'un repertoire */
  m = repertoire_apprendre(fichier, 1u, mots, &options);
  assert(m != NULL);
  assert(hachage_nb_mots(mots) == 5u);       /* "" cher ami viens vite */
  assert(markov_poids(m, id(mots, "cher"), id(mots, "ami")) == 1u);
  assert(bilan.nb_fichiers == 1u && bilan.nb_phrases == 2u);
  assert(bilan.nb_mots == 4u && bilan.nb_messages == 0u);
  assert(bilan.nb_phrases_ecartees == 0u && bilan.nb_mots_ecartes == 0u);
  assert(bilan.langue == LANGUE_INCONNUE);   /* pas de filtre */
  markov_detruire(&m);
  hachage_detruire(&mots);
  /* le fichier puis le repertoire */
  mots = hachage_creer(0u);
  chemins[0] = fichier;
  chemins[1] = repertoire;
  m = repertoire_apprendre_corpus(chemins, 2u, 1u, mots, &options);
  assert(m != NULL);
  assert(id(mots, "cher") == 1u);            /* le fichier est lu d'abord */
  assert(hachage_contient(mots, "chat") && hachage_contient(mots, "ronfle"));
  assert(markov_poids(m, id(mots, "le"), id(mots, "chien")) == 1u);
  assert(markov_poids(m, id(mots, "dort"), REPERTOIRE_DEBUT_PHRASE) == 2u);
  assert(bilan.nb_fichiers == 3u && bilan.nb_phrases == 5u);
  assert(bilan.nb_mots == 12u);
  markov_detruire(&m);
  hachage_detruire(&mots);
  /* un echec ne modifie pas le bilan */
  mots = hachage_creer(0u);
  chemins[1] = "/tmp/repertoire_absent_markov";
  bilan.nb_fichiers = 77u;
  errno = 0;
  assert(repertoire_apprendre_corpus(chemins, 2u, 1u, mots, &options) == NULL);
  assert(errno == ENOENT);
  assert(bilan.nb_fichiers == 77u);
  assert(hachage_nb_mots(mots) == 0u);
  hachage_detruire(&mots);
  free(fichier);
  detruire_repertoire_temporaire(ailleurs);
  detruire_repertoire_temporaire(repertoire);
}

/** Identifiant d'un triplet dont on sait qu'il est dans la table. */
static unsigned int triplet(const hachage_nuplets_t *t, unsigned int a,
                            unsigned int b, unsigned int c)
{
  unsigned int elements[3];
  unsigned int identifiant;

  elements[0] = a;
  elements[1] = b;
  elements[2] = c;
  identifiant = hachage_nuplets_identifiant(t, elements);
  assert(identifiant != HACHAGE_ABSENT);
  return identifiant;
}

static void test_apprendre_ordre_3(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  hachage_nuplets_t *nuplets = hachage_nuplets_creer(3u, 0u);
  repertoire_options_t options = { 3u, false, NULL, NULL, NULL, NULL, false,
                                   false, NULL, 0u, NULL, NULL };
  markov_t *m;
  unsigned int le;
  unsigned int chat;
  unsigned int chien;
  unsigned int dort;

  options.nuplets = nuplets;
  creer_fichier_dans(repertoire, "a.txt", "Le chat dort. Le chien dort. Dort.");
  m = repertoire_apprendre(repertoire, 3u, mots, &options);
  assert(m != NULL && markov_est_finalisee(m));
  le = id(mots, "le");
  chat = id(mots, "chat");
  chien = id(mots, "chien");
  dort = id(mots, "dort");
  /* etats = triplets : (0,0,0) (0,0,le) (0,le,chat) (le,chat,dort)
     (0,le,chien) (le,chien,dort) (0,0,dort) */
  assert(hachage_nuplets_nb(nuplets) == 7u);
  assert(markov_nb_etats(m) == 7u);
  assert(triplet(nuplets, 0u, 0u, 0u) == 0u);
  assert(markov_poids(m, 0u, triplet(nuplets, 0u, 0u, le)) == 2u);
  assert(markov_poids(m, 0u, triplet(nuplets, 0u, 0u, dort)) == 1u);
  assert(markov_poids(m, triplet(nuplets, 0u, 0u, le),
                      triplet(nuplets, 0u, le, chat)) == 1u);
  assert(markov_poids(m, triplet(nuplets, 0u, le, chien),
                      triplet(nuplets, le, chien, dort)) == 1u);
  assert(markov_poids(m, triplet(nuplets, le, chat, dort), 0u) == 1u);
  assert(markov_poids(m, triplet(nuplets, 0u, 0u, dort), 0u) == 1u);
  assert(markov_nb_voisins(m, triplet(nuplets, 0u, 0u, le)) == 2u);
  /* la fenetre repart de zero a chaque phrase : pas de (chat,dort,le) */
  assert(markov_nb_voisins(m, triplet(nuplets, le, chien, dort)) == 1u);
  markov_detruire(&m);
  hachage_nuplets_detruire(&nuplets);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_apprendre_tous_les_ordres(void)
{
  /* une seule phrase de 10 mots differents : a tout ordre, la chaine est un
     chemin unique 0 -> e1 -> ... -> e10 -> 0 qui redonne la phrase */
  const char *const phrase = "un deux trois quatre cinq six sept huit neuf "
                             "dix.";
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots;
  hachage_paires_t *bigrammes;
  hachage_nuplets_t *nuplets;
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL, false,
                                   false, NULL, 0u, NULL, NULL };
  markov_t *m;
  unsigned int ordre;
  unsigned int etat;
  unsigned int mot;
  unsigned int rang;

  creer_fichier_dans(repertoire, "a.txt", phrase);
  for (ordre = 1u; ordre <= REPERTOIRE_ORDRE_MAX; ordre++) {
    mots = hachage_creer(0u);
    bigrammes = hachage_paires_creer(0u);
    nuplets = (ordre >= 3u) ? hachage_nuplets_creer(ordre, 0u) : NULL;
    options.ordre = ordre;
    options.bigrammes = bigrammes;
    options.nuplets = nuplets;
    m = repertoire_apprendre(repertoire, 3u, mots, &options);
    assert(m != NULL);
    assert(markov_nb_etats(m) == 11u);
    assert(hachage_paires_nb(bigrammes) == 11u);
    etat = REPERTOIRE_DEBUT_PHRASE;
    for (rang = 1u; rang <= 10u; rang++) {
      assert(markov_nb_voisins(m, etat) == 1u);
      etat = markov_deplacer(m, etat);
      /* dernier mot de l'etat : le mot de rang rang (identifiant rang) */
      if (ordre == 1u) {
        mot = etat;
      } else if (ordre == 2u) {
        mot = hachage_paires_second(bigrammes, etat);
      } else {
        mot = hachage_nuplets_element(nuplets, etat, ordre - 1u);
        /* l'avant-dernier element est le mot precedent (0 au debut) */
        assert(hachage_nuplets_element(nuplets, etat, ordre - 2u) == rang - 1u);
      }
      assert(mot == rang);
    }
    assert(markov_deplacer(m, etat) == REPERTOIRE_DEBUT_PHRASE);
    markov_detruire(&m);
    hachage_nuplets_detruire(&nuplets);
    hachage_paires_detruire(&bigrammes);
    hachage_detruire(&mots);
  }
  detruire_repertoire_temporaire(repertoire);
}

/** Les deux chaines (finalisees) ont exactement les memes transitions. */
static void verifier_chaines_egales(const markov_t *a, const markov_t *b)
{
  unsigned int e;
  unsigned int r;
  unsigned int poids_a;
  unsigned int poids_b;

  assert(markov_nb_etats(a) == markov_nb_etats(b));
  for (e = 0u; e < markov_nb_etats(a); e++) {
    assert(markov_nb_voisins(a, e) == markov_nb_voisins(b, e));
    for (r = 0u; r < markov_nb_voisins(a, e); r++) {
      assert(markov_voisin(a, e, r, &poids_a) == markov_voisin(b, e, r, &poids_b));
      assert(poids_a == poids_b);
    }
  }
}

/**
 * Apprend un corpus a l'ordre donne, avec un repli eventuel ; renvoie la
 * chaine principale et, si ordre_repli >= 1, la chaine de repli dans
 * *repli. Les tables sont detruites.
 */
static markov_t *apprendre_niveaux(const char *repertoire, unsigned int ordre,
                                   unsigned int ordre_repli, markov_t **repli)
{
  hachage_t *mots = hachage_creer(0u);
  hachage_paires_t *bigrammes = hachage_paires_creer(0u);
  hachage_nuplets_t *nuplets = NULL;
  hachage_nuplets_t *nuplets_repli = NULL;
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL, false,
                                   false, NULL, 0u, NULL, NULL };
  markov_t *m;

  if (ordre >= 3u) {
    nuplets = hachage_nuplets_creer(ordre, 0u);
  }
  if (ordre_repli >= 3u) {
    nuplets_repli = hachage_nuplets_creer(ordre_repli, 0u);
  }
  if (ordre_repli >= 1u) {
    *repli = markov_creer(8u);
  }
  options.ordre = ordre;
  options.bigrammes = bigrammes;
  options.nuplets = nuplets;
  options.ordre_repli = ordre_repli;
  options.repli = (ordre_repli >= 1u) ? *repli : NULL;
  options.nuplets_repli = nuplets_repli;
  m = repertoire_apprendre(repertoire, 7u, mots, &options);
  assert(m != NULL);
  assert(ordre_repli == 0u || (markov_est_finalisee(*repli) && markov_nb_etats(*repli) > 0u));
  hachage_nuplets_detruire(&nuplets_repli);
  hachage_nuplets_detruire(&nuplets);
  hachage_paires_detruire(&bigrammes);
  hachage_detruire(&mots);
  return m;
}

static void test_apprendre_repli(void)
{
  char *repertoire = creer_repertoire_temporaire();
  markov_t *m;
  markov_t *repli = NULL;
  markov_t *reference;
  markov_t *inutile = NULL;
  unsigned int ordre;
  unsigned int ordre_repli;

  creer_fichier_dans(repertoire, "a.txt",
                     "Le chat dort. Le chien dort. Dort. Le chat de la "
                     "voisine dort, le chien de la maison mange.");
  creer_fichier_dans(repertoire, "b.txt", "Un chat mange vite.");
  /* la chaine de repli est apprise sur les memes phrases que la chaine
     principale : c'est la chaine d'un modele de son ordre ; la chaine
     principale est inchangee */
  for (ordre = 2u; ordre <= REPERTOIRE_ORDRE_MAX; ordre++) {
    for (ordre_repli = 1u; ordre_repli < ordre; ordre_repli++) {
      m = apprendre_niveaux(repertoire, ordre, ordre_repli, &repli);
      reference = apprendre_niveaux(repertoire, ordre_repli, 0u, &inutile);
      verifier_chaines_egales(repli, reference);
      markov_detruire(&reference);
      reference = apprendre_niveaux(repertoire, ordre, 0u, &inutile);
      verifier_chaines_egales(m, reference);
      markov_detruire(&reference);
      markov_detruire(&repli);
      markov_detruire(&m);
    }
  }
  detruire_repertoire_temporaire(repertoire);
}

static void test_apprendre_contextes(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *sans_accent = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  hachage_paires_t *bigrammes = hachage_paires_creer(0u);
  lexique_t *lexique = lexique_creer();
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL, false,
                                   false, NULL, 0u, NULL, NULL };
  markov_t *m;
  unsigned int est_la;
  unsigned int rang;
  unsigned int mot_avant;
  unsigned int bigramme;
  unsigned int nb_exceptions = 0u;

  options.bigrammes = bigrammes;
  options.lexique = lexique;
  creer_fichier_dans(repertoire, "a.txt",
                     "Qui est l\xC3\xA0 ? Elle est la reine. Il est l\xC3\xA0.");
  m = repertoire_apprendre(repertoire, 1u, mots, &options);
  assert(m != NULL);
  /* contextes (t, (u, v)) de "qui est la", "elle est la reine", "il est
     la" : (0, (qui, est)), (qui, (est, la)), (0, (elle, est)), (elle, (est,
     la)), (est, (la, reine)), (0, (il, est)), (il, (est, la)) */
  assert(lexique_nb_contextes(lexique) == 7u);
  est_la = hachage_paires_identifiant(bigrammes, id(mots, "est"), id(mots, "la"));
  /* le bigramme (est, la) choisit l'accent grave (2 voix sur 3) ; seul le
     contexte (elle, (est, la)) est une exception */
  assert(strcmp(lexique_variante(lexique, id(mots, "la"), lexique_variante_bigramme(lexique, est_la)),
                "l\xC3\xA0") == 0);
  for (rang = 0u; rang < lexique_nb_contextes(lexique); rang++) {
    if (lexique_contexte(lexique, rang, &mot_avant, &bigramme) != LEXIQUE_AUCUNE) {
      nb_exceptions++;
      assert(mot_avant == id(mots, "elle") && bigramme == est_la);
    }
  }
  assert(nb_exceptions == 1u);
  assert(strcmp(lexique_variante(lexique, id(mots, "la"),
                                 lexique_variante_contexte(lexique, id(mots, "elle"), est_la)),
                "la") == 0);
  markov_detruire(&m);
  lexique_detruire(&lexique);
  hachage_paires_detruire(&bigrammes);
  hachage_detruire(&mots);
  /* un fichier sans accent ne renseigne pas sur les accents : aucun contexte */
  mots = hachage_creer(0u);
  bigrammes = hachage_paires_creer(0u);
  lexique = lexique_creer();
  options.bigrammes = bigrammes;
  options.lexique = lexique;
  creer_fichier_dans(sans_accent, "a.txt", "Qui est la ? Elle est la reine. Il est la.");
  m = repertoire_apprendre(sans_accent, 1u, mots, &options);
  assert(m != NULL && lexique_nb_contextes(lexique) == 0u);
  markov_detruire(&m);
  lexique_detruire(&lexique);
  hachage_paires_detruire(&bigrammes);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(sans_accent);
  detruire_repertoire_temporaire(repertoire);
}

static void test_apprendre_courriers(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  repertoire_bilan_t bilan;
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL, false,
                                   false, NULL, 0u, NULL, NULL };
  markov_t *m;

  options.bilan = &bilan;
  creer_fichier_dans(repertoire, "a.mbox",
                     "From jean@example.com Mon Mar  3 10:00:00 2024\n"
                     "From: Jean\nTo: Paul\nSubject: Chat\n\n"
                     "Le chat dort.\n-- \nJean\n\n"
                     "From paul@example.com Mon Mar  3 11:00:00 2024\n"
                     "From: Paul\nTo: Jean\nReceived: from mx\n\n"
                     "Le chien dort.\n> Le chat dort.\n");
  creer_fichier_dans(repertoire, "b.txt", "Le rat dort.");
  m = repertoire_apprendre(repertoire, 1u, mots, &options);
  assert(m != NULL);
  /* deux fichiers, deux messages ; ni en-tetes, ni citation, ni signature */
  assert(bilan.nb_fichiers == 2u && bilan.nb_messages == 2u);
  assert(bilan.nb_phrases == 3u && bilan.nb_mots == 9u);
  assert(hachage_nb_mots(mots) == 6u);       /* "" le chat dort chien rat */
  assert(hachage_identifiant(mots, "subject") == HACHAGE_ABSENT);
  assert(hachage_identifiant(mots, "jean") == HACHAGE_ABSENT);
  assert(markov_poids(m, id(mots, "chat"), id(mots, "dort")) == 1u);
  markov_detruire(&m);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_apprendre_marques(void)
{
  /* le lexique retient les tirets et les guillemets majoritaires */
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  hachage_paires_t *bigrammes = hachage_paires_creer(0u);
  lexique_t *lexique = lexique_creer();
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL, false,
                                   false, NULL, 0u, NULL, NULL };
  markov_t *m;
  unsigned int b;

  options.bigrammes = bigrammes;
  options.lexique = lexique;
  creer_fichier_dans(repertoire, "a.txt",
                     "--Oui, dit-il. --Oui, dit-elle. Oui. Il dit: " GO
                     "Viens" GF ", puis sortit. Il dit: " GO "Viens ici!"
                     GF "\n\n" CADRATIN " Non, dit-il.");
  m = repertoire_apprendre(repertoire, 3u, mots, &options);
  assert(m != NULL);
  /* "oui" commence 3 phrases, dont 2 repliques : tiret */
  b = hachage_paires_identifiant(bigrammes, 0u, id(mots, "oui"));
  assert(lexique_marques(lexique, b) == MARQUE_TIRET);
  assert(lexique_liaison(lexique, b) == LIAISON_ESPACE);
  b = hachage_paires_identifiant(bigrammes, 0u, id(mots, "non"));
  assert(lexique_marques(lexique, b) == MARQUE_TIRET);
  /* "il" commence deux phrases sans tiret (le guillemet fermant qui le
     precede appartient a la phrase d'avant) */
  b = hachage_paires_identifiant(bigrammes, 0u, id(mots, "il"));
  assert(lexique_marques(lexique, b) == MARQUE_AUCUNE);
  /* "dit: <<Viens" : deux-points et guillemet ouvrant */
  b = hachage_paires_identifiant(bigrammes, id(mots, "dit"), id(mots, "viens"));
  assert(lexique_liaison(lexique, b) == LIAISON_DEUX_POINTS);
  assert(lexique_marques(lexique, b) == MARQUE_OUVRE_GUILLEMET);
  /* "Viens>>, puis" : guillemet fermant et virgule */
  b = hachage_paires_identifiant(bigrammes, id(mots, "viens"), id(mots, "puis"));
  assert(lexique_liaison(lexique, b) == LIAISON_VIRGULE);
  assert(lexique_marques(lexique, b) == MARQUE_FERME_GUILLEMET);
  b = hachage_paires_identifiant(bigrammes, id(mots, "dit"), id(mots, "il"));
  assert(lexique_liaison(lexique, b) == LIAISON_TRAIT_UNION);
  assert(lexique_marques(lexique, b) == MARQUE_AUCUNE);
  markov_detruire(&m);
  lexique_detruire(&lexique);
  hachage_paires_detruire(&bigrammes);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_apprendre_phrase_par_ligne(void)
{
  const char *const contenu = "Un vers\nUn vers!\nUn autre\n";
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  markov_t *ponctuation = markov_creer(5u);
  repertoire_bilan_t bilan;
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL, true,
                                   false, NULL, 0u, NULL, NULL };
  markov_t *m;
  unsigned int vers;
  unsigned int autre;

  options.ponctuation = ponctuation;
  options.bilan = &bilan;
  creer_fichier_dans(repertoire, "poeme.txt", contenu);
  /* une phrase par ligne : trois phrases, les fins sans ponctuation sont
     comptees telles quelles */
  m = repertoire_apprendre(repertoire, 3u, mots, &options);
  assert(m != NULL);
  vers = id(mots, "vers");
  autre = id(mots, "autre");
  assert(bilan.nb_phrases == 3u && bilan.nb_mots == 6u);
  assert(markov_poids(m, REPERTOIRE_DEBUT_PHRASE, id(mots, "un")) == 3u);
  assert(markov_poids(m, vers, REPERTOIRE_DEBUT_PHRASE) == 2u);
  assert(markov_poids(m, vers, id(mots, "un")) == 0u);
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + vers, PONCTUATION_AUCUNE) == 1u);
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + vers, PONCTUATION_EXCLAMATION) == 1u);
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + autre, PONCTUATION_AUCUNE) == 1u);
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + autre, PONCTUATION_POINT) == 0u);
  markov_detruire(&m);
  markov_detruire(&ponctuation);
  hachage_detruire(&mots);
  /* lecture habituelle : deux phrases, la fin sans ponctuation est un point */
  mots = hachage_creer(0u);
  ponctuation = markov_creer(5u);
  options.ponctuation = ponctuation;
  options.phrase_par_ligne = false;
  m = repertoire_apprendre(repertoire, 3u, mots, &options);
  assert(m != NULL);
  vers = id(mots, "vers");
  autre = id(mots, "autre");
  assert(bilan.nb_phrases == 2u && bilan.nb_mots == 6u);
  assert(markov_poids(m, vers, id(mots, "un")) == 1u);
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + autre, PONCTUATION_POINT) == 1u);
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + autre, PONCTUATION_AUCUNE) == 0u);
  markov_detruire(&m);
  markov_detruire(&ponctuation);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_apprendre_filtre_langue(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *anglais = chemin_dans(repertoire, "b_anglais.txt");
  hachage_t *mots = hachage_creer(0u);
  hachage_t *temoin;
  repertoire_bilan_t bilan;
  repertoire_bilan_t bilan_anglais;
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL, false,
                                   true, NULL, 0u, NULL, NULL };
  markov_t *m;
  markov_t *m_temoin;
  unsigned int etats_francais;
  unsigned int e;

  options.bilan = &bilan;
  creer_fichier_dans(repertoire, "a_francais.txt", TEXTE_FRANCAIS);
  creer_fichier_dans(repertoire, "c_francais.txt", TEXTE_FRANCAIS);
  /* temoin : le francais seul, sans filtre */
  temoin = hachage_creer(0u);
  m_temoin = repertoire_creer_markov(repertoire, 3u, temoin);
  assert(m_temoin != NULL);
  etats_francais = markov_nb_etats(m_temoin);
  /* le texte anglais seul : nombre de phrases et de mots */
  ecrire_fichier(anglais, TEXTE_ANGLAIS);
  options.bilan = &bilan_anglais;
  m = repertoire_apprendre(anglais, 3u, mots, &options);
  assert(m != NULL);
  assert(bilan_anglais.langue == LANGUE_ANGLAIS);
  assert(bilan_anglais.nb_phrases == 6u);
  assert(bilan_anglais.nb_phrases_ecartees == 0u);
  assert(hachage_contient(mots, "river"));
  markov_detruire(&m);
  hachage_detruire(&mots);

  /* avec le filtre : l'anglais ne laisse aucune trace, la chaine est celle du francais seul */
  mots = hachage_creer(0u);
  options.bilan = &bilan;
  m = repertoire_apprendre(repertoire, 3u, mots, &options);
  assert(m != NULL);
  assert(bilan.langue == LANGUE_FRANCAIS);
  assert(bilan.nb_fichiers == 3u);
  assert(bilan.nb_phrases_ecartees == bilan_anglais.nb_phrases);
  assert(bilan.nb_mots_ecartes == bilan_anglais.nb_mots);
  assert(bilan.nb_phrases > bilan.nb_phrases_ecartees);
  assert(!hachage_contient(mots, "the") && !hachage_contient(mots, "river"));
  assert(hachage_contient(mots, "chat") && hachage_contient(mots, "village"));
  assert(markov_nb_etats(m) == etats_francais);
  assert(hachage_nb_mots(mots) == hachage_nb_mots(temoin));
  for (e = 0u; e < etats_francais; e++) {
    assert(strcmp(hachage_mot(mots, e), hachage_mot(temoin, e)) == 0);
    assert(markov_poids_total(m, e) == markov_poids_total(m_temoin, e));
  }
  markov_detruire(&m);
  hachage_detruire(&mots);

  /* sans le filtre : tout est appris */
  mots = hachage_creer(0u);
  options.filtrer_langues = false;
  m = repertoire_apprendre(repertoire, 3u, mots, &options);
  assert(m != NULL);
  assert(bilan.langue == LANGUE_INCONNUE);
  assert(bilan.nb_phrases_ecartees == 0u && bilan.nb_mots_ecartes == 0u);
  assert(hachage_contient(mots, "the") && hachage_contient(mots, "chat"));
  assert(markov_nb_etats(m) > etats_francais);
  markov_detruire(&m);
  hachage_detruire(&mots);

  /* le filtre s'applique aussi a l'ordre 3 et en lecture par ligne */
  mots = hachage_creer(0u);
  options.filtrer_langues = true;
  options.ordre = 3u;
  options.phrase_par_ligne = true;
  options.nuplets = hachage_nuplets_creer(3u, 0u);
  m = repertoire_apprendre(repertoire, 3u, mots, &options);
  assert(m != NULL);
  assert(bilan.langue == LANGUE_FRANCAIS);
  assert(!hachage_contient(mots, "river") && hachage_contient(mots, "chat"));
  assert(markov_nb_etats(m) == hachage_nuplets_nb(options.nuplets));
  markov_detruire(&m);
  hachage_nuplets_detruire(&options.nuplets);
  hachage_detruire(&mots);

  markov_detruire(&m_temoin);
  hachage_detruire(&temoin);
  free(anglais);
  detruire_repertoire_temporaire(repertoire);
}

static void test_apprendre_filtre_langue_inconnue(void)
{
  /* langue non reconnue : rien n'est ecarte */
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  repertoire_bilan_t bilan;
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL, false,
                                   true, NULL, 0u, NULL, NULL };
  markov_t *m;

  options.bilan = &bilan;
  creer_fichier_dans(repertoire, "a.txt", "Zorg blax krum. Blax zorg.");
  creer_fichier_dans(repertoire, "vide.txt", "");
  m = repertoire_apprendre(repertoire, 3u, mots, &options);
  assert(m != NULL);
  assert(bilan.langue == LANGUE_INCONNUE);
  assert(bilan.nb_fichiers == 2u && bilan.nb_phrases == 2u);
  assert(bilan.nb_phrases_ecartees == 0u);
  assert(hachage_nb_mots(mots) == 4u);
  markov_detruire(&m);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

/* ======================================================================== */
/*                        Verification des contrats                         */
/* ======================================================================== */

static void violation_graphie_sans_mot(void)
{
  iterateur_mots_t *it = iterateur_mots_creer("Makefile");

  iterateur_mots_graphie(it);
}

static void violation_liaison_apres_fin(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "a.txt");
  iterateur_mots_t *it;

  ecrire_fichier(chemin, "seul");
  it = iterateur_mots_creer(chemin);
  iterateur_mots_suivant(it, NULL);
  iterateur_mots_suivant(it, NULL);
  iterateur_mots_liaison(it);
}

static void violation_options_nulles(void)
{
  hachage_t *mots = hachage_creer(0u);

  repertoire_apprendre("/tmp", 1u, mots, NULL);
}

static void violation_ordre_3_sans_nuplets(void)
{
  hachage_t *mots = hachage_creer(0u);
  hachage_paires_t *bigrammes = hachage_paires_creer(0u);
  repertoire_options_t options = { 3u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };

  options.bigrammes = bigrammes;
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_repli_pas_inferieur(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 2u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 2u, NULL, NULL };

  options.bigrammes = hachage_paires_creer(0u);
  options.repli = markov_creer(1u);
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_repli_sans_chaine(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 2u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 1u, NULL, NULL };

  options.bigrammes = hachage_paires_creer(0u);
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_chaine_de_repli_en_trop(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 2u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };

  options.bigrammes = hachage_paires_creer(0u);
  options.repli = markov_creer(1u);
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_repli_2_sans_bigrammes(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 3u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 2u, NULL, NULL };

  options.nuplets = hachage_nuplets_creer(3u, 0u);
  options.repli = markov_creer(1u);
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_repli_3_sans_nuplets(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 4u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 3u, NULL, NULL };

  options.nuplets = hachage_nuplets_creer(4u, 0u);
  options.repli = markov_creer(1u);
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_nuplets_de_repli_de_mauvaise_taille(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 5u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 3u, NULL, NULL };

  options.nuplets = hachage_nuplets_creer(5u, 0u);
  options.repli = markov_creer(1u);
  options.nuplets_repli = hachage_nuplets_creer(4u, 0u);
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_chaine_de_repli_finalisee(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 2u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 1u, NULL, NULL };

  options.bigrammes = hachage_paires_creer(0u);
  options.repli = markov_creer(1u);
  markov_finaliser(options.repli);
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_ordre_nul(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 0u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };

  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_ordre_trop_grand(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { REPERTOIRE_ORDRE_MAX + 1u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };

  options.nuplets = hachage_nuplets_creer(REPERTOIRE_ORDRE_MAX + 1u, 0u);
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_nuplets_de_mauvaise_taille(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 4u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };

  options.nuplets = hachage_nuplets_creer(3u, 0u);
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_nuplets_a_l_ordre_1(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };

  options.nuplets = hachage_nuplets_creer(1u, 0u);
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_nuplets_non_vides(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 3u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };
  const unsigned int elements[3] = { 0u, 0u, 0u };

  options.nuplets = hachage_nuplets_creer(3u, 0u);
  hachage_nuplets_ajouter(options.nuplets, elements);
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_corpus_sans_chemin(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };
  const char *chemins[1] = { "/tmp" };

  repertoire_apprendre_corpus(chemins, 0u, 1u, mots, &options);
}

static void violation_corpus_chemin_nul(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };
  const char *chemins[2] = { "/tmp", NULL };

  repertoire_apprendre_corpus(chemins, 2u, 1u, mots, &options);
}

static void violation_lister_corpus_nul(void)
{
  size_t nb;

  repertoire_lister_corpus(NULL, 1u, false, &nb);
}

static void violation_marques_sans_mot(void)
{
  iterateur_mots_t *it = iterateur_mots_creer("Makefile");

  iterateur_mots_marques(it);
}

static void violation_phrase_par_ligne_trop_tard(void)
{
  iterateur_mots_t *it = iterateur_mots_creer("Makefile");

  iterateur_mots_suivant(it, NULL);
  iterateur_mots_phrase_par_ligne(it, true);
}

static void violation_cle_nulle(void)
{
  repertoire_cle(NULL);
}

static void violation_lexique_sans_bigrammes(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };

  options.lexique = lexique_creer();
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_ponctuation_non_vide(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };

  options.ponctuation = markov_creer(1u);
  markov_ajouter_etat(options.ponctuation);
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_lister_nombre_nul(void)
{
  repertoire_lister_fichiers("/tmp", false, NULL);
}

static void violation_iterateur_chemin_nul(void)
{
  iterateur_mots_creer(NULL);
}

static void violation_iterateur_nul(void)
{
  iterateur_mots_suivant(NULL, NULL);
}

static void violation_iterateur_detruire_nul(void)
{
  iterateur_mots_detruire(NULL);
}

static void violation_iterateur_erreur_nul(void)
{
  iterateur_mots_erreur(NULL);
}

static void violation_markov_chemin_nul(void)
{
  hachage_t *mots = hachage_creer(0u);

  repertoire_creer_markov(NULL, 1u, mots);
}

static void violation_markov_table_nulle(void)
{
  repertoire_creer_markov("/tmp", 1u, NULL);
}

static void violation_markov_table_non_vide(void)
{
  hachage_t *mots = hachage_creer(0u);

  hachage_ajouter(mots, "deja");
  repertoire_creer_markov("/tmp", 1u, mots);
}

static void sans_violation(void)
{
  hachage_t *mots = hachage_creer(0u);
  markov_t *m = repertoire_creer_markov("/tmp/repertoire_absent_markov", 1u, mots);

  assert(m == NULL);
  hachage_detruire(&mots);
}

static void test_contrats(void)
{
  assert(!viole_contrat(sans_violation));
  assert(viole_contrat(violation_iterateur_chemin_nul));
  assert(viole_contrat(violation_iterateur_nul));
  assert(viole_contrat(violation_iterateur_detruire_nul));
  assert(viole_contrat(violation_iterateur_erreur_nul));
  assert(viole_contrat(violation_markov_chemin_nul));
  assert(viole_contrat(violation_markov_table_nulle));
  assert(viole_contrat(violation_markov_table_non_vide));
  assert(viole_contrat(violation_graphie_sans_mot));
  assert(viole_contrat(violation_liaison_apres_fin));
  assert(viole_contrat(violation_options_nulles));
  assert(viole_contrat(violation_ordre_3_sans_nuplets));
  assert(viole_contrat(violation_ordre_nul));
  assert(viole_contrat(violation_repli_pas_inferieur));
  assert(viole_contrat(violation_repli_sans_chaine));
  assert(viole_contrat(violation_chaine_de_repli_en_trop));
  assert(viole_contrat(violation_repli_2_sans_bigrammes));
  assert(viole_contrat(violation_repli_3_sans_nuplets));
  assert(viole_contrat(violation_nuplets_de_repli_de_mauvaise_taille));
  assert(viole_contrat(violation_chaine_de_repli_finalisee));
  assert(viole_contrat(violation_ordre_trop_grand));
  assert(viole_contrat(violation_nuplets_de_mauvaise_taille));
  assert(viole_contrat(violation_nuplets_a_l_ordre_1));
  assert(viole_contrat(violation_nuplets_non_vides));
  assert(viole_contrat(violation_corpus_sans_chemin));
  assert(viole_contrat(violation_corpus_chemin_nul));
  assert(viole_contrat(violation_lister_corpus_nul));
  assert(viole_contrat(violation_marques_sans_mot));
  assert(viole_contrat(violation_phrase_par_ligne_trop_tard));
  assert(viole_contrat(violation_cle_nulle));
  assert(viole_contrat(violation_lexique_sans_bigrammes));
  assert(viole_contrat(violation_ponctuation_non_vide));
  assert(viole_contrat(violation_lister_nombre_nul));
}


int main(void)
{
  printf("Tests du module repertoire :\n");
  LANCER_TEST(test_iterateur_phrases_simples);
  LANCER_TEST(test_iterateur_ponctuation_retiree);
  LANCER_TEST(test_iterateur_majuscules);
  LANCER_TEST(test_iterateur_accents_utf8);
  LANCER_TEST(test_iterateur_latin1);
  LANCER_TEST(test_iterateur_bom_et_crlf);
  LANCER_TEST(test_iterateur_mot_tres_long);
  LANCER_TEST(test_iterateur_fin_de_fichier);
  LANCER_TEST(test_iterateur_lignes_vides);
  LANCER_TEST(test_iterateur_abreviations);
  LANCER_TEST(test_iterateur_nombres);
  LANCER_TEST(test_iterateur_points_de_suspension);
  LANCER_TEST(test_iterateur_courrier);
  LANCER_TEST(test_iterateur_grand_courrier);
  LANCER_TEST(test_iterateur_sans_mot);
  LANCER_TEST(test_iterateur_graphies);
  LANCER_TEST(test_iterateur_graphie_latin1);
  LANCER_TEST(test_iterateur_liaisons);
  LANCER_TEST(test_iterateur_ponctuations);
  LANCER_TEST(test_iterateur_accentue);
  LANCER_TEST(test_iterateur_gutenberg);
  LANCER_TEST(test_graphie_valide);
  LANCER_TEST(test_iterateur_fichier_inexistant);
  LANCER_TEST(test_iterateur_repertoire);
  LANCER_TEST(test_iterateur_detruire_en_cours);
  LANCER_TEST(test_iterateur_ponctuation_interne);
  LANCER_TEST(test_iterateur_parentheses);
  LANCER_TEST(test_iterateur_lettres_etendues);
  LANCER_TEST(test_iterateur_guillemets);
  LANCER_TEST(test_iterateur_tirets);
  LANCER_TEST(test_iterateur_phrase_par_ligne);
  LANCER_TEST(test_cle);
  LANCER_TEST(test_markov_poids);
  LANCER_TEST(test_markov_fichiers_independants);
  LANCER_TEST(test_markov_ordre_des_fichiers);
  LANCER_TEST(test_markov_fichiers_ignores);
  LANCER_TEST(test_markov_repertoire_vide);
  LANCER_TEST(test_markov_erreurs);
  LANCER_TEST(test_markov_corpus_reel);
  LANCER_TEST(test_markov_meme_graine_meme_texte);
  LANCER_TEST(test_lister_fichiers);
  LANCER_TEST(test_lister_profondeur_maximale);
  LANCER_TEST(test_apprendre_ordre_2);
  LANCER_TEST(test_apprendre_lexique_ponctuation);
  LANCER_TEST(test_apprendre_recursif);
  LANCER_TEST(test_lister_corpus);
  LANCER_TEST(test_apprendre_corpus);
  LANCER_TEST(test_apprendre_ordre_3);
  LANCER_TEST(test_apprendre_tous_les_ordres);
  LANCER_TEST(test_apprendre_repli);
  LANCER_TEST(test_apprendre_courriers);
  LANCER_TEST(test_apprendre_contextes);
  LANCER_TEST(test_apprendre_marques);
  LANCER_TEST(test_apprendre_phrase_par_ligne);
  LANCER_TEST(test_apprendre_filtre_langue);
  LANCER_TEST(test_apprendre_filtre_langue_inconnue);
  LANCER_TEST(test_contrats);
  afficher_bilan("repertoire");
  return EXIT_SUCCESS;
}
