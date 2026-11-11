/**
@file mail.h
Fichier mail.h

Lecture des courriers electroniques : un message (format RFC 5322, fichier
".eml") ou une boite aux lettres au format mbox (messages a la suite,
chacun precede d'une ligne "From ...") peut faire partie du corpus. Seul le
texte ecrit par l'expediteur est garde.

  - EN-TETES : les en-tetes des messages (From, To, Subject, Received...)
    sont ignores ; ceux des parties MIME servent seulement a les decoder.
  - MIME : un corps "multipart" est decoupe selon sa frontiere (parametre
    boundary). Dans une alternative (multipart/alternative), la partie
    text/plain est preferee ; sans elle, la derniere partie est lue. Les
    pieces jointes (Content-Disposition: attachment) et les parties qui ne
    sont ni du texte (text/plain, text/html) ni un message joint
    (message/rfc822, lu comme un message) sont ignorees. Les corps sont
    decodes (quoted-printable, base64) puis convertis en UTF-8 depuis leur
    jeu de caracteres : UTF-8 et US-ASCII sont recopies, ISO-8859-1 et
    Windows-1252 (lus tous deux comme Windows-1252, ainsi que le font les
    navigateurs) et ISO-8859-15 sont convertis ; un autre jeu est recopie
    tel quel (l'iterateur lit alors les octets invalides en Latin-1).
  - HTML : seul le texte est garde. Les balises sont retirees (un saut de
    ligne pour br, div, li, tr ; une ligne vide pour p, h1 a h6, table,
    hr), les entites decodees (&amp;, &eacute;, &#233;, &#xE9; ...), le
    contenu de head, script, style et blockquote (citations des
    messageries) ignore, et les fins de ligne du source deviennent des
    espaces.
  - CITATIONS : les lignes qui commencent par '>' (apres d'eventuelles
    espaces) sont ignorees, ainsi que la ligne qui annonce une citation
    ("Le ... a ecrit :", "On ... wrote:", "... schrieb:") et, si cette
    annonce est coupee en deux lignes, la ligne qui la precede. Apres
    l'annonce d'un message d'origine ou transfere ("-----Original
    Message-----", "-------- Message original --------", "----------
    Forwarded message ---------", "Begin forwarded message:"), la suite de
    la partie est une citation : elle est ignoree. Dans une boite mbox, une
    ligne "From " du texte est protegee par un '>' (convention mboxrd :
    ">From ", et ">From " devient ">>From ") : ce premier '>' est retire
    avant ces regles, sauf dans un corps en base64 ou en HTML.
  - SIGNATURES : apres une ligne "-- " (ou "--"), la suite de la partie est
    une signature ; les lignes "Envoye de mon ...", "Sent from my ..." et
    "Get Outlook for ..." sont ignorees.
Le texte de chaque message est suivi d'une ligne vide : la derniere phrase
d'un message ne se poursuit pas dans le suivant. Les lignes vides
consecutives (paragraphes HTML, lignes retirees) sont reduites a une
seule.

LIMITES : les en-tetes encodes (RFC 2047) ne sont pas decodes, puisqu'ils
sont ignores ; les parametres MIME decoupes ou encodes (RFC 2231 :
boundary*0=, charset*=) ne sont pas reconnus ; une piece jointe inseree dans
le texte meme (uuencode) est lue comme du texte ; dans une boite mbox, une
ligne "From " non protegee qui suit une ligne vide commence un nouveau
message, comme dans tout lecteur mbox, et la boite est lue entierement en
memoire.

Notations utilisees dans les contrats :
  courrier(t, n) = mail_est_courrier(t, min(n, MAIL_TAILLE_DETECTION))
*/
#ifndef __MAIL_H__
#define __MAIL_H__

#include <stdbool.h>
#include <stddef.h>

/** Nombre d'octets du debut d'un fichier examines pour reconnaitre un
    courrier (cf. mail_est_courrier). */
#define MAIL_TAILLE_DETECTION 4096u

