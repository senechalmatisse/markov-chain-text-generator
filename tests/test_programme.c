/**
Fichier test_programme.c

Test du programme principal : lance l'executable (premier argument, par
defaut ./projet) avec differentes options et verifie le texte produit :
une phrase par ligne, qui commence par une majuscule (apres un tiret de
dialogue et un guillemet eventuels), se termine par une ponctuation finale
(suivie eventuellement d'un guillemet) et ne contient que des mots du
corpus. Verifie aussi la reproductibilite (graine), les ordres superieurs,
le premier mot impose, les corpus de plusieurs chemins, la lecture "une
phrase par ligne", le filtre de langue, la sauvegarde et le rechargement
d'un modele, l'ancienne syntaxe et les codes de retour en cas d'erreur.
A lancer depuis la racine du projet (le corpus est dataset/).
*/
/* realpath est une fonction POSIX de l'extension XSI */
#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "outils_tests.h"
#include "../src/repertoire.h"
#include "../src/lexique.h"

/** Executable teste (chemin absolu) et corpus (chemins absolus). */
static char *programme = NULL;
static char *corpus = NULL;
static char *corpus_anglais = NULL;

/** Repertoire des fichiers produits par les tests. */
static char *travail = NULL;

/** Mots du corpus (non recursif, puis avec les sous-repertoires). */
static hachage_t *mots_corpus = NULL;
static hachage_t *mots_corpus_recursif = NULL;
static hachage_t *mots_corpus_anglais = NULL;

/* ======================================================================== */
/*                                 Utilitaires                              */
/* ======================================================================== */

/**
 * Lance le programme avec les arguments donnes (tableau termine par NULL),
 * depuis le repertoire courant donne (ou celui du test si NULL). Ses
 * sorties sont ecrites dans travail/sortie.txt et travail/erreurs.txt.
 * Renvoie son code de retour, ou -1 s'il a ete arrete par un signal.
 */
static int lancer(const char *const arguments[], const char *courant)
{
  const char *argv[16];
  char *sortie = chemin_dans(travail, "sortie.txt");
  char *erreurs = chemin_dans(travail, "erreurs.txt");
  size_t n = 0u;
  pid_t fils;
  int statut = 0;
  int code = -1;

  argv[0] = programme;
  while (arguments[n] != NULL) {
    assert(n + 2u < sizeof(argv) / sizeof(argv[0]));
    argv[n + 1u] = arguments[n];
    n++;
  }
  argv[n + 1u] = NULL;
  fflush(stdout);
  fflush(stderr);
  fils = fork();
  if (fils == 0) {
    if (freopen(sortie, "w", stdout) == NULL
        || freopen(erreurs, "w", stderr) == NULL
        || (courant != NULL && chdir(courant) != 0)) {
      _exit(126);
    }
    execv(programme, (char *const *)argv);
    _exit(127);
  }
  assert(fils > 0);
  assert(waitpid(fils, &statut, 0) == fils);
  if (WIFEXITED(statut)) {
    code = WEXITSTATUS(statut);
  }
  free(sortie);
  free(erreurs);
  return code;
}

/** Textes courts dans les langues ajoutees au detecteur, en UTF-8 avec
    leurs accents (lettres d'Europe centrale comprises). */
