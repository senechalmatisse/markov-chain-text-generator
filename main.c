/**
@file main.c
Fichier main.c

Programme principal : apprend un modele de langue (chaine de Markov) a
partir d'un corpus (fichiers texte et repertoires de fichiers texte), ou
charge un modele sauvegarde, genere un texte aleatoire et le sauvegarde
dans un nouveau fichier (le texte est aussi affiche sur la sortie
standard).

@verbatim
Usage :
  ./projet [options] <corpus> [<corpus> ...]
  ./projet [options] <repertoire> <graine> [nb_phrases] [fichier_sortie]
  ./projet [options] -m <modele>
Options :
  -g <graine>   graine du generateur aleatoire (defaut : l'heure)
  -n <nombre>   nombre de phrases a generer (defaut : 10)
  -o <fichier>  fichier a creer (defaut : texte_genere_<graine>.txt dans le
                repertoire courant, jamais ecrase : un suffixe _2, _3, ...
                est ajoute si ce nom existe deja)
  -p <mot>      faire commencer chaque phrase par ce mot (accents et
                majuscules facultatifs)
  -k <ordre>    ordre de la chaine : de 1 (defaut) a 8
  -b <ordre>    ordre de repli, inferieur a celui de la chaine : quand le
                contexte n'a qu'un successeur dans le corpus, le mot
                suivant est tire a cet ordre (defaut : 0, sans repli)
  -r            lire aussi les sous-repertoires
  -l            une phrase par ligne (poemes, chansons, listes)
  -t            tout garder : ne pas ecarter les passages ecrits dans une
                autre langue que la langue majoritaire du corpus
  -s <fichier>  sauvegarder le modele appris dans ce fichier
  -m <fichier>  charger un modele sauvegarde au lieu d'apprendre
  -h, --aide    afficher l'aide
@endverbatim
Un corpus est un fichier texte ou un repertoire de fichiers texte ; on peut
en donner plusieurs. Un courrier electronique (message ".eml" ou boite
mbox) est reconnu : seul le texte de ses messages est lu, sans en-tetes,
citations ni signatures (cf. mail.h). L'ancienne syntaxe reste acceptee : si le deuxieme
argument est un nombre, c'est la graine, suivie eventuellement du nombre de
phrases et du fichier de sortie ("./projet dataset 42 5" fonctionne). Pour
un repertoire dont le nom est un nombre, ecrire "./42".

Code de retour : EXIT_SUCCESS si le texte a ete genere et sauvegarde,
EXIT_FAILURE sinon (un message d'erreur est alors affiche).
*/
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include "src/hachage.h"
#include "src/markov.h"
#include "src/repertoire.h"
#include "src/sauvegarde.h"
#include "src/langue.h"
#include "src/modele.h"

/** Nombre de phrases generees par defaut. */
#define NB_PHRASES_DEFAUT 10u

/** Nom par defaut du fichier genere : PREFIXE<graine>[_n]EXTENSION. */
#define PREFIXE_SORTIE "texte_genere_"
#define EXTENSION_SORTIE ".txt"

/** Resultat de l'analyse de la ligne de commande. */
typedef enum {
  ARGUMENTS_VALIDES,
  ARGUMENTS_AIDE,
  ARGUMENTS_INVALIDES
} analyse_t;

/** Parametres du programme. */
typedef struct {
  const char **chemins;         /* arguments positionnels (tableau alloue) */
  unsigned int nb_chemins;      /* chemins du corpus (0 avec -m) */
  const char *modele_charge;    /* -m */
  const char *modele_sauve;     /* -s */
  const char *fichier_sortie;   /* -o ; NULL : nom choisi automatiquement */
  const char *premier_mot;      /* -p ; NULL : premier mot tire au hasard */
  unsigned int graine;
  unsigned int nb_phrases;
  unsigned int ordre;
  unsigned int ordre_repli;     /* -b */
  bool recursif;                /* -r */
  bool phrase_par_ligne;        /* -l */
  bool tout_garder;             /* -t */
  bool graine_donnee;
  bool nb_phrases_donne;
  bool ordre_donne;
  bool ordre_repli_donne;
} parametres_t;


