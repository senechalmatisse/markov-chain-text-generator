/**
@file test_mail.c
Fichier test_mail.c

Tests unitaires du module mail (cf. src/mail.h) : reconnaissance d'un
courrier, decodages quoted-printable et base64, en-tetes ignores, parties
MIME (alternatives, pieces jointes, messages joints, imbrication), jeux de
caracteres, HTML, citations, signatures, boites mbox, contrats.
*/
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/mail.h"
#include "outils_tests.h"

/** En-tetes minimaux d'un message (suivis de la ligne vide). */
#define ENTETES "From: Jean <jean@example.com>\nTo: paul@example.com\n" \
                "Subject: Nouvelles\n"

/* ======================================================================== */
/*                                  Outils                                  */
/* ======================================================================== */

/** Texte extrait d'un courrier (a liberer), et nombre de messages. */
static char *extraire(const char *courrier, size_t *nb_messages)
{
  size_t taille = 0u;
  size_t nb = 0u;
  char *texte = mail_extraire_texte(courrier, strlen(courrier), &taille, &nb);

  assert(texte != NULL);
  assert(strlen(texte) == taille && nb >= 1u);
  /* un texte non vide se termine par une ligne vide */
  assert(taille == 0u || (taille >= 2u && texte[taille - 1u] == '\n' && texte[taille - 2u] == '\n'));
  if (nb_messages != NULL) {
    *nb_messages = nb;
  }
  return texte;
}

/** Texte reduit a ses mots : blancs consecutifs remplaces par une espace,
    sans blancs de tete ni de queue (a liberer). */
static char *mots(const char *texte)
{
  char *resultat = malloc(strlen(texte) + 1u);
  size_t n = 0u;
  bool blanc = false;
  const char *c;

  assert(resultat != NULL);
  for (c = texte; *c != '\0'; c++) {
    if (*c == ' ' || *c == '\n' || *c == '\t' || *c == '\r') {
      blanc = (n > 0u);
    } else {
      if (blanc) {
        resultat[n] = ' ';
        n++;
        blanc = false;
      }
      resultat[n] = *c;
      n++;
    }
  }
  resultat[n] = '\0';
  return resultat;
}

/** Verifie que le texte extrait d'un courrier, reduit a ses mots, est le texte attendu. */
static void verifier_mots(const char *courrier, const char *attendu)
{
  char *texte = extraire(courrier, NULL);
  char *obtenu = mots(texte);

  if (strcmp(obtenu, attendu) != 0) {
    fprintf(stderr, "\nattendu : \"%s\"\nobtenu  : \"%s\"\n", attendu, obtenu);
  }
  assert(strcmp(obtenu, attendu) == 0);
  free(obtenu);
  free(texte);
}

/** Verifie le decodage quoted-printable, copie puis sur place. */
static void verifier_qp(const char *encode, const char *attendu)
{
  char sortie[256];
  char sur_place[256];
  size_t n;

  n = mail_decoder_quoted_printable(encode, strlen(encode), sortie);
  assert(n == strlen(attendu) && memcmp(sortie, attendu, n) == 0);
  strcpy(sur_place, encode);
  n = mail_decoder_quoted_printable(sur_place, strlen(sur_place), sur_place);
  assert(n == strlen(attendu) && memcmp(sur_place, attendu, n) == 0);
}

/** Verifie le decodage base64 (n octets attendus), copie puis sur place. */
static void verifier_base64(const char *encode, const char *attendu, size_t n_attendu)
{
  char sortie[256];
  char sur_place[256];
  size_t n;

  n = mail_decoder_base64(encode, strlen(encode), sortie);
  assert(n == n_attendu && memcmp(sortie, attendu, n) == 0);
  assert(n <= (strlen(encode) / 4u) * 3u + 2u);
  strcpy(sur_place, encode);
  n = mail_decoder_base64(sur_place, strlen(sur_place), sur_place);
  assert(n == n_attendu && memcmp(sur_place, attendu, n) == 0);
}

static bool est_courrier(const char *texte)
{
  return mail_est_courrier(texte, strlen(texte));
}


