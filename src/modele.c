/**
@file modele.c
Fichier modele.c

Implementation du modele de langue (cf. modele.h) : apprentissage,
generation de phrases lisibles, sauvegarde et chargement.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "contrat.h"
#include "modele.h"
#include "repertoire.h"
#include "sauvegarde.h"

/** Premiere ligne d'un fichier de modele (format version 3). */
#define ENTETE_MODELE "modele-markov 3"

/** Premiere ligne d'un fichier de la version 2 (sans repli ni contextes),
    toujours accepte au chargement. */
#define ENTETE_MODELE_2 "modele-markov 2"

/** Aucun mot de depart memorise (cf. etat_de_depart). */
#define AUCUN_DEPART UINT_MAX

/** Melange de la graine pour la chaine de ponctuation : ses tirages sont
    independants de ceux de la chaine des mots. */
#define MELANGE_GRAINE_PONCTUATION 0x5BD1E995u

/** Melange de la graine pour la chaine de repli. */
#define MELANGE_GRAINE_REPLI 0x27D4EB2Fu

/* la fenetre des derniers mots d'une phrase contient au moins trois mots
   (le mot, le precedent et celui d'avant, cf. generer) */
typedef char verification_fenetre[(MODELE_ORDRE_MAX >= 3u) ? 1 : -1];

/** Taille a partir de laquelle le tampon d'ecriture est vide dans le
    fichier lors d'une sauvegarde. */
#define TAILLE_ECRITURE ((size_t)65536)

/** Capacite initiale des tampons de texte. */
#define CAPACITE_TAMPON_INITIALE ((size_t)128)

/** Signe de chaque liaison (cf. liaison_t), suivi d'une espace sauf pour
    l'apostrophe et le trait d'union. */
static const char *const SIGNE_LIAISON[NB_LIAISONS] = {
  "", "'", "-", ".", ",", ";", ":", "!", "?", "..."
};

/** Texte de chaque fin de phrase (etats 0 .. 4 de la ponctuation ; la
    derniere est la fin sans ponctuation). */
static const char *const TEXTE_PONCTUATION[REPERTOIRE_NB_FINS] = {
  ".", "!", "?", "...", ""
};

/** Marques de dialogue, en UTF-8 : tiret cadratin suivi d'une espace,
    guillemets francais ouvrant et fermant, parentheses. */
#define TEXTE_TIRET "\xE2\x80\x94 "
#define TEXTE_GUILLEMET_OUVRANT "\xC2\xAB"
#define TEXTE_GUILLEMET_FERMANT "\xC2\xBB"
#define TEXTE_PARENTHESE_OUVRANTE "("
#define TEXTE_PARENTHESE_FERMANTE ")"

struct modele_s {
  unsigned int ordre;
  hachage_t *mots;
  hachage_paires_t *bigrammes;
  hachage_nuplets_t *etats;     /* k-uplets de mots (ordre >= 3), ou NULL */
  lexique_t *lexique;
  markov_t *chaine;
  markov_t *ponctuation;
  unsigned int ordre_repli;     /* 0 : sans repli */
  markov_t *repli;              /* chaine de repli, ou NULL */
  hachage_nuplets_t *etats_repli; /* j-uplets (repli d'ordre >= 3), ou NULL */
  /* dernier resultat de etat_de_depart (evite de reparcourir les etats) */
  unsigned int depart_mot;      /* AUCUN_DEPART si rien n'est memorise */
  unsigned int depart_etat;
};


/* ======================================================================== */
/*                            Tampon de texte                               */
/* ======================================================================== */

/** Texte en construction (toujours termine par '\0' une fois alloue). */
typedef struct {
  char *texte;
  size_t longueur;
  size_t capacite;
} tampon_t;

/** Garantit la place de supplement octets (plus le '\0'). */
static bool reserver(tampon_t *t, size_t supplement)
{
  size_t capacite = t->capacite;
  char *agrandi;
  bool succes = true;

  if (supplement > SIZE_MAX / 4u - t->longueur) {
    succes = false;
  } else if (t->longueur + supplement + 1u > t->capacite) {
    if (capacite == 0u) {
      capacite = CAPACITE_TAMPON_INITIALE;
    }
    while (t->longueur + supplement + 1u > capacite) {
      capacite *= 2u;
    }
    agrandi = realloc(t->texte, capacite);
    if (agrandi == NULL) {
      succes = false;
    } else {
      t->texte = agrandi;
      t->capacite = capacite;
    }
  }
  if (!succes) {
    errno = ENOMEM;
  }
  return succes;
}

static bool ajouter(tampon_t *t, const char *texte)
{
  size_t longueur = strlen(texte);
  bool succes = reserver(t, longueur);

  if (succes) {
    memcpy(t->texte + t->longueur, texte, longueur + 1u);
    t->longueur += longueur;
  }
  return succes;
}

static bool ajouter_nombre(tampon_t *t, unsigned int nombre)
{
  char texte[16];

  snprintf(texte, sizeof(texte), "%u", nombre);
  return ajouter(t, texte);
}

/** Ajoute " <nombre>". */
static bool ajouter_champ(tampon_t *t, unsigned int nombre)
{
  return ajouter(t, " ") && ajouter_nombre(t, nombre);
}

/** Ajoute " <nombre>", ou " -" si nombre == LEXIQUE_AUCUNE (choix absent). */
static bool ajouter_choix(tampon_t *t, unsigned int nombre)
{
  return ajouter(t, " ") && ((nombre == LEXIQUE_AUCUNE) ? ajouter(t, "-") : ajouter_nombre(t, nombre));
}


/* ======================================================================== */
/*                         Creation et destruction                          */
/* ======================================================================== */

/** Modele dont toutes les structures sont absentes. */
static modele_t *modele_vide(unsigned int ordre)
{
  modele_t *m = malloc(sizeof(modele_t));

  if (m == NULL) {
    errno = ENOMEM;
  } else {
    m->ordre = ordre;
    m->mots = NULL;
    m->bigrammes = NULL;
    m->etats = NULL;
    m->lexique = NULL;
    m->chaine = NULL;
    m->ponctuation = NULL;
    m->ordre_repli = 0u;
    m->repli = NULL;
    m->etats_repli = NULL;
    m->depart_mot = AUCUN_DEPART;
    m->depart_etat = REPERTOIRE_DEBUT_PHRASE;
  }
  return m;
}

void modele_detruire(modele_t **m)
{
  PRECONDITION(m != NULL);
  if (*m != NULL) {
    hachage_detruire(&(*m)->mots);
    hachage_paires_detruire(&(*m)->bigrammes);
    hachage_nuplets_detruire(&(*m)->etats);
    hachage_nuplets_detruire(&(*m)->etats_repli);
    markov_detruire(&(*m)->repli);
    lexique_detruire(&(*m)->lexique);
    markov_detruire(&(*m)->chaine);
    markov_detruire(&(*m)->ponctuation);
    free(*m);
    *m = NULL;
  }
  POSTCONDITION(*m == NULL);
}

modele_t *modele_apprendre_corpus(const char *const *chemins,
                                  size_t nb_chemins, unsigned int graine,
                                  const modele_options_t *options,
                                  repertoire_bilan_t *bilan)
{
  modele_t *m;
  repertoire_options_t o;
  int erreur;

  PRECONDITION(chemins != NULL);
  PRECONDITION(nb_chemins >= 1u);
  PRECONDITION(options != NULL);
  PRECONDITION(options->ordre >= 1u && options->ordre <= MODELE_ORDRE_MAX);
  PRECONDITION(options->ordre_repli < options->ordre);

  m = modele_vide(options->ordre);
  if (m != NULL) {
    m->ordre_repli = options->ordre_repli;
    m->mots = hachage_creer(0u);
    m->bigrammes = hachage_paires_creer(0u);
    m->lexique = lexique_creer();
    m->ponctuation = markov_creer(graine ^ MELANGE_GRAINE_PONCTUATION);
    if (options->ordre >= 3u) {
      m->etats = hachage_nuplets_creer(options->ordre, 0u);
    }
    if (options->ordre_repli >= 1u) {
      m->repli = markov_creer(graine ^ MELANGE_GRAINE_REPLI);
    }
    if (options->ordre_repli >= 3u) {
      m->etats_repli = hachage_nuplets_creer(options->ordre_repli, 0u);
    }
    if (m->mots == NULL || m->bigrammes == NULL || m->lexique == NULL
        || m->ponctuation == NULL
        || (options->ordre >= 3u && m->etats == NULL)
        || (options->ordre_repli >= 1u && m->repli == NULL)
        || (options->ordre_repli >= 3u && m->etats_repli == NULL)) {
      modele_detruire(&m);
      errno = ENOMEM;
    }
  }
  if (m != NULL) {
    o.ordre = options->ordre;
    o.recursif = options->recursif;
    o.bigrammes = m->bigrammes;
    o.lexique = m->lexique;
    o.ponctuation = m->ponctuation;
    o.nuplets = m->etats;
    o.phrase_par_ligne = options->phrase_par_ligne;
    o.filtrer_langues = options->filtrer_langues;
    o.bilan = bilan;
    o.ordre_repli = options->ordre_repli;
    o.repli = m->repli;
    o.nuplets_repli = m->etats_repli;
    m->chaine = repertoire_apprendre_corpus(chemins, nb_chemins, graine, m->mots, &o);
    if (m->chaine == NULL) {
      erreur = errno;
      modele_detruire(&m);
      errno = erreur;
    }
  }
  POSTCONDITION(m == NULL || (modele_ordre(m) == options->ordre
                              && modele_ordre_repli(m) == options->ordre_repli));
  return m;
}