/* ======================================================================== */
/*                         Ligne de commande                                */
/* ======================================================================== */

static void afficher_usage(FILE *flux, const char *programme)
{
  fprintf(flux,
          "Usage : %s [options] <corpus> [<corpus> ...]\n"
          "        %s [options] <repertoire> <graine> [nb_phrases] "
          "[fichier_sortie]\n"
          "        %s [options] -m <modele>\n"
          "Un corpus est un fichier texte ou un repertoire de fichiers "
          "texte ;\n"
          "les courriers (.eml, mbox) sont reduits au texte de leurs "
          "messages.\n"
          "Options :\n"
          "  -g <graine>   graine du generateur aleatoire (defaut : l'heure)\n"
          "  -n <nombre>   nombre de phrases a generer (defaut : %u)\n"
          "  -o <fichier>  fichier a creer (defaut : %s<graine>%s)\n"
          "  -p <mot>      faire commencer chaque phrase par ce mot\n"
          "  -k <ordre>    ordre de la chaine de Markov, de 1 (defaut) a %u\n"
          "  -b <ordre>    ordre de repli, inferieur a -k : quand le contexte "
          "n'a qu'un\n"
          "                successeur, le mot suivant est tire a cet ordre "
          "(defaut : 0)\n"
          "  -r            lire aussi les sous-repertoires\n"
          "  -l            une phrase par ligne (poemes, chansons, listes)\n"
          "  -t            tout garder : ne pas ecarter les passages ecrits "
          "dans une\n"
          "                autre langue que la langue majoritaire du "
          "corpus\n"
          "  -s <fichier>  sauvegarder le modele appris dans ce fichier\n"
          "  -m <fichier>  charger un modele sauvegarde au lieu d'apprendre\n"
          "  -h, --aide    afficher cette aide\n",
          programme, programme, programme, NB_PHRASES_DEFAUT, PREFIXE_SORTIE,
          EXTENSION_SORTIE, MODELE_ORDRE_MAX);
}

/**
 * Convertit un texte en entier non signe : uniquement des chiffres
 * decimaux, valeur dans [minimum, maximum]. Renvoie false sinon.
 */
static bool lire_entier(const char *texte, unsigned int minimum,
                        unsigned int maximum, unsigned int *valeur)
{
  const char *c;
  unsigned long long nombre = 0u;
  bool valide = (texte[0] != '\0');

  for (c = texte; valide && *c != '\0'; c++) {
    valide = (*c >= '0' && *c <= '9');
    if (valide) {
      nombre = (10u * nombre) + (unsigned long long)(*c - '0');
      valide = (nombre <= UINT_MAX);
    }
  }
  if (valide && nombre >= minimum && nombre <= maximum) {
    *valeur = (unsigned int)nombre;
  } else {
    valide = false;
  }
  return valide;
}

/** Indique si un texte est fait uniquement de chiffres decimaux. */
static bool est_nombre(const char *texte)
{
  const char *c;
  bool nombre = (texte[0] != '\0');

  for (c = texte; nombre && *c != '\0'; c++) {
    nombre = (*c >= '0' && *c <= '9');
  }
  return nombre;
}

/** Enregistre un nom de fichier (non vide, donne une seule fois). */
static analyse_t lire_fichier(const char **destination, const char *valeur, const char *nom)
{
  analyse_t analyse = ARGUMENTS_VALIDES;

  if (*destination != NULL) {
    fprintf(stderr, "Erreur : %s donne deux fois.\n", nom);
    analyse = ARGUMENTS_INVALIDES;
  } else if (valeur[0] == '\0') {
    fprintf(stderr, "Erreur : %s vide.\n", nom);
    analyse = ARGUMENTS_INVALIDES;
  } else {
    *destination = valeur;
  }
  return analyse;
}