/* ======================================================================== */
/*                                   Tests                                  */
/* ======================================================================== */

static void test_reconnaissance(void)
{
  char *long_entete;
  size_t i;

  /* message : au moins deux champs propres au courrier */
  assert(est_courrier(ENTETES "\nBonjour.\n"));
  assert(est_courrier("Subject: Bonjour\nDate: Mon, 3 Mar 2024\n\nTexte\n"));
  assert(est_courrier("Received: from mx\n by mx.example.com\n"
                      "Return-Path: <a@b.c>\n\n"));
  assert(est_courrier("FROM: a\r\nTO: b\r\n\r\nTexte\r\n"));
  /* boite mbox : ligne "From " puis les en-tetes */
  assert(est_courrier("From jean@example.com Mon Mar  3 10:00:00 2024\n"
                      ENTETES "\nBonjour.\n"));
  /* bloc d'en-tetes sans fin dans les octets examines ; la derniere ligne,
     incomplete, n'est pas examinee */
  assert(est_courrier("From: a\nTo: b\nSubj"));
  assert(!est_courrier("From: a\nSubj"));
  /* textes ordinaires */
  assert(!est_courrier("Le chat dort.\n"));
  assert(!est_courrier("Titre: Les Miserables\nAuteur: Victor Hugo\n\n"
                       "Texte\n"));
  assert(!est_courrier("Release Date: 1862\nFrom: Hugo\n\n"));
  assert(!est_courrier("From: a\n\nTo: b\n"));    /* un seul champ */
  assert(!est_courrier("From: a\nbonjour\nTo: b\n\n"));
  assert(!est_courrier(" From: a\nTo: b\n\n"));
  assert(!est_courrier("From a\nTexte\n"));       /* "From " sans en-tetes */
  assert(!est_courrier("\nFrom: a\nTo: b\n\n"));
  assert(!est_courrier("Pas de fin de ligne"));
  assert(!mail_est_courrier(NULL, 0u));
  assert(!mail_est_courrier(ENTETES, 0u));
  /* seuls les octets indiques sont examines */
  assert(!mail_est_courrier(ENTETES, 12u));
  /* en-tetes plus longs que la fenetre de detection */
  long_entete = malloc(2u * MAIL_TAILLE_DETECTION + 64u);
  assert(long_entete != NULL);
  strcpy(long_entete, "From: a\nTo: b\nReceived: ");
  for (i = strlen(long_entete); i < 2u * MAIL_TAILLE_DETECTION; i++) {
    long_entete[i] = (i % 70u == 0u) ? '\n' : ((i % 70u == 1u) ? ' ' : 'x');
  }
  strcpy(long_entete + i, "\n\nTexte\n");
  assert(mail_est_courrier(long_entete, MAIL_TAILLE_DETECTION));
  free(long_entete);
}

static void test_quoted_printable(void)
{
  char sortie[8];

  verifier_qp("caf=C3=A9", "caf\xC3\xA9");
  verifier_qp("caf=c3=a9", "caf\xC3\xA9");
  verifier_qp("ligne coup=\nee", "ligne coupee");
  verifier_qp("ligne coup=\r\nee", "ligne coupee");
  verifier_qp("ligne coup=  \nee", "ligne coupee");
  verifier_qp("fin= ", "fin");
  verifier_qp("x=3D1", "x=1");
  /* '=' mal forme : recopie */
  verifier_qp("a=ZZb", "a=ZZb");
  verifier_qp("a=4", "a=4");
  verifier_qp("a=", "a=");
  verifier_qp("=\rX", "=\rX");
  verifier_qp("", "");
  assert(mail_decoder_quoted_printable(NULL, 0u, NULL) == 0u);
  assert(mail_decoder_quoted_printable("=00", 3u, sortie) == 1u);
  assert(sortie[0] == '\0');
}