modele_t *modele_apprendre(const char *chemin_repertoire, unsigned int graine,
                           unsigned int ordre, bool recursif)
{
  modele_options_t options;

  PRECONDITION(chemin_repertoire != NULL);
  PRECONDITION(ordre >= 1u && ordre <= MODELE_ORDRE_MAX);

  options.ordre = ordre;
  options.recursif = recursif;
  options.phrase_par_ligne = false;
  options.filtrer_langues = false;
  options.ordre_repli = 0u;
  return modele_apprendre_corpus(&chemin_repertoire, 1u, graine, &options, NULL);
}


/* ======================================================================== */
/*                               Consultation                               */
/* ======================================================================== */

unsigned int modele_ordre(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->ordre;
}

unsigned int modele_nb_mots(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return hachage_nb_mots(m->mots) - 1u;
}

bool modele_peut_generer(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return markov_nb_voisins(m->chaine, REPERTOIRE_DEBUT_PHRASE) > 0u;
}

const hachage_t *modele_mots(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->mots;
}

const hachage_paires_t *modele_bigrammes(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->bigrammes;
}

const hachage_nuplets_t *modele_etats(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->etats;
}

unsigned int modele_ordre_repli(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->ordre_repli;
}

const markov_t *modele_repli(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->repli;
}

const hachage_nuplets_t *modele_etats_repli(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->etats_repli;
}

unsigned int modele_mot(const modele_t *m, const char *graphie)
{
  char *cle;
  unsigned int mot = HACHAGE_ABSENT;

  PRECONDITION(m != NULL);
  PRECONDITION(graphie != NULL);

  cle = repertoire_cle(graphie);            /* errno : EINVAL ou ENOMEM */
  if (cle != NULL) {
    mot = hachage_identifiant(m->mots, cle);
    free(cle);
    if (mot == HACHAGE_ABSENT) {
      errno = ENOENT;
    }
  }
  POSTCONDITION(mot == HACHAGE_ABSENT || (mot >= 1u && mot <= modele_nb_mots(m)));
  return mot;
}

const lexique_t *modele_lexique(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->lexique;
}

const markov_t *modele_chaine(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->chaine;
}

const markov_t *modele_ponctuation(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->ponctuation;
}


/* ======================================================================== */
/*                               Generation                                 */
/* ======================================================================== */

/** Dernier mot d'un etat d'une chaine d'ordre donne (etats : la table des
    n-uplets de cette chaine si ordre >= 3). */
static unsigned int dernier_mot_ordre(const modele_t *m, unsigned int ordre,
                                      const hachage_nuplets_t *etats, unsigned int etat)
{
  unsigned int mot;

  if (ordre == 1u) {
    mot = etat;
  } else if (ordre == 2u) {
    mot = hachage_paires_second(m->bigrammes, etat);
  } else {
    mot = hachage_nuplets_element(etats, etat, ordre - 1u);
  }
  return mot;
}

/** Dernier mot d'un etat de la chaine principale. */
static unsigned int dernier_mot(const modele_t *m, unsigned int etat)
{
  return dernier_mot_ordre(m, m->ordre, m->etats, etat);
}

/**
 * Etat d'une chaine d'ordre donne forme par les derniers mots d'une
 * fenetre (MODELE_ORDRE_MAX mots, le plus recent a la fin), ou
 * HACHAGE_ABSENT si cette suite de mots n'est pas un etat de la chaine.
 */
static unsigned int etat_fenetre(const modele_t *m, unsigned int ordre,
                                 const hachage_nuplets_t *etats,
                                 const unsigned int *fenetre)
{
  unsigned int etat;

  if (ordre == 1u) {
    etat = fenetre[MODELE_ORDRE_MAX - 1u];
  } else if (ordre == 2u) {
    etat = hachage_paires_identifiant(m->bigrammes,
                                      fenetre[MODELE_ORDRE_MAX - 2u],
                                      fenetre[MODELE_ORDRE_MAX - 1u]);
  } else {
    etat = hachage_nuplets_identifiant(etats, fenetre + (MODELE_ORDRE_MAX - ordre));
  }
  return etat;
}

/** Fenetre des mots d'un etat de la chaine principale : ses mots a la fin,
    precedes de 0 (debut de phrase). */
static void remplir_fenetre(const modele_t *m, unsigned int etat, unsigned int *fenetre)
{
  unsigned int r;

  for (r = 0u; r < MODELE_ORDRE_MAX; r++) {
    fenetre[r] = REPERTOIRE_DEBUT_PHRASE;
  }
  if (m->ordre == 1u) {
    fenetre[MODELE_ORDRE_MAX - 1u] = etat;
  } else if (m->ordre == 2u) {
    fenetre[MODELE_ORDRE_MAX - 2u] = hachage_paires_premier(m->bigrammes, etat);
    fenetre[MODELE_ORDRE_MAX - 1u] = hachage_paires_second(m->bigrammes, etat);
  } else {
    for (r = 0u; r < m->ordre; r++) {
      fenetre[MODELE_ORDRE_MAX - m->ordre + r] = hachage_nuplets_element(m->etats, etat, r);
    }
  }
}

/**
 * Marche aleatoire d'une phrase : derniers mots de la phrase (fenetre,
 * completee a gauche par des 0, le plus recent a la fin), etat de la chaine
 * principale qu'ils forment (HACHAGE_ABSENT si cette suite de mots n'est
 * pas un etat, apres un mot tire dans la chaine de repli) et etat de la
 * chaine de repli (0 sans repli).
 */
typedef struct {
  unsigned int fenetre[MODELE_ORDRE_MAX];
  unsigned int etat;
  unsigned int etat_repli;
} marche_t;

/** Place la marche dans un etat de la chaine principale. */
static void placer(const modele_t *m, marche_t *marche, unsigned int etat)
{
  remplir_fenetre(m, etat, marche->fenetre);
  marche->etat = etat;
  marche->etat_repli = REPERTOIRE_DEBUT_PHRASE;
  if (m->repli != NULL) {
    /* les derniers mots d'un etat forment un etat de repli (propriete des
       modeles appris, verifiee au chargement) */
    marche->etat_repli = etat_fenetre(m, m->ordre_repli, m->etats_repli, marche->fenetre);
  }
  INVARIANT(marche->etat_repli != HACHAGE_ABSENT);
}

/**
 * Tire le mot suivant de la phrase et avance la marche. Avec une chaine de
 * repli, le mot est tire dans la chaine de repli quand le contexte
 * principal n'a qu'un successeur ou n'apparait pas dans le corpus : la
 * phrase garde la coherence de l'ordre principal sans recopier le corpus.
 * Renvoie le mot, ou REPERTOIRE_DEBUT_PHRASE si la phrase se termine (la
 * marche est alors inchangee).
 */
static unsigned int avancer(modele_t *m, marche_t *marche)
{
  bool repli = m->repli != NULL && (marche->etat == HACHAGE_ABSENT
                   || markov_nb_voisins(m->chaine, marche->etat) == 1u);
  unsigned int suivant;
  unsigned int mot = REPERTOIRE_DEBUT_PHRASE;
  unsigned int r;

  if (repli) {
    suivant = markov_deplacer(m->repli, marche->etat_repli);
    if (suivant != REPERTOIRE_DEBUT_PHRASE) {
      mot = dernier_mot_ordre(m, m->ordre_repli, m->etats_repli, suivant);
    }
  } else {
    suivant = markov_deplacer(m->chaine, marche->etat);
    if (suivant != REPERTOIRE_DEBUT_PHRASE) {
      mot = dernier_mot(m, suivant);
    }
  }
  if (mot != REPERTOIRE_DEBUT_PHRASE) {
    for (r = 0u; r + 1u < MODELE_ORDRE_MAX; r++) {
      marche->fenetre[r] = marche->fenetre[r + 1u];
    }
    marche->fenetre[MODELE_ORDRE_MAX - 1u] = mot;
    if (repli) {
      marche->etat_repli = suivant;
      marche->etat = etat_fenetre(m, m->ordre, m->etats, marche->fenetre);
    } else {
      marche->etat = suivant;
      if (m->repli != NULL) {
        marche->etat_repli = etat_fenetre(m, m->ordre_repli, m->etats_repli, marche->fenetre);
      }
    }
    INVARIANT(marche->etat_repli != HACHAGE_ABSENT);
  }
  return mot;
}