/** Enregistre la graine (donnee une seule fois). */
static analyse_t lire_graine(parametres_t *p, const char *valeur)
{
  analyse_t analyse = ARGUMENTS_VALIDES;

  if (p->graine_donnee) {
    fprintf(stderr, "Erreur : graine donnee deux fois.\n");
    analyse = ARGUMENTS_INVALIDES;
  } else if (!lire_entier(valeur, 0u, UINT_MAX, &p->graine)) {
    fprintf(stderr, "Erreur : graine invalide \"%s\" (entier de 0 a %u "
            "attendu).\n", valeur, UINT_MAX);
    analyse = ARGUMENTS_INVALIDES;
  } else {
    p->graine_donnee = true;
  }
  return analyse;
}

/** Enregistre le nombre de phrases (donne une seule fois). */
static analyse_t lire_nb_phrases(parametres_t *p, const char *valeur)
{
  analyse_t analyse = ARGUMENTS_VALIDES;

  if (p->nb_phrases_donne) {
    fprintf(stderr, "Erreur : nombre de phrases donne deux fois.\n");
    analyse = ARGUMENTS_INVALIDES;
  } else if (!lire_entier(valeur, 1u, UINT_MAX, &p->nb_phrases)) {
    fprintf(stderr, "Erreur : nombre de phrases invalide \"%s\" (entier de "
            "1 a %u attendu).\n", valeur, UINT_MAX);
    analyse = ARGUMENTS_INVALIDES;
  } else {
    p->nb_phrases_donne = true;
  }
  return analyse;
}

/** Enregistre un ordre (-k ou -b, donne une seule fois). */
static analyse_t lire_ordre(unsigned int *ordre, bool *donne, unsigned int minimum,
                            const char *valeur, const char *nom)
{
  analyse_t analyse = ARGUMENTS_VALIDES;

  if (*donne || !lire_entier(valeur, minimum, MODELE_ORDRE_MAX, ordre)) {
    fprintf(stderr, "Erreur : %s invalide ou donne deux fois \"%s\" "
            "(entier de %u a %u attendu).\n", nom, valeur, minimum, MODELE_ORDRE_MAX);
    analyse = ARGUMENTS_INVALIDES;
  }
  *donne = true;
  return analyse;
}

/** Applique une option a valeur (-g, -n, -o, -p, -k, -b, -s, -m). */
static analyse_t appliquer_option(parametres_t *p, char option, const char *valeur)
{
  analyse_t analyse = ARGUMENTS_VALIDES;

  switch (option) {
  case 'g':
    analyse = lire_graine(p, valeur);
    break;
  case 'n':
    analyse = lire_nb_phrases(p, valeur);
    break;
  case 'o':
    analyse = lire_fichier(&p->fichier_sortie, valeur, "fichier de sortie");
    break;
  case 's':
    analyse = lire_fichier(&p->modele_sauve, valeur, "fichier de modele (-s)");
    break;
  case 'm':
    analyse = lire_fichier(&p->modele_charge, valeur, "fichier de modele (-m)");
    break;
  case 'p':
    analyse = lire_fichier(&p->premier_mot, valeur, "premier mot (-p)");
    break;
  case 'b':
    analyse = lire_ordre(&p->ordre_repli, &p->ordre_repli_donne, 0u, valeur, "ordre de repli");
    break;
  default:                                  /* 'k' */
    analyse = lire_ordre(&p->ordre, &p->ordre_donne, 1u, valeur, "ordre");
    break;
  }
  return analyse;
}

/**
 * Interprete les arguments positionnels : tous des chemins du corpus, sauf
 * dans l'ancienne syntaxe (deuxieme argument numerique) ou ils sont le
 * repertoire, la graine, le nombre de phrases et le fichier de sortie.
 */
