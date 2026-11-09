/**
@file main.c
Fichier main.c

Programme principal : apprend un modele de langue (chaine de Markov) a
partir des fichiers texte d'un repertoire, ou charge un modele sauvegarde,
genere un texte aleatoire et le sauvegarde dans un nouveau fichier (le
texte est aussi affiche sur la sortie standard).

@verbatim
Usage :
  ./projet [options] <repertoire> [graine] [nb_phrases] [fichier_sortie]
  ./projet [options] -m <modele>
Options :
  -g <graine>   graine du generateur aleatoire (defaut : l'heure)
  -n <nombre>   nombre de phrases a generer (defaut : 10)
  -o <fichier>  fichier a creer (defaut : texte_genere_<graine>.txt dans le
                repertoire courant, jamais ecrase : un suffixe _2, _3, ...
                est ajoute si ce nom existe deja)
  -k <ordre>    ordre de la chaine : 1 (defaut) ou 2
  -r            lire aussi les sous-repertoires
  -s <fichier>  sauvegarder le modele appris dans ce fichier
  -m <fichier>  charger un modele sauvegarde au lieu d'apprendre
  -h, --aide    afficher l'aide
@endverbatim
Les arguments places apres le repertoire (graine, nombre de phrases,
fichier de sortie) restent acceptes : "./projet dataset 42 5" fonctionne.

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
  const char *repertoire;       /* corpus a apprendre (NULL avec -m) */
  const char *modele_charge;    /* -m */
  const char *modele_sauve;     /* -s */
  const char *fichier_sortie;   /* -o ; NULL : nom choisi automatiquement */
  unsigned int graine;
  unsigned int nb_phrases;
  unsigned int ordre;
  bool recursif;
  bool graine_donnee;
  bool nb_phrases_donne;
  bool ordre_donne;
} parametres_t;


/* ======================================================================== */
/*                         Ligne de commande                                */
/* ======================================================================== */

static void afficher_usage(FILE *flux, const char *programme)
{
  fprintf(flux,
          "Usage : %s [options] <repertoire> [graine] [nb_phrases] "
          "[fichier_sortie]\n"
          "        %s [options] -m <modele>\n"
          "Options :\n"
          "  -g <graine>   graine du generateur aleatoire (defaut : l'heure)\n"
          "  -n <nombre>   nombre de phrases a generer (defaut : %u)\n"
          "  -o <fichier>  fichier a creer (defaut : %s<graine>%s)\n"
          "  -k <ordre>    ordre de la chaine de Markov : 1 (defaut) ou 2\n"
          "  -r            lire aussi les sous-repertoires\n"
          "  -s <fichier>  sauvegarder le modele appris dans ce fichier\n"
          "  -m <fichier>  charger un modele sauvegarde au lieu d'apprendre\n"
          "  -h, --aide    afficher cette aide\n",
          programme, programme, NB_PHRASES_DEFAUT, PREFIXE_SORTIE, EXTENSION_SORTIE);
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

/** Applique une option a valeur (-g, -n, -o, -k, -s, -m). */
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
    analyse = lire_fichier(&p->modele_charge, valeur,
                           "fichier de modele (-m)");
    break;
  default:                                  /* 'k' */
    if (p->ordre_donne || !lire_entier(valeur, 1u, 2u, &p->ordre)) {
      fprintf(stderr, "Erreur : ordre invalide ou donne deux fois \"%s\" (1 "
              "ou 2 attendu).\n", valeur);
      analyse = ARGUMENTS_INVALIDES;
    }
    p->ordre_donne = true;
    break;
  }
  return analyse;
}

/** Applique l'argument positionnel de rang donne. */
static analyse_t appliquer_positionnel(parametres_t *p, unsigned int rang, const char *valeur)
{
  analyse_t analyse = ARGUMENTS_INVALIDES;

  switch (rang) {
  case 0u:
    p->repertoire = valeur;
    analyse = ARGUMENTS_VALIDES;
    break;
  case 1u:
    analyse = lire_graine(p, valeur);
    break;
  case 2u:
    analyse = lire_nb_phrases(p, valeur);
    break;
  case 3u:
    analyse = lire_fichier(&p->fichier_sortie, valeur, "fichier de sortie");
    break;
  default:
    fprintf(stderr, "Erreur : trop d'arguments (\"%s\").\n", valeur);
    break;
  }
  return analyse;
}