/**
 * Phrase en cours d'ecriture. Les marques ouvertes (MARQUE_OUVRE_GUILLEMET,
 * MARQUE_OUVRE_PARENTHESE) forment une pile : ouvertes[0] est la plus
 * ancienne ; chaque sorte de marque y figure au plus une fois.
 * citation[r] indique si le passage ouvert par ouvertes[r] commence par une
 * majuscule : c'est alors une phrase citee, dont la ponctuation finale se
 * place avant la marque fermante ("Viens ici!" entre guillemets) ; sinon
 * la marque se ferme avant la ponctuation ("(en riant).").
 */
typedef struct {
  tampon_t texte;
  unsigned int nb_mots;
  unsigned int ouvertes[2];
  bool citation[2];
  unsigned int nb_ouvertes;
  bool succes;            /* false des que la memoire a manque */
} ecriture_t;

/** Ajoute un texte a la phrase (sans effet apres un echec). */
static void ecrire(ecriture_t *e, const char *texte)
{
  e->succes = e->succes && ajouter(&e->texte, texte);
}

/** Ferme la marque ouverte au sommet de la pile. */
static void fermer_sommet(ecriture_t *e)
{
  INVARIANT(e->nb_ouvertes > 0u);
  e->nb_ouvertes--;
  ecrire(e, (e->ouvertes[e->nb_ouvertes] == MARQUE_OUVRE_GUILLEMET)
            ? TEXTE_GUILLEMET_FERMANT : TEXTE_PARENTHESE_FERMANTE);
}

/**
 * Ferme la marque ouvrante donnee si elle est ouverte, ainsi que celles
 * ouvertes apres elle : les marques restent bien imbriquees.
 */
static void fermer(ecriture_t *e, unsigned int ouvrante)
{
  unsigned int rang = e->nb_ouvertes;
  unsigned int r;

  for (r = 0u; r < e->nb_ouvertes; r++) {
    if (e->ouvertes[r] == ouvrante) {
      rang = r;
    }
  }
  while (e->nb_ouvertes > rang) {
    fermer_sommet(e);
  }
}

/** Ouvre une marque si elle n'est pas deja ouverte. */
static void ouvrir(ecriture_t *e, unsigned int ouvrante)
{
  unsigned int r;
  bool ouverte = false;

  for (r = 0u; r < e->nb_ouvertes; r++) {
    ouverte = ouverte || (e->ouvertes[r] == ouvrante);
  }
  if (!ouverte) {
    INVARIANT(e->nb_ouvertes < 2u);
    e->ouvertes[e->nb_ouvertes] = ouvrante;
    e->citation[e->nb_ouvertes] = true;      /* fixe apres le mot suivant */
    e->nb_ouvertes++;
    ecrire(e, (ouvrante == MARQUE_OUVRE_GUILLEMET) ? TEXTE_GUILLEMET_OUVRANT : TEXTE_PARENTHESE_OUVRANTE);
  }
}

/**
 * Indique si le signe d'une liaison se place avant les marques fermantes :
 * c'est le cas du point d'une abreviation et de la ponctuation interne
 * ('!', '?', "...") qui termine un passage cite ("Victoire!" entre
 * guillemets, suivi de "et la fumee..."), mais pas de la virgule, du
 * point-virgule ni des deux-points, qui suivent la marque (dans le corpus,
 * "etc." entre parentheses, "Victoire!" entre guillemets, mais "(en
 * riant)," ou "Viens" entre guillemets suivi d'une virgule).
 */
static bool signe_avant_marques(liaison_t liaison)
{
  return liaison == LIAISON_ABREVIATION || liaison == LIAISON_EXCLAMATION
         || liaison == LIAISON_INTERROGATION
         || liaison == LIAISON_SUSPENSION;
}

/**
 * Ecrit les marques et la liaison qui precedent un mot : signe de la
 * liaison et marques fermantes (dans l'ordre donne par
 * signe_avant_marques), espace, tiret, parenthese ouvrante, guillemet
 * ouvrant. Devant le premier mot, ni marque fermante ni liaison. Renvoie
 * la hauteur de la pile avant l'ouverture des marques ouvrantes : les
 * marques de rang superieur ou egal ont ete ouvertes devant ce mot.
 */
static unsigned int ecrire_separateur(ecriture_t *e, liaison_t liaison, unsigned int marques)
{
  bool avant = signe_avant_marques(liaison);
  unsigned int hauteur;

  if (e->nb_mots > 0u) {
    if (avant) {
      ecrire(e, SIGNE_LIAISON[liaison]);
    }
    if ((marques & MARQUE_FERME_PARENTHESE) != 0u) {
      fermer(e, MARQUE_OUVRE_PARENTHESE);
    }
    if ((marques & MARQUE_FERME_GUILLEMET) != 0u) {
      fermer(e, MARQUE_OUVRE_GUILLEMET);
    }
    if (!avant) {
      ecrire(e, SIGNE_LIAISON[liaison]);
    }
    if (liaison != LIAISON_APOSTROPHE && liaison != LIAISON_TRAIT_UNION) {
      ecrire(e, " ");
    }
  }
  if ((marques & MARQUE_TIRET) != 0u) {
    ecrire(e, TEXTE_TIRET);
  }
  hauteur = e->nb_ouvertes;
  if ((marques & MARQUE_OUVRE_PARENTHESE) != 0u) {
    ouvrir(e, MARQUE_OUVRE_PARENTHESE);
  }
  if ((marques & MARQUE_OUVRE_GUILLEMET) != 0u) {
    ouvrir(e, MARQUE_OUVRE_GUILLEMET);
  }
  return hauteur;
}

/**
 * Apres l'ecriture d'un mot commence a la position donnee : les marques
 * ouvertes juste devant lui (rangs hauteur .. nb_ouvertes - 1) ouvrent une
 * citation si le mot commence par une majuscule.
 */
static void noter_citations(ecriture_t *e, unsigned int hauteur, size_t position)
{
  bool citation;
  unsigned int r;

  if (e->succes && hauteur < e->nb_ouvertes) {
    citation = (lexique_casse(e->texte.texte + position) != CASSE_MINUSCULE);
    for (r = hauteur; r < e->nb_ouvertes; r++) {
      e->citation[r] = citation;
    }
  }
}

/**
 * Ajoute a la phrase le mot d'un bigramme, precede de son separateur (cf.
 * ecrire_separateur), avec la variante choisie dans le contexte du mot qui
 * precede le bigramme (mot_avant, ou LEXIQUE_AUCUNE : aucun contexte). Une
 * marque n'est fermee que si elle est ouverte, et ouverte que si elle est
 * fermee.
 */
static void ecrire_mot(const modele_t *m, ecriture_t *e,
                       unsigned int mot_avant, unsigned int bigramme)
{
  const lexique_t *l = m->lexique;
  bool debut = (e->nb_mots == 0u);
  size_t longueur;
  size_t position;
  unsigned int hauteur;

  hauteur = ecrire_separateur(e, lexique_liaison(l, bigramme), lexique_marques(l, bigramme));
  position = e->texte.longueur;
  longueur = lexique_ecrire_contexte(l, mot_avant, bigramme, debut, NULL, 0u);
  e->succes = e->succes && reserver(&e->texte, longueur);
  if (e->succes) {
    lexique_ecrire_contexte(l, mot_avant, bigramme, debut,
                            e->texte.texte + e->texte.longueur,
                            e->texte.capacite - e->texte.longueur);
    e->texte.longueur += longueur;
  }
  noter_citations(e, hauteur, position);
  e->nb_mots++;
}

/** Ajoute a la phrase son premier mot, ecrit hors de tout contexte. */
static void ecrire_premier_mot_seul(const modele_t *m, ecriture_t *e, unsigned int mot)
{
  size_t longueur = lexique_ecrire_mot(m->lexique, mot, true, NULL, 0u);

  e->succes = e->succes && reserver(&e->texte, longueur);
  if (e->succes) {
    lexique_ecrire_mot(m->lexique, mot, true,
                       e->texte.texte + e->texte.longueur,
                       e->texte.capacite - e->texte.longueur);
    e->texte.longueur += longueur;
  }
  e->nb_mots++;
}

/**
 * Ecrit une phrase. Si depart vaut REPERTOIRE_DEBUT_PHRASE, la phrase part
 * d'un debut de phrase ; sinon elle part de cet etat de la chaine
 * principale, dont le dernier mot est le premier mot de la phrase, ecrit
 * hors contexte si premier_seul (l'etat n'est pas un debut de phrase).
 * Chaque mot suivant est ecrit selon le bigramme qu'il forme avec le mot
 * precedent et selon le mot d'avant (accents des homographes). Renvoie la
 * phrase, ou NULL si la memoire manque.
 */