static void test_base64(void)
{
  char sortie[8];

  verifier_base64("Q2Fmw6k=", "Caf\xC3\xA9", 5u);
  verifier_base64("Q2Fm\r\nw6k=", "Caf\xC3\xA9", 5u);
  verifier_base64("Q2Fmw6k", "Caf\xC3\xA9", 5u);   /* sans '=' */
  verifier_base64("YQ==", "a", 1u);
  verifier_base64("YWI=", "ab", 2u);
  verifier_base64("YWJj", "abc", 3u);
  verifier_base64("Y!Q==", "a", 1u);               /* '!' ignore */
  verifier_base64("YQ==YWJj", "a", 1u);            /* arret au '=' */
  verifier_base64("Y", "", 0u);                    /* sextet seul */
  verifier_base64("+/+/", "\xFB\xFF\xBF", 3u);
  verifier_base64("", "", 0u);
  assert(mail_decoder_base64(NULL, 0u, NULL) == 0u);
  assert(mail_decoder_base64("AAAA", 4u, sortie) == 3u);
  assert(sortie[0] == '\0' && sortie[1] == '\0' && sortie[2] == '\0');
}

static void test_message_simple(void)
{
  size_t nb = 0u;
  char *texte = extraire(ENTETES "\nBonjour Paul,\nCa va ?\n", &nb);

  /* les en-tetes sont ignores, le corps est recopie ligne par ligne */
  assert(nb == 1u);
  assert(strcmp(texte, "Bonjour Paul,\nCa va ?\n\n") == 0);
  free(texte);
  /* fins de ligne CRLF, pas de fin de ligne finale */
  texte = extraire("From: a\r\nTo: b\r\n\r\nUne ligne\r\nDeux", &nb);
  assert(strcmp(texte, "Une ligne\nDeux\n\n") == 0);
  free(texte);
  /* message sans corps : texte vide */
  texte = extraire(ENTETES, &nb);
  assert(nb == 1u && strcmp(texte, "") == 0);
  free(texte);
  /* en-tete replie sur plusieurs lignes */
  verifier_mots("From: a\nSubject: un sujet\n  sur deux lignes\nTo: b\n\n"
                "Corps.\n", "Corps.");
  /* partie dont les en-tetes ne sont pas suivis d'une ligne vide : le corps
     commence a la premiere ligne qui n'est ni un champ ni une suite */
  verifier_mots(ENTETES "Content-Type: multipart/mixed; boundary=b\n\n--b\n"
                "Content-Type: text/plain\nVoici le corps.\n--b--\n",
                "Voici le corps.");
}