static analyse_t interpreter_positionnels(parametres_t *p, unsigned int nb_positionnels)
{
  analyse_t analyse = ARGUMENTS_VALIDES;

  if (nb_positionnels >= 2u && est_nombre(p->chemins[1])) {
    p->nb_chemins = 1u;
    analyse = lire_graine(p, p->chemins[1]);
    if (analyse == ARGUMENTS_VALIDES && nb_positionnels >= 3u) {
      analyse = lire_nb_phrases(p, p->chemins[2]);
    }
    if (analyse == ARGUMENTS_VALIDES && nb_positionnels >= 4u) {
      analyse = lire_fichier(&p->fichier_sortie, p->chemins[3], "fichier de sortie");
    }
    if (analyse == ARGUMENTS_VALIDES && nb_positionnels >= 5u) {
      fprintf(stderr, "Erreur : trop d'arguments (\"%s\").\n", p->chemins[4]);
      analyse = ARGUMENTS_INVALIDES;
    }
  } else {
    p->nb_chemins = nb_positionnels;
  }
  return analyse;
}

/** Verifie que les arguments ne se contredisent pas. */
static analyse_t verifier_coherence(const parametres_t *p)
{
  analyse_t analyse = ARGUMENTS_INVALIDES;

  if (p->modele_charge == NULL && p->nb_chemins == 0u) {
    fprintf(stderr, "Erreur : corpus (fichiers ou repertoires) ou -m "
            "<modele> attendu.\n");
  } else if (p->modele_charge != NULL && p->nb_chemins > 0u) {
    fprintf(stderr, "Erreur : -m charge un modele, pas de corpus a donner "
            "(\"%s\").\n", p->chemins[0]);
  } else if (p->modele_charge != NULL && (p->ordre_donne || p->ordre_repli_donne || p->recursif
                 || p->phrase_par_ligne || p->tout_garder)) {
    fprintf(stderr, "Erreur : -k, -b, -r, -l et -t concernent "
            "l'apprentissage, pas le chargement d'un modele.\n");
  } else if (p->ordre_repli >= p->ordre) {
    fprintf(stderr, "Erreur : l'ordre de repli (-b %u) doit etre inferieur "
            "a l'ordre de la chaine (-k %u).\n", p->ordre_repli, p->ordre);
  } else {
    analyse = ARGUMENTS_VALIDES;
  }
  return analyse;
}

/**
 * Analyse la ligne de commande et remplit p. Le tableau p->chemins est
 * alloue (a liberer avec free, meme en cas d'erreur).
 */
static analyse_t analyser_arguments(int argc, char *argv[], parametres_t *p)
{
  analyse_t analyse = ARGUMENTS_VALIDES;
  unsigned int nb_positionnels = 0u;
  bool options_finies = false;
  const char *argument;
  int i;

  p->nb_chemins = 0u;
  p->modele_charge = NULL;
  p->modele_sauve = NULL;
  p->fichier_sortie = NULL;
  p->premier_mot = NULL;
  p->graine = (unsigned int)time(NULL);
  p->nb_phrases = NB_PHRASES_DEFAUT;
  p->ordre = 1u;
  p->ordre_repli = 0u;
  p->recursif = false;
  p->phrase_par_ligne = false;
  p->tout_garder = false;
  p->graine_donnee = false;
  p->nb_phrases_donne = false;
  p->ordre_donne = false;
  p->ordre_repli_donne = false;
  /* au plus argc - 1 arguments positionnels */
  p->chemins = malloc(((argc > 1) ? (size_t)argc : 1u) * sizeof(const char *));
  if (p->chemins == NULL) {
    fprintf(stderr, "Erreur : %s.\n", strerror(ENOMEM));
    analyse = ARGUMENTS_INVALIDES;
  }

  for (i = 1; analyse == ARGUMENTS_VALIDES && i < argc; i++) {
    argument = argv[i];
    if (!options_finies && argument[0] == '-' && argument[1] != '\0') {
      if (strcmp(argument, "--") == 0) {
        options_finies = true;
      } else if (strcmp(argument, "-h") == 0 || strcmp(argument, "--aide") == 0
                 || strcmp(argument, "--help") == 0) {
        analyse = ARGUMENTS_AIDE;
      } else if (strcmp(argument, "-r") == 0) {
        p->recursif = true;
      } else if (strcmp(argument, "-l") == 0) {
        p->phrase_par_ligne = true;
      } else if (strcmp(argument, "-t") == 0) {
        p->tout_garder = true;
      } else if (argument[2] == '\0' && strchr("gnokbpsm", argument[1]) != NULL) {
        if (i + 1 >= argc) {
          fprintf(stderr, "Erreur : valeur attendue apres %s.\n", argument);
          analyse = ARGUMENTS_INVALIDES;
        } else {
          i++;
          analyse = appliquer_option(p, argument[1], argv[i]);
        }
      } else {
        fprintf(stderr, "Erreur : option inconnue \"%s\".\n", argument);
        analyse = ARGUMENTS_INVALIDES;
      }
    } else {
      p->chemins[nb_positionnels] = argument;
      nb_positionnels++;
    }
  }
  if (analyse == ARGUMENTS_VALIDES) {
    analyse = interpreter_positionnels(p, nb_positionnels);
  }
  if (analyse == ARGUMENTS_VALIDES) {
    analyse = verifier_coherence(p);
  }
  return analyse;
}