/**
 * @brief  Indique si un debut de fichier est celui d'un courrier : un bloc
 *         d'en-tetes en tete de fichier, eventuellement precede de la ligne
 *         "From ..." d'une boite mbox. Un bloc d'en-tetes est une suite de
 *         champs "Nom: valeur" (nom fait de caracteres ASCII visibles,
 *         sans ':') et de lignes de suite (commencant par une espace ou une
 *         tabulation), terminee par une ligne vide ou par la fin des octets
 *         examines (la derniere ligne, si elle n'est pas terminee, n'est pas
 *         examinee), qui contient au moins deux champs propres au courrier
 *         (From, To, Cc, Subject, Date, Message-ID, Received, Return-Path,
 *         MIME-Version, Delivered-To, Reply-To, In-Reply-To, References,
 *         Sender, Content-Type, Content-Transfer-Encoding, X-Mailer,
 *         User-Agent...). Un texte ordinaire, meme commence par "Titre:" et
 *         "Auteur:", n'est pas un courrier.
 * @param  debut   les premiers octets du fichier (pas forcement termines
 *                 par '\0').
 * @param  taille  leur nombre (MAIL_TAILLE_DETECTION suffit).
 * @pre    debut != NULL || taille == 0.
 * @return true si c'est un courrier, false sinon.
 * @note   Complexite : O(taille).
 */
bool mail_est_courrier(const char *debut, size_t taille);

/**
 * @brief  Decode un texte en quoted-printable : "=XX" (deux chiffres
 *         hexadecimaux, majuscules ou minuscules) donne l'octet XX, un '='
 *         en fin de ligne (coupure douce, eventuellement suivi d'espaces)
 *         disparait avec la fin de ligne ; tout autre octet est recopie, y
 *         compris un '=' mal forme.
 * @param  texte   le texte encode.
 * @param  taille  sa taille en octets.
 * @param  sortie  destination d'au moins taille octets ; peut etre egale a
 *                 texte (decodage sur place).
 * @pre    (texte != NULL && sortie != NULL) || taille == 0.
 * @post   les octets de sortie ne sont pas termines par '\0'.
 * @return le nombre d'octets ecrits (au plus taille).
 * @note   Complexite : O(taille).
 */
size_t mail_decoder_quoted_printable(const char *texte, size_t taille, char *sortie);

/**
 * @brief  Decode un texte en base64 : chaque groupe de quatre caracteres de
 *         l'alphabet (A-Z, a-z, 0-9, '+', '/') donne trois octets ; les
 *         autres caracteres (fins de ligne, espaces) sont ignores ; le
 *         decodage s'arrete au premier '=' (un groupe incomplet donne les
 *         octets qu'il contient entierement).
 * @param  texte   le texte encode.
 * @param  taille  sa taille en octets.
 * @param  sortie  destination d'au moins (taille / 4) * 3 + 3 octets ; peut
 *                 etre egale a texte (decodage sur place).
 * @pre    (texte != NULL && sortie != NULL) || taille == 0.
 * @return le nombre d'octets ecrits (au plus (taille / 4) * 3 + 2).
 * @note   Complexite : O(taille).
 */
size_t mail_decoder_base64(const char *texte, size_t taille, char *sortie);

/**
 * @brief  Extrait le texte d'un courrier ou d'une boite mbox (cf. en-tete
 *         du fichier) : les corps des messages, decodes, en UTF-8, sans
 *         en-tetes, citations ni signatures.
 * @param  contenu       le contenu du fichier (pas forcement termine par
 *                       '\0').
 * @param  taille        sa taille en octets.
 * @param  taille_texte  recoit la longueur du texte extrait.
 * @param  nb_messages   recoit le nombre de messages lus (1 pour un message
 *                       seul, le nombre de lignes "From " de debut de
 *                       message pour une boite mbox).
 * @pre    contenu != NULL ; courrier(contenu, taille) ;
 *         taille_texte != NULL ; nb_messages != NULL.
 * @post   en cas de succes : le texte compte *taille_texte octets, suivis
 *         d'un '\0' ; il ne contient aucun octet nul ; *nb_messages >= 1 ;
 *         s'il n'est pas vide, il se termine par une ligne vide ("\n\n").
 *         En cas d'echec, *taille_texte et *nb_messages sont inchanges.
 * @return le texte (a liberer avec free), ou NULL si la memoire manque
 *         (errno == ENOMEM).
 * @note   Complexite : O(taille * profondeur des parties MIME), la
 *         profondeur etant bornee (16 niveaux, au-dela les parties sont
 *         ignorees).
 */
char *mail_extraire_texte(const char *contenu, size_t taille, size_t *taille_texte, size_t *nb_messages);

#endif