static char *generer(modele_t *m, unsigned int depart, bool premier_seul)
{
  ecriture_t e = { { NULL, 0u, 0u }, 0u, { 0u, 0u }, { false, false }, 0u, true };
  marche_t marche;
  unsigned int mot;
  unsigned int precedent;
  unsigned int bigramme;
  unsigned int ponctuation = (unsigned int)PONCTUATION_POINT;

  placer(m, &marche, depart);
  mot = (depart == REPERTOIRE_DEBUT_PHRASE) ? avancer(m, &marche) : marche.fenetre[MODELE_ORDRE_MAX - 1u];
  INVARIANT(mot != REPERTOIRE_DEBUT_PHRASE);       /* pas de phrase vide */
  while (mot != REPERTOIRE_DEBUT_PHRASE && e.nb_mots < MODELE_NB_MOTS_MAX_PHRASE) {
    precedent = marche.fenetre[MODELE_ORDRE_MAX - 2u];
    if (premier_seul && e.nb_mots == 0u) {
      ecrire_premier_mot_seul(m, &e, mot);
    } else {
      /* le bigramme (mot precedent, mot) existe : ces deux mots se suivent
         dans le corpus ; le contexte est le mot d'avant (0 au deuxieme mot
         de la phrase), sauf pour le premier mot */
      bigramme = hachage_paires_identifiant(m->bigrammes, precedent, mot);
      INVARIANT(bigramme != HACHAGE_ABSENT);
      ecrire_mot(m, &e, (precedent == REPERTOIRE_DEBUT_PHRASE)
                        ? LEXIQUE_AUCUNE
                        : marche.fenetre[MODELE_ORDRE_MAX - 3u], bigramme);
    }
    mot = avancer(m, &marche);
  }
  /* ponctuation finale tiree selon le dernier mot (sauf phrase coupee) ;
     les marques restees ouvertes sont fermees avant elle, sauf celles qui
     ouvrent une citation : un mot entre guillemets suivi d'un point, mais
     une replique entre guillemets terminee par sa propre ponctuation */
  if (mot == REPERTOIRE_DEBUT_PHRASE) {
    ponctuation = markov_deplacer(m->ponctuation,
                                  REPERTOIRE_NB_FINS
                                  + marche.fenetre[MODELE_ORDRE_MAX - 1u]);
  }
  INVARIANT(ponctuation < REPERTOIRE_NB_FINS);
  while (e.nb_ouvertes > 0u && !e.citation[e.nb_ouvertes - 1u]) {
    fermer_sommet(&e);
  }
  ecrire(&e, TEXTE_PONCTUATION[ponctuation]);
  while (e.nb_ouvertes > 0u) {
    fermer_sommet(&e);
  }
  if (!e.succes) {
    free(e.texte.texte);
    e.texte.texte = NULL;
    errno = ENOMEM;
  }
  POSTCONDITION(e.texte.texte == NULL || e.texte.longueur >= 1u);
  return e.texte.texte;
}

char *modele_generer_phrase(modele_t *m)
{
  PRECONDITION(m != NULL);
  PRECONDITION(modele_peut_generer(m));

  return generer(m, REPERTOIRE_DEBUT_PHRASE, false);
}

/**
 * Etat "debut de phrase suivi du mot" : (0, mot) a l'ordre 2,
 * (0, ..., 0, mot) au-dela, le mot lui-meme a l'ordre 1 ; HACHAGE_ABSENT si
 * le mot ne commence aucune phrase du corpus.
 */
static unsigned int etat_debut_de_phrase(const modele_t *m, unsigned int mot)
{
  unsigned int elements[MODELE_ORDRE_MAX] = { 0u };
  unsigned int etat = hachage_paires_identifiant(m->bigrammes, REPERTOIRE_DEBUT_PHRASE, mot);

  if (etat != HACHAGE_ABSENT && m->ordre == 1u) {
    etat = mot;
  } else if (etat != HACHAGE_ABSENT && m->ordre >= 3u) {
    elements[m->ordre - 1u] = mot;
    etat = hachage_nuplets_identifiant(m->etats, elements);
  }
  return etat;
}

/**
 * Etat d'ou part une phrase qui commence par le mot donne, et indication
 * (premier_seul) que cet etat n'est pas un debut de phrase. Le resultat est
 * memorise : le parcours des etats n'est refait que si le mot change.
 */
static unsigned int etat_de_depart(modele_t *m, unsigned int mot, bool *premier_seul)
{
  unsigned int nb_etats = markov_nb_etats(m->chaine);
  unsigned int etat = etat_debut_de_phrase(m, mot);
  unsigned int e;
  unsigned int poids;
  unsigned int meilleur_poids = 0u;

  *premier_seul = (etat == HACHAGE_ABSENT);
  if (etat == HACHAGE_ABSENT && m->ordre == 1u) {
    etat = mot;
  } else if (etat == HACHAGE_ABSENT && m->depart_mot == mot) {
    etat = m->depart_etat;
  } else if (etat == HACHAGE_ABSENT) {
    /* contexte le plus frequent : etat de plus grand poids total dont le
       mot est le dernier (le premier cree, a poids egal) */
    for (e = 1u; e < nb_etats; e++) {
      if (dernier_mot(m, e) == mot) {
        poids = markov_poids_total(m->chaine, e);
        if (poids > meilleur_poids) {
          meilleur_poids = poids;
          etat = e;
        }
      }
    }
    m->depart_mot = mot;
    m->depart_etat = etat;
  }
  /* tout mot est le dernier mot d'un etat qui a un voisin (propriete des
     modeles appris, verifiee au chargement) */
  INVARIANT(etat != HACHAGE_ABSENT && etat != REPERTOIRE_DEBUT_PHRASE);
  return etat;
}

char *modele_generer_phrase_depuis(modele_t *m, unsigned int mot)
{
  unsigned int etat;
  bool premier_seul = false;

  PRECONDITION(m != NULL);
  PRECONDITION(mot >= 1u && mot <= modele_nb_mots(m));

  etat = etat_de_depart(m, mot, &premier_seul);
  return generer(m, etat, premier_seul);
}


/* ======================================================================== */
/*                               Sauvegarde                                 */
/* ======================================================================== */

/** Ecrit le tampon dans le fichier puis le vide. */
static bool vider(tampon_t *t, FILE *fichier)
{
  bool succes = true;

  if (t->longueur > 0u) {
    succes = sauvegarde_ecrire(fichier, t->texte);
    t->longueur = 0u;
    t->texte[0] = '\0';
  }
  return succes;
}

/** Termine une ligne et vide le tampon s'il est assez rempli. */
static bool fin_de_ligne(tampon_t *t, FILE *fichier)
{
  bool succes = ajouter(t, "\n");

  if (succes && t->longueur >= TAILLE_ECRITURE) {
    succes = vider(t, fichier);
  }
  return succes;
}

/** Ecrit "<titre> <nombre>". */
static bool ecrire_section(tampon_t *t, FILE *fichier, const char *titre, unsigned int nombre)
{
  return ajouter(t, titre) && ajouter_champ(t, nombre) && fin_de_ligne(t, fichier);
}

/** Ecrit les transitions de chaque etat d'une chaine finalisee. */
static bool ecrire_chaine(tampon_t *t, FILE *fichier, const char *titre, const markov_t *chaine)
{
  unsigned int e;
  unsigned int r;
  unsigned int k;
  unsigned int destination;
  unsigned int poids;
  bool succes = ecrire_section(t, fichier, titre, markov_nb_etats(chaine));

  for (e = 0u; succes && e < markov_nb_etats(chaine); e++) {
    k = markov_nb_voisins(chaine, e);
    succes = ajouter_nombre(t, k);
    for (r = 0u; succes && r < k; r++) {
      destination = markov_voisin(chaine, e, r, &poids);
      succes = ajouter_champ(t, destination) && ajouter_champ(t, poids);
    }
    succes = succes && fin_de_ligne(t, fichier);
  }
  return succes;
}

/** Ecrit une table d'etats : les mots de chaque n-uplet. */
static bool ecrire_etats(tampon_t *t, FILE *fichier, const char *titre,
                         const hachage_nuplets_t *etats, unsigned int ordre)
{
  unsigned int e = hachage_nuplets_nb(etats);
  unsigned int i;
  unsigned int r;
  bool succes = ecrire_section(t, fichier, titre, e);

  for (i = 0u; succes && i < e; i++) {
    succes = ajouter_nombre(t, hachage_nuplets_element(etats, i, 0u));
    for (r = 1u; succes && r < ordre; r++) {
      succes = ajouter_champ(t, hachage_nuplets_element(etats, i, r));
    }
    succes = succes && fin_de_ligne(t, fichier);
  }
  return succes;
}