static void test_citations_et_signatures(void)
{
  verifier_mots(ENTETES "\nJe suis d'accord.\n\n"
                "Le 3 mars 2024 \xC3\xA0 10:00, Paul a \xC3\xA9" "crit :\n"
                "> Tu viens demain ?\n>> Et toi ?\n  > encore\n"
                "Oui, je viens.\n",
                "Je suis d'accord. Oui, je viens.");
  /* annonce coupee en deux lignes (adresse longue) */
  verifier_mots(ENTETES "\nMerci.\n\n"
                "Le lun. 3 mars 2024 \xC3\xA0 10:00, Paul Martin <\n"
                "paul.martin@example.com> a \xC3\xA9" "crit :\n"
                "> Bonjour\n", "Merci.");
  verifier_mots(ENTETES "\nThanks.\n"
                "On Mon, Mar 3, 2024 at 10:00 AM Paul <paul@example.com>\n"
                "wrote:\n> Hello\n", "Thanks.");
  verifier_mots(ENTETES "\nThanks.\nOn Mon, Mar 3, 2024, Paul wrote:\n"
                "> Hello\n", "Thanks.");
  /* une ligne qui ne fait que finir par "a ecrit :" reste seule ignoree */
  verifier_mots(ENTETES "\nVoici ce que Paul a \xC3\xA9" "crit :\n"
                "> Bonjour\n", "");
  verifier_mots(ENTETES "\nUn.\nPaul a ecrit :\nDeux.\n", "Un. Deux.");
  /* signature */
  verifier_mots(ENTETES "\nA bientot.\n-- \nJean Dupont\n06 00 00 00 00\n",
                "A bientot.");
  verifier_mots(ENTETES "\nA bientot.\n--\nJean\n", "A bientot.");
  verifier_mots(ENTETES "\nUn -- deux.\n---\nTrois.\n", "Un -- deux. --- Trois.");
  /* message d'origine ou transfere : la suite est une citation */
  verifier_mots(ENTETES "\nVoir plus bas.\n\n-----Original Message-----\n"
                "From: Paul\nSent: Monday\n\nTexte cite.\n", "Voir plus bas.");
  verifier_mots(ENTETES "\nPour info.\n-------- Message original --------\n"
                "Sujet : x\n\nTexte cite.\n", "Pour info.");
  verifier_mots(ENTETES "\nPour info.\n---------- Forwarded message "
                "---------\nFrom: Paul\n\nTexte.\n", "Pour info.");
  verifier_mots(ENTETES "\nTiens.\n\nD\xC3\xA9" "but du message "
                "r\xC3\xA9" "exp\xC3\xA9" "di\xC3\xA9 :\n\nTexte.\n", "Tiens.");
  verifier_mots(ENTETES "\nTiens.\nBegin forwarded message:\nTexte.\n",
                "Tiens.");
  /* mentions d'envoi */
  verifier_mots(ENTETES "\nOk.\n\nEnvoy\xC3\xA9 de mon iPhone\n", "Ok.");
  verifier_mots(ENTETES "\nOk.\nSent from my Android phone\n", "Ok.");
  verifier_mots(ENTETES "\nOk.\nGet Outlook for iOS\n", "Ok.");
  /* message signe avec PGP : en-tete et signature ignores */
  verifier_mots(ENTETES "\n-----BEGIN PGP SIGNED MESSAGE-----\n"
                "Hash: SHA256\n\nTexte signe.\n"
                "-----BEGIN PGP SIGNATURE-----\n\niQEzBAEBCAAdFiEE\n"
                "-----END PGP SIGNATURE-----\n", "Texte signe.");
}