static const struct {
  const char *langue;
  const char *texte;
} TEXTES_EUROPEENS[] = {
  { "latin",
    "Puer in horto ambulat et cum amicis suis ludit, sed mater "
    "eum vocat, quia cena iam parata est. Non est facile semper "
    "bene agere, nam homines saepe errant; tamen qui laborat et "
    "sperat ad finem perveniet. Ego te video et tu me audis, et "
    "nos omnes in hac urbe magna vivimus, quae inter montes et "
    "mare posita est. Si vis pacem, para bellum, ut dicunt "
    "antiqui, sed ego pacem sine bello malo." },
  { "catalan",
    "El meu germ\xC3\xA0 viu a Barcelona amb la seva dona i els "
    "seus dos fills. Cada mat\xC3\xAD agafa el tren per anar a "
    "la feina, per\xC3\xB2 a vegades prefereix caminar "
    "perqu\xC3\xA8 li agrada molt passejar pel barri. Quan "
    "arriba a casa, sopen tots junts i parlen del que han fet "
    "durant el dia. Tamb\xC3\xA9 els agrada llegir llibres i "
    "escoltar m\xC3\xBAsica abans d'anar a dormir, i els caps de "
    "setmana sovint van a la muntanya." },
  { "suedois",
    "Det var en g\xC3\xA5ng en liten pojke som bodde i ett "
    "r\xC3\xB6tt hus vid sj\xC3\xB6n. Varje morgon gick han ner "
    "till vattnet f\xC3\xB6r att se p\xC3\xA5 f\xC3\xA5glarna, "
    "och hans hund f\xC3\xB6ljde alltid med honom. Han tyckte "
    "mycket om att fiska, men han hade inte s\xC3\xA5 mycket "
    "t\xC3\xA5lamod. N\xC3\xA4r solen gick ner kom hans mor och "
    "kallade p\xC3\xA5 honom, och d\xC3\xA5 sprang de hem "
    "tillsammans f\xC3\xB6r att \xC3\xA4ta middag med resten av "
    "familjen." },
  { "danois",
    "Der var engang en gammel mand, som boede i et lille hus ved "
    "havet. Hver dag gik han ned til stranden for at se "
    "p\xC3\xA5 b\xC3\xB8lgerne, og han havde altid sin hund med "
    "sig. Om aftenen sad han ved vinduet og l\xC3\xA6ste i en "
    "bog, mens hans kone lavede mad. De havde ikke mange penge, "
    "men de var glade, og deres b\xC3\xB8rn kom ofte og "
    "bes\xC3\xB8gte dem om s\xC3\xB8ndagen, n\xC3\xA5r vejret "
    "var godt." },
  { "norvegien",
    "Det var en gang en gammel mann som bodde i et lite hus ved "
    "sj\xC3\xB8"
    "en. Hver dag gikk han ned til stranden for \xC3\xA5 se "
    "p\xC3\xA5 b\xC3\xB8lgene, og han hadde alltid hunden sin "
    "med seg. Om kvelden satt han ved vinduet og leste i en bok, "
    "mens kona hans laget middag. De hadde ikke mye penger, men "
    "de var glade, og barna deres kom ofte for \xC3\xA5 "
    "bes\xC3\xB8ke dem p\xC3\xA5 s\xC3\xB8ndager n\xC3\xA5r "
    "v\xC3\xA6ret var fint." },
  { "finnois",
    "Olipa kerran pieni tytt\xC3\xB6, joka asui mets\xC3\xA4n "
    "laidalla punaisessa talossa. Joka aamu h\xC3\xA4n meni "
    "j\xC3\xA4rvelle katsomaan lintuja, ja h\xC3\xA4nen koiransa "
    "seurasi h\xC3\xA4nt\xC3\xA4 aina. H\xC3\xA4n piti kovasti "
    "kalastamisesta, mutta h\xC3\xA4nell\xC3\xA4 ei ollut paljon "
    "k\xC3\xA4rsiv\xC3\xA4llisyytt\xC3\xA4. Kun aurinko laski, "
    "\xC3\xA4iti kutsui h\xC3\xA4net kotiin, ja sitten he "
    "s\xC3\xB6iv\xC3\xA4t yhdess\xC3\xA4 illallista. Illalla "
    "is\xC3\xA4 luki heille kirjaa, ja lapset kuuntelivat hiljaa "
    "ennen kuin meniv\xC3\xA4t nukkumaan." },
  { "polonais",
    "Pewnego dnia ma\xC5\x82y ch\xC5\x82opiec poszed\xC5\x82 do "
    "lasu, \xC5\xBC"
    "eby zbiera\xC4\x87 grzyby. Jego matka powiedzia\xC5\x82"
    "a mu, \xC5\xBC"
    "e nie mo\xC5\xBC"
    "e wraca\xC4\x87 zbyt p\xC3\xB3\xC5\xBAno, bo wieczorem robi "
    "si\xC4\x99 zimno. Ch\xC5\x82opiec szed\xC5\x82 d\xC5\x82ugo "
    "mi\xC4\x99"
    "dzy drzewami i nagle zobaczy\xC5\x82 starego "
    "cz\xC5\x82owieka, kt\xC3\xB3ry siedzia\xC5\x82 pod d\xC4\x99"
    "bem. Starzec u\xC5\x9Bmiechn\xC4\x85\xC5\x82 si\xC4\x99 do "
    "niego i zapyta\xC5\x82, czy szuka czego\xC5\x9B. "
    "Ch\xC5\x82opiec odpowiedzia\xC5\x82, \xC5\xBC"
    "e tak, ale jeszcze nic nie znalaz\xC5\x82, wi\xC4\x99"
    "c starzec pokaza\xC5\x82 mu drog\xC4\x99." },
  { "tcheque",
    "Jednoho dne \xC5\xA1"
    "el mal\xC3\xBD chlapec do lesa, aby sb\xC3\xADral houby. "
    "Jeho matka mu \xC5\x99"
    "ekla, \xC5\xBE"
    "e se nesm\xC3\xAD vracet p\xC5\x99\xC3\xADli\xC5\xA1 "
    "pozd\xC4\x9B, proto\xC5\xBE"
    "e ve\xC4\x8D"
    "er je zima. Chlapec \xC5\xA1"
    "el dlouho mezi stromy a najednou uvid\xC4\x9Bl "
    "star\xC3\xA9ho mu\xC5\xBE"
    "e, kter\xC3\xBD sed\xC4\x9Bl pod dubem. Sta\xC5\x99"
    "ec se na n\xC4\x9Bj usm\xC3\xA1l a zeptal se ho, jestli "
    "n\xC4\x9B"
    "co hled\xC3\xA1. Chlapec odpov\xC4\x9B"
    "d\xC4\x9Bl, \xC5\xBE"
    "e ano, ale je\xC5\xA1t\xC4\x9B nic nena\xC5\xA1"
    "el, a tak mu sta\xC5\x99"
    "ec uk\xC3\xA1zal cestu k velk\xC3\xA9 louce." },
  { "hongrois",
    "Egyszer volt, hol nem volt, \xC3\xA9lt egy \xC3\xB6reg "
    "hal\xC3\xA1sz a tenger partj\xC3\xA1n. Minden reggel kiment "
    "a cs\xC3\xB3nakj\xC3\xA1val, \xC3\xA9s csak este "
    "t\xC3\xA9rt haza. A feles\xC3\xA9ge mindig v\xC3\xA1rta "
    "\xC5\x91t a kis h\xC3\xA1zban, ahol egy\xC3\xBCtt laktak "
    "m\xC3\xA1r nagyon r\xC3\xA9g\xC3\xB3ta. Egy napon a "
    "hal\xC3\xA1sz egy k\xC3\xBCl\xC3\xB6n\xC3\xB6s aranyhalat "
    "fogott, amely emberi hangon sz\xC3\xB3lalt meg, \xC3\xA9s "
    "azt k\xC3\xA9rte, hogy engedje szabadon. Az \xC3\xB6reg "
    "megsajn\xC3\xA1lta, \xC3\xA9s visszadobta a v\xC3\xADzbe, "
    "mert j\xC3\xB3 sz\xC3\xADve volt." },
  { "roumain",
    "A fost odat\xC4\x83 un b\xC4\x83iat care locuia "
    "\xC3\xAEntr-un sat mic, l\xC3\xA2ng\xC4\x83 un r\xC3\xA2u "
    "mare. \xC3\x8En fiecare diminea\xC8\x9B\xC4\x83 mergea la "
    "\xC8\x99"
    "coal\xC4\x83 cu prietenii lui \xC8\x99i se \xC3\xAEntorcea "
    "acas\xC4\x83 dup\xC4\x83-amiaza. \xC3\x8Ei pl\xC4\x83"
    "cea foarte mult s\xC4\x83 citeasc\xC4\x83 \xC8\x99i "
    "s\xC4\x83 asculte pove\xC8\x99tile bunicului, care "
    "\xC8\x99tia multe lucruri despre lume. C\xC3\xA2nd se "
    "f\xC4\x83"
    "cea sear\xC4\x83, toat\xC4\x83 familia se aduna \xC3\xAEn "
    "jurul mesei \xC8\x99i vorbeau despre ce s-a "
    "\xC3\xAEnt\xC3\xA2mplat \xC3\xAEn timpul zilei." },
  { "turc",
    "Bir zamanlar k\xC3\xBC\xC3\xA7\xC3\xBCk bir k\xC3\xB6yde "
    "ya\xC5\x9Fl\xC4\xB1 bir adam ya\xC5\x9F"
    "ard\xC4\xB1. Her sabah erkenden kalkar, bah\xC3\xA7"
    "esine gider ve a\xC4\x9F"
    "a\xC3\xA7lar\xC4\xB1na su verirdi. K\xC3\xB6y\xC3\xBCn "
    "\xC3\xA7ocuklar\xC4\xB1 onu \xC3\xA7ok severdi, "
    "\xC3\xA7\xC3\xBCnk\xC3\xBC ona ne zaman gitseler onlara "
    "g\xC3\xBCzel hik\xC3\xA2yeler anlat\xC4\xB1rd\xC4\xB1. Bir "
    "g\xC3\xBCn \xC3\xA7ocuklardan biri ona neden bu kadar mutlu "
    "oldu\xC4\x9Funu sordu. Ya\xC5\x9Fl\xC4\xB1 adam "
    "g\xC3\xBCl\xC3\xBCmsedi ve hayat\xC4\xB1n "
    "k\xC3\xBC\xC3\xA7\xC3\xBCk \xC5\x9F"
    "eylerde sakl\xC4\xB1 oldu\xC4\x9Funu, bunu anlamak "
    "i\xC3\xA7in sab\xC4\xB1rl\xC4\xB1 olmak gerekti\xC4\x9Fini "
    "s\xC3\xB6yledi." },
  { "esperanto",
    "Iam estis knabo, kiu lo\xC4\x9Dis en malgranda domo apud la "
    "arbaro. \xC4\x88iun matenon li iris al la lago por rigardi "
    "la birdojn, kaj lia hundo \xC4\x89iam akompanis lin. Li tre "
    "\xC5\x9D"
    "atis fi\xC5\x9Dkapti, sed li ne havis multe da pacienco. "
    "Kiam la suno subiris, lia patrino vokis lin hejmen, kaj "
    "tiam ili kune man\xC4\x9Dis la vesperman\xC4\x9Don kun la "
    "tuta familio. Poste la patro legis al ili libron "
    "anta\xC5\xAD ol ili iris dormi." },
};

