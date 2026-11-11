/**
Fichier test_aleatoire.c

Test aleatoire (fuzzing) des lectures de fichiers : on donne au programme
des octets au hasard et on verifie que les contrats tiennent toujours.
Compile avec AddressSanitizer et UndefinedBehaviorSanitizer (cible
test_aleatoire du Makefile) : tout debordement de tampon, lecture hors
bornes ou comportement indefini arrete le test.

  1. Iterateur : fichiers d'octets quelconques ou de fragments "piegeux"
     (UTF-8 tronque, Latin-1, lettres d'Europe centrale, marques
     Gutenberg, guillemets, tirets et parentheses, ponctuation interne,
     octets nuls, ...), lus normalement ou "une phrase par ligne". Chaque
     mot doit etre non vide, sur {a..z}, avec une graphie valide qui lui
     correspond et des marques de dialogue valides ; le dernier mot
     termine une phrase.
  2. Chaine complete : apprentissage sur des corpus aleatoires (ordres 1 a
     MODELE_ORDRE_MAX, ordre de repli, un ou deux chemins, options tirees
     au hasard), generation (de tout debut et depuis un mot), sauvegarde
     et rechargement (memes phrases).
  3. Chargement de modeles abimes : un modele valide (avec ou sans repli,
     avec un contexte de deux mots) est modifie au hasard
     (octets changes, lignes supprimees ou dupliquees, chiffres modifies,
     fichier tronque). Le chargement doit refuser le fichier ou donner un
     modele utilisable sans risque.
  4. Detecteur de langue : suites de mots au hasard ; les bilans (phrases
     et mots ecartes) doivent correspondre aux phrases ecartees.
  5. Courriers : en-tetes de courrier suivis de fragments "piegeux"
     (delimiteurs MIME, encodages, jeux de caracteres, HTML, citations,
     signatures, lignes mbox) ; le texte extrait respecte les
     post-conditions de mail_extraire_texte et l'iterateur le lit sans
     erreur.

Usage : ./test_aleatoire [nombre d'iterations] [graine] (defaut : 3000
iterations, graine fixe). Le hasard est deterministe : un echec est
toujours reproductible avec les memes arguments.
*/
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include "outils_tests.h"
#include "../src/hachage.h"
#include "../src/lexique.h"
#include "../src/langue.h"
#include "../src/repertoire.h"
#include "../src/modele.h"
#include "../src/mail.h"

/** Nombre d'iterations par defaut. */
#define ITERATIONS_DEFAUT 3000u

/** Taille maximale d'un fichier aleatoire. */
#define TAILLE_MAX 3000u

static uint64_t etat_aleatoire = UINT64_C(0x9E3779B97F4A7C15);
static unsigned int nb_iterations = ITERATIONS_DEFAUT;
static char *travail = NULL;

/** Generateur deterministe (xorshift64*). */
static uint64_t aleatoire(void)
{
  etat_aleatoire ^= etat_aleatoire >> 12;
  etat_aleatoire ^= etat_aleatoire << 25;
  etat_aleatoire ^= etat_aleatoire >> 27;
  return etat_aleatoire * UINT64_C(0x2545F4914F6CDD1D);
}

static size_t au_hasard(size_t borne)
{
  return (size_t)(aleatoire() % (uint64_t)borne);
}

/** Fragments qui exercent les cas delicats de la lecture. */
static const char *const FRAGMENTS[] = {
  "le ", "chat", "Dort", "ETE", " ", ".", "!", "?", "...", ",", ";", ":",
  "'", "\xE2\x80\x99", "-", "\xE2\x80\x91", "\n", "\n\n", "\r\n", "\t",
  "\xC3\xA9", "\xC3\x89", "\xC5\x93", "\xC5\x92", "\xC3\x9F", "\xC3\xBF",
  "\xC5\xB8", "\xC3\x97", "\xE2\x80\xA6", "\xEF\xBB\xBF", "\xC2\xA0",
  "\xC3", "\xE2\x80", "\xF0\x9F\x98\x80", "\xE9", "\xFF", "\x80", "\xC0\xAF",
  "M. ", "MM. ", "J.-J. ", "3.5", "42", "\xC2\xAB ", " \xC2\xBB",
  "\xC2\xAB", "\xC2\xBB", "\"", " \"", "\" ", "\xE2\x80\x9C", "\xE2\x80\x9D",
  "\xE2\x80\x9E", "--", "---", "----", " - ", "\n- ", "\xE2\x80\x94",
  "\xE2\x80\x93 ", "\xE2\x80\x95", "\n--",
  "(", ")", "[", "]", " (", ") ", "! et ", "? qui ", "... puis ", ". mais ",
  "\xC5\x81", "\xC5\x82", "\xC4\xB0", "\xC4\xB1", "\xC8\x98", "\xC8\x99",
  "\xC4\x8C", "\xC5\xBE", "\xC5\x89", "\xC5\xBF", "\xC4\xB2", "\xC4\xB8",
  "\xC5\xB9", "\xC4\x85",
  "*** START OF THE PROJECT GUTENBERG EBOOK X ***\n",
  "*** END OF THE PROJECT GUTENBERG EBOOK X ***\n",
  "End of the Project Gutenberg EBook\n", "***", "\n***\n"
};