/* ======================================================================== */
/*                         Generation du texte                              */
/* ======================================================================== */

/**
 * Choisit un nom de fichier qui n'existe pas encore :
 * texte_genere_<graine>.txt, sinon texte_genere_<graine>_2.txt, etc.
 * Renvoie le nom (a liberer), ou NULL si la memoire manque ou si tous les
 * noms sont pris.
 */
static char *choisir_fichier_sortie(unsigned int graine)
{
  size_t taille = sizeof(PREFIXE_SORTIE) + sizeof(EXTENSION_SORTIE) + 24u;
  char *chemin = malloc(taille);
  unsigned int numero = 2u;

  if (chemin != NULL) {
    snprintf(chemin, taille, "%s%u%s", PREFIXE_SORTIE, graine, EXTENSION_SORTIE);
    while (chemin != NULL && sauvegarde_existe(chemin)) {
      if (numero == UINT_MAX) {
        free(chemin);
        chemin = NULL;
        errno = EEXIST;
      } else {
        snprintf(chemin, taille, "%s%u_%u%s", PREFIXE_SORTIE, graine, numero, EXTENSION_SORTIE);
        numero++;
      }
    }
  }
  return chemin;
}

/** Ecrit un morceau de texte dans le fichier et sur la sortie standard. */
static bool ecrire(FILE *sortie, const char *texte)
{
  fputs(texte, stdout);
  return sauvegarde_ecrire(sortie, texte);
}

/**
 * Cree le fichier de sortie (vide) puis y ajoute les phrases generees, une
 * par ligne, chacune commencant par premier_mot s'il est different de
 * HACHAGE_ABSENT. Renvoie false en cas d'erreur (errno indique la cause).
 */
static bool sauvegarder_texte(modele_t *modele, unsigned int nb_phrases,
                              unsigned int premier_mot, const char *chemin)
{
  FILE *sortie = NULL;
  char *phrase;
  unsigned int i;
  bool succes;
  int erreur = 0;

  /* nouveau fichier vide (ecrase un fichier explicitement designe) */
  succes = sauvegarde_creer_fichier("", chemin);
  if (succes) {
    sortie = sauvegarde_ouvrir_ajout(chemin);
    succes = (sortie != NULL);
  }
  for (i = 0u; succes && i < nb_phrases; i++) {
    phrase = (premier_mot == HACHAGE_ABSENT)
             ? modele_generer_phrase(modele)
             : modele_generer_phrase_depuis(modele, premier_mot);
    succes = (phrase != NULL) && ecrire(sortie, phrase) && ecrire(sortie, "\n");
    free(phrase);
  }
  if (!succes) {
    erreur = errno;
  }
  if (sortie != NULL && !sauvegarde_fermer(sortie) && succes) {
    erreur = errno;
    succes = false;
  }
  if (!succes) {
    errno = erreur;
  }
  return succes;
}