/** Chemin d'un fichier du repertoire de travail (a liberer). */
static char *fichier(const char *nom)
{
  return chemin_dans(travail, nom);
}

/* Marques de dialogue des phrases generees, en UTF-8. */
/** Indique si la ligne commence par une lettre majuscule, apres ses
    marques ouvrantes eventuelles (tiret, parenthese, guillemet). */
static bool commence_par_majuscule(const char *ligne)
{
  char lettre[3] = { '\0', '\0', '\0' };
  bool majuscule = false;

  ligne = apres_marques_ouvrantes(ligne);
  lettre[0] = ligne[0];
  if ((unsigned char)ligne[0] >= 0x80u) {
    lettre[1] = ligne[1];
  }
  if (lexique_graphie_valide(lettre)) {
    majuscule = (lexique_casse(lettre) != CASSE_MINUSCULE);
  }
  return majuscule;
}

/** Nombre de lignes d'un fichier qui commencent par un texte donne. */
static unsigned int compter_lignes_commencant(const char *chemin, const char *debut)
{
  char *contenu = lire_fichier(chemin);
  const char *ligne = contenu;
  unsigned int nb = 0u;

  assert(contenu != NULL);
  while (ligne != NULL && *ligne != '\0') {
    nb += (strncmp(ligne, debut, strlen(debut)) == 0) ? 1u : 0u;
    ligne = strchr(ligne, '\n');
    if (ligne != NULL) {
      ligne++;
    }
  }
  free(contenu);
  return nb;
}

/**
 * Verifie un texte genere : nb_phrases lignes, chacune commencant par une
 * majuscule et se terminant par une ponctuation finale (sauf si
 * sans_ponctuation), avec des guillemets et des parentheses bien formes ;
 * tous les mots (au sens de l'iterateur) appartiennent a la table donnee.
 */