/** Fragments de courriers : en-tetes, delimiteurs MIME, encodages, jeux
    de caracteres, HTML, citations, signatures. */
static const char *const FRAGMENTS_COURRIER[] = {
  "From: a\n", "To: b\n", "Subject: x\n", "Content-Type: text/plain\n",
  "Content-Type: text/html\n", "Content-Type: multipart/mixed; boundary=b\n",
  "Content-Type: multipart/alternative; boundary=\"b\"\n",
  "Content-Type: message/rfc822\n",
  "Content-Type: text/plain; charset=iso-8859-1\n",
  "Content-Type: text/plain; charset=windows-1252\n",
  "Content-Type: text/html; charset=iso-8859-15\n",
  "Content-Transfer-Encoding: quoted-printable\n",
  "Content-Transfer-Encoding: base64\n", "Content-Disposition: attachment\n",
  " suite\n", "\n", "\n\n", "--b\n", "--b--\n", "--b  \n", "--bb\n",
  "\nFrom x Mon\n", ">From y\n", "> cite\n", "-- \n", "--\n",
  "Le 3 mars, Paul a \xC3\xA9" "crit :\n", "Le lun. 3 mars,\n", "wrote:\n",
  "-----Original Message-----\n", "-----BEGIN PGP SIGNED MESSAGE-----\n",
  "Hash: SHA1\n", "Envoy\xC3\xA9 de mon iPhone\n", "=C3=A9", "=\n", "=E9",
  "=ZZ", "=", "Q2Fmw6k=", "YQ==", "====", "<p>", "</p>", "<br>",
  "<blockquote>", "</blockquote>", "<script>", "</script>", "<head>",
  "<!--", "-->", "<!DOCTYPE html>", "<a href=\"x>y\">", "<", ">", "&eacute;",
  "&amp;", "&#233;", "&#x2014;", "&#0;", "&#99999999;", "&#xD800;", "&", ";",
  "\"", "'", "\xE9", "\x93", "\x81", "\xC3\xA9", "\xC3", "\r\n", "\r", " ",
  "mot ", "Bonjour. ", "fin\n", "\xC2\xA0:"
};

/** Nombre de fragments de FRAGMENTS_COURRIER qui sont des lignes
    d'en-tete (les premiers). */
#define NB_FRAGMENTS_ENTETE 15u

/**
 * Remplit tampon d'un courrier au hasard : en-tetes (eventuellement
 * precedes d'une ligne mbox, avec quelques en-tetes MIME tires au hasard
 * et presque toujours suivis d'une ligne vide), puis fragments de courrier
 * ou de texte. Renvoie le nombre d'octets.
 */