/** Affiche les chemins du corpus, entre guillemets et separes par des virgules. */
static void afficher_chemins(FILE *flux, const parametres_t *p)
{
  unsigned int i;

  for (i = 0u; i < p->nb_chemins; i++) {
    fprintf(flux, "%s\"%s\"", (i == 0u) ? "" : ", ", p->chemins[i]);
  }
}

/** Message d'erreur d'un echec d'apprentissage ou de chargement. */
static void signaler_echec_modele(const parametres_t *p, int erreur)
{
  if (p->modele_charge != NULL) {
    fprintf(stderr, "Erreur : impossible de charger le modele \"%s\" : %s.\n",
            p->modele_charge,
            (erreur == EINVAL) ? "fichier de modele invalide" : strerror(erreur));
  } else {
    fprintf(stderr, "Erreur : impossible d'apprendre a partir de ");
    afficher_chemins(stderr, p);
    fprintf(stderr, " : %s.\n", (erreur == EINVAL)
                                ? "ni fichier ordinaire ni repertoire"
                                : strerror(erreur));
  }
}

/** Affiche l'ordre de la chaine du modele, et celui de son repli. */
static void afficher_ordre(const modele_t *modele)
{
  printf("chaine d'ordre %u", modele_ordre(modele));
  if (modele_ordre_repli(modele) > 0u) {
    printf(", repli a l'ordre %u", modele_ordre_repli(modele));
  }
}

/** Apprend ou charge le modele et affiche le bilan ; NULL en cas d'echec
    (un message d'erreur est alors affiche). */
static modele_t *obtenir_modele(const parametres_t *p)
{
  modele_options_t options;
  repertoire_bilan_t bilan;
  modele_t *modele;

  if (p->modele_charge != NULL) {
    modele = modele_charger(p->modele_charge, p->graine);
    if (modele != NULL) {
      printf("Modele charge depuis \"%s\" : %u mots distincts (", p->modele_charge, modele_nb_mots(modele));
      afficher_ordre(modele);
      printf(").\n");
    }
  } else {
    options.ordre = p->ordre;
    options.recursif = p->recursif;
    options.phrase_par_ligne = p->phrase_par_ligne;
    options.filtrer_langues = !p->tout_garder;
    options.ordre_repli = p->ordre_repli;
    modele = modele_apprendre_corpus(p->chemins, p->nb_chemins, p->graine, &options, &bilan);
    if (modele != NULL) {
      printf("Apprentissage : %zu fichier(s)", bilan.nb_fichiers);
      if (bilan.nb_messages > 0u) {
        printf(" (%zu message(s) electronique(s))", bilan.nb_messages);
      }
      printf(", %zu phrases, %zu mots dont %u distincts (",
             bilan.nb_phrases - bilan.nb_phrases_ecartees,
             bilan.nb_mots - bilan.nb_mots_ecartes, modele_nb_mots(modele));
      afficher_ordre(modele);
      printf(").\n");
      if (bilan.langue != LANGUE_INCONNUE) {
        printf("Langue du corpus : %s ; %zu phrase(s) ecrite(s) dans une "
               "autre langue ecartee(s) (%zu mots).\n",
               langue_nom(bilan.langue), bilan.nb_phrases_ecartees,
               bilan.nb_mots_ecartes);
      } else if (!p->tout_garder) {
        printf("Langue du corpus non reconnue : aucun passage ecarte.\n");
      }
    }
  }
  if (modele == NULL) {
    signaler_echec_modele(p, errno);
  }
  return modele;
}

/**
 * Identifiant du premier mot demande (-p), ou HACHAGE_ABSENT s'il n'y en a
 * pas. Renvoie false (avec un message) si le mot n'est pas utilisable.
 */
