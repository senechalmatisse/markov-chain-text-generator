/**
@file sauvegarde.h
Fichier sauvegarde.h

Ecriture de fichiers texte.

Les chemins peuvent etre relatifs (au repertoire courant) ou absolus.
Toutes les erreurs liees a l'environnement (repertoire parent inexistant,
droits insuffisants, chemin designant un repertoire, disque plein, ...)
sont signalees par la valeur de retour, et errno indique leur cause.
*/
#ifndef __SAUVEGARDE_H__
#define __SAUVEGARDE_H__

#include <stdio.h>
#include <stdbool.h>

/**
 * @brief  Cree un fichier au chemin donne, contenant exactement le texte
 *         passe en entree. Si le fichier existe deja, son contenu est
 *         remplace.
 * @param  texte   texte a ecrire (eventuellement vide).
 * @param  chemin  chemin relatif ou absolu du fichier a creer.
 * @pre    texte != NULL ; chemin != NULL.
 * @post   en cas de succes : le fichier existe et son contenu est
 *         exactement texte (strlen(texte) octets).
 *         En cas d'echec d'ecriture apres la creation (disque plein, ...),
 *         le fichier incomplet est supprime s'il s'agit d'un fichier
 *         ordinaire (jamais un peripherique comme /dev/full, ni un lien).
 * @return true en cas de succes ; false sinon (chemin vide, repertoire
 *         parent inexistant, chemin designant un repertoire, droits
 *         insuffisants, erreur d'ecriture), errno indiquant la cause.
 * @note   Complexite : O(strlen(texte)).
 */
bool sauvegarde_creer_fichier(const char *texte, const char *chemin);

/**
 * @brief  Ouvre un fichier en mode "ajout" (concatenation) : toutes les
 *         ecritures se feront a la fin du fichier. Le fichier est cree s'il
 *         n'existe pas ; son contenu eventuel est conserve.
 * @param  chemin  chemin relatif ou absolu du fichier.
 * @pre    chemin != NULL.
 * @post   si le resultat est non NULL, il designe un fichier ouvert en
 *         ecriture, en mode ajout.
 * @return le descripteur (flux) du fichier, a fermer avec sauvegarde_fermer ;
 *         NULL en cas d'echec (errno indique la cause).
 * @note   Complexite : O(1).
 */
FILE *sauvegarde_ouvrir_ajout(const char *chemin);

/**
 * @brief  Ecrit une chaine de caracteres a la fin d'un fichier ouvert en
 *         mode ajout.
 * @param  fichier  descripteur obtenu par sauvegarde_ouvrir_ajout.
 * @param  chaine   chaine a ecrire (eventuellement vide).
 * @pre    fichier != NULL ; chaine != NULL ;
 *         fichier est ouvert en ecriture et en mode ajout (verifie).
 * @post   en cas de succes, la chaine (sans son '\0' final) est ajoutee a la
 *         suite du contenu du fichier. Les ecritures sont mises en tampon :
 *         elles sont garanties sur le disque apres sauvegarde_fermer.
 * @return true en cas de succes, false en cas d'erreur d'ecriture.
 * @note   Complexite : O(strlen(chaine)).
 */
bool sauvegarde_ecrire(FILE *fichier, const char *chaine);

/**
 * @brief  Ferme un fichier ouvert par sauvegarde_ouvrir_ajout, apres avoir
 *         ecrit les donnees encore en tampon.
 * @param  fichier  descripteur a fermer.
 * @pre    fichier != NULL ; fichier n'a pas deja ete ferme.
 * @post   le descripteur est libere et ne doit plus etre utilise
 *         (meme en cas d'echec).
 * @return true si toutes les donnees ont ete ecrites, false sinon.
 * @note   Complexite : O(taille du tampon).
 */
bool sauvegarde_fermer(FILE *fichier);

/**
 * @brief  Indique si un chemin designe une entree existante du systeme de
 *         fichiers (fichier, repertoire, lien, ...).
 * @param  chemin  chemin relatif ou absolu.
 * @pre    chemin != NULL.
 * @return true si l'entree existe, false sinon.
 * @note   Complexite : O(1).
 */
bool sauvegarde_existe(const char *chemin);

/**
 * @brief  Supprime un fichier ordinaire. Un repertoire, un lien symbolique,
 *         un peripherique ou un tube n'est jamais supprime.
 * @param  chemin  chemin relatif ou absolu du fichier.
 * @pre    chemin != NULL.
 * @post   en cas de succes, !sauvegarde_existe(chemin).
 * @return true si le fichier a ete supprime ; false sinon : il n'existe pas
 *         (ENOENT), ce n'est pas un fichier ordinaire (EPERM), ou il ne peut
 *         pas etre supprime (errno indique la cause).
 * @note   Complexite : O(1).
 */
bool sauvegarde_supprimer(const char *chemin);

#endif
