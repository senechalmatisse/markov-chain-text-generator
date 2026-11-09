/**
Fichier sauvegarde.c

Implementation de l'ecriture de fichiers (cf. sauvegarde.h).
Les fonctions POSIX fileno, fcntl et lstat servent a verifier les contrats
et a tester l'existence d'un chemin.
*/
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "contrat.h"
#include "sauvegarde.h"

/**
 * Indique si un flux est ouvert en ecriture et en mode ajout, en consultant
 * les attributs du descripteur systeme sous-jacent.
 */
static bool est_ouvert_en_ajout(FILE *fichier)
{
  int descripteur = fileno(fichier);
  int attributs = -1;

  if (descripteur >= 0) {
    attributs = fcntl(descripteur, F_GETFL);
  }
  return attributs != -1
         && (attributs & O_APPEND) != 0
         && (attributs & O_ACCMODE) != O_RDONLY;
}

bool sauvegarde_creer_fichier(const char *texte, const char *chemin)
{
  FILE *fichier;
  size_t longueur;
  bool succes = false;
  int erreur;

  PRECONDITION(texte != NULL);
  PRECONDITION(chemin != NULL);

  fichier = fopen(chemin, "w");
  if (fichier != NULL) {
    longueur = strlen(texte);
    succes = (fwrite(texte, 1u, longueur, fichier) == longueur);
    erreur = errno;
    /* fclose ecrit le tampon : une erreur peut n'apparaitre qu'ici */
    if (fclose(fichier) != 0) {
      if (succes) {
        erreur = errno;
      }
      succes = false;
    }
    if (!succes) {
      /* pas de fichier incomplet (mais jamais de peripherique supprime) */
      sauvegarde_supprimer(chemin);
      errno = erreur;
    }
  }
  return succes;
}

FILE *sauvegarde_ouvrir_ajout(const char *chemin)
{
  FILE *fichier;

  PRECONDITION(chemin != NULL);

  fichier = fopen(chemin, "a");
  POSTCONDITION(fichier == NULL || est_ouvert_en_ajout(fichier));
  return fichier;
}

bool sauvegarde_ecrire(FILE *fichier, const char *chaine)
{
  PRECONDITION(fichier != NULL);
  PRECONDITION(chaine != NULL);
  PRECONDITION(est_ouvert_en_ajout(fichier));

  /* fputs renvoie EOF en cas d'erreur (y compris pour une chaine vide si
     le flux est deja en erreur) */
  return fputs(chaine, fichier) != EOF && !ferror(fichier);
}

bool sauvegarde_fermer(FILE *fichier)
{
  PRECONDITION(fichier != NULL);
  return fclose(fichier) == 0;
}

bool sauvegarde_existe(const char *chemin)
{
  struct stat informations;

  PRECONDITION(chemin != NULL);
  /* lstat : un lien symbolique casse existe aussi (il occupe le nom) */
  return lstat(chemin, &informations) == 0;
}

bool sauvegarde_supprimer(const char *chemin)
{
  struct stat informations;
  bool succes = false;

  PRECONDITION(chemin != NULL);
  if (lstat(chemin, &informations) == 0) {
    if (!S_ISREG(informations.st_mode)) {
      errno = EPERM;
    } else {
      succes = (remove(chemin) == 0);
    }
  }
  POSTCONDITION(!succes || !sauvegarde_existe(chemin));
  return succes;
}