static size_t generer_courrier(char *tampon, size_t capacite)
{
  const char *debut = (au_hasard(3u) == 0u) ? "From x Mon\nFrom: a\nTo: b\n" : "From: a\nTo: b\n";
  const char *fragment;
  size_t n = strlen(debut);
  size_t longueur;
  size_t nb = au_hasard(80u);
  size_t k;

  memcpy(tampon, debut, n);
  for (k = au_hasard(4u); k > 0u; k--) {
    fragment = FRAGMENTS_COURRIER[au_hasard(NB_FRAGMENTS_ENTETE)];
    memcpy(tampon + n, fragment, strlen(fragment));
    n += strlen(fragment);
  }
  if (au_hasard(10u) != 0u) {
    tampon[n] = '\n';
    n++;
  }
  for (k = 0u; k < nb; k++) {
    fragment = (au_hasard(4u) == 0u)
               ? FRAGMENTS[au_hasard(sizeof(FRAGMENTS) / sizeof(FRAGMENTS[0]))]
               : FRAGMENTS_COURRIER[au_hasard(sizeof(FRAGMENTS_COURRIER) / sizeof(FRAGMENTS_COURRIER[0]))];
    longueur = strlen(fragment);
    if (au_hasard(50u) == 0u) {
      fragment = "\0";                      /* octet nul */
      longueur = 1u;
    }
    if (n + longueur > capacite) {
      longueur = capacite - n;
    }
    memcpy(tampon + n, fragment, longueur);
    n += longueur;
  }
  return n;
}

/**
 * Remplit tampon au hasard : octets quelconques (mode 0) ou suite de
 * fragments (mode 1). Renvoie le nombre d'octets.
 */
static size_t generer(char *tampon, size_t capacite, unsigned int mode)
{
  size_t taille = au_hasard(capacite);
  size_t n = 0u;
  size_t longueur;
  const char *fragment;

  if (mode == 0u) {
    for (n = 0u; n < taille; n++) {
      tampon[n] = (char)au_hasard(256u);
    }
  } else {
    while (n < taille) {
      fragment = FRAGMENTS[au_hasard(sizeof(FRAGMENTS) / sizeof(FRAGMENTS[0]))];
      longueur = strlen(fragment);
      if (au_hasard(40u) == 0u) {
        fragment = "\0";                    /* octet nul */
        longueur = 1u;
      }
      if (n + longueur > capacite) {
        longueur = capacite - n;
      }
      memcpy(tampon + n, fragment, longueur);
      n += longueur;
    }
  }
  return n;
}

/**
 * Lit tout un fichier et verifie les contrats de l'iterateur (lecture
 * normale, ou une phrase par ligne). Renvoie le nombre de mots.
 */
static unsigned int verifier_iterateur(const char *chemin, bool par_ligne)
{
  iterateur_mots_t *it = iterateur_mots_creer(chemin);
  const char *mot;
  const char *graphie;
  liaison_t liaison_precedente = LIAISON_ESPACE;
  bool fin = true;
  bool dernier_fin = true;
  unsigned int nb_mots = 0u;

  assert(it != NULL);
  iterateur_mots_phrase_par_ligne(it, par_ligne);
  while ((mot = iterateur_mots_suivant(it, &fin)) != NULL) {
    graphie = iterateur_mots_graphie(it);
    assert(mot[0] != '\0' && hachage_mot_valide(mot));
    assert(lexique_graphie_valide(graphie));
    assert(repertoire_graphie_valide(graphie, mot));
    assert(iterateur_mots_liaison(it) < NB_LIAISONS);
    assert(iterateur_mots_ponctuation(it) < NB_PONCTUATIONS);
    assert(fin || iterateur_mots_ponctuation(it) == PONCTUATION_AUCUNE);
    assert(!fin || iterateur_mots_liaison(it) == LIAISON_ESPACE);
    /* marques valides ; aucune apres une apostrophe ou un trait d'union */
    assert(iterateur_mots_marques(it) <= MARQUES_TOUTES);
    assert(lexique_separateur_valide(liaison_precedente, iterateur_mots_marques(it)));
    liaison_precedente = iterateur_mots_liaison(it);
    dernier_fin = fin;
    nb_mots++;
  }
  assert(!fin);
  assert(dernier_fin);                      /* le dernier mot finit une phrase */
  assert(iterateur_mots_erreur(it) == 0);
  assert(iterateur_mots_suivant(it, NULL) == NULL);
  iterateur_mots_detruire(&it);
  return nb_mots;
}

/**
 * Verifie une phrase generee : guillemets et parentheses bien formes,
 * marques en tete (tiret, parenthese puis guillemet eventuels) suivies du
 * premier mot, ponctuation finale (facultative si sans_ponctuation) puis
 * marques fermantes eventuelles, mots du modele. Si premier est different
 * de HACHAGE_ABSENT, le premier mot doit etre ce mot. Renvoie le nombre de mots.
 */