static void test_mime(void)
{
  /* alternative : la partie text/plain, decodee (quoted-printable) */
  verifier_mots(ENTETES "MIME-Version: 1.0\n"
                "Content-Type: multipart/alternative; boundary=\"b1\"\n\n"
                "Preambule ignore.\n--b1\n"
                "Content-Type: text/plain; charset=utf-8\n"
                "Content-Transfer-Encoding: quoted-printable\n\n"
                "Un caf=C3=A9 tr=C3=A8s chaud, s'il vous pla=C3=AEt.\n"
                "--b1\nContent-Type: text/html\n\n<p>Version HTML</p>\n"
                "--b1--\nEpilogue ignore.\n",
                "Un caf\xC3\xA9 tr\xC3\xA8s chaud, s'il vous pla\xC3\xAEt.");
  /* alternative sans text/plain : la derniere partie */
  verifier_mots(ENTETES "Content-Type: multipart/alternative;\n"
                "  boundary=frontiere\n\n--frontiere\n"
                "Content-Type: text/enriched\n\n<bold>Non</bold>\n"
                "--frontiere\nContent-Type: text/html; charset=utf-8\n\n"
                "<p>Oui</p>\n--frontiere--\n", "Oui");
  /* partie sans en-tetes : texte */
  verifier_mots(ENTETES "Content-Type: multipart/alternative; boundary=b\n\n"
                "--b\n\nTexte brut.\n--b\nContent-Type: text/html\n\n"
                "<p>HTML</p>\n--b--\n", "Texte brut.");
  /* melange : texte, piece jointe ignoree, alternative imbriquee, message
     joint (ses en-tetes ignores), partie qui n'est pas du texte */
  verifier_mots(ENTETES "Content-Type: multipart/mixed; boundary=\"=_ext;1\"\n"
                "\n--=_ext;1\n"
                "Content-Type: multipart/alternative; boundary=int\n\n"
                "--int\nContent-Type: text/plain\n\nPremier.\n"
                "--int\nContent-Type: text/html\n\n<p>Doublon</p>\n--int--\n"
                "--=_ext;1\nContent-Type: text/plain; name=\"notes.txt\"\n"
                "Content-Disposition: attachment; filename=\"notes.txt\"\n\n"
                "Piece jointe.\n"
                "--=_ext;1\nContent-Type: application/pdf\n"
                "Content-Transfer-Encoding: base64\n\nJVBERi0xLjQK\n"
                "--=_ext;1\nContent-Type: message/rfc822\n\n"
                "From: Paul\nSubject: Ancien\n\nDeuxieme.\n"
                "--=_ext;1--\n", "Premier. Deuxieme.");
  /* corps en base64 */
  verifier_mots(ENTETES "Content-Type: text/plain; charset=UTF-8\n"
                "Content-Transfer-Encoding: BASE64\n\nQ2Fmw6kgY3LDqG1lLg==\n",
                "Caf\xC3\xA9 cr\xC3\xA8me.");
  /* dernier delimiteur absent : la derniere partie va jusqu'a la fin */
  verifier_mots(ENTETES "Content-Type: multipart/mixed; boundary=b\n\n"
                "--b\nContent-Type: text/plain\n\nUn.\n--b\n\nDeux.\n",
                "Un. Deux.");
  /* delimiteur final en premier, multipart sans frontiere, type inconnu,
     delimiteur suivi de blancs, frontiere qui n'est qu'un prefixe */
  verifier_mots(ENTETES "Content-Type: multipart/mixed; boundary=b\n\n"
                "--b--\nRien.\n", "");
  verifier_mots(ENTETES "Content-Type: multipart/mixed\n\n--b\n\nRien.\n", "");
  verifier_mots(ENTETES "Content-Type: image/png\n\nRien.\n", "");
  verifier_mots(ENTETES "Content-Type: multipart/mixed; boundary=b\n\n"
                "--b  \n\nUn.\n--bb\nDeux.\n--b--\n", "Un. --bb Deux.");
  /* piece jointe au niveau du message */
  verifier_mots(ENTETES "Content-Disposition: attachment\n\nRien.\n", "");
}

static void test_entetes_longs(void)
{
  char *courrier = malloc(8000u);
  char frontiere[260];
  size_t i;

  /* frontiere de 250 caracteres : seuls les 200 premiers sont gardes, elle
     ne correspond plus aux delimiteurs, rien n'est lu */
  assert(courrier != NULL);
  for (i = 0u; i < 250u; i++) {
    frontiere[i] = (char)('a' + (char)(i % 26u));
  }
  frontiere[i] = '\0';
  sprintf(courrier, ENTETES "Content-Type: multipart/mixed; boundary=%s\n\n"
          "--%s\n\nTexte.\n--%s--\n", frontiere, frontiere, frontiere);
  verifier_mots(courrier, "");
  /* en-tete replie de plus de 1024 octets : la fin de sa valeur est
     ignoree, ici le jeu de caracteres */
  strcpy(courrier, ENTETES "Content-Type: text/plain;\n");
  for (i = 0u; i < 30u; i++) {
    strcat(courrier, "  x-parametre=\"une valeur assez longue\";\n");
  }
  strcat(courrier, "  charset=iso-8859-1\n\nCaf\xE9.\n");
  verifier_mots(courrier, "Caf\xE9.");
  free(courrier);
}

static void test_imbrication_profonde(void)
{
  char *courrier = malloc(64u * 128u + 256u);
  size_t i;

  /* 40 niveaux de multipart : au-dela de 16, les parties sont ignorees */
  assert(courrier != NULL);
  strcpy(courrier, "From: a\nTo: b\n");
  for (i = 0u; i < 40u; i++) {
    sprintf(courrier + strlen(courrier), "Content-Type: multipart/mixed; boundary=n%zu\n\n--n%zu\n", i, i);
  }
  strcat(courrier, "Content-Type: text/plain\n\nTres profond.\n");
  verifier_mots(courrier, "");
  /* 10 niveaux : lus */
  strcpy(courrier, "From: a\nTo: b\n");
  for (i = 0u; i < 10u; i++) {
    sprintf(courrier + strlen(courrier), "Content-Type: multipart/mixed; boundary=n%zu\n\n--n%zu\n", i, i);
  }
  strcat(courrier, "Content-Type: text/plain\n\nProfond.\n");
  verifier_mots(courrier, "Profond.");
  free(courrier);
}