/** Ecrit les contextes de deux mots qui sont des exceptions (les autres
    suivent le choix de leur bigramme). */
static bool ecrire_contextes(const modele_t *m, tampon_t *t, FILE *fichier)
{
  unsigned int nb = lexique_nb_contextes(m->lexique);
  unsigned int nb_exceptions = 0u;
  unsigned int mot_avant;
  unsigned int bigramme;
  unsigned int variante;
  unsigned int i;
  bool succes;

  for (i = 0u; i < nb; i++) {
    if (lexique_contexte(m->lexique, i, &mot_avant, &bigramme) != LEXIQUE_AUCUNE) {
      nb_exceptions++;
    }
  }
  succes = ecrire_section(t, fichier, "contextes", nb_exceptions);
  for (i = 0u; succes && i < nb; i++) {
    variante = lexique_contexte(m->lexique, i, &mot_avant, &bigramme);
    if (variante != LEXIQUE_AUCUNE) {
      succes = ajouter_nombre(t, mot_avant) && ajouter_champ(t, bigramme)
               && ajouter_champ(t, variante) && fin_de_ligne(t, fichier);
    }
  }
  return succes;
}

/** Ecrit tout le modele dans le tampon et le fichier. */
static bool ecrire_modele(const modele_t *m, tampon_t *t, FILE *fichier)
{
  unsigned int n = hachage_nb_mots(m->mots);
  unsigned int b = hachage_paires_nb(m->bigrammes);
  unsigned int i;
  unsigned int r;
  unsigned int k;
  bool succes;

  succes = ajouter(t, ENTETE_MODELE) && fin_de_ligne(t, fichier)
           && ecrire_section(t, fichier, "ordre", m->ordre)
           && ecrire_section(t, fichier, "repli", m->ordre_repli)
           && ecrire_section(t, fichier, "mots", n);
  /* mots : cle, variante choisie, casse, variantes */
  for (i = 0u; succes && i < n; i++) {
    k = lexique_nb_variantes(m->lexique, i);
    succes = ajouter(t, (i == REPERTOIRE_DEBUT_PHRASE) ? "-" : hachage_mot(m->mots, i))
             && ajouter_choix(t, lexique_variante_mot(m->lexique, i))
             && ajouter_champ(t, (unsigned int)lexique_casse_mot(m->lexique, i))
             && ajouter_champ(t, k);
    for (r = 0u; succes && r < k; r++) {
      succes = ajouter(t, " ") && ajouter(t, lexique_variante(m->lexique, i, r));
    }
    succes = succes && fin_de_ligne(t, fichier);
  }
  /* bigrammes : paire et choix d'affichage */
  succes = succes && ecrire_section(t, fichier, "bigrammes", b);
  for (i = 0u; succes && i < b; i++) {
    succes = ajouter_nombre(t, hachage_paires_premier(m->bigrammes, i))
             && ajouter_champ(t, hachage_paires_second(m->bigrammes, i))
             && ajouter_choix(t, lexique_variante_bigramme(m->lexique, i))
             && ajouter_choix(t, lexique_casse_bigramme(m->lexique, i))
             && ajouter_champ(t, (unsigned int)lexique_liaison(m->lexique, i))
             && ajouter_champ(t, lexique_marques(m->lexique, i))
             && fin_de_ligne(t, fichier);
  }
  /* contextes de deux mots, puis etats (ordre >= 3) et chaine principale,
     etats (ordre de repli >= 3) et chaine de repli, ponctuation */
  succes = succes && ecrire_contextes(m, t, fichier);
  if (m->etats != NULL) {
    succes = succes && ecrire_etats(t, fichier, "etats", m->etats, m->ordre);
  }
  succes = succes && ecrire_chaine(t, fichier, "chaine", m->chaine);
  if (m->etats_repli != NULL) {
    succes = succes && ecrire_etats(t, fichier, "etats_repli", m->etats_repli, m->ordre_repli);
  }
  if (m->repli != NULL) {
    succes = succes && ecrire_chaine(t, fichier, "chaine_repli", m->repli);
  }
  succes = succes && ecrire_chaine(t, fichier, "ponctuation", m->ponctuation)
           && ajouter(t, "fin") && fin_de_ligne(t, fichier)
           && vider(t, fichier);
  return succes;
}

bool modele_sauvegarder(const modele_t *m, const char *chemin_fichier)
{
  tampon_t tampon = { NULL, 0u, 0u };
  FILE *fichier = NULL;
  bool succes;
  int erreur = 0;

  PRECONDITION(m != NULL);
  PRECONDITION(chemin_fichier != NULL);

  /* fichier vide (cree ou remplace) puis ecritures en mode ajout */
  succes = sauvegarde_creer_fichier("", chemin_fichier);
  if (succes) {
    fichier = sauvegarde_ouvrir_ajout(chemin_fichier);
    succes = (fichier != NULL);
  }
  if (succes) {
    succes = ecrire_modele(m, &tampon, fichier);
  }
  if (!succes) {
    erreur = errno;
  }
  if (fichier != NULL && !sauvegarde_fermer(fichier) && succes) {
    erreur = errno;
    succes = false;
  }
  free(tampon.texte);
  if (!succes) {
    if (fichier != NULL) {
      sauvegarde_supprimer(chemin_fichier);  /* pas de modele incomplet */
    }
    errno = erreur;
  }
  return succes;
}


/* ======================================================================== */
/*                               Chargement                                 */
/* ======================================================================== */

/** Lecture ligne par ligne d'un fichier de modele. */
typedef struct {
  FILE *fichier;
  tampon_t ligne;
  int erreur;      /* 0, EINVAL, ENOMEM ou EIO */
} lecteur_t;

/**
 * Lit la ligne suivante (sans son '\n') dans lecteur->ligne. Renvoie false
 * a la fin du fichier ou en cas d'erreur (lecteur->erreur est alors non
 * nul). Une ligne contenant un octet nul, ou qui commence ou finit par une
 * espace, est invalide.
 */
static bool lire_ligne(lecteur_t *lecteur)
{
  int octet;
  bool lue = false;
  bool fini = false;

  lecteur->ligne.longueur = 0u;
  while (lecteur->erreur == 0 && !fini) {
    octet = getc(lecteur->fichier);
    if (octet == EOF) {
      fini = true;
      if (ferror(lecteur->fichier)) {
        lecteur->erreur = EIO;
      }
    } else {
      lue = true;
      if (octet == '\n') {
        fini = true;
      } else if (octet == '\0') {
        lecteur->erreur = EINVAL;
      } else if (!reserver(&lecteur->ligne, 1u)) {
        lecteur->erreur = ENOMEM;
      } else {
        lecteur->ligne.texte[lecteur->ligne.longueur] = (char)octet;
        lecteur->ligne.longueur++;
      }
    }
  }
  if (lue && lecteur->erreur == 0 && !reserver(&lecteur->ligne, 0u)) {
    lecteur->erreur = ENOMEM;              /* ligne vide : tampon a creer */
  }
  if (lue && lecteur->erreur == 0) {
    lecteur->ligne.texte[lecteur->ligne.longueur] = '\0';
    if (lecteur->ligne.longueur > 0u && (lecteur->ligne.texte[0] == ' '
            || lecteur->ligne.texte[lecteur->ligne.longueur - 1u] == ' ')) {
      lecteur->erreur = EINVAL;
    }
  }
  return lue && lecteur->erreur == 0;
}

/**
 * Lit la ligne suivante, qui doit exister. Renvoie le curseur de lecture,
 * ou NULL (lecteur->erreur vaut alors EINVAL a la fin du fichier).
 */
static char *ligne_suivante(lecteur_t *lecteur)
{
  char *curseur = NULL;

  if (lire_ligne(lecteur)) {
    curseur = lecteur->ligne.texte;
  } else if (lecteur->erreur == 0) {
    lecteur->erreur = EINVAL;              /* fichier tronque */
  }
  return curseur;
}

/** Lit le jeton suivant (non vide) de la ligne et avance le curseur. */
static bool lire_jeton(char **curseur, char **jeton)
{
  bool lu = (**curseur != '\0' && **curseur != ' ');

  if (lu) {
    *jeton = *curseur;
    while (**curseur != '\0' && **curseur != ' ') {
      (*curseur)++;
    }
    if (**curseur == ' ') {
      **curseur = '\0';
      (*curseur)++;
    }
  }
  return lu;
}