static void verifier_texte_lu(const char *chemin, unsigned int nb_phrases,
                              const hachage_t *mots, bool sans_ponctuation)
{
  char *contenu = lire_fichier(chemin);
  char *ligne;
  char *fin_ligne;
  iterateur_mots_t *it;
  const char *mot;
  unsigned int nb_lignes = 0u;
  unsigned int nb_mots = 0u;

  assert(contenu != NULL);
  ligne = contenu;
  while (*ligne != '\0') {
    fin_ligne = strchr(ligne, '\n');
    assert(fin_ligne != NULL);              /* chaque phrase finit la ligne */
    *fin_ligne = '\0';
    if (!commence_par_majuscule(ligne) || !marques_bien_formees(ligne)
        || (!sans_ponctuation && !termine_par_ponctuation(ligne))) {
      fprintf(stderr, "\nphrase mal formee : \"%s\"\n", ligne);
    }
    assert(commence_par_majuscule(ligne));
    assert(marques_bien_formees(ligne));
    assert(sans_ponctuation || termine_par_ponctuation(ligne));
    nb_lignes++;
    ligne = fin_ligne + 1;
  }
  assert(nb_lignes == nb_phrases);
  free(contenu);
  /* tous les mots sont des mots du corpus */
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  while ((mot = iterateur_mots_suivant(it, NULL)) != NULL) {
    if (!hachage_contient(mots, mot)) {
      fprintf(stderr, "\nmot hors du corpus : \"%s\"\n", mot);
    }
    assert(hachage_contient(mots, mot));
    nb_mots++;
  }
  assert(iterateur_mots_erreur(it) == 0);
  assert(nb_mots >= nb_phrases);
  iterateur_mots_detruire(&it);
}

/** Verifie un texte genere dont chaque phrase a une ponctuation finale. */
static void verifier_texte_genere(const char *chemin, unsigned int nb_phrases, const hachage_t *mots)
{
  verifier_texte_lu(chemin, nb_phrases, mots, false);
}

/** Indique si un fichier contient un texte. */
static bool contient(const char *chemin, const char *texte)
{
  char *contenu = lire_fichier(chemin);
  bool trouve;

  assert(contenu != NULL);
  trouve = (strstr(contenu, texte) != NULL);
  free(contenu);
  return trouve;
}

/** Indique si deux fichiers ont exactement le meme contenu. */
static bool fichiers_identiques(const char *a, const char *b)
{
  char *contenu_a = lire_fichier(a);
  char *contenu_b = lire_fichier(b);
  bool identiques;

  assert(contenu_a != NULL && contenu_b != NULL);
  identiques = (strcmp(contenu_a, contenu_b) == 0);
  free(contenu_a);
  free(contenu_b);
  return identiques;
}

/** Table des mots d'un corpus. */
static hachage_t *mots_du_corpus(const char *chemin, bool recursif)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL,
                                   false, false, NULL, 0u, NULL, NULL };
  markov_t *chaine;

  options.recursif = recursif;
  chaine = repertoire_apprendre(chemin, 0u, mots, &options);
  assert(chaine != NULL);
  markov_detruire(&chaine);
  return mots;
}


/* ======================================================================== */
/*                                   Tests                                  */
/* ======================================================================== */

static void test_aide(void)
{
  const char *const aide[] = { "-h", NULL };
  const char *const aide_longue[] = { "--aide", NULL };
  char *sortie = fichier("sortie.txt");
  char *contenu;

  assert(lancer(aide, NULL) == 0);
  contenu = lire_fichier(sortie);
  assert(contenu != NULL && strstr(contenu, "Usage") != NULL);
  assert(strstr(contenu, "-b <ordre>") != NULL);
  free(contenu);
  assert(lancer(aide_longue, NULL) == 0);
  free(sortie);
}

static void test_texte_genere_valide(void)
{
  char *texte = fichier("texte.txt");
  char *sortie = fichier("sortie.txt");
  char *affiche;
  char *contenu;
  const char *const arguments[] = { "-g", "42", "-n", "300", "-o", texte, corpus, NULL };

  assert(lancer(arguments, NULL) == 0);
  verifier_texte_genere(texte, 300u, mots_corpus);
  /* le texte est aussi affiche */
  affiche = lire_fichier(sortie);
  contenu = lire_fichier(texte);
  assert(affiche != NULL && contenu != NULL && strstr(affiche, contenu) != NULL);
  free(affiche);
  free(contenu);
  free(texte);
  free(sortie);
}

static void test_ordre_2_et_recursif(void)
{
  char *texte = fichier("texte_k2.txt");
  const char *const arguments[] = { "-k", "2", "-r", "-g", "3", "-n", "300", "-o", texte, corpus, NULL };

  assert(lancer(arguments, NULL) == 0);
  verifier_texte_genere(texte, 300u, mots_corpus_recursif);
  free(texte);
}

static void test_reproductible(void)
{
  char *a = fichier("a.txt");
  char *b = fichier("b.txt");
  char *c = fichier("c.txt");
  const char *const premier[] = { "-g", "123", "-n", "40", "-o", a, corpus, NULL };
  const char *const second[] = { "-g", "123", "-n", "40", "-o", b, corpus, NULL };
  const char *const autre[] = { "-g", "124", "-n", "40", "-o", c, corpus, NULL };

  assert(lancer(premier, NULL) == 0 && lancer(second, NULL) == 0);
  assert(fichiers_identiques(a, b));
  assert(lancer(autre, NULL) == 0);
  assert(!fichiers_identiques(a, c));
  free(a);
  free(b);
  free(c);
}