static void test_jeux_de_caracteres(void)
{
  verifier_mots(ENTETES "Content-Type: text/plain; charset=iso-8859-1\n\n"
                "Caf\xE9 cr\xE8me \xE0 la fran\xE7" "aise.\n",
                "Caf\xC3\xA9 cr\xC3\xA8me \xC3\xA0 la fran\xC3\xA7" "aise.");
  /* ISO-8859-1 lu comme Windows-1252 (guillemets et apostrophes courbes) */
  verifier_mots(ENTETES "Content-Type: text/plain; charset=\"ISO-8859-1\"\n\n"
                "\x93Salut\x94 l\x92" "ami \x80 \x9C" "uvre \x81.\n",
                "\xE2\x80\x9CSalut\xE2\x80\x9D l\xE2\x80\x99" "ami "
                "\xE2\x82\xAC \xC5\x93uvre \xEF\xBF\xBD.");
  verifier_mots(ENTETES "Content-Type: text/plain; charset=windows-1252\n\n"
                "\x85 fin\n", "\xE2\x80\xA6 fin");
  /* ISO-8859-15 : euro, oe lie, S et Z caron, Y trema */
  verifier_mots(ENTETES "Content-Type: text/plain; charset=iso-8859-15\n\n"
                "\xA4 \xBD \xBC \xA6 \xA8 \xB4 \xB8 \xBE \xE9.\n",
                "\xE2\x82\xAC \xC5\x93 \xC5\x92 \xC5\xA0 \xC5\xA1 \xC5\xBD "
                "\xC5\xBE \xC5\xB8 \xC3\xA9.");
  /* UTF-8, US-ASCII et jeu inconnu : recopies */
  verifier_mots(ENTETES "Content-Type: text/plain; charset=utf-8\n\n"
                "\xC3\xA9t\xC3\xA9\n", "\xC3\xA9t\xC3\xA9");
  verifier_mots(ENTETES "Content-Type: text/plain; charset=koi8-r\n\n"
                "\xE9\n", "\xE9");
  verifier_mots(ENTETES "Content-Type: text/plain; charset=us-ascii\n\n"
                "ok\n", "ok");
  /* quoted-printable en Latin-1 */
  verifier_mots(ENTETES "Content-Type: text/plain; charset=latin1\n"
                "Content-Transfer-Encoding: quoted-printable\n\n"
                "=E9t=E9\n", "\xC3\xA9t\xC3\xA9");
}