/** Lit un entier decimal non signe (au plus UINT_MAX). */
static bool lire_entier(char **curseur, unsigned int *valeur)
{
  char *jeton;
  const char *c;
  unsigned long long nombre = 0u;
  bool lu = lire_jeton(curseur, &jeton);

  for (c = jeton; lu && *c != '\0'; c++) {
    lu = (*c >= '0' && *c <= '9');
    nombre = (10u * nombre) + (unsigned long long)(*c - '0');
    lu = lu && nombre <= UINT_MAX;
  }
  if (lu) {
    *valeur = (unsigned int)nombre;
  }
  return lu;
}

/** Lit un entier, ou "-" (valeur LEXIQUE_AUCUNE). */
static bool lire_entier_ou_tiret(char **curseur, unsigned int *valeur)
{
  bool lu;

  if ((*curseur)[0] == '-' && ((*curseur)[1] == ' ' || (*curseur)[1] == '\0')) {
    *valeur = LEXIQUE_AUCUNE;
    lu = true;
    (*curseur)++;
    if (**curseur == ' ') {
      (*curseur)++;
    }
  } else {
    lu = lire_entier(curseur, valeur);
  }
  return lu;
}

/** Lit une ligne "<titre> <nombre>". */
static bool lire_section(lecteur_t *lecteur, const char *titre, unsigned int *nombre)
{
  char *curseur = ligne_suivante(lecteur);
  char *jeton;
  bool lu = curseur != NULL && lire_jeton(&curseur, &jeton)
            && strcmp(jeton, titre) == 0 && lire_entier(&curseur, nombre)
            && *curseur == '\0';

  if (!lu && lecteur->erreur == 0) {
    lecteur->erreur = EINVAL;
  }
  return lu;
}

/** Choix d'affichage lus, appliques apres la finalisation du lexique. */
typedef struct {
  unsigned int *variante_mot;
  casse_t *casse_mot;
  unsigned int *variante_bigramme;
  unsigned int *casse_bigramme;
  liaison_t *liaison_bigramme;
  unsigned int *marques_bigramme;
} choix_t;

/** Lit la ligne du mot i (cle, choix, variantes). */
static void lire_mot(lecteur_t *lecteur, modele_t *m, choix_t *choix, unsigned int i)
{
  char *curseur = ligne_suivante(lecteur);
  char *cle = NULL;
  char *variante;
  unsigned int variante_choisie = LEXIQUE_AUCUNE;
  unsigned int casse = 0u;
  unsigned int nb_variantes = 0u;
  unsigned int r;
  unsigned int identifiant;
  bool valide = curseur != NULL && lire_jeton(&curseur, &cle)
                && lire_entier_ou_tiret(&curseur, &variante_choisie)
                && lire_entier(&curseur, &casse) && casse < NB_CASSES
                && lire_entier(&curseur, &nb_variantes);

  /* le mot 0 est "- - c 0", les autres ont une cle {a..z} et au moins une
     variante, dont une choisie */
  if (valide && i == REPERTOIRE_DEBUT_PHRASE) {
    valide = strcmp(cle, "-") == 0 && variante_choisie == LEXIQUE_AUCUNE
             && nb_variantes == 0u;
    cle[0] = '\0';
  } else if (valide) {
    valide = hachage_mot_valide(cle) && nb_variantes > 0u && variante_choisie < nb_variantes;
  }
  if (valide) {
    identifiant = hachage_ajouter(m->mots, cle);
    if (identifiant == HACHAGE_ABSENT) {
      lecteur->erreur = ENOMEM;
    } else if (identifiant != i) {
      valide = false;                      /* mot en double */
    } else if (lexique_ajouter_mot(m->lexique) != i) {
      lecteur->erreur = ENOMEM;
    }
  }
  for (r = 0u; valide && lecteur->erreur == 0 && r < nb_variantes; r++) {
    valide = lire_jeton(&curseur, &variante)
             && lexique_variante_valide(variante)
             && repertoire_graphie_valide(variante, cle);
    if (valide) {
      identifiant = lexique_ajouter_variante(m->lexique, i, variante);
      if (identifiant == LEXIQUE_ABSENT) {
        lecteur->erreur = ENOMEM;
      } else {
        valide = (identifiant == r);       /* variante en double */
      }
    }
  }
  if (valide && lecteur->erreur == 0) {
    valide = (*curseur == '\0');
    choix->variante_mot[i] = variante_choisie;
    choix->casse_mot[i] = (casse_t)casse;
  }
  if (!valide && lecteur->erreur == 0) {
    lecteur->erreur = EINVAL;
  }
}

/** Lit la ligne du bigramme j (paire et choix d'affichage). */
static void lire_bigramme(lecteur_t *lecteur, modele_t *m, choix_t *choix, unsigned int j)
{
  char *curseur = ligne_suivante(lecteur);
  unsigned int n = hachage_nb_mots(m->mots);
  unsigned int premier = 0u;
  unsigned int second = 0u;
  unsigned int variante = 0u;
  unsigned int casse = 0u;
  unsigned int liaison = 0u;
  unsigned int marques = 0u;
  unsigned int identifiant;
  bool valide = curseur != NULL && lire_entier(&curseur, &premier)
                && lire_entier(&curseur, &second)
                && lire_entier_ou_tiret(&curseur, &variante)
                && lire_entier_ou_tiret(&curseur, &casse)
                && lire_entier(&curseur, &liaison)
                && lire_entier(&curseur, &marques) && *curseur == '\0'
                && premier < n && second < n && liaison < NB_LIAISONS
                && lexique_separateur_valide((liaison_t)liaison, marques)
                && (casse == LEXIQUE_AUCUNE || casse < NB_CASSES)
                /* la paire 0 est (0, 0) ; les autres decrivent un vrai mot */
                && ((j == 0u) ? (premier == 0u && second == 0u && variante == LEXIQUE_AUCUNE)
                              : second != REPERTOIRE_DEBUT_PHRASE)
                && (variante == LEXIQUE_AUCUNE || variante < lexique_nb_variantes(m->lexique, second));

  if (valide) {
    identifiant = hachage_paires_ajouter(m->bigrammes, premier, second);
    if (identifiant == HACHAGE_ABSENT) {
      lecteur->erreur = ENOMEM;
    } else if (identifiant != j) {
      valide = false;                      /* paire en double */
    } else if (lexique_ajouter_bigramme(m->lexique, second) != j) {
      lecteur->erreur = ENOMEM;
    } else {
      choix->variante_bigramme[j] = variante;
      choix->casse_bigramme[j] = casse;
      choix->liaison_bigramme[j] = (liaison_t)liaison;
      choix->marques_bigramme[j] = marques;
    }
  }
  if (!valide && lecteur->erreur == 0) {
    lecteur->erreur = EINVAL;
  }
}

/** Variante affichee pour le mot d'un bigramme hors de tout contexte. */
static unsigned int variante_hors_contexte(const modele_t *m, unsigned int bigramme)
{
  unsigned int variante = lexique_variante_bigramme(m->lexique, bigramme);

  if (variante == LEXIQUE_AUCUNE) {
    variante = lexique_variante_mot(m->lexique, hachage_paires_second(m->bigrammes, bigramme));
  }
  return variante;
}

/**
 * Lit la ligne d'un contexte de deux mots (le lexique est finalise) : mot
 * avant, bigramme et variante. Les trois mots se suivent dans le corpus
 * (le bigramme ne commence pas une phrase et le bigramme (mot avant,
 * premier mot) existe) ; la variante est une variante du mot, differente
 * de celle du bigramme (seules les exceptions sont sauvegardees), et le
 * contexte n'a pas deja ete lu.
 */
static void lire_contexte(lecteur_t *lecteur, modele_t *m)
{
  char *curseur = ligne_suivante(lecteur);
  unsigned int mot_avant = 0u;
  unsigned int bigramme = 0u;
  unsigned int variante = 0u;
  unsigned int premier;
  bool valide = curseur != NULL && lire_entier(&curseur, &mot_avant)
                && lire_entier(&curseur, &bigramme)
                && lire_entier(&curseur, &variante) && *curseur == '\0'
                && mot_avant < hachage_nb_mots(m->mots)
                && bigramme < hachage_paires_nb(m->bigrammes);

  if (valide) {
    premier = hachage_paires_premier(m->bigrammes, bigramme);
    valide = premier != REPERTOIRE_DEBUT_PHRASE
             && hachage_paires_identifiant(m->bigrammes, mot_avant, premier) != HACHAGE_ABSENT
             && variante < lexique_nb_variantes(m->lexique, hachage_paires_second(m->bigrammes, bigramme))
             && variante != variante_hors_contexte(m, bigramme)
             && lexique_variante_contexte(m->lexique, mot_avant, bigramme) == LEXIQUE_AUCUNE;
  }
  if (valide && !lexique_fixer_contexte(m->lexique, mot_avant, bigramme, variante)) {
    lecteur->erreur = ENOMEM;
  }
  if (!valide && lecteur->erreur == 0) {
    lecteur->erreur = EINVAL;
  }
}