static unsigned int verifier_phrase(const modele_t *m, const char *phrase,
                                    bool sans_ponctuation,
                                    unsigned int premier)
{
  char *chemin = chemin_dans(travail, "phrase.txt");
  iterateur_mots_t *it;
  const char *mot;
  const char *debut = apres_marques_ouvrantes(phrase);
  char *texte;
  unsigned int nb_mots = 0u;

  assert(strlen(phrase) >= 1u && strchr(phrase, '\n') == NULL);
  if (!marques_bien_formees(phrase) || !(sans_ponctuation || termine_par_ponctuation(phrase))) {
    fprintf(stderr, "\nphrase mal formee : \"%s\" (ordre %u)\n", phrase, modele_ordre(m));
  }
  assert(marques_bien_formees(phrase));
  assert(sans_ponctuation || termine_par_ponctuation(phrase));
  /* la phrase est relue apres un mot "x" sur la meme ligne : seule sur sa
     ligne, une phrase comme "END OF THE PROJECT GUTENBERG EBOOK" serait
     prise pour une marque de fin de livre (cf. repertoire.h) */
  texte = malloc(strlen(phrase) + 3u);
  assert(texte != NULL);
  strcpy(texte, "x ");
  strcat(texte, phrase);
  ecrire_fichier(chemin, texte);
  free(texte);
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  mot = iterateur_mots_suivant(it, NULL);
  assert(mot != NULL && strcmp(mot, "x") == 0);
  while ((mot = iterateur_mots_suivant(it, NULL)) != NULL) {
    assert(hachage_contient(modele_mots(m), mot));
    if (nb_mots == 0u) {
      assert(strncmp(debut, iterateur_mots_graphie(it), strlen(iterateur_mots_graphie(it))) == 0);
      assert(premier == HACHAGE_ABSENT || strcmp(mot, hachage_mot(modele_mots(m), premier)) == 0);
    }
    nb_mots++;
  }
  if (nb_mots == 0u) {
    fprintf(stderr, "\nphrase sans mot : \"%s\" (ordre %u)\n", phrase,
            modele_ordre(m));
  }
  assert(nb_mots >= 1u);
  iterateur_mots_detruire(&it);
  free(chemin);
  return nb_mots;
}


/* ======================================================================== */
/*                                   Tests                                  */
/* ======================================================================== */

static void test_iterateur_octets_au_hasard(void)
{
  static char tampon[TAILLE_MAX];
  char *chemin = chemin_dans(travail, "aleatoire.txt");
  unsigned int i;
  size_t taille;
  unsigned long nb_mots = 0u;

  for (i = 0u; i < nb_iterations; i++) {
    taille = generer(tampon, TAILLE_MAX, i % 2u);
    ecrire_octets(chemin, tampon, taille);
    /* les deux lectures donnent les memes mots */
    nb_mots += verifier_iterateur(chemin, false);
    assert(verifier_iterateur(chemin, true) == verifier_iterateur(chemin, false));
  }
  assert(nb_mots > nb_iterations);          /* le hasard produit des mots */
  free(chemin);
}

static void test_courriers_aleatoires(void)
{
  static char tampon[TAILLE_MAX];
  char *chemin = chemin_dans(travail, "courrier.eml");
  char *texte;
  size_t taille;
  size_t taille_texte;
  size_t nb_messages;
  unsigned int i;
  unsigned int nb_courriers = 0u;

  for (i = 0u; i < nb_iterations; i++) {
    taille = generer_courrier(tampon, TAILLE_MAX);
    if (mail_est_courrier(tampon, (taille < MAIL_TAILLE_DETECTION)
                                  ? taille : MAIL_TAILLE_DETECTION)) {
      nb_courriers++;
      texte = mail_extraire_texte(tampon, taille, &taille_texte, &nb_messages);
      assert(texte != NULL && nb_messages >= 1u);
      assert(strlen(texte) == taille_texte);
      assert(taille_texte == 0u
             || (taille_texte >= 2u && texte[taille_texte - 1u] == '\n'
                 && texte[taille_texte - 2u] == '\n'));
      free(texte);
    }
    ecrire_octets(chemin, tampon, taille);
    verifier_iterateur(chemin, i % 2u == 0u);
  }
  /* la plupart sont des courriers (les autres : un fragment a coupe les
     en-tetes) */
  assert(nb_courriers > nb_iterations / 2u);
  free(chemin);
}