static bool trouver_premier_mot(const parametres_t *p, const modele_t *modele, unsigned int *mot)
{
  bool trouve = true;

  *mot = HACHAGE_ABSENT;
  if (p->premier_mot != NULL) {
    *mot = modele_mot(modele, p->premier_mot);
    if (*mot == HACHAGE_ABSENT) {
      trouve = false;
      if (errno == ENOENT) {
        fprintf(stderr, "Erreur : le mot \"%s\" n'apparait pas dans le "
                "corpus.\n", p->premier_mot);
      } else if (errno == EINVAL) {
        fprintf(stderr, "Erreur : \"%s\" n'est pas un mot (des lettres "
                "seulement, sans espace ni apostrophe).\n", p->premier_mot);
      } else {
        fprintf(stderr, "Erreur : %s.\n", strerror(errno));
      }
    }
  }
  return trouve;
}

/** Execute le programme ; toute la memoire allouee est liberee. */
static int executer(const parametres_t *p)
{
  modele_t *modele;
  char *chemin_automatique = NULL;
  const char *chemin = p->fichier_sortie;
  unsigned int premier_mot = HACHAGE_ABSENT;
  int code = EXIT_FAILURE;

  /* 1. Apprentissage ou chargement, puis premier mot eventuel */
  modele = obtenir_modele(p);
  if (modele != NULL && !modele_peut_generer(modele)) {
    fprintf(stderr, "Erreur : aucun mot a apprendre dans %s.\n",
            (p->modele_charge != NULL) ? "le modele" : "le corpus");
  } else if (modele != NULL && trouver_premier_mot(p, modele, &premier_mot)) {
    code = EXIT_SUCCESS;
    /* 2. Sauvegarde eventuelle du modele */
    if (p->modele_sauve != NULL) {
      if (modele_sauvegarder(modele, p->modele_sauve)) {
        printf("Modele sauvegarde dans \"%s\".\n", p->modele_sauve);
      } else {
        fprintf(stderr, "Erreur : impossible de sauvegarder le modele dans "
                "\"%s\" : %s.\n", p->modele_sauve, strerror(errno));
        code = EXIT_FAILURE;
      }
    }
    /* 3. Choix du fichier de sortie, generation et sauvegarde du texte */
    if (code == EXIT_SUCCESS && chemin == NULL) {
      chemin_automatique = choisir_fichier_sortie(p->graine);
      chemin = chemin_automatique;
      if (chemin == NULL) {
        fprintf(stderr, "Erreur : impossible de choisir un fichier de "
                "sortie : %s.\n", strerror(errno));
        code = EXIT_FAILURE;
      }
    }
    if (code == EXIT_SUCCESS) {
      printf("Graine : %u\n\n", p->graine);
      if (sauvegarder_texte(modele, p->nb_phrases, premier_mot, chemin)) {
        printf("\nTexte de %u phrase(s) sauvegarde dans \"%s\".\n", p->nb_phrases, chemin);
      } else {
        fprintf(stderr, "\nErreur : impossible d'ecrire le fichier \"%s\" "
                ": %s.\n", chemin, strerror(errno));
        code = EXIT_FAILURE;
      }
    }
  }
  free(chemin_automatique);
  modele_detruire(&modele);
  return code;
}

int main(int argc, char *argv[])
{
  parametres_t parametres;
  const char *programme = (argc > 0) ? argv[0] : "projet";
  int code;

  switch (analyser_arguments(argc, argv, &parametres)) {
  case ARGUMENTS_AIDE:
    afficher_usage(stdout, programme);
    code = EXIT_SUCCESS;
    break;
  case ARGUMENTS_INVALIDES:
    afficher_usage(stderr, programme);
    code = EXIT_FAILURE;
    break;
  default:
    code = executer(&parametres);
    break;
  }
  free(parametres.chemins);
  return code;
}