static void test_modele_sauvegarde_puis_charge(void)
{
  char *modele = fichier("modele.txt");
  char *direct = fichier("direct.txt");
  char *recharge = fichier("recharge.txt");
  const char *const apprendre[] = { "-k", "2", "-g", "9", "-n", "60", "-s",
                                    modele, "-o", direct, corpus, NULL };
  const char *const charger[] = { "-m", modele, "-g", "9", "-n", "60", "-o", recharge, NULL };

  assert(lancer(apprendre, NULL) == 0);
  assert(lancer(charger, NULL) == 0);
  /* le modele recharge donne exactement le meme texte */
  assert(fichiers_identiques(direct, recharge));
  verifier_texte_genere(recharge, 60u, mots_corpus);
  free(modele);
  free(direct);
  free(recharge);
}

static void test_syntaxe_historique(void)
{
  char *ancienne = fichier("ancienne.txt");
  char *nouvelle = fichier("nouvelle.txt");
  const char *const historique[] = { corpus_anglais, "42", "5", ancienne, NULL };
  const char *const options[] = { "-g", "42", "-n", "5", "-o", nouvelle, corpus_anglais, NULL };

  const char *const separateur[] = { "-g", "42", "-n", "5", "-o", nouvelle,
                                     "--", corpus_anglais, NULL };

  assert(lancer(historique, NULL) == 0);
  assert(lancer(options, NULL) == 0);
  assert(fichiers_identiques(ancienne, nouvelle));
  /* "--" : la suite n'est faite que d'arguments positionnels */
  assert(lancer(separateur, NULL) == 0);
  assert(fichiers_identiques(ancienne, nouvelle));
  free(ancienne);
  free(nouvelle);
}

static void test_nom_automatique_jamais_ecrase(void)
{
  char *dossier = fichier("automatique");
  char *premier = chemin_dans(dossier, "texte_genere_5.txt");
  char *second = chemin_dans(dossier, "texte_genere_5_2.txt");
  const char *const arguments[] = { "-g", "5", "-n", "2", corpus_anglais, NULL };
  char *contenu;

  assert(mkdir(dossier, 0700) == 0);
  assert(lancer(arguments, dossier) == 0);
  contenu = lire_fichier(premier);
  assert(contenu != NULL);
  assert(lancer(arguments, dossier) == 0);
  /* le premier fichier n'a pas ete ecrase, un second a ete cree */
  assert(fichiers_identiques(premier, second));
  free(contenu);
  free(dossier);
  free(premier);
  free(second);
}

static void test_ordres_superieurs(void)
{
  char *texte = fichier("texte_k.txt");
  char *modele = fichier("modele_k.txt");
  char *recharge = fichier("recharge_k.txt");
  const char *const ordre_3[] = { "-k", "3", "-g", "5", "-n", "200", "-o", texte, corpus, NULL };
  const char *const ordre_8[] = { "-k", "8", "-g", "5", "-n", "50", "-s",
                                  modele, "-o", texte, corpus, NULL };
  const char *const charger[] = { "-m", modele, "-g", "5", "-n", "50", "-o", recharge, NULL };

  assert(lancer(ordre_3, NULL) == 0);
  verifier_texte_genere(texte, 200u, mots_corpus);
  assert(lancer(ordre_8, NULL) == 0);
  verifier_texte_genere(texte, 50u, mots_corpus);
  assert(lancer(charger, NULL) == 0);
  assert(fichiers_identiques(texte, recharge));
  free(texte);
  free(modele);
  free(recharge);
}

static void test_repli(void)
{
  char *texte = fichier("texte_b.txt");
  char *modele = fichier("modele_b.txt");
  char *recharge = fichier("recharge_b.txt");
  char *sortie = fichier("sortie.txt");
  const char *const repli[] = { "-k", "5", "-b", "3", "-g", "5", "-n", "100",
                                "-s", modele, "-o", texte, corpus, NULL };
  const char *const charger[] = { "-m", modele, "-g", "5", "-n", "100", "-o",
                                  recharge, NULL };
  const char *const depuis_un_mot[] = { "-k", "4", "-b", "1", "-p",
                                        "fauteuil", "-g", "3", "-n", "20",
                                        "-o", texte, corpus, NULL };
  const char *const sans_repli[] = { "-k", "5", "-b", "0", "-g", "5", "-n",
                                     "100", "-o", recharge, corpus, NULL };

  /* ordre 5, repli a l'ordre 3 : textes valides, modele sauvegarde et
     recharge avec son repli */
  assert(lancer(repli, NULL) == 0);
  verifier_texte_genere(texte, 100u, mots_corpus);
  assert(contient(sortie, "(chaine d'ordre 5, repli a l'ordre 3)"));
  assert(lancer(charger, NULL) == 0);
  assert(contient(sortie, "(chaine d'ordre 5, repli a l'ordre 3)"));
  assert(fichiers_identiques(texte, recharge));
  /* "-b 0" : sans repli, autre texte */
  assert(lancer(sans_repli, NULL) == 0);
  assert(contient(sortie, "(chaine d'ordre 5)"));
  verifier_texte_genere(recharge, 100u, mots_corpus);
  assert(!fichiers_identiques(texte, recharge));
  /* premier mot impose */
  assert(lancer(depuis_un_mot, NULL) == 0);
  verifier_texte_genere(texte, 20u, mots_corpus);
  assert(compter_lignes_commencant(texte, "Fauteuil") == 20u);
  free(texte);
  free(modele);
  free(recharge);
  free(sortie);
}

