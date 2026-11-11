/**
@file mail.c
Fichier mail.c
Auteur(s):

Implementation de la lecture des courriers electroniques (cf. mail.h).

Le contenu n'est jamais recopie pour etre decoupe : un message, une partie
MIME ou une ligne est un morceau [debut, fin[ du contenu. Seuls les corps
des parties de texte sont recopies pour etre decodes, convertis en UTF-8,
reduits a leur texte (HTML), puis nettoyes ligne par ligne (citations,
signatures) dans le texte produit.
*/
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include "contrat.h"
#include "mail.h"

/** Imbrication maximale des parties MIME lues (au-dela : ignorees). */
#define PROFONDEUR_MIME_MAX 16u

/** Longueur maximale gardee de la valeur d'un en-tete MIME deplie. */
#define TAILLE_VALEUR_MAX 1024u

/** Capacite initiale d'un texte produit. */
#define CAPACITE_INITIALE ((size_t)256)

/** Point de code de remplacement (caractere inconnu). */
#define REMPLACEMENT 0xFFFDul

/** Morceau d'un texte : les octets [debut, fin[. */
typedef struct {
  const char *debut;
  const char *fin;
} morceau_t;

/** Texte produit : tampon dynamique, termine par '\0' une fois alloue ;
    succes passe a false (definitivement) si la memoire manque ; boite_mbox
    indique que les lignes "From " des corps sont protegees (">From "). */
typedef struct {
  char *texte;
  size_t longueur;
  size_t capacite;
  bool succes;
  bool boite_mbox;
} sortie_t;

/** En-tetes MIME utiles d'une partie (valeurs en minuscules, sauf la
    frontiere). */
typedef struct {
  char type[64];        /* "text/plain", ... ; "" si absent */
  char frontiere[201];  /* parametre boundary ; "" si absent */
  char jeu[41];         /* parametre charset ; "" si absent */
  char encodage[41];    /* Content-Transfer-Encoding ; "" si absent */
  bool piece_jointe;    /* Content-Disposition: attachment */
} entetes_t;

/** Jeux de caracteres convertis en UTF-8. */
typedef enum {
  JEU_RECOPIE,          /* UTF-8, US-ASCII ou jeu inconnu : tel quel */
  JEU_WINDOWS_1252,     /* ISO-8859-1 et Windows-1252 */
  JEU_LATIN_9           /* ISO-8859-15 */
} jeu_t;


/* ======================================================================== */
/*                               Outils                                     */
/* ======================================================================== */

