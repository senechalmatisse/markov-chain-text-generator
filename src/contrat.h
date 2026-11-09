/**
@file contrat.h
Fichier contrat.h

Macros de programmation par contrat, communes a tous les modules.

  - PRECONDITION  : ce que l'appelant doit garantir avant l'appel ;
  - POSTCONDITION : ce que la fonction garantit au retour ;
  - INVARIANT     : propriete toujours vraie d'une structure de donnees.

Une violation de contrat est une ERREUR DE PROGRAMMATION : le programme
s'arrete immediatement (assert, signal SIGABRT) en affichant le fichier,
la ligne et la condition violee, suivie du type de contrat.

Les erreurs d'ENVIRONNEMENT (memoire insuffisante, fichier absent ou
illisible, disque plein, ...) ne sont PAS des violations de contrat : elles
sont signalees par la valeur de retour des fonctions (et par errno), et
doivent etre traitees par l'appelant.

Les verifications sont actives tant que NDEBUG n'est pas defini (c'est le
cas avec le Makefile du projet). Elles sont toutes de cout constant ou
negligeable devant le travail de la fonction verifiee.
*/
#ifndef __CONTRAT_H__
#define __CONTRAT_H__

#include <assert.h>

#define PRECONDITION(condition) assert((condition) && "pre-condition")
#define POSTCONDITION(condition) assert((condition) && "post-condition")
#define INVARIANT(condition) assert((condition) && "invariant")

#endif