static void test_premier_mot(void)
{
  char *texte = fichier("premier.txt");
  char *sortie = fichier("sortie.txt");
  char *modele = fichier("modele_p.txt");
  char *recharge = fichier("recharge_p.txt");
  /* "Marius" commence des phrases ; "fauteuil" (presque) jamais ; "ete"
     s'ecrit avec ses accents */
  const char *const marius[] = { "-p", "Marius", "-g", "1", "-n", "30", "-o", texte, corpus, NULL };
  const char *const fauteuil[] = { "-k", "2", "-p", "FAUTEUIL", "-g", "1",
                                   "-n", "30", "-s", modele, "-o", texte,
                                   corpus, NULL };
  const char *const charge[] = { "-m", modele, "-p", "fauteuil", "-g", "1",
                                 "-n", "30", "-o", recharge, NULL };
  const char *const ete[] = { "-k", "3", "-p", "ete", "-g", "1", "-n", "30",
                              "-o", texte, corpus, NULL };

  assert(lancer(marius, NULL) == 0);
  verifier_texte_genere(texte, 30u, mots_corpus);
  assert(compter_lignes_commencant(texte, "Marius") == 30u);
  assert(lancer(fauteuil, NULL) == 0);
  verifier_texte_genere(texte, 30u, mots_corpus);
  assert(compter_lignes_commencant(texte, "Fauteuil") == 30u);
  /* le modele recharge donne les memes phrases depuis le meme mot */
  assert(lancer(charge, NULL) == 0);
  assert(fichiers_identiques(texte, recharge));
  assert(lancer(ete, NULL) == 0);
  verifier_texte_genere(texte, 30u, mots_corpus);
  assert(compter_lignes_commencant(texte, "\xC3\x89t\xC3\xA9") == 30u);
  free(texte);
  free(sortie);
  free(modele);
  free(recharge);
}

static void test_langues_europeennes(void)
{
  char *texte = fichier("langue.txt");
  char *genere = fichier("genere_langue.txt");
  char *sortie = fichier("sortie.txt");
  const char *arguments[] = { "-g", "1", "-n", "5", "-o", NULL, NULL, NULL };
  char attendu[64];
  hachage_t *mots;
  size_t i;

  /* chaque texte est reconnu (lettres translitterees puis mots-outils) et
     les phrases generees n'utilisent que ses mots */
  arguments[5] = genere;
  arguments[6] = texte;
  for (i = 0u; i < sizeof(TEXTES_EUROPEENS) / sizeof(TEXTES_EUROPEENS[0]);
       i++) {
    ecrire_fichier(texte, TEXTES_EUROPEENS[i].texte);
    assert(lancer(arguments, NULL) == 0);
    snprintf(attendu, sizeof(attendu), "Langue du corpus : %s ;",
             TEXTES_EUROPEENS[i].langue);
    if (!contient(sortie, attendu)) {
      fprintf(stderr, "\nlangue non reconnue : %s\n",
              TEXTES_EUROPEENS[i].langue);
    }
    assert(contient(sortie, attendu));
    mots = mots_du_corpus(texte, false);
    verifier_texte_genere(genere, 5u, mots);
    hachage_detruire(&mots);
  }
  free(texte);
  free(genere);
  free(sortie);
}

static void test_courriers(void)
{
  char *boite = fichier("courriers");
  char *chemin_mbox = fichier("courriers/boite.mbox");
  char *chemin_eml = fichier("courriers/lettre.eml");
  char *texte = fichier("texte_courriers.txt");
  char *sortie = fichier("sortie.txt");
  const char *const arguments[] = { "-k", "2", "-t", "-g", "3", "-n", "30",
                                    "-o", texte, boite, NULL };
  hachage_t *mots;
  char *contenu;

  assert(mkdir(boite, 0700) == 0);
  ecrire_fichier(chemin_mbox,
                 "From jean@example.com Mon Mar  3 10:00:00 2024\n"
                 "From: Jean <jean@example.com>\nTo: Paul\n"
                 "Subject: Reunion\nContent-Type: text/plain; charset=UTF-8\n"
                 "Content-Transfer-Encoding: quoted-printable\n\n"
                 "La r=C3=A9union est d=C3=A9plac=C3=A9e =C3=A0 quinze "
                 "heures. Peux-tu pr=C3=A9venir les autres ?\n\n"
                 "Le 2 mars 2024, Paul a =C3=A9crit :\n> Tu viens ?\n"
                 "-- \nJean Dupont, directeur\n\n"
                 "From paul@example.com Mon Mar  3 11:00:00 2024\n"
                 "From: Paul\nTo: Jean\nSubject: Re: Reunion\n"
                 "Content-Type: multipart/alternative; boundary=b\n\n"
                 "--b\nContent-Type: text/plain; charset=iso-8859-1\n\n"
                 "Tr\xE8s bien, je pr\xE9viens tout le monde.\n"
                 "--b\nContent-Type: text/html\n\n<p>Version HTML</p>\n"
                 "--b--\n");
  ecrire_fichier(chemin_eml,
                 "Date: Tue, 4 Mar 2024 09:00:00 +0100\nFrom: Marie\n"
                 "To: Jean\nSubject: Merci\n\nMerci pour la r\xC3\xA9union, "
                 "elle \xC3\xA9tait tr\xC3\xA8s utile.\n");
  assert(lancer(arguments, NULL) == 0);
  assert(contient(sortie, "Apprentissage : 2 fichier(s) (3 message(s) "
                          "electronique(s)), 4 phrases"));
  /* les phrases ne contiennent que des mots des corps des messages :
     ni en-tetes, ni citation, ni signature, ni partie HTML */
  mots = mots_du_corpus(boite, false);
  assert(hachage_identifiant(mots, "subject") == HACHAGE_ABSENT);
  assert(hachage_identifiant(mots, "directeur") == HACHAGE_ABSENT);
  assert(hachage_identifiant(mots, "html") == HACHAGE_ABSENT);
  assert(hachage_identifiant(mots, "viens") == HACHAGE_ABSENT);
  verifier_texte_genere(texte, 30u, mots);
  hachage_detruire(&mots);
  /* accents decodes (quoted-printable, Latin-1) */
  contenu = lire_fichier(texte);
  assert(contenu != NULL);
  assert(strstr(contenu, "r\xC3\xA9union") != NULL
         || strstr(contenu, "Tr\xC3\xA8s") != NULL);
  free(contenu);
  free(boite);
  free(chemin_mbox);
  free(chemin_eml);
  free(texte);
  free(sortie);
}