static void test_html(void)
{
  char *texte;

  verifier_mots(ENTETES "Content-Type: text/html; charset=utf-8\n\n"
                "<!DOCTYPE html>\n<html><head><title>Titre</title>"
                "<style>p { color: red; }</style></head>\n<body>\n"
                "<!-- commentaire -->\n<p>Bonjour <b>tout</b>\nle monde</p>"
                "<script>if (a < b) { x(); }</script>"
                "<p>Caf&eacute; &amp; th&#233; &#x2014; &laquo;fin&raquo;"
                "</p><p>a &lt; b, &inconnue; &#xZZ; &#;</p>"
                "<a href=\"x>y\" title='z>t'>lien</a>"
                "</body></html>\n",
                "Bonjour tout le monde Caf\xC3\xA9 & th\xC3\xA9 \xE2\x80\x94 "
                "\xC2\xAB" "fin\xC2\xBB a < b, &inconnue; &#xZZ; &#; lien");
  /* citations des messageries (blockquote imbriques), sauts de ligne */
  verifier_mots(ENTETES "Content-Type: text/html\n\n"
                "<div>Oui.</div><div class=\"gmail_quote\">"
                "<div>Le 3 mars, Paul a &eacute;crit&nbsp;:<br></div>"
                "<blockquote>Tu viens ?<blockquote>Avant</blockquote>"
                "Encore</blockquote></div><div>Fin.</div>\n",
                "Oui. Fin.");
  /* br et p donnent des fins de ligne et des lignes vides ; un message se
     termine par une seule ligne vide */
  texte = extraire(ENTETES "Content-Type: text/html\n\n"
                   "Un<br>deux<p>trois</p>\n", NULL);
  assert(strcmp(texte, "Un\ndeux\n\ntrois\n\n") == 0);
  free(texte);
  /* caracteres hors du plan multilingue de base (4 octets en UTF-8) */
  verifier_mots(ENTETES "Content-Type: text/html\n\n&#x1F600; &#128512;\n",
                "\xF0\x9F\x98\x80 \xF0\x9F\x98\x80");
  /* balise non fermee, entite tronquee, '<' isole */
  verifier_mots(ENTETES "Content-Type: text/html\n\nUn <b deux", "Un");
  verifier_mots(ENTETES "Content-Type: text/html\n\nUn &eacute", "Un &eacute");
  verifier_mots(ENTETES "Content-Type: text/html\n\nUn < 3 <!-- x", "Un < 3");
  verifier_mots(ENTETES "Content-Type: text/html\n\nUn<style>x", "Un");
  /* HTML en quoted-printable et Windows-1252 */
  verifier_mots(ENTETES "Content-Type: text/html; charset=windows-1252\n"
                "Content-Transfer-Encoding: quoted-printable\n\n"
                "<p>l=92=E9t=E9</p>\n", "l\xE2\x80\x99\xC3\xA9t\xC3\xA9");
}

static void test_boite_mbox(void)
{
  size_t nb = 0u;
  char *texte;

  texte = extraire("From jean@example.com Mon Mar  3 10:00:00 2024\n"
                   ENTETES "\nPremier message.\n"
                   "From jean said, not a separator\n\n"
                   "From paul@example.com Tue Mar  4 11:00:00 2024\n"
                   "From: Paul\nTo: Jean\n\nDeuxieme.\n>From the start.\n"
                   ">>From a quote.\n> From a quote.\n\n"
                   "From marie@example.com Wed Mar  5 12:00:00 2024\n"
                   "From: Marie\nTo: Jean\n\nTroisieme.\n", &nb);
  /* trois messages (une ligne "From " qui ne suit pas une ligne vide ne
     separe pas) ; une ligne protegee ">From " redevient du texte, une
     ligne ">>From " (">From " protegee) reste une citation */
  assert(nb == 3u);
  assert(strcmp(texte, "Premier message.\nFrom jean said, not a separator\n"
                       "\nDeuxieme.\nFrom the start.\n\nTroisieme.\n\n") == 0);
  free(texte);
  /* hors d'une boite mbox, ou dans un corps en base64 ou en HTML, rien
     n'est protege : ">From " est une citation */
  texte = extraire(ENTETES "\nTexte.\n>From the start.\n", &nb);
  assert(nb == 1u && strcmp(texte, "Texte.\n\n") == 0);
  free(texte);
  texte = extraire("From a Mon\nFrom: a\nTo: b\n"
                   "Content-Transfer-Encoding: base64\n\n"
                   "PkZyb20gdGhlIHNlYS4KVGV4dGUuCg==\n\n"
                   "From b Tue\nFrom: b\nTo: a\nContent-Type: text/html\n\n"
                   "<p>Debut</p><br>>From the sea.\n", &nb);
  assert(nb == 2u && strcmp(texte, "Texte.\n\nDebut\n\n") == 0);
  free(texte);
  /* boite d'un seul message, messages vides */
  texte = extraire("From a Mon\nFrom: a\nTo: b\n\nSeul.\n", &nb);
  assert(nb == 1u && strcmp(texte, "Seul.\n\n") == 0);
  free(texte);
  texte = extraire("From a Mon\nFrom: a\nTo: b\n\nFrom b Tue\n"
                   "From: b\nTo: a\n\n", &nb);
  assert(nb == 2u && strcmp(texte, "") == 0);
  free(texte);
}