static analyse_t analyser_arguments(int argc, char *argv[], parametres_t *p)
{
  analyse_t analyse = ARGUMENTS_VALIDES;
  unsigned int nb_positionnels = 0u;
  bool options_finies = false;
  const char *argument;
  int i;

  p->repertoire = NULL;
  p->modele_charge = NULL;
  p->modele_sauve = NULL;
  p->fichier_sortie = NULL;
  p->graine = (unsigned int)time(NULL);
  p->nb_phrases = NB_PHRASES_DEFAUT;
  p->ordre = 1u;
  p->recursif = false;
  p->graine_donnee = false;
  p->nb_phrases_donne = false;
  p->ordre_donne = false;

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
      } else if (argument[2] == '\0' && strchr("gnoksm", argument[1]) != NULL) {
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
      analyse = appliquer_positionnel(p, nb_positionnels, argument);
      nb_positionnels++;
    }
  }
  /* coherence : un corpus OU un modele */
  if (analyse == ARGUMENTS_VALIDES) {
    if (p->modele_charge == NULL && p->repertoire == NULL) {
      fprintf(stderr, "Erreur : repertoire du corpus (ou -m <modele>) "
              "attendu.\n");
      analyse = ARGUMENTS_INVALIDES;
    } else if (p->modele_charge != NULL && p->repertoire != NULL) {
      fprintf(stderr, "Erreur : -m charge un modele, pas de repertoire a "
              "donner (\"%s\").\n", p->repertoire);
      analyse = ARGUMENTS_INVALIDES;
    } else if (p->modele_charge != NULL && (p->ordre_donne || p->recursif)) {
      fprintf(stderr, "Erreur : -k et -r concernent l'apprentissage, pas le "
              "chargement d'un modele.\n");
      analyse = ARGUMENTS_INVALIDES;
    }
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
    snprintf(chemin, taille, "%s%u%s", PREFIXE_SORTIE, graine,
             EXTENSION_SORTIE);
    while (chemin != NULL && sauvegarde_existe(chemin)) {
      if (numero == UINT_MAX) {
        free(chemin);
        chemin = NULL;
        errno = EEXIST;
      } else {
        snprintf(chemin, taille, "%s%u_%u%s", PREFIXE_SORTIE, graine, numero,
                 EXTENSION_SORTIE);
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
 * par ligne. Renvoie false en cas d'erreur (errno indique la cause).
 */
static bool sauvegarder_texte(modele_t *modele, unsigned int nb_phrases, const char *chemin)
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
    phrase = modele_generer_phrase(modele);
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

/** Message d'erreur d'un echec d'apprentissage ou de chargement. */
static void signaler_echec_modele(const parametres_t *p, int erreur)
{
  if (p->modele_charge != NULL) {
    fprintf(stderr, "Erreur : impossible de charger le modele \"%s\" : %s.\n",
            p->modele_charge,
            (erreur == EINVAL) ? "fichier de modele invalide"
                               : strerror(erreur));
  } else {
    fprintf(stderr, "Erreur : impossible d'apprendre a partir du "
            "repertoire \"%s\" : %s.\n", p->repertoire, strerror(erreur));
  }
}

/** Execute le programme ; toute la memoire allouee est liberee. */
static int executer(const parametres_t *p)
{
  modele_t *modele;
  char *chemin_automatique = NULL;
  const char *chemin = p->fichier_sortie;
  int code = EXIT_FAILURE;

  /* 1. Apprentissage ou chargement */
  if (p->modele_charge != NULL) {
    modele = modele_charger(p->modele_charge, p->graine);
  } else {
    modele = modele_apprendre(p->repertoire, p->graine, p->ordre, p->recursif);
  }
  if (modele == NULL) {
    signaler_echec_modele(p, errno);
  } else if (!modele_peut_generer(modele)) {
    fprintf(stderr, "Erreur : aucun mot trouve dans le repertoire "
            "\"%s\".\n", p->repertoire);
  } else {
    if (p->modele_charge != NULL) {
      printf("Modele charge depuis \"%s\" : %u mots distincts (chaine "
             "d'ordre %u).\n", p->modele_charge, modele_nb_mots(modele),
             modele_ordre(modele));
    } else {
      printf("Apprentissage : %u mots distincts dans \"%s\"%s (chaine "
             "d'ordre %u).\n", modele_nb_mots(modele), p->repertoire,
             p->recursif ? " et ses sous-repertoires" : "",
             modele_ordre(modele));
    }
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
      if (sauvegarder_texte(modele, p->nb_phrases, chemin)) {
        printf("\nTexte de %u phrase(s) sauvegarde dans \"%s\".\n",
               p->nb_phrases, chemin);
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
  return code;
}