/**
 * Compare les phrases d'un modele appris et du meme modele recharge (meme
 * graine) : de tout debut, puis depuis des mots tires au hasard.
 */
static void comparer_phrases(modele_t *m, modele_t *recharge, bool sans_ponctuation)
{
  char *phrase;
  char *phrase_rechargee;
  unsigned int mot;
  unsigned int k;

  for (k = 0u; k < 8u; k++) {
    mot = HACHAGE_ABSENT;
    if (k >= 4u) {
      mot = 1u + (unsigned int)au_hasard(modele_nb_mots(m));
    }
    if (mot == HACHAGE_ABSENT) {
      phrase = modele_generer_phrase(m);
      phrase_rechargee = modele_generer_phrase(recharge);
    } else {
      phrase = modele_generer_phrase_depuis(m, mot);
      phrase_rechargee = modele_generer_phrase_depuis(recharge, mot);
    }
    assert(phrase != NULL && phrase_rechargee != NULL);
    assert(strcmp(phrase, phrase_rechargee) == 0);
    verifier_phrase(m, phrase, sans_ponctuation, mot);
    free(phrase);
    free(phrase_rechargee);
  }
}

static void test_chaine_complete(void)
{
  static char tampon[TAILLE_MAX];
  char *corpus = chemin_dans(travail, "corpus");
  char *chemin_modele = chemin_dans(travail, "modele.txt");
  char *fichier_seul = chemin_dans(travail, "seul.txt");
  char *chemin_fichier;
  const char *chemins[2];
  char nom[32];
  modele_options_t options;
  repertoire_bilan_t bilan;
  modele_t *m;
  modele_t *recharge;
  unsigned int i;
  unsigned int f;
  size_t taille;
  size_t nb_chemins;
  unsigned int graine;

  for (i = 0u; i < nb_iterations / 10u + 1u; i++) {
    supprimer_arborescence(corpus);
    assert(mkdir(corpus, 0700) == 0);
    for (f = 0u; f < 1u + (unsigned int)au_hasard(3u); f++) {
      taille = generer(tampon, TAILLE_MAX, 1u);
      snprintf(nom, sizeof(nom), "f%u.txt", f);
      chemin_fichier = chemin_dans(corpus, nom);
      ecrire_octets(chemin_fichier, tampon, taille);
      free(chemin_fichier);
    }
    chemins[0] = corpus;
    nb_chemins = 1u;
    if (au_hasard(3u) == 0u) {
      taille = generer(tampon, TAILLE_MAX, 1u);
      ecrire_octets(fichier_seul, tampon, taille);
      chemins[1] = fichier_seul;
      nb_chemins = 2u;
    }
    options.ordre = 1u + (i % MODELE_ORDRE_MAX);
    options.recursif = false;
    options.phrase_par_ligne = (au_hasard(3u) == 0u);
    options.filtrer_langues = (au_hasard(2u) == 0u);
    options.ordre_repli = (unsigned int)au_hasard(options.ordre);
    graine = (unsigned int)aleatoire();
    m = modele_apprendre_corpus(chemins, nb_chemins, graine, &options, &bilan);
    assert(m != NULL);
    assert(bilan.nb_phrases_ecartees <= bilan.nb_phrases);
    assert(bilan.nb_mots_ecartes <= bilan.nb_mots);
    assert(options.filtrer_langues || bilan.nb_phrases_ecartees == 0u);
    if (modele_peut_generer(m)) {
      assert(modele_sauvegarder(m, chemin_modele));
      recharge = modele_charger(chemin_modele, graine);
      assert(recharge != NULL);
      assert(modele_ordre(recharge) == options.ordre);
      assert(modele_ordre_repli(recharge) == options.ordre_repli);
      comparer_phrases(m, recharge, options.phrase_par_ligne);
      modele_detruire(&recharge);
    }
    modele_detruire(&m);
  }
  supprimer_arborescence(corpus);
  remove(fichier_seul);
  free(corpus);
  free(chemin_modele);
  free(fichier_seul);
}