static void test_plusieurs_chemins(void)
{
  char *texte = fichier("plusieurs.txt");
  char *sortie = fichier("sortie.txt");
  char *dylan = chemin_dans(corpus_anglais, "bobdylan_iwysb.txt");
  char *beatles = chemin_dans(corpus_anglais, "thebeatles_iwy.txt");
  const char *const deux_fichiers[] = { "-g", "2", "-n", "20", "-o", texte,
                                        dylan, beatles, NULL };
  const char *const un_fichier[] = { "-g", "2", "-n", "20", "-o", texte, dylan, NULL };

  /* deux fichiers donnes un par un : comme le repertoire qui les contient
     (memes fichiers, meme ordre) */
  assert(lancer(deux_fichiers, NULL) == 0);
  verifier_texte_genere(texte, 20u, mots_corpus_anglais);
  assert(contient(sortie, "2 fichier(s)"));
  assert(contient(sortie, "Langue du corpus : anglais"));
  assert(lancer(un_fichier, NULL) == 0);
  verifier_texte_genere(texte, 20u, mots_corpus_anglais);
  assert(contient(sortie, "1 fichier(s)"));
  free(texte);
  free(sortie);
  free(dylan);
  free(beatles);
}

static void test_phrase_par_ligne(void)
{
  char *texte = fichier("chansons.txt");
  const char *const chansons[] = { "-l", "-k", "2", "-g", "4", "-n", "60",
                                   "-o", texte, corpus_anglais, NULL };

  /* une phrase par vers : des lignes peuvent finir sans ponctuation */
  assert(lancer(chansons, NULL) == 0);
  verifier_texte_lu(texte, 60u, mots_corpus_anglais, true);
  free(texte);
}

static void test_filtre_de_langue(void)
{
  char *modele = fichier("modele_filtre.txt");
  char *modele_tout = fichier("modele_tout.txt");
  char *texte = fichier("texte_filtre.txt");
  char *sortie = fichier("sortie.txt");
  /* corpus francais et chansons anglaises : l'anglais est ecarte */
  const char *const filtre[] = { "-g", "1", "-n", "5", "-s", modele, "-o",
                                 texte, corpus, corpus_anglais, NULL };
  const char *const tout[] = { "-t", "-g", "1", "-n", "5", "-s", modele_tout,
                               "-o", texte, corpus, corpus_anglais, NULL };

  assert(lancer(filtre, NULL) == 0);
  assert(contient(sortie, "Langue du corpus : francais"));
  assert(contient(sortie, "ecartee(s)"));
  /* dans le modele, la ligne d'un mot commence par sa cle : "shyness"
     (notes anglaises de contes.txt) et "yeah" (qui n'apparait que dans
     les chansons) sont absents */
  assert(!contient(modele, "\nshyness ") && !contient(modele, "\nyeah "));
  assert(lancer(tout, NULL) == 0);
  assert(!contient(sortie, "Langue du corpus"));
  assert(contient(modele_tout, "\nshyness ") && contient(modele_tout, "\nyeah "));
  free(modele);
  free(modele_tout);
  free(texte);
  free(sortie);
}