/**
 * Lit la ligne de l'etat j d'une table de n-uplets (ordre >= 3) : n mots.
 * L'etat 0 est (0, ..., 0) ; les autres se terminent par un vrai mot et
 * leurs 0 (debut de phrase) sont tous en tete.
 */
static void lire_etat(lecteur_t *lecteur, const modele_t *m,
                      hachage_nuplets_t *etats, unsigned int ordre, unsigned int j)
{
  char *curseur = ligne_suivante(lecteur);
  unsigned int n = hachage_nb_mots(m->mots);
  unsigned int elements[MODELE_ORDRE_MAX] = { 0u };
  unsigned int identifiant;
  unsigned int r;
  bool valide = (curseur != NULL);

  for (r = 0u; valide && r < ordre; r++) {
    valide = lire_entier(&curseur, &elements[r]) && elements[r] < n
             && (r == 0u || elements[r] != REPERTOIRE_DEBUT_PHRASE
                 || elements[r - 1u] == REPERTOIRE_DEBUT_PHRASE);
  }
  valide = valide && *curseur == '\0' && ((elements[ordre - 1u] == REPERTOIRE_DEBUT_PHRASE) == (j == 0u));
  if (valide) {
    identifiant = hachage_nuplets_ajouter(etats, elements);
    if (identifiant == HACHAGE_ABSENT) {
      lecteur->erreur = ENOMEM;
    } else if (identifiant != j) {
      valide = false;                      /* etat en double */
    }
  }
  if (!valide && lecteur->erreur == 0) {
    lecteur->erreur = EINVAL;
  }
}

/** Lit une section de transitions dans une chaine (qui est creee). */
static markov_t *lire_chaine(lecteur_t *lecteur, const char *titre,
                             unsigned int nb_etats_attendu,
                             unsigned int graine)
{
  markov_t *chaine = NULL;
  char *curseur;
  unsigned int nb_etats = 0u;
  unsigned int e;
  unsigned int r;
  unsigned int k = 0u;
  unsigned int destination = 0u;
  unsigned int poids = 0u;
  unsigned int minimum;
  bool valide = lire_section(lecteur, titre, &nb_etats) && nb_etats == nb_etats_attendu;

  if (valide) {
    chaine = markov_creer(graine);
    if (chaine == NULL) {
      lecteur->erreur = ENOMEM;
    }
  }
  for (e = 0u; valide && lecteur->erreur == 0 && e < nb_etats; e++) {
    if (markov_ajouter_etat(chaine) != e) {
      lecteur->erreur = ENOMEM;
    }
  }
  for (e = 0u; valide && lecteur->erreur == 0 && e < nb_etats; e++) {
    curseur = ligne_suivante(lecteur);
    valide = curseur != NULL && lire_entier(&curseur, &k);
    minimum = 0u;
    for (r = 0u; valide && lecteur->erreur == 0 && r < k; r++) {
      /* etats d'arrivee strictement croissants : pas de doublon */
      valide = lire_entier(&curseur, &destination)
               && lire_entier(&curseur, &poids) && destination < nb_etats
               && destination >= minimum && poids > 0u;
      if (valide && !markov_augmenter_poids(chaine, e, destination, poids)) {
        if (errno == ENOMEM) {
          lecteur->erreur = ENOMEM;
        } else {
          valide = false;                  /* poids total trop grand */
        }
      }
      minimum = destination + 1u;
    }
    valide = valide && *curseur == '\0';
  }
  if (!valide && lecteur->erreur == 0) {
    lecteur->erreur = EINVAL;
  }
  if (lecteur->erreur != 0) {
    markov_detruire(&chaine);
  } else {
    markov_finaliser(chaine);
  }
  return chaine;
}

/**
 * Verifie qu'une transition e -> d (d != 0) d'une chaine d'ordre donne
 * enchaine bien deux etats : les mots de d sont ceux de e decales d'un
 * rang, et le bigramme (dernier mot de e, dernier mot de d) est connu du
 * lexique.
 */
static bool transition_coherente(const modele_t *m, unsigned int ordre,
                                 const hachage_nuplets_t *etats,
                                 unsigned int e, unsigned int d)
{
  unsigned int r;
  bool valide;

  if (ordre == 1u) {
    valide = (hachage_paires_identifiant(m->bigrammes, e, d) != HACHAGE_ABSENT);
  } else if (ordre == 2u) {
    valide = (hachage_paires_premier(m->bigrammes, d) == hachage_paires_second(m->bigrammes, e));
  } else {
    valide = (hachage_paires_identifiant(m->bigrammes,
                                         dernier_mot_ordre(m, ordre, etats, e),
                                         dernier_mot_ordre(m, ordre, etats, d))
              != HACHAGE_ABSENT);
    for (r = 0u; valide && r + 1u < ordre; r++) {
      valide = (hachage_nuplets_element(etats, d, r) == hachage_nuplets_element(etats, e, r + 1u));
    }
  }
  return valide;
}

/**
 * Verifie les transitions d'une chaine d'ordre donne : chaque etat autre
 * que le debut de phrase a un voisin, chaque transition enchaine deux etats
 * coherents, et chaque fin de phrase (jamais juste apres le debut : pas de
 * phrase vide) a une ponctuation possible.
 */
static bool chaine_coherente(const modele_t *m, unsigned int ordre,
                             const hachage_nuplets_t *etats,
                             const markov_t *chaine)
{
  unsigned int e;
  unsigned int r;
  unsigned int d;
  unsigned int poids;
  bool valide = true;

  for (e = 0u; valide && e < markov_nb_etats(chaine); e++) {
    valide = (e == REPERTOIRE_DEBUT_PHRASE || markov_nb_voisins(chaine, e) > 0u);
    for (r = 0u; valide && r < markov_nb_voisins(chaine, e); r++) {
      d = markov_voisin(chaine, e, r, &poids);
      if (d == REPERTOIRE_DEBUT_PHRASE) {
        valide = (e != REPERTOIRE_DEBUT_PHRASE)
                 && markov_nb_voisins(m->ponctuation,
                                      REPERTOIRE_NB_FINS
                                      + dernier_mot_ordre(m, ordre, etats, e))
                    > 0u;
      } else {
        valide = transition_coherente(m, ordre, etats, e, d);
      }
    }
  }
  return valide;
}

/**
 * Verifie qu'un modele charge peut etre utilise sans risque : ses chaines
 * sont coherentes (cf. chaine_coherente), chaque mot est le dernier mot
 * d'un etat (on peut faire commencer une phrase par n'importe quel mot)
 * et, avec un repli, les derniers mots de chaque etat forment un etat de
 * la chaine de repli, qui a un debut de phrase si la chaine principale en
 * a un. Renvoie false si le modele est incoherent ou si la memoire manque
 * (*erreur recoit EINVAL ou ENOMEM).
 */
static bool verifier_coherence(const modele_t *m, int *erreur)
{
  unsigned int nb_etats = markov_nb_etats(m->chaine);
  unsigned int nb_mots = hachage_nb_mots(m->mots);
  unsigned char *vus = calloc(nb_mots, sizeof(unsigned char));
  unsigned int fenetre[MODELE_ORDRE_MAX];
  unsigned int e;
  unsigned int r;
  unsigned int d;
  unsigned int poids;
  bool valide = (vus != NULL);

  *erreur = valide ? EINVAL : ENOMEM;
  valide = valide && chaine_coherente(m, m->ordre, m->etats, m->chaine);
  if (valide && m->repli != NULL) {
    valide = chaine_coherente(m, m->ordre_repli, m->etats_repli, m->repli)
             && ((markov_nb_voisins(m->repli, REPERTOIRE_DEBUT_PHRASE) > 0u)
                 == (markov_nb_voisins(m->chaine, REPERTOIRE_DEBUT_PHRASE)
                     > 0u));
  }
  for (e = 0u; valide && e < nb_etats; e++) {
    vus[dernier_mot(m, e)] = 1u;
    if (m->repli != NULL) {
      remplir_fenetre(m, e, fenetre);
      valide = (etat_fenetre(m, m->ordre_repli, m->etats_repli, fenetre) != HACHAGE_ABSENT);
    }
  }
  /* chaque mot termine au moins un etat */
  for (e = 0u; valide && e < nb_mots; e++) {
    valide = (vus[e] != 0u);
  }
  /* ponctuation : seuls les mots menent, et seulement a une ponctuation */
  for (e = 0u; valide && e < markov_nb_etats(m->ponctuation); e++) {
    for (r = 0u; valide && r < markov_nb_voisins(m->ponctuation, e); r++) {
      d = markov_voisin(m->ponctuation, e, r, &poids);
      valide = (e >= REPERTOIRE_NB_FINS && d < REPERTOIRE_NB_FINS);
    }
  }
  free(vus);
  return valide;
}