/** Applique une modification aleatoire a un texte (resultat a liberer). */
static char *abimer(const char *texte, size_t taille, size_t *nouvelle_taille)
{
  char *resultat = malloc(2u * taille + 16u);
  size_t debut;
  size_t fin;
  size_t n;
  size_t i;

  assert(resultat != NULL);
  memcpy(resultat, texte, taille);
  n = taille;
  switch (au_hasard(5u)) {
  case 0u:                                  /* quelques octets changes */
    for (i = 0u; i < 1u + au_hasard(3u); i++) {
      resultat[au_hasard(n)] = (char)au_hasard(256u);
    }
    break;
  case 1u:                                  /* un chiffre modifie */
    for (i = au_hasard(n); i < n && (resultat[i] < '0' || resultat[i] > '9');
         i++) {
    }
    if (i < n) {
      resultat[i] = (char)('0' + au_hasard(10u));
    }
    break;
  case 2u:                                  /* un passage supprime */
    debut = au_hasard(n);
    fin = debut + au_hasard(n - debut + 1u);
    memmove(resultat + debut, resultat + fin, n - fin);
    n -= fin - debut;
    break;
  case 3u:                                  /* un passage duplique */
    debut = au_hasard(n);
    fin = debut + au_hasard(n - debut + 1u);
    memmove(resultat + fin + (fin - debut), resultat + fin, n - fin);
    memcpy(resultat + fin, resultat + debut, fin - debut);
    n += fin - debut;
    break;
  default:                                  /* fichier tronque */
    n = au_hasard(n + 1u);
    break;
  }
  *nouvelle_taille = n;
  return resultat;
}

static void test_charger_modeles_abimes(void)
{
  char *repertoire = chemin_dans(travail, "corpus_modele");
  char *chemin = chemin_dans(travail, "modele_valide.txt");
  char *abime = chemin_dans(travail, "modele_abime.txt");
  char *valide;
  char *modifie;
  char *phrase;
  modele_t *m;
  size_t taille;
  size_t nouvelle_taille;
  unsigned int i;
  size_t k;
  unsigned int nb_acceptes = 0u;
  unsigned int nb_essais = 0u;
  /* ordres et ordres de repli des modeles abimes */
  static const unsigned int ORDRES[][2] = {
    { 1u, 0u }, { 2u, 0u }, { 2u, 1u }, { 3u, 0u }, { 3u, 1u }, { 4u, 2u }, { 5u, 3u }
  };
  modele_options_t options = { 1u, false, false, false, 0u };
  const char *chemins[1];

  assert(mkdir(repertoire, 0700) == 0);
  creer_fichier_dans(repertoire, "a.txt",
                     "C'\xC3\xA9tait l'\xC3\xA9t\xC3\xA9. Il a dit : "
                     "peut-\xC3\xAAtre ! M. Dupont va \xC3\xA0 Paris ? "
                     "Oui, monsieur. Le chat dort, le chien aussi.\n"
                     "--Oui, dit-il. Il dit: \xC2\xABViens!\xC2\xBB Le "
                     "chat, \"le chien\", dort. Qui est l\xC3\xA0 ? Elle est "
                     "la reine (ah! oui). Il est l\xC3\xA0.");
  chemins[0] = repertoire;
  for (k = 0u; k < sizeof(ORDRES) / sizeof(ORDRES[0]); k++) {
    options.ordre = ORDRES[k][0];
    options.ordre_repli = ORDRES[k][1];
    m = modele_apprendre_corpus(chemins, 1u, 1u, &options, NULL);
    assert(m != NULL && modele_sauvegarder(m, chemin));
    modele_detruire(&m);
    valide = lire_fichier(chemin);
    assert(valide != NULL);
    taille = strlen(valide);
    for (i = 0u; i < nb_iterations / 6u + 1u; i++) {
      modifie = abimer(valide, taille, &nouvelle_taille);
      nb_essais++;
      ecrire_octets(abime, modifie, nouvelle_taille);
      m = modele_charger(abime, (unsigned int)i);
      if (m != NULL) {
        /* modele accepte : il doit etre utilisable sans risque */
        nb_acceptes++;
        if (modele_peut_generer(m)) {
          phrase = modele_generer_phrase(m);
          assert(phrase != NULL);
          verifier_phrase(m, phrase, true, HACHAGE_ABSENT);
          free(phrase);
          phrase = modele_generer_phrase_depuis(m, modele_nb_mots(m));
          assert(phrase != NULL);
          verifier_phrase(m, phrase, true, modele_nb_mots(m));
          free(phrase);
        }
        modele_detruire(&m);
      } else {
        assert(errno != 0);
      }
      free(modifie);
    }
    free(valide);
  }
  /* la plupart des modifications sont detectees (moins de 7 % des
     modeles abimes sont acceptes avec la graine par defaut) */
  assert(nb_acceptes < nb_essais / 4u);
  supprimer_arborescence(repertoire);
  free(repertoire);
  free(chemin);
  free(abime);
}


