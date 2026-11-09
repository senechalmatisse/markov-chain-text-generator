/**
Fichier outils_tests.h

Outils communs aux tests unitaires. Ce fichier ne contient que des
fonctions "static inline" : il n'ajoute aucun fichier objet a l'edition de
liens des tests (les regles du Makefile restent inchangees).

Le fichier qui l'inclut doit definir _POSIX_C_SOURCE (200809L) avant tout
#include, car on utilise fork, waitpid, mkdtemp, ...

  - LANCER_TEST(f)        execute la fonction de test f et affiche "OK" ;
  - viole_contrat(f)      execute f dans un processus fils et indique si
                          elle a ete arretee par une violation de contrat
                          (assert -> signal SIGABRT) ;
  - creer_repertoire_temporaire, chemin_dans, ecrire_fichier, lire_fichier,
    supprimer_arborescence : gestion de fichiers temporaires.
*/
#ifndef __OUTILS_TESTS_H__
#define __OUTILS_TESTS_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <signal.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

/** Nombre de tests reussis (chaque fichier de test a le sien). */
static unsigned int nb_tests_reussis = 0u;

/** Execute une fonction de test ; un echec arrete le programme (assert). */
#define LANCER_TEST(fonction)                 \
  do {                                        \
    printf("  %-50s ", #fonction);            \
    fflush(stdout);                           \
    fonction();                               \
    nb_tests_reussis++;                       \
    printf("OK\n");                           \
  } while (0)

/** Affiche le bilan d'un fichier de test. */
static inline void afficher_bilan(const char *module)
{
  printf("Module %s : %u tests reussis.\n\n", module, nb_tests_reussis);
}

#ifdef COUVERTURE
/* Mesure de couverture (make couverture) : un fils arrete par assert ne
   passe pas par exit, qui ecrit les compteurs de gcov ; on les ecrit donc
   avant de laisser le signal terminer le fils. */
extern void __gcov_dump(void);

static inline void ecrire_couverture(int signal_recu)
{
  __gcov_dump();
  signal(signal_recu, SIG_DFL);
  raise(signal_recu);
}
#endif

/**
 * Execute fonction dans un processus fils (sortie d'erreur redirigee vers
 * /dev/null) et renvoie true si et seulement si le fils a ete arrete par
 * le signal SIGABRT, c'est-a-dire si un contrat (assert) a ete viole.
 * La memoire allouee par le fils est liberee par le systeme a sa mort.
 */
static inline bool viole_contrat(void (*fonction)(void))
{
  pid_t fils;
  int statut = 0;
  bool viole = false;

  fflush(stdout);
  fflush(stderr);
  fils = fork();
  if (fils == 0) {
    if (freopen("/dev/null", "w", stderr) == NULL) {
      _exit(2);
    }
#ifdef COUVERTURE
    signal(SIGABRT, ecrire_couverture);
#endif
    fonction();
    _exit(0);                     /* aucun contrat viole */
  }
  if (fils > 0 && waitpid(fils, &statut, 0) == fils) {
    viole = WIFSIGNALED(statut) && WTERMSIG(statut) == SIGABRT;
  }
  return viole;
}

/** Cree un repertoire temporaire vide ; renvoie son chemin (a liberer). */
static inline char *creer_repertoire_temporaire(void)
{
  char *chemin = malloc(sizeof("/tmp/test_markov_XXXXXX"));

  assert(chemin != NULL);
  strcpy(chemin, "/tmp/test_markov_XXXXXX");
  assert(mkdtemp(chemin) != NULL);
  return chemin;
}

/** Concatene repertoire + "/" + nom ; renvoie le chemin (a liberer). */
static inline char *chemin_dans(const char *repertoire, const char *nom)
{
  char *chemin = malloc(strlen(repertoire) + strlen(nom) + 2u);

  assert(chemin != NULL);
  strcpy(chemin, repertoire);
  strcat(chemin, "/");
  strcat(chemin, nom);
  return chemin;
}

/** Ecrit taille octets dans un fichier (cree ou ecrase). */
static inline void ecrire_octets(const char *chemin, const char *octets,
                                 size_t taille)
{
  FILE *fichier = fopen(chemin, "wb");

  assert(fichier != NULL);
  assert(fwrite(octets, 1u, taille, fichier) == taille);
  assert(fclose(fichier) == 0);
}

/** Ecrit une chaine dans un fichier (cree ou ecrase). */
static inline void ecrire_fichier(const char *chemin, const char *contenu)
{
  ecrire_octets(chemin, contenu, strlen(contenu));
}

/** Cree le fichier repertoire/nom avec le contenu donne. */
static inline void creer_fichier_dans(const char *repertoire,
                                      const char *nom, const char *contenu)
{
  char *chemin = chemin_dans(repertoire, nom);

  ecrire_fichier(chemin, contenu);
  free(chemin);
}

/** Contenu complet d'un fichier (a liberer), ou NULL s'il est illisible. */
static inline char *lire_fichier(const char *chemin)
{
  FILE *fichier = fopen(chemin, "rb");
  char *contenu = NULL;
  size_t taille = 0u;
  size_t capacite = 256u;
  size_t lus;
  char *agrandi;

  if (fichier != NULL) {
    contenu = malloc(capacite);
    assert(contenu != NULL);
    while ((lus = fread(contenu + taille, 1u, capacite - taille - 1u,
                        fichier)) > 0u) {
      taille += lus;
      if (taille + 1u == capacite) {
        capacite *= 2u;
        agrandi = realloc(contenu, capacite);
        assert(agrandi != NULL);
        contenu = agrandi;
      }
    }
    contenu[taille] = '\0';
    fclose(fichier);
  }
  return contenu;
}

/** Supprime un fichier ou un repertoire et tout son contenu. */
static inline void supprimer_arborescence(const char *chemin)
{
  struct stat informations;
  DIR *repertoire;
  struct dirent *entree;
  char *sous_chemin;

  if (lstat(chemin, &informations) == 0) {
    if (S_ISDIR(informations.st_mode)) {
      chmod(chemin, 0700);
      repertoire = opendir(chemin);
      if (repertoire != NULL) {
        while ((entree = readdir(repertoire)) != NULL) {
          if (strcmp(entree->d_name, ".") != 0
              && strcmp(entree->d_name, "..") != 0) {
            sous_chemin = chemin_dans(chemin, entree->d_name);
            supprimer_arborescence(sous_chemin);
            free(sous_chemin);
          }
        }
        closedir(repertoire);
      }
      rmdir(chemin);
    } else {
      unlink(chemin);
    }
  }
}

/** Supprime une arborescence temporaire et libere son chemin. */
static inline void detruire_repertoire_temporaire(char *chemin)
{
  supprimer_arborescence(chemin);
  free(chemin);
}

/** Valeur absolue d'une difference de reels (evite de dependre de libm). */
static inline double ecart(double a, double b)
{
  return (a > b) ? (a - b) : (b - a);
}

#endif