/**
 * Lit l'en-tete (version 2 ou 3), l'ordre et l'ordre de repli (version 3)
 * et cree les tables d'etats. Renvoie la version (0 en cas d'erreur).
 */
static unsigned int lire_entete(lecteur_t *lecteur, modele_t *m)
{
  char *curseur = ligne_suivante(lecteur);
  unsigned int version = 0u;

  if (curseur != NULL) {
    if (strcmp(curseur, ENTETE_MODELE) == 0) {
      version = 3u;
    } else if (strcmp(curseur, ENTETE_MODELE_2) == 0) {
      version = 2u;
    } else {
      lecteur->erreur = EINVAL;
    }
  }
  if (lecteur->erreur == 0 && lire_section(lecteur, "ordre", &m->ordre)
      && (m->ordre < 1u || m->ordre > MODELE_ORDRE_MAX)) {
    lecteur->erreur = EINVAL;
  }
  if (lecteur->erreur == 0 && version >= 3u
      && lire_section(lecteur, "repli", &m->ordre_repli)
      && m->ordre_repli >= m->ordre) {
    lecteur->erreur = EINVAL;
  }
  if (lecteur->erreur == 0 && m->ordre >= 3u) {
    m->etats = hachage_nuplets_creer(m->ordre, 0u);
    if (m->etats == NULL) {
      lecteur->erreur = ENOMEM;
    }
  }
  if (lecteur->erreur == 0 && m->ordre_repli >= 3u) {
    m->etats_repli = hachage_nuplets_creer(m->ordre_repli, 0u);
    if (m->etats_repli == NULL) {
      lecteur->erreur = ENOMEM;
    }
  }
  return (lecteur->erreur == 0) ? version : 0u;
}

/**
 * Lit une section d'etats (n-uplets de mots, ordre >= 3), qui doit
 * contenir au moins l'etat 0, et renvoie leur nombre.
 */
static unsigned int lire_etats(lecteur_t *lecteur, const modele_t *m,
                               const char *titre, hachage_nuplets_t *etats,
                               unsigned int ordre)
{
  unsigned int e = 0u;
  unsigned int i;

  if (lire_section(lecteur, titre, &e) && e == 0u) {
    lecteur->erreur = EINVAL;
  }
  for (i = 0u; lecteur->erreur == 0 && i < e; i++) {
    lire_etat(lecteur, m, etats, ordre, i);
  }
  return e;
}

/** Nombre d'etats d'une chaine d'ordre donne : n mots a l'ordre 1, b
    bigrammes a l'ordre 2, e n-uplets au-dela. */
static unsigned int nb_etats_chaine(unsigned int ordre, unsigned int n,
                                    unsigned int b, unsigned int e)
{
  return (ordre == 1u) ? n : ((ordre == 2u) ? b : e);
}

/** Lit tout le fichier (le modele m a deja ses tables vides). */
static void lire_modele(lecteur_t *lecteur, modele_t *m, unsigned int graine)
{
  choix_t choix = { NULL, NULL, NULL, NULL, NULL, NULL };
  char *curseur;
  unsigned int version;
  unsigned int n = 0u;
  unsigned int b = 0u;
  unsigned int c = 0u;
  unsigned int e = 0u;
  unsigned int i;
  int erreur_coherence = 0;

  /* en-tete, ordre, ordre de repli et nombre de mots */
  version = lire_entete(lecteur, m);
  if (lecteur->erreur == 0 && lire_section(lecteur, "mots", &n) && n == 0u) {
    lecteur->erreur = EINVAL;
  }
  if (lecteur->erreur == 0) {
    choix.variante_mot = calloc(n, sizeof(unsigned int));
    choix.casse_mot = calloc(n, sizeof(casse_t));
    if (choix.variante_mot == NULL || choix.casse_mot == NULL) {
      lecteur->erreur = ENOMEM;
    }
  }
  for (i = 0u; lecteur->erreur == 0 && i < n; i++) {
    lire_mot(lecteur, m, &choix, i);
  }
  /* bigrammes */
  if (lecteur->erreur == 0 && lire_section(lecteur, "bigrammes", &b)
      && b == 0u) {
    lecteur->erreur = EINVAL;
  }
  if (lecteur->erreur == 0) {
    choix.variante_bigramme = calloc(b, sizeof(unsigned int));
    choix.casse_bigramme = calloc(b, sizeof(unsigned int));
    choix.liaison_bigramme = calloc(b, sizeof(liaison_t));
    choix.marques_bigramme = calloc(b, sizeof(unsigned int));
    if (choix.variante_bigramme == NULL || choix.casse_bigramme == NULL
        || choix.liaison_bigramme == NULL
        || choix.marques_bigramme == NULL) {
      lecteur->erreur = ENOMEM;
    }
  }
  for (i = 0u; lecteur->erreur == 0 && i < b; i++) {
    lire_bigramme(lecteur, m, &choix, i);
  }
  /* le lexique est complet : choix d'affichage des mots et des bigrammes */
  if (lecteur->erreur == 0) {
    lexique_finaliser(m->lexique);
    for (i = 1u; i < n; i++) {
      lexique_fixer_mot(m->lexique, i, choix.variante_mot[i], choix.casse_mot[i]);
    }
    for (i = 0u; i < b; i++) {
      lexique_fixer_bigramme(m->lexique, i, choix.variante_bigramme[i],
                             choix.casse_bigramme[i],
                             choix.liaison_bigramme[i],
                             choix.marques_bigramme[i]);
    }
  }
  /* contextes de deux mots (version 3) */
  if (lecteur->erreur == 0 && version >= 3u) {
    lire_section(lecteur, "contextes", &c);
  }
  for (i = 0u; lecteur->erreur == 0 && i < c; i++) {
    lire_contexte(lecteur, m);
  }
  /* chaine principale (et ses etats si ordre >= 3) */
  if (lecteur->erreur == 0 && m->ordre >= 3u) {
    e = lire_etats(lecteur, m, "etats", m->etats, m->ordre);
  }
  if (lecteur->erreur == 0) {
    m->chaine = lire_chaine(lecteur, "chaine", nb_etats_chaine(m->ordre, n, b, e), graine);
  }
  /* chaine de repli (et ses etats si ordre de repli >= 3) */
  if (lecteur->erreur == 0 && m->ordre_repli >= 3u) {
    e = lire_etats(lecteur, m, "etats_repli", m->etats_repli, m->ordre_repli);
  }
  if (lecteur->erreur == 0 && m->ordre_repli >= 1u) {
    m->repli = lire_chaine(lecteur, "chaine_repli",
                           nb_etats_chaine(m->ordre_repli, n, b, e),
                           graine ^ MELANGE_GRAINE_REPLI);
  }
  if (lecteur->erreur == 0) {
    m->ponctuation = lire_chaine(lecteur, "ponctuation",
                                 REPERTOIRE_NB_FINS + n,
                                 graine ^ MELANGE_GRAINE_PONCTUATION);
  }
  /* "fin", puis plus rien ; verification globale */
  if (lecteur->erreur == 0) {
    curseur = ligne_suivante(lecteur);
    if (curseur != NULL && (strcmp(curseur, "fin") != 0 || lire_ligne(lecteur))) {
      lecteur->erreur = EINVAL;
    }
  }
  if (lecteur->erreur == 0 && !verifier_coherence(m, &erreur_coherence)) {
    lecteur->erreur = erreur_coherence;
  }
  free(choix.variante_mot);
  free(choix.casse_mot);
  free(choix.variante_bigramme);
  free(choix.casse_bigramme);
  free(choix.liaison_bigramme);
  free(choix.marques_bigramme);
}

modele_t *modele_charger(const char *chemin_fichier, unsigned int graine)
{
  lecteur_t lecteur = { NULL, { NULL, 0u, 0u }, 0 };
  modele_t *m;
  int erreur = 0;

  PRECONDITION(chemin_fichier != NULL);

  m = modele_vide(1u);
  if (m == NULL) {
    erreur = ENOMEM;
  } else {
    m->mots = hachage_creer(0u);
    m->bigrammes = hachage_paires_creer(0u);
    m->lexique = lexique_creer();
    if (m->mots == NULL || m->bigrammes == NULL || m->lexique == NULL) {
      erreur = ENOMEM;
    } else {
      lecteur.fichier = fopen(chemin_fichier, "rb");
      if (lecteur.fichier == NULL) {
        erreur = errno;
      } else {
        lire_modele(&lecteur, m, graine);
        erreur = lecteur.erreur;
        fclose(lecteur.fichier);
      }
    }
  }
  free(lecteur.ligne.texte);
  if (erreur != 0) {
    modele_detruire(&m);
    errno = erreur;
  }
  POSTCONDITION(m == NULL || (modele_ordre(m) >= 1u
                              && modele_ordre(m) <= MODELE_ORDRE_MAX
                              && modele_ordre_repli(m) < modele_ordre(m)));
  return m;
}