/** Mots du detecteur aleatoire : mots-outils de plusieurs langues, mots
    sans profil. */
static const char *const MOTS_DETECTEUR[] = {
  "le", "la", "de", "et", "il", "the", "of", "and", "he", "der", "die",
  "und", "el", "los", "que", "het", "een", "il", "che", "nao", "chat",
  "maison", "river", "zorg", "blax", "a", "s", "qu"
};

static void test_detecteur_de_langue(void)
{
  const size_t nb_mots_possibles = sizeof(MOTS_DETECTEUR) / sizeof(MOTS_DETECTEUR[0]);
  static size_t mots_par_phrase[4000];
  langue_detecteur_t *d;
  unsigned int i;
  size_t phrase;
  size_t nb_phrases;
  size_t nb_mots;
  size_t n;
  size_t mots_ecartes;
  size_t phrases_ecartees;
  bool fin;

  for (i = 0u; i < nb_iterations / 30u + 1u; i++) {
    d = langue_detecteur_creer();
    assert(d != NULL);
    nb_phrases = 0u;
    nb_mots = 0u;
    n = au_hasard(3000u);
    mots_par_phrase[0] = 0u;
    while (nb_mots < n) {
      fin = (au_hasard(8u) == 0u);
      assert(langue_detecteur_ajouter_mot(d, MOTS_DETECTEUR[au_hasard(nb_mots_possibles)], fin));
      nb_mots++;
      mots_par_phrase[nb_phrases]++;
      if (au_hasard(200u) == 0u) {
        langue_detecteur_fin_texte(d);      /* termine aussi la phrase */
        fin = true;
      }
      if (fin) {
        nb_phrases++;
        mots_par_phrase[nb_phrases] = 0u;
      }
    }
    assert(langue_detecteur_conclure(d));
    if (mots_par_phrase[nb_phrases] > 0u) {
      nb_phrases++;                         /* terminee par la conclusion */
    }
    assert(langue_detecteur_nb_phrases(d) == nb_phrases);
    assert(langue_detecteur_nb_mots(d) == nb_mots);
    mots_ecartes = 0u;
    phrases_ecartees = 0u;
    for (phrase = 0u; phrase < nb_phrases; phrase++) {
      if (!langue_detecteur_phrase_gardee(d, phrase)) {
        phrases_ecartees++;
        mots_ecartes += mots_par_phrase[phrase];
      }
    }
    assert(langue_detecteur_nb_phrases_ecartees(d) == phrases_ecartees);
    assert(langue_detecteur_nb_mots_ecartes(d) == mots_ecartes);
    assert(langue_detecteur_majoritaire(d) != LANGUE_INCONNUE || phrases_ecartees == 0u);
    langue_detecteur_detruire(&d);
  }
}


int main(int argc, char *argv[])
{
  if (argc > 1) {
    nb_iterations = (unsigned int)strtoul(argv[1], NULL, 10);
    if (nb_iterations == 0u) {
      nb_iterations = 1u;
    }
  }
  if (argc > 2) {
    /* autre graine : explorer d'autres suites aleatoires */
    etat_aleatoire ^= (uint64_t)strtoull(argv[2], NULL, 10);
    if (etat_aleatoire == 0u) {
      etat_aleatoire = 1u;
    }
  }
  travail = creer_repertoire_temporaire();
  printf("Tests aleatoires (%u iterations) :\n", nb_iterations);
  LANCER_TEST(test_iterateur_octets_au_hasard);
  LANCER_TEST(test_chaine_complete);
  LANCER_TEST(test_charger_modeles_abimes);
  LANCER_TEST(test_detecteur_de_langue);
  LANCER_TEST(test_courriers_aleatoires);
  afficher_bilan("aleatoire");
  detruire_repertoire_temporaire(travail);
  return EXIT_SUCCESS;
}