static char minuscule(char c)
{
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/** Longueur d'un morceau. */
static size_t longueur(morceau_t m)
{
  return (size_t)(m.fin - m.debut);
}

/** Indique si les n octets debut sont le mot (en minuscules), la casse
    ASCII etant ignoree. */
static bool egal_sans_casse(const char *debut, size_t n, const char *mot)
{
  size_t i;
  bool egal = (strlen(mot) == n);

  for (i = 0u; egal && i < n; i++) {
    egal = (minuscule(debut[i]) == mot[i]);
  }
  return egal;
}

/** Indique si le morceau commence par le prefixe (en minuscules), la casse
    ASCII etant ignoree. */
static bool commence_par(morceau_t m, const char *prefixe)
{
  size_t n = strlen(prefixe);

  return longueur(m) >= n && egal_sans_casse(m.debut, n, prefixe);
}

/** Indique si le morceau contient le mot (en minuscules), la casse ASCII
    etant ignoree. */
static bool contient(morceau_t m, const char *mot)
{
  size_t n = strlen(mot);
  const char *c;
  bool trouve = false;

  for (c = m.debut; !trouve && c + n <= m.fin; c++) {
    trouve = egal_sans_casse(c, n, mot);
  }
  return trouve;
}

/** Morceau sans ses blancs (espaces, tabulations) de tete et de queue. */
static morceau_t sans_blancs(morceau_t m)
{
  while (m.debut < m.fin && (*m.debut == ' ' || *m.debut == '\t')) {
    m.debut++;
  }
  while (m.fin > m.debut && (m.fin[-1] == ' ' || m.fin[-1] == '\t')) {
    m.fin--;
  }
  return m;
}

/**
 * Detache la ligne suivante d'un morceau, sans sa fin de ligne ("\n" ou
 * "\r\n"). Renvoie false si le morceau est vide.
 */
static bool ligne_suivante(morceau_t *reste, morceau_t *ligne)
{
  const char *fin;
  bool lue = (reste->debut < reste->fin);

  if (lue) {
    fin = memchr(reste->debut, '\n', longueur(*reste));
    ligne->debut = reste->debut;
    if (fin == NULL) {
      ligne->fin = reste->fin;
      reste->debut = reste->fin;
    } else {
      ligne->fin = fin;
      reste->debut = fin + 1;
    }
    if (ligne->fin > ligne->debut && ligne->fin[-1] == '\r') {
      ligne->fin--;
    }
  }
  return lue;
}


/* ======================================================================== */
/*                            Texte produit                                 */
/* ======================================================================== */

/** Garantit la place de supplement octets (plus le '\0'). */
static bool reserver(sortie_t *s, size_t supplement)
{
  size_t capacite = s->capacite;
  char *agrandi;

  if (s->succes && s->longueur + supplement + 1u > s->capacite) {
    if (supplement > SIZE_MAX / 4u - s->longueur) {
      s->succes = false;
    } else {
      if (capacite == 0u) {
        capacite = CAPACITE_INITIALE;
      }
      while (s->longueur + supplement + 1u > capacite) {
        capacite *= 2u;
      }
      agrandi = realloc(s->texte, capacite);
      if (agrandi == NULL) {
        s->succes = false;
      } else {
        s->texte = agrandi;
        s->capacite = capacite;
      }
    }
  }
  return s->succes;
}

/** Ajoute n octets (un octet nul devient une espace). */
static void ajouter(sortie_t *s, const char *octets, size_t n)
{
  size_t i;

  if (reserver(s, n)) {
    for (i = 0u; i < n; i++) {
      s->texte[s->longueur + i] = (octets[i] == '\0') ? ' ' : octets[i];
    }
    s->longueur += n;
    s->texte[s->longueur] = '\0';
  }
}

static void ajouter_chaine(sortie_t *s, const char *chaine)
{
  ajouter(s, chaine, strlen(chaine));
}

/** Ajoute un point de code en UTF-8 (U+FFFD s'il n'est pas valide). */
static void ajouter_point_code(sortie_t *s, unsigned long point_code)
{
  char octets[4];
  size_t n;

  if (point_code == 0u || point_code > 0x10FFFFul
      || (point_code >= 0xD800ul && point_code <= 0xDFFFul)) {
    point_code = REMPLACEMENT;
  }
  if (point_code < 0x80u) {
    octets[0] = (char)point_code;
    n = 1u;
  } else if (point_code < 0x800u) {
    octets[0] = (char)(0xC0u | (point_code >> 6));
    octets[1] = (char)(0x80u | (point_code & 0x3Fu));
    n = 2u;
  } else if (point_code < 0x10000ul) {
    octets[0] = (char)(0xE0u | (point_code >> 12));
    octets[1] = (char)(0x80u | ((point_code >> 6) & 0x3Fu));
    octets[2] = (char)(0x80u | (point_code & 0x3Fu));
    n = 3u;
  } else {
    octets[0] = (char)(0xF0u | (point_code >> 18));
    octets[1] = (char)(0x80u | ((point_code >> 12) & 0x3Fu));
    octets[2] = (char)(0x80u | ((point_code >> 6) & 0x3Fu));
    octets[3] = (char)(0x80u | (point_code & 0x3Fu));
    n = 4u;
  }
  ajouter(s, octets, n);
}


/* ======================================================================== */
/*                          Reconnaissance                                  */
/* ======================================================================== */

/** Champs d'en-tete propres au courrier (en minuscules). */
static const char *const CHAMPS_COURRIER[] = {
  "from", "to", "cc", "bcc", "subject", "date", "message-id", "received",
  "return-path", "mime-version", "delivered-to", "reply-to", "in-reply-to",
  "references", "sender", "content-type", "content-transfer-encoding",
  "x-mailer", "user-agent", "envelope-to", "dkim-signature",
  "authentication-results", "x-original-to", "thread-topic", "thread-index",
  "list-id"
};

/** Longueur du nom du champ d'en-tete qui commence la ligne ("Nom:"), ou
    0 si la ligne n'est pas un champ. */
static size_t nom_de_champ(morceau_t ligne)
{
  const char *c = ligne.debut;

  while (c < ligne.fin && *c > ' ' && *c <= '~' && *c != ':') {
    c++;
  }
  return (c > ligne.debut && c < ligne.fin && *c == ':')
         ? (size_t)(c - ligne.debut) : 0u;
}

/** Indique si une ligne poursuit le champ precedent (repliement). */
static bool est_suite(morceau_t ligne)
{
  return ligne.debut < ligne.fin && (*ligne.debut == ' ' || *ligne.debut == '\t');
}

static bool est_champ_courrier(const char *nom, size_t n)
{
  size_t i;
  bool trouve = false;

  for (i = 0u; !trouve && i < sizeof(CHAMPS_COURRIER) / sizeof(CHAMPS_COURRIER[0]); i++) {
    trouve = egal_sans_casse(nom, n, CHAMPS_COURRIER[i]);
  }
  return trouve;
}

/** Indique si une ligne est la ligne "From " qui commence un message d'une
    boite mbox. */
static bool est_ligne_mbox(morceau_t ligne)
{
  return longueur(ligne) >= 5u && memcmp(ligne.debut, "From ", 5u) == 0;
}

/** Indique si une ligne d'un corps est protegee par une boite mbox
    (convention mboxrd) : une ligne "From " du texte y est ecrite ">From ",
    une ligne ">From " ">>From ", etc. */
static bool est_ligne_protegee(morceau_t ligne)
{
  const char *c = ligne.debut;

  while (c < ligne.fin && *c == '>') {
    c++;
  }
  return c > ligne.debut && (size_t)(ligne.fin - c) >= 5u && memcmp(c, "From ", 5u) == 0;
}

bool mail_est_courrier(const char *debut, size_t taille)
{
  const char *fin_lignes;
  morceau_t reste;
  morceau_t ligne;
  unsigned int nb_champs = 0u;
  size_t n;
  bool premiere = true;
  bool dans_entetes = true;
  bool courrier = false;

  PRECONDITION(debut != NULL || taille == 0u);

  /* seules les lignes completes sont examinees */
  fin_lignes = (taille > 0u) ? debut + taille : NULL;
  while (fin_lignes != NULL && fin_lignes > debut && fin_lignes[-1] != '\n') {
    fin_lignes--;
  }
  if (fin_lignes != NULL && fin_lignes > debut) {
    reste.debut = debut;
    reste.fin = fin_lignes;
    if (ligne_suivante(&reste, &ligne) && !est_ligne_mbox(ligne)) {
      reste.debut = debut;                  /* pas de ligne "From " */
    }
    while (dans_entetes && ligne_suivante(&reste, &ligne)) {
      if (ligne.debut == ligne.fin) {
        dans_entetes = false;               /* fin du bloc d'en-tetes */
        courrier = (nb_champs >= 2u);
      } else if (est_suite(ligne) && !premiere) {
        premiere = false;                   /* suite du champ precedent */
      } else if ((n = nom_de_champ(ligne)) > 0u) {
        premiere = false;
        if (est_champ_courrier(ligne.debut, n)) {
          nb_champs++;
        }
      } else {
        dans_entetes = false;               /* ni champ ni suite */
      }
    }
    if (dans_entetes) {
      courrier = (nb_champs >= 2u);         /* bloc plus long qu'examine */
    }
  }
  return courrier;
}


/* ======================================================================== */
/*                               Decodages                                  */
/* ======================================================================== */

/** Valeur d'un chiffre hexadecimal, ou -1. */
static int valeur_hexadecimale(char c)
{
  int valeur = -1;

  if (c >= '0' && c <= '9') {
    valeur = c - '0';
  } else if (c >= 'a' && c <= 'f') {
    valeur = c - 'a' + 10;
  } else if (c >= 'A' && c <= 'F') {
    valeur = c - 'A' + 10;
  }
  return valeur;
}

size_t mail_decoder_quoted_printable(const char *texte, size_t taille, char *sortie)
{
  size_t i = 0u;
  size_t j;
  size_t n = 0u;
  int fort;
  int faible;

  PRECONDITION((texte != NULL && sortie != NULL) || taille == 0u);

  while (i < taille) {
    if (texte[i] == '=') {
      /* coupure douce : '=', espaces eventuelles, fin de ligne */
      j = i + 1u;
      while (j < taille && (texte[j] == ' ' || texte[j] == '\t')) {
        j++;
      }
      if (j < taille && texte[j] == '\r') {
        j++;
      }
      if (j < taille && texte[j] == '\n') {
        i = j + 1u;
      } else if (j == taille && j > i + 1u) {
        i = j;                              /* '=' et espaces finales */
      } else {
        fort = (i + 1u < taille) ? valeur_hexadecimale(texte[i + 1u]) : -1;
        faible = (i + 2u < taille) ? valeur_hexadecimale(texte[i + 2u]) : -1;
        if (fort >= 0 && faible >= 0) {
          sortie[n] = (char)(unsigned char)(fort * 16 + faible);
          i += 3u;
        } else {
          sortie[n] = '=';                  /* '=' mal forme : recopie */
          i++;
        }
        n++;
      }
    } else {
      sortie[n] = texte[i];
      n++;
      i++;
    }
  }
  POSTCONDITION(n <= taille);
  return n;
}

/** Valeur d'un caractere de l'alphabet base64, ou -1. */
static int valeur_base64(char c)
{
  int valeur = -1;

  if (c >= 'A' && c <= 'Z') {
    valeur = c - 'A';
  } else if (c >= 'a' && c <= 'z') {
    valeur = c - 'a' + 26;
  } else if (c >= '0' && c <= '9') {
    valeur = c - '0' + 52;
  } else if (c == '+') {
    valeur = 62;
  } else if (c == '/') {
    valeur = 63;
  }
  return valeur;
}

size_t mail_decoder_base64(const char *texte, size_t taille, char *sortie)
{
  unsigned long groupe = 0u;
  unsigned int nb_sextets = 0u;
  size_t i;
  size_t n = 0u;
  int valeur;
  bool fini = false;

  PRECONDITION((texte != NULL && sortie != NULL) || taille == 0u);

  /* sur place : l'ecriture (3 octets pour 4 lus) ne rattrape jamais la lecture */
  for (i = 0u; !fini && i < taille; i++) {
    valeur = valeur_base64(texte[i]);
    if (texte[i] == '=') {
      fini = true;
    } else if (valeur >= 0) {
      groupe = (groupe << 6) | (unsigned long)valeur;
      nb_sextets++;
      if (nb_sextets == 4u) {
        sortie[n] = (char)(unsigned char)((groupe >> 16) & 0xFFu);
        sortie[n + 1u] = (char)(unsigned char)((groupe >> 8) & 0xFFu);
        sortie[n + 2u] = (char)(unsigned char)(groupe & 0xFFu);
        n += 3u;
        groupe = 0u;
        nb_sextets = 0u;
      }
    }
  }
  /* groupe incomplet : 2 sextets = 1 octet, 3 sextets = 2 octets */
  if (nb_sextets == 2u) {
    sortie[n] = (char)(unsigned char)((groupe >> 4) & 0xFFu);
    n++;
  } else if (nb_sextets == 3u) {
    sortie[n] = (char)(unsigned char)((groupe >> 10) & 0xFFu);
    sortie[n + 1u] = (char)(unsigned char)((groupe >> 2) & 0xFFu);
    n += 2u;
  }
  POSTCONDITION(n <= (taille / 4u) * 3u + 2u);
  return n;
}


/* ======================================================================== */
/*                              En-tetes MIME                               */
/* ======================================================================== */

/** Recopie au plus taille - 1 octets d'un morceau (en minuscules si
    demande) dans une chaine. */
static void copier(morceau_t m, char *chaine, size_t taille, bool en_minuscules)
{
  size_t n = longueur(m);
  size_t i;

  if (n > taille - 1u) {
    n = taille - 1u;
  }
  for (i = 0u; i < n; i++) {
    chaine[i] = en_minuscules ? minuscule(m.debut[i]) : m.debut[i];
  }
  chaine[n] = '\0';
}

/** Premier jeton d'une valeur (jusqu'a ';' ou un blanc), sans blancs. */
static morceau_t premier_jeton(morceau_t valeur)
{
  morceau_t jeton = sans_blancs(valeur);
  const char *c = jeton.debut;

  while (c < jeton.fin && *c != ';' && *c != ' ' && *c != '\t') {
    c++;
  }
  jeton.fin = c;
  return jeton;
}

/** Premier ';' d'un morceau hors des guillemets, ou fin du morceau. */
static const char *point_virgule_suivant(morceau_t m)
{
  const char *c = m.debut;
  bool entre_guillemets = false;

  while (c < m.fin && (entre_guillemets || *c != ';')) {
    if (*c == '"') {
      entre_guillemets = !entre_guillemets;
    }
    c++;
  }
  return c;
}

/**
 * Valeur du parametre "nom=valeur" ou "nom="valeur"" d'une valeur d'en-tete
 * (nom en minuscules) ; morceau vide si le parametre est absent.
 */
static morceau_t parametre(morceau_t valeur, const char *nom)
{
  morceau_t resultat = { NULL, NULL };
  morceau_t reste = valeur;
  morceau_t element;
  const char *point_virgule;
  const char *egal;
  bool trouve = false;

  /* le premier element (le type) n'est pas un parametre */
  point_virgule = point_virgule_suivant(reste);
  while (!trouve && point_virgule < reste.fin) {
    reste.debut = point_virgule + 1;
    point_virgule = point_virgule_suivant(reste);
    element.debut = reste.debut;
    element.fin = point_virgule;
    element = sans_blancs(element);
    egal = memchr(element.debut, '=', longueur(element));
    if (egal != NULL) {
      morceau_t cle = { element.debut, egal };
      cle = sans_blancs(cle);
      if (egal_sans_casse(cle.debut, longueur(cle), nom)) {
        trouve = true;
        resultat.debut = egal + 1;
        resultat.fin = element.fin;
        resultat = sans_blancs(resultat);
        if (longueur(resultat) >= 1u && *resultat.debut == '"') {
          /* valeur entre guillemets */
          resultat.debut++;
          egal = memchr(resultat.debut, '"', longueur(resultat));
          if (egal != NULL) {
            resultat.fin = egal;
          }
        }
      }
    }
  }
  if (!trouve) {
    resultat = valeur;
    resultat.debut = resultat.fin;          /* morceau vide */
  }
  return resultat;
}

/** Prend en compte un champ d'en-tete deplie (nom, valeur). */
static void noter_champ(entetes_t *e, morceau_t nom, morceau_t valeur)
{
  if (egal_sans_casse(nom.debut, longueur(nom), "content-type")) {
    copier(premier_jeton(valeur), e->type, sizeof(e->type), true);
    copier(parametre(valeur, "boundary"), e->frontiere, sizeof(e->frontiere), false);
    copier(parametre(valeur, "charset"), e->jeu, sizeof(e->jeu), true);
  } else if (egal_sans_casse(nom.debut, longueur(nom), "content-transfer-encoding")) {
    copier(premier_jeton(valeur), e->encodage, sizeof(e->encodage), true);
  } else if (egal_sans_casse(nom.debut, longueur(nom), "content-disposition")) {
    e->piece_jointe = commence_par(premier_jeton(valeur), "attachment");
  }
}

/** Ajoute une ligne (ou une suite de ligne) a la valeur d'un champ, dans
    la limite de TAILLE_VALEUR_MAX octets. */
static void allonger_valeur(char *valeur, size_t *longueur_valeur, morceau_t ligne)
{
  size_t copie = longueur(ligne);

  if (copie > TAILLE_VALEUR_MAX - *longueur_valeur) {
    copie = TAILLE_VALEUR_MAX - *longueur_valeur;
  }
  memcpy(valeur + *longueur_valeur, ligne.debut, copie);
  *longueur_valeur += copie;
}

/**
 * Lit le bloc d'en-tetes d'un message ou d'une partie et renvoie le corps
 * qui le suit. Le bloc se termine par une ligne vide (sautee) ou par une
 * ligne qui n'est ni un champ ni la suite d'un champ (elle commence le
 * corps). Les champs replies sont deplies (au plus TAILLE_VALEUR_MAX
 * octets gardes par valeur).
 */
static morceau_t lire_entetes(morceau_t partie, entetes_t *e)
{
  char valeur[TAILLE_VALEUR_MAX];
  size_t longueur_valeur = 0u;
  morceau_t nom = { NULL, NULL };
  morceau_t ligne;
  morceau_t morceau_valeur;
  morceau_t reste = partie;
  const char *debut_ligne;
  size_t n;
  bool fini = false;

  e->type[0] = '\0';
  e->frontiere[0] = '\0';
  e->jeu[0] = '\0';
  e->encodage[0] = '\0';
  e->piece_jointe = false;
  while (!fini) {
    debut_ligne = reste.debut;
    n = 0u;
    if (!ligne_suivante(&reste, &ligne) || ligne.debut == ligne.fin) {
      fini = true;                          /* fin, ou ligne vide */
    } else if (nom.debut != NULL && est_suite(ligne)) {
      allonger_valeur(valeur, &longueur_valeur, ligne);
    } else if ((n = nom_de_champ(ligne)) == 0u) {
      reste.debut = debut_ligne;            /* le corps commence ici */
      fini = true;
    }
    /* le champ precedent est complet des qu'une ligne n'est pas sa suite */
    if (nom.debut != NULL && (fini || n > 0u)) {
      morceau_valeur.debut = valeur;
      morceau_valeur.fin = valeur + longueur_valeur;
      noter_champ(e, nom, morceau_valeur);
      nom.debut = NULL;
    }
    if (n > 0u) {
      nom.debut = ligne.debut;
      nom.fin = ligne.debut + n;
      ligne.debut += n + 1u;
      longueur_valeur = 0u;
      allonger_valeur(valeur, &longueur_valeur, ligne);
    }
  }
  return reste;
}


/* ======================================================================== */
/*                     Jeux de caracteres et HTML                           */
/* ======================================================================== */

/** Caracteres de Windows-1252 de 0x80 a 0x9F (0 : non defini). */
static const unsigned short WINDOWS_1252[32] = {
  0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
  0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
  0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
  0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
};

/** Jeu de caracteres d'apres son nom (en minuscules). */
static jeu_t reconnaitre_jeu(const char *nom)
{
  static const char *const WINDOWS[] = {
    "iso-8859-1", "iso8859-1", "iso_8859-1", "latin1", "latin-1", "l1",
    "windows-1252", "cp1252", "x-cp1252", "win-1252"
  };
  static const char *const LATIN_9[] = {
    "iso-8859-15", "iso8859-15", "iso_8859-15", "latin9", "latin-9", "l9"
  };
  jeu_t jeu = JEU_RECOPIE;
  size_t i;

  for (i = 0u; i < sizeof(WINDOWS) / sizeof(WINDOWS[0]); i++) {
    if (strcmp(nom, WINDOWS[i]) == 0) {
      jeu = JEU_WINDOWS_1252;
    }
  }
  for (i = 0u; i < sizeof(LATIN_9) / sizeof(LATIN_9[0]); i++) {
    if (strcmp(nom, LATIN_9[i]) == 0) {
      jeu = JEU_LATIN_9;
    }
  }
  return jeu;
}

/** Point de code d'un octet dans un jeu converti. */
static unsigned long point_code_octet(unsigned char octet, jeu_t jeu)
{
  unsigned long point_code = octet;

  if (jeu == JEU_WINDOWS_1252 && octet >= 0x80u && octet <= 0x9Fu) {
    point_code = WINDOWS_1252[octet - 0x80u];
    if (point_code == 0u) {
      point_code = REMPLACEMENT;
    }
  } else if (jeu == JEU_LATIN_9) {
    switch (octet) {
    case 0xA4u: point_code = 0x20ACu; break;  /* euro */
    case 0xA6u: point_code = 0x0160u; break;  /* S caron */
    case 0xA8u: point_code = 0x0161u; break;  /* s caron */
    case 0xB4u: point_code = 0x017Du; break;  /* Z caron */
    case 0xB8u: point_code = 0x017Eu; break;  /* z caron */
    case 0xBCu: point_code = 0x0152u; break;  /* OE lie */
    case 0xBDu: point_code = 0x0153u; break;  /* oe lie */
    case 0xBEu: point_code = 0x0178u; break;  /* Y trema */
    default: break;
    }
  }
  return point_code;
}

/** Ajoute des octets convertis en UTF-8 depuis un jeu de caracteres. */
static void ajouter_converti(sortie_t *s, const char *octets, size_t n, jeu_t jeu)
{
  size_t i;

  if (jeu == JEU_RECOPIE) {
    ajouter(s, octets, n);
  } else {
    for (i = 0u; i < n && s->succes; i++) {
      ajouter_point_code(s, point_code_octet((unsigned char)octets[i], jeu));
    }
  }
}

/** Entites HTML nommees reconnues et leur point de code. */
static const struct {
  const char *nom;
  unsigned short point_code;
} ENTITES[] = {
  { "amp", 0x26 }, { "lt", 0x3C }, { "gt", 0x3E }, { "quot", 0x22 },
  { "apos", 0x27 }, { "nbsp", 0xA0 }, { "laquo", 0xAB }, { "raquo", 0xBB },
  { "agrave", 0xE0 }, { "aacute", 0xE1 }, { "acirc", 0xE2 },
  { "atilde", 0xE3 }, { "auml", 0xE4 }, { "aring", 0xE5 }, { "aelig", 0xE6 },
  { "ccedil", 0xE7 }, { "egrave", 0xE8 }, { "eacute", 0xE9 },
  { "ecirc", 0xEA }, { "euml", 0xEB }, { "igrave", 0xEC }, { "iacute", 0xED },
  { "icirc", 0xEE }, { "iuml", 0xEF }, { "ntilde", 0xF1 }, { "ograve", 0xF2 },
  { "oacute", 0xF3 }, { "ocirc", 0xF4 }, { "otilde", 0xF5 }, { "ouml", 0xF6 },
  { "oslash", 0xF8 }, { "ugrave", 0xF9 }, { "uacute", 0xFA },
  { "ucirc", 0xFB }, { "uuml", 0xFC }, { "yacute", 0xFD }, { "yuml", 0xFF },
  { "szlig", 0xDF }, { "Agrave", 0xC0 }, { "Aacute", 0xC1 },
  { "Acirc", 0xC2 }, { "Auml", 0xC4 }, { "Ccedil", 0xC7 }, { "Egrave", 0xC8 },
  { "Eacute", 0xC9 }, { "Ecirc", 0xCA }, { "Euml", 0xCB }, { "Icirc", 0xCE },
  { "Iuml", 0xCF }, { "Ocirc", 0xD4 }, { "Ouml", 0xD6 }, { "Ugrave", 0xD9 },
  { "Ucirc", 0xDB }, { "Uuml", 0xDC }, { "oelig", 0x153 },
  { "OElig", 0x152 }, { "euro", 0x20AC }, { "hellip", 0x2026 },
  { "ndash", 0x2013 }, { "mdash", 0x2014 }, { "lsquo", 0x2018 },
  { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D },
  { "copy", 0xA9 }, { "deg", 0xB0 }
};

/**
 * Decode l'entite HTML qui commence en texte[i] ('&') : ajoute son
 * caractere et renvoie le nombre d'octets lus, ou 0 si ce n'est pas une
 * entite reconnue (le '&' est alors un caractere ordinaire).
 */
static size_t decoder_entite(sortie_t *s, const char *texte, size_t i, size_t taille)
{
  const char *point_virgule;
  size_t n;
  size_t k;
  size_t lus = 0u;
  unsigned long point_code = 0u;
  int chiffre;
  bool valide = true;

  n = (taille - i < 12u) ? taille - i : 12u;
  point_virgule = memchr(texte + i, ';', n);
  if (point_virgule != NULL) {
    n = (size_t)(point_virgule - (texte + i));     /* '&' ... avant ';' */
    if (n >= 3u && texte[i + 1u] == '#'
        && (texte[i + 2u] == 'x' || texte[i + 2u] == 'X')) {
      for (k = i + 3u; valide && k < i + n; k++) {
        chiffre = valeur_hexadecimale(texte[k]);
        valide = (chiffre >= 0);
        point_code = (point_code << 4) | (unsigned long)(chiffre & 0xF);
      }
      valide = valide && n >= 4u;
    } else if (n >= 2u && texte[i + 1u] == '#') {
      for (k = i + 2u; valide && k < i + n; k++) {
        valide = (texte[k] >= '0' && texte[k] <= '9');
        point_code = point_code * 10u + (unsigned long)(texte[k] - '0');
      }
      valide = valide && n >= 3u;
    } else {
      valide = false;
      for (k = 0u; !valide && k < sizeof(ENTITES) / sizeof(ENTITES[0]); k++) {
        valide = (strlen(ENTITES[k].nom) == n - 1u && memcmp(ENTITES[k].nom, texte + i + 1u, n - 1u) == 0);
        point_code = ENTITES[k].point_code;
      }
    }
    if (valide) {
      ajouter_point_code(s, point_code);
      lus = n + 1u;
    }
  }
  return lus;
}

/** Nom (en minuscules) d'une balise HTML et indicateur de balise
    fermante ; renvoie la longueur du nom (0 si ce n'est pas une balise). */
static size_t nom_de_balise(const char *texte, size_t i, size_t taille,
                            char *nom, size_t taille_nom, bool *fermante)
{
  size_t k = i + 1u;
  size_t n = 0u;

  *fermante = (k < taille && texte[k] == '/');
  if (*fermante) {
    k++;
  }
  while (k < taille && ((texte[k] >= 'a' && texte[k] <= 'z')
                        || (texte[k] >= 'A' && texte[k] <= 'Z')
                        || (n > 0u && texte[k] >= '0' && texte[k] <= '9'))) {
    if (n + 1u < taille_nom) {
      nom[n] = minuscule(texte[k]);
    }
    n++;
    k++;
  }
  nom[(n + 1u < taille_nom) ? n : taille_nom - 1u] = '\0';
  return n;
}

/** Position qui suit la fin de la balise commencee en i (apres '>', en
    ignorant les '>' des valeurs entre guillemets), ou taille. */
static size_t fin_de_balise(const char *texte, size_t i, size_t taille)
{
  char guillemet = '\0';
  size_t k = i + 1u;

  while (k < taille && (guillemet != '\0' || texte[k] != '>')) {
    if (guillemet != '\0' && texte[k] == guillemet) {
      guillemet = '\0';
    } else if (guillemet == '\0' && (texte[k] == '"' || texte[k] == '\'')) {
      guillemet = texte[k];
    }
    k++;
  }
  return (k < taille) ? k + 1u : taille;
}

/**
 * Position qui suit la balise fermante "</nom>" correspondant a la balise
 * ouvrante qui se termine en i (les balises de meme nom imbriquees sont
 * comptees), ou taille.
 */
static size_t apres_element(const char *texte, size_t i, size_t taille, const char *nom)
{
  char autre[16];
  size_t profondeur = 1u;
  bool fermante;

  while (i < taille && profondeur > 0u) {
    if (texte[i] == '<' && nom_de_balise(texte, i, taille, autre, sizeof(autre), &fermante) > 0u
        && strcmp(autre, nom) == 0) {
      profondeur = fermante ? profondeur - 1u : profondeur + 1u;
      i = fin_de_balise(texte, i, taille);
    } else {
      i++;
    }
  }
  return i;
}

/** Indique si un nom figure dans une liste terminee par NULL. */
static bool dans(const char *nom, const char *const *liste)
{
  bool trouve = false;

  while (!trouve && *liste != NULL) {
    trouve = (strcmp(nom, *liste) == 0);
    liste++;
  }
  return trouve;
}

/** Ajoute le texte d'un document HTML (en UTF-8) : cf. mail.h. */
static void ajouter_html(sortie_t *s, const char *texte, size_t taille)
{
  static const char *const IGNORES[] = {
    "head", "script", "style", "blockquote", "title", NULL
  };
  static const char *const LIGNE[] = { "br", "div", "li", "tr", NULL };
  static const char *const PARAGRAPHE[] = {
    "p", "h1", "h2", "h3", "h4", "h5", "h6", "table", "hr", NULL
  };
  char nom[16];
  const char *fin_commentaire;
  size_t i = 0u;
  size_t lus;
  bool fermante;

  while (i < taille && s->succes) {
    if (texte[i] == '<' && taille - i >= 4u
        && memcmp(texte + i, "<!--", 4u) == 0) {
      fin_commentaire = NULL;
      for (lus = i + 4u; fin_commentaire == NULL && lus + 3u <= taille;
           lus++) {
        if (memcmp(texte + lus, "-->", 3u) == 0) {
          fin_commentaire = texte + lus + 3u;
        }
      }
      i = (fin_commentaire != NULL) ? (size_t)(fin_commentaire - texte) : taille;
    } else if (texte[i] == '<' && i + 1u < taille && (texte[i + 1u] == '!' || texte[i + 1u] == '?')) {
      i = fin_de_balise(texte, i, taille);  /* <!DOCTYPE ...>, <?xml ...?> */
    } else if (texte[i] == '<' && nom_de_balise(texte, i, taille, nom, sizeof(nom), &fermante) > 0u) {
      i = fin_de_balise(texte, i, taille);
      if (!fermante && dans(nom, IGNORES)) {
        i = apres_element(texte, i, taille, nom);
      } else if (dans(nom, LIGNE)) {
        ajouter_chaine(s, "\n");
      } else if (dans(nom, PARAGRAPHE)) {
        ajouter_chaine(s, "\n\n");
      }
    } else if (texte[i] == '&' && (lus = decoder_entite(s, texte, i, taille)) > 0u) {
      i += lus;
    } else {
      /* les fins de ligne du source ne sont que des espaces */
      ajouter(s, (texte[i] == '\n' || texte[i] == '\r' || texte[i] == '\t') ? " " : texte + i, 1u);
      i++;
    }
  }
}


/* ======================================================================== */
/*                     Citations et signatures                              */
/* ======================================================================== */

/** Indique si une ligne (sans blancs) annonce une citation : elle se
    termine par "a ecrit :", "wrote:"... */
static bool est_annonce_citation(morceau_t ligne)
{
  /* "a ecrit :" avec une espace, une espace insecable (U+00A0) ou rien
     avant les deux-points */
  static const char *const FINS[] = {
    "a \xC3\xA9" "crit :", "a \xC3\xA9" "crit\xC2\xA0:", "a \xC3\xA9" "crit:",
    "a ecrit :", "a ecrit\xC2\xA0:", "a ecrit:", "wrote:", "schrieb:",
    "escribi\xC3\xB3:", "ha scritto:"
  };
  size_t k;
  size_t n;
  bool annonce = false;

  for (k = 0u; !annonce && k < sizeof(FINS) / sizeof(FINS[0]); k++) {
    n = strlen(FINS[k]);
    annonce = longueur(ligne) >= n && egal_sans_casse(ligne.fin - n, n, FINS[k]);
  }
  return annonce;
}

/** Indique si une ligne (sans blancs) commence comme une annonce de
    citation ("Le 3 mars...", "On Mon, ...", "Am ...", "El ..."). */
static bool debute_comme_annonce(morceau_t ligne)
{
  return commence_par(ligne, "le ") || commence_par(ligne, "on ")
         || commence_par(ligne, "am ") || commence_par(ligne, "el ")
         || commence_par(ligne, "il ");
}

/** Indique si une ligne (sans blancs) annonce un message d'origine ou
    transfere, ou une signature PGP : la suite de la partie est ignoree. */
static bool est_annonce_message_cite(morceau_t ligne)
{
  return (commence_par(ligne, "---")
          && (contient(ligne, "original message")
              || contient(ligne, "message original")
              || contient(ligne, "message d'origine")
              || contient(ligne, "forwarded message")
              || contient(ligne, "message transf")
              || contient(ligne, "courriel transf")
              || contient(ligne, "mail transf")
              || contient(ligne, "begin pgp signature")))
         || commence_par(ligne, "begin forwarded message")
         || commence_par(ligne, "d\xC3\xA9" "but du message r\xC3\xA9"
                                "exp\xC3\xA9" "di\xC3\xA9");
}

/** Indique si une ligne (sans blancs) est une mention d'envoi depuis un
    appareil ("Envoye de mon iPhone"). */
static bool est_mention_d_envoi(morceau_t ligne)
{
  return longueur(ligne) <= 60u
         && (commence_par(ligne, "envoy\xC3\xA9 de mon ")
             || commence_par(ligne, "envoy\xC3\xA9 depuis mon ")
             || commence_par(ligne, "envoye de mon ")
             || commence_par(ligne, "sent from my ")
             || commence_par(ligne, "get outlook for "));
}

/** Indique si une ligne est un delimiteur de signature ("-- " ou "--"). */
static bool est_delimiteur_signature(morceau_t ligne)
{
  return (longueur(ligne) == 3u && memcmp(ligne.debut, "-- ", 3u) == 0)
         || (longueur(ligne) == 2u && memcmp(ligne.debut, "--", 2u) == 0);
}

/**
 * Ajoute le texte d'une partie (en UTF-8), ligne par ligne, sans citations
 * ni signature (cf. mail.h) ; les lignes vides consecutives sont reduites a
 * une seule. Si lignes_protegees, le premier '>' d'une ligne protegee par
 * une boite mbox est d'abord retire.
 */
static void ajouter_nettoye(sortie_t *s, const char *texte, size_t taille, bool lignes_protegees)
{
  morceau_t reste = { texte, texte + taille };
  morceau_t ligne;
  morceau_t utile;
  morceau_t derniere;
  size_t debut_derniere = SIZE_MAX;         /* derniere ligne non vide */
  bool suite_ignoree = false;
  bool entete_pgp = false;

  while (!suite_ignoree && s->succes && ligne_suivante(&reste, &ligne)) {
    if (lignes_protegees && est_ligne_protegee(ligne)) {
      ligne.debut++;                        /* ">From " : "From " du texte */
    }
    utile = sans_blancs(ligne);
    if (entete_pgp) {
      entete_pgp = (longueur(utile) > 0u);  /* "Hash: ..." jusqu'au vide */
    } else if (commence_par(utile, "-----begin pgp signed message")) {
      entete_pgp = true;
    } else if (est_delimiteur_signature(ligne) || est_annonce_message_cite(utile)) {
      suite_ignoree = true;                 /* signature ou message cite */
    } else if (longueur(utile) > 0u && *utile.debut == '>') {
      ;                                     /* ligne citee */
    } else if (est_annonce_citation(utile)) {
      /* annonce coupee en deux lignes ("Le ... <adresse>" puis "a ecrit
         :") : la ligne precedente en fait partie */
      if (!debute_comme_annonce(utile) && debut_derniere != SIZE_MAX) {
        derniere.debut = s->texte + debut_derniere;
        derniere.fin = s->texte + s->longueur;
        if (debute_comme_annonce(sans_blancs(derniere))) {
          s->longueur = debut_derniere;
          s->texte[s->longueur] = '\0';
          debut_derniere = SIZE_MAX;
        }
      }
    } else if (longueur(utile) > 0u) {
      if (!est_mention_d_envoi(utile)) {
        debut_derniere = s->longueur;
        ajouter(s, ligne.debut, longueur(ligne));
        ajouter_chaine(s, "\n");
      }
    } else if (s->longueur > 0u && s->texte[s->longueur - 1u] == '\n'
               && (s->longueur < 2u || s->texte[s->longueur - 2u] != '\n')) {
      /* une ligne de blancs termine un paragraphe ; jamais deux lignes
         vides de suite, ni de ligne vide en tete */
      ajouter_chaine(s, "\n");
    }
  }
}


/* ======================================================================== */
/*                         Messages et parties MIME                         */
/* ======================================================================== */

/** Delimiteurs d'un corps multipart. */
typedef enum {
  PAS_DELIMITEUR,
  DELIMITEUR,           /* "--frontiere" */
  DELIMITEUR_FINAL      /* "--frontiere--" */
} delimiteur_t;

/** Nature d'une ligne d'un corps multipart (blancs finaux toleres). */
static delimiteur_t delimiteur(morceau_t ligne, const char *frontiere)
{
  size_t n = strlen(frontiere);
  morceau_t suite;
  delimiteur_t nature = PAS_DELIMITEUR;

  if (longueur(ligne) >= 2u + n && ligne.debut[0] == '-'
      && ligne.debut[1] == '-'
      && memcmp(ligne.debut + 2u, frontiere, n) == 0) {
    suite.debut = ligne.debut + 2u + n;
    suite.fin = ligne.fin;
    suite = sans_blancs(suite);
    if (longueur(suite) == 0u) {
      nature = DELIMITEUR;
    } else if (longueur(suite) == 2u && memcmp(suite.debut, "--", 2u) == 0) {
      nature = DELIMITEUR_FINAL;
    }
  }
  return nature;
}

/**
 * Detache la partie suivante d'un corps multipart : les octets entre deux
 * delimiteurs (le preambule avant le premier et l'epilogue apres le
 * delimiteur final sont ignores ; sans delimiteur final, la derniere
 * partie va jusqu'a la fin). *commence indique si le premier delimiteur a
 * ete lu. Renvoie false s'il n'y a plus de partie.
 */
static bool partie_suivante(morceau_t *reste, const char *frontiere,
                            bool *commence, morceau_t *partie)
{
  morceau_t ligne;
  const char *debut_ligne;
  delimiteur_t nature;
  bool trouvee = false;
  bool fini = false;

  while (!*commence && ligne_suivante(reste, &ligne)) {
    nature = delimiteur(ligne, frontiere);
    *commence = (nature == DELIMITEUR);
    if (nature == DELIMITEUR_FINAL) {
      reste->debut = reste->fin;            /* aucune partie */
    }
  }
  if (*commence && reste->debut < reste->fin) {
    partie->debut = reste->debut;
    partie->fin = reste->fin;
    while (!fini) {
      debut_ligne = reste->debut;
      fini = !ligne_suivante(reste, &ligne);
      nature = fini ? PAS_DELIMITEUR : delimiteur(ligne, frontiere);
      if (nature != PAS_DELIMITEUR) {
        partie->fin = debut_ligne;
        fini = true;
        if (nature == DELIMITEUR_FINAL) {
          reste->debut = reste->fin;        /* epilogue ignore */
        }
      }
    }
    trouvee = true;
  }
  return trouvee;
}

static void traiter_partie(sortie_t *s, morceau_t partie, unsigned int profondeur);

/** Ajoute le texte d'une partie text/plain ou text/html : decodage,
    conversion en UTF-8, texte du HTML, nettoyage. */
static void ajouter_corps(sortie_t *s, morceau_t corps, const entetes_t *e)
{
  sortie_t utf8 = { NULL, 0u, 0u, true, false };
  sortie_t texte = { NULL, 0u, 0u, true, false };
  char *decode = malloc(longueur(corps) + 4u);
  size_t n;

  s->succes = s->succes && (decode != NULL);
  if (s->succes) {
    if (strcmp(e->encodage, "quoted-printable") == 0) {
      n = mail_decoder_quoted_printable(corps.debut, longueur(corps), decode);
    } else if (strcmp(e->encodage, "base64") == 0) {
      n = mail_decoder_base64(corps.debut, longueur(corps), decode);
    } else {
      n = longueur(corps);
      memcpy(decode, corps.debut, n);
    }
    ajouter_converti(&utf8, decode, n, reconnaitre_jeu(e->jeu));
    if (strcmp(e->type, "text/html") == 0) {
      ajouter_html(&texte, (utf8.texte != NULL) ? utf8.texte : "",
                   utf8.longueur);
    } else {
      texte = utf8;
      utf8.texte = NULL;
    }
    s->succes = utf8.succes && texte.succes;
    /* les lignes protegees sont celles du source : pas celles d'un corps
       en base64, ni celles du texte d'un HTML */
    ajouter_nettoye(s, (texte.texte != NULL) ? texte.texte : "", texte.longueur,
                    s->boite_mbox && strcmp(e->encodage, "base64") != 0
                    && strcmp(e->type, "text/html") != 0);
  }
  free(decode);
  free(utf8.texte);
  free(texte.texte);
}

/** Lit un corps multipart : dans une alternative, la partie text/plain (a
    defaut, la derniere) ; sinon, chaque partie. */
static void traiter_multipart(sortie_t *s, morceau_t corps, const entetes_t *e, unsigned int profondeur)
{
  entetes_t entetes_partie;
  morceau_t reste = corps;
  morceau_t partie;
  morceau_t choisie = { NULL, NULL };
  bool commence = false;
  bool texte_trouve = false;

  if (strcmp(e->type, "multipart/alternative") == 0) {
    while (!texte_trouve && partie_suivante(&reste, e->frontiere, &commence, &partie)) {
      choisie = partie;
      lire_entetes(partie, &entetes_partie);
      texte_trouve = (entetes_partie.type[0] == '\0' || strcmp(entetes_partie.type, "text/plain") == 0);
    }
    if (choisie.debut != NULL) {
      traiter_partie(s, choisie, profondeur + 1u);
    }
  } else {
    while (s->succes && partie_suivante(&reste, e->frontiere, &commence, &partie)) {
      traiter_partie(s, partie, profondeur + 1u);
    }
  }
}

/** Lit une partie (ou un message) : ses en-tetes MIME, puis son corps
    selon son type. */
static void traiter_partie(sortie_t *s, morceau_t partie, unsigned int profondeur)
{
  entetes_t e;
  morceau_t corps = lire_entetes(partie, &e);

  if (profondeur < PROFONDEUR_MIME_MAX && !e.piece_jointe) {
    if (strncmp(e.type, "multipart/", 10u) == 0) {
      if (e.frontiere[0] != '\0') {
        traiter_multipart(s, corps, &e, profondeur);
      }
    } else if (strcmp(e.type, "message/rfc822") == 0) {
      traiter_partie(s, corps, profondeur + 1u);
    } else if (e.type[0] == '\0' || strcmp(e.type, "text/plain") == 0
               || strcmp(e.type, "text/html") == 0) {
      ajouter_corps(s, corps, &e);
    }
  }
}

/** Ajoute le texte d'un message, s'il n'est pas vide, suivi d'une seule
    ligne vide. */
static void traiter_message(sortie_t *s, morceau_t message)
{
  size_t avant = s->longueur;

  traiter_partie(s, message, 0u);
  if (s->succes) {
    while (s->longueur > avant && s->texte[s->longueur - 1u] == '\n') {
      s->longueur--;
    }
    if (s->longueur > avant) {
      ajouter_chaine(s, "\n\n");
    } else {
      s->texte[s->longueur] = '\0';      /* message sans texte */
    }
  }
}

char *mail_extraire_texte(const char *contenu, size_t taille,
                          size_t *taille_texte, size_t *nb_messages)
{
  sortie_t s = { NULL, 0u, 0u, true, false };
  morceau_t reste = { contenu, contenu + taille };
  morceau_t message;
  morceau_t ligne;
  const char *debut_ligne;
  size_t nb = 0u;
  bool ligne_vide_avant = true;

  PRECONDITION(contenu != NULL);
  PRECONDITION(mail_est_courrier(contenu, (taille < MAIL_TAILLE_DETECTION)
                                          ? taille : MAIL_TAILLE_DETECTION));
  PRECONDITION(taille_texte != NULL && nb_messages != NULL);

  if (reserver(&s, 0u)) {
    s.texte[0] = '\0';
  }
  if (taille >= 5u && memcmp(contenu, "From ", 5u) == 0) {
    /* boite mbox : un message commence a chaque ligne "From " qui suit
       une ligne vide (ou ouvre le fichier) */
    s.boite_mbox = true;
    ligne_suivante(&reste, &ligne);
    message.debut = reste.debut;
    while (s.succes && reste.debut < reste.fin) {
      debut_ligne = reste.debut;
      ligne_suivante(&reste, &ligne);
      if (ligne_vide_avant && est_ligne_mbox(ligne)) {
        message.fin = debut_ligne;
        traiter_message(&s, message);
        nb++;
        message.debut = reste.debut;
      }
      ligne_vide_avant = (ligne.debut == ligne.fin);
    }
    message.fin = reste.fin;
  } else {
    message = reste;                        /* un seul message */
  }
  if (s.succes) {
    traiter_message(&s, message);
    nb++;
  }
  if (!s.succes) {
    free(s.texte);
    s.texte = NULL;
    errno = ENOMEM;
  } else {
    *taille_texte = s.longueur;
    *nb_messages = nb;
  }
  POSTCONDITION(s.texte == NULL || (*nb_messages >= 1u && strlen(s.texte) == *taille_texte));
  return s.texte;
}