static void test_erreurs(void)
{
  char *vide = fichier("vide");
  char *modele = fichier("modele.txt");
  char *sans_parent = fichier("absent/texte.txt");
  char *modele_sans_parent = fichier("absent/modele.txt");
  const char *const aucun[] = { NULL };
  const char *const inconnue[] = { "-z", corpus_anglais, NULL };
  const char *const sans_valeur[] = { corpus_anglais, "-g", NULL };
  const char *const graine_texte[] = { "-g", "abc", corpus_anglais, NULL };
  const char *const graine_negative[] = { "-g", "-1", corpus_anglais, NULL };
  const char *const graine_deux_fois[] = { "-g", "1", "-g", "2", corpus_anglais, NULL };
  const char *const graine_conflit[] = { corpus_anglais, "1", "-g", "2", NULL };
  const char *const zero_phrase[] = { "-n", "0", corpus_anglais, NULL };
  const char *const ordre_9[] = { "-k", "9", corpus_anglais, NULL };
  const char *const ordre_0[] = { "-k", "0", corpus_anglais, NULL };
  const char *const mot_absent[] = { "-p", "xyzzy", corpus_anglais, NULL };
  const char *const mot_invalide[] = { "-p", "l'eau", corpus_anglais, NULL };
  const char *const mot_vide[] = { "-p", "", corpus_anglais, NULL };
  const char *const modele_et_lignes[] = { "-m", modele, "-l", NULL };
  const char *const modele_et_tout[] = { "-m", modele, "-t", NULL };
  const char *const second_chemin_absent[] = { corpus_anglais, "/tmp/absent_markov", NULL };
  const char *const tube[] = { "/dev/null", NULL };
  const char *const trop[] = { corpus_anglais, "1", "2", "x.txt", "y", NULL };
  const char *const sortie_vide[] = { "-o", "", corpus_anglais, NULL };
  const char *const modele_et_corpus[] = { "-m", modele, corpus_anglais, NULL };
  const char *const modele_et_ordre[] = { "-m", modele, "-k", "2", NULL };
  const char *const modele_et_repli[] = { "-m", modele, "-b", "1", NULL };
  const char *const repli_sans_ordre[] = { "-b", "1", corpus_anglais, NULL };
  const char *const repli_egal[] = { "-k", "3", "-b", "3", corpus_anglais, NULL };
  const char *const repli_9[] = { "-k", "8", "-b", "9", corpus_anglais, NULL };
  const char *const repli_texte[] = { "-k", "3", "-b", "un", corpus_anglais, NULL };
  const char *const repli_deux_fois[] = { "-k", "3", "-b", "1", "-b", "2", corpus_anglais, NULL };
  const char *const modele_absent[] = { "-m", "/tmp/modele_absent_markov", NULL };
  const char *const modele_invalide[] = { "-m", "Makefile", NULL };
  const char *const corpus_absent[] = { "/tmp/corpus_absent_markov", NULL };
  const char *const corpus_vide[] = { vide, NULL };
  const char *const sortie_impossible[] = { "-o", sans_parent, corpus_anglais, NULL };
  const char *const sauvegarde_impossible[] = { "-s", modele_sans_parent, corpus_anglais, NULL };
  const char *const sortie_deux_fois[] = { "-o", "a.txt", "-o", "b.txt", corpus_anglais, NULL };
  const char *const nombre_deux_fois[] = { "-n", "1", "-n", "2", corpus_anglais, NULL };
  const char *const nombre_conflit[] = { corpus_anglais, "1", "2", "-n", "3", NULL };
  const char *const modele_deux_fois[] = { "-m", modele, "-m", modele, NULL };
  const char *const *const cas[] = {
    aucun, inconnue, sans_valeur, graine_texte, graine_negative,
    graine_deux_fois, graine_conflit, zero_phrase, ordre_9, ordre_0,
    mot_absent, mot_invalide, mot_vide, modele_et_lignes, modele_et_tout,
    second_chemin_absent, tube, trop,
    sortie_vide, modele_et_corpus, modele_et_ordre, modele_et_repli,
    repli_sans_ordre, repli_egal, repli_9, repli_texte, repli_deux_fois,
    modele_absent,
    modele_invalide, corpus_absent, corpus_vide, sortie_impossible,
    sauvegarde_impossible, sortie_deux_fois, nombre_deux_fois,
    nombre_conflit, modele_deux_fois
  };
  /* disque plein pour le texte ou pour le modele */
  const char *const texte_disque_plein[] = { "-o", "/dev/full", corpus_anglais, NULL };
  const char *const modele_disque_plein[] = { "-s", "/dev/full", corpus_anglais, NULL };
  struct stat informations;
  size_t i;

  assert(mkdir(vide, 0700) == 0);
  for (i = 0u; i < sizeof(cas) / sizeof(cas[0]); i++) {
    if (lancer(cas[i], NULL) != 1) {
      fprintf(stderr, "\ncas d'erreur %u : code de retour different de 1\n", (unsigned int)i);
    }
    assert(lancer(cas[i], NULL) == 1);
  }
  if (stat("/dev/full", &informations) == 0) {
    assert(lancer(texte_disque_plein, NULL) == 1);
    assert(lancer(modele_disque_plein, NULL) == 1);
    assert(stat("/dev/full", &informations) == 0);  /* jamais supprime */
  }
  free(vide);
  free(modele);
  free(sans_parent);
  free(modele_sans_parent);
}


int main(int argc, char *argv[])
{
  struct stat informations;

  programme = realpath((argc > 1) ? argv[1] : "./projet", NULL);
  corpus = realpath("dataset", NULL);
  corpus_anglais = realpath("dataset/en", NULL);
  if (programme == NULL || corpus == NULL || corpus_anglais == NULL
      || stat(programme, &informations) != 0) {
    fprintf(stderr, "test_programme : lancer depuis la racine du projet, "
            "apres make (usage : %s [executable]).\n", argv[0]);
    free(programme);
    free(corpus);
    free(corpus_anglais);
    return EXIT_FAILURE;
  }
  travail = creer_repertoire_temporaire();
  mots_corpus = mots_du_corpus(corpus, false);
  mots_corpus_recursif = mots_du_corpus(corpus, true);
  mots_corpus_anglais = mots_du_corpus(corpus_anglais, false);

  printf("Tests du programme principal (%s) :\n", programme);
  LANCER_TEST(test_aide);
  LANCER_TEST(test_texte_genere_valide);
  LANCER_TEST(test_ordre_2_et_recursif);
  LANCER_TEST(test_reproductible);
  LANCER_TEST(test_modele_sauvegarde_puis_charge);
  LANCER_TEST(test_syntaxe_historique);
  LANCER_TEST(test_nom_automatique_jamais_ecrase);
  LANCER_TEST(test_ordres_superieurs);
  LANCER_TEST(test_repli);
  LANCER_TEST(test_premier_mot);
  LANCER_TEST(test_plusieurs_chemins);
  LANCER_TEST(test_langues_europeennes);
  LANCER_TEST(test_courriers);
  LANCER_TEST(test_phrase_par_ligne);
  LANCER_TEST(test_filtre_de_langue);
  LANCER_TEST(test_erreurs);
  afficher_bilan("programme principal");

  hachage_detruire(&mots_corpus);
  hachage_detruire(&mots_corpus_recursif);
  hachage_detruire(&mots_corpus_anglais);
  detruire_repertoire_temporaire(travail);
  free(programme);
  free(corpus);
  free(corpus_anglais);
  return EXIT_SUCCESS;
}