static void test_octets_nuls(void)
{
  char *texte;

  /* un corps contenant des octets nuls (base64) : remplaces par des
     espaces */
  texte = extraire(ENTETES "Content-Transfer-Encoding: base64\n\n"
                   "QQBC\n", NULL);
  assert(strcmp(texte, "A B\n\n") == 0);
  free(texte);
  texte = extraire(ENTETES "Content-Type: text/html\n\nA&#0;B\n", NULL);
  assert(strcmp(texte, "A\xEF\xBF\xBD" "B \n\n") == 0);
  free(texte);
}

static void test_grand_message(void)
{
  size_t taille_corps = 200000u;
  char *courrier = malloc(taille_corps + 128u);
  char *texte;
  size_t taille = 0u;
  size_t nb = 0u;
  size_t i;

  /* corps de 200 Ko : le texte produit s'agrandit plusieurs fois */
  assert(courrier != NULL);
  strcpy(courrier, ENTETES "\n");
  for (i = strlen(courrier); i < taille_corps; i++) {
    courrier[i] = (i % 60u == 0u) ? '\n' : (char)('a' + (char)(i % 26u));
  }
  courrier[i] = '\0';
  texte = mail_extraire_texte(courrier, i, &taille, &nb);
  assert(texte != NULL && nb == 1u);
  assert(taille + strlen(ENTETES) + 3u >= taille_corps);
  assert(strlen(texte) == taille);
  free(texte);
  free(courrier);
}


/* ======================================================================== */
/*                       Violations de contrat                              */
/* ======================================================================== */

static void violation_courrier_nul(void)
{
  mail_est_courrier(NULL, 4u);
}

static void violation_qp_nul(void)
{
  char sortie[4];

  mail_decoder_quoted_printable(NULL, 3u, sortie);
}

static void violation_base64_sortie_nulle(void)
{
  mail_decoder_base64("YQ==", 4u, NULL);
}

static void violation_extraire_pas_un_courrier(void)
{
  size_t taille;
  size_t nb;

  mail_extraire_texte("Le chat dort.\n", 14u, &taille, &nb);
}

static void violation_extraire_taille_nulle(void)
{
  size_t nb;

  mail_extraire_texte(ENTETES "\nx\n", strlen(ENTETES) + 3u, NULL, &nb);
}

static void violation_extraire_contenu_nul(void)
{
  size_t taille;
  size_t nb;

  mail_extraire_texte(NULL, 0u, &taille, &nb);
}

static void sans_violation(void)
{
  size_t taille;
  size_t nb;
  char *texte = mail_extraire_texte(ENTETES "\nx\n", strlen(ENTETES) + 3u, &taille, &nb);

  free(texte);
}

static void test_contrats(void)
{
  assert(!viole_contrat(sans_violation));
  assert(viole_contrat(violation_courrier_nul));
  assert(viole_contrat(violation_qp_nul));
  assert(viole_contrat(violation_base64_sortie_nulle));
  assert(viole_contrat(violation_extraire_pas_un_courrier));
  assert(viole_contrat(violation_extraire_taille_nulle));
  assert(viole_contrat(violation_extraire_contenu_nul));
}

int main(void)
{
  printf("Tests du module mail :\n");
  LANCER_TEST(test_reconnaissance);
  LANCER_TEST(test_quoted_printable);
  LANCER_TEST(test_base64);
  LANCER_TEST(test_message_simple);
  LANCER_TEST(test_citations_et_signatures);
  LANCER_TEST(test_mime);
  LANCER_TEST(test_entetes_longs);
  LANCER_TEST(test_imbrication_profonde);
  LANCER_TEST(test_jeux_de_caracteres);
  LANCER_TEST(test_html);
  LANCER_TEST(test_boite_mbox);
  LANCER_TEST(test_octets_nuls);
  LANCER_TEST(test_grand_message);
  LANCER_TEST(test_contrats);
  afficher_bilan("mail");
  return EXIT_SUCCESS;
}
