/**
Fichier test_langue.c

Tests unitaires du module langue : profils, langue majoritaire, passages
ecartes, et verification des contrats.

Les textes sont donnes sans accents ni majuscules (ce sont des mots de
l'alphabet {a, ..., z}) : un point termine une phrase.
*/
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include "../src/langue.h"
#include "outils_tests.h"

/* Textes courants, un par langue reconnue : d'au moins 80 mots pour les
   sept premieres langues, de 59 a 76 mots pour les suivantes (ecrits en
   UTF-8 avec leurs accents, puis lus par l'iterateur). */
static const char *const TEXTE_FRANCAIS =
  "le chat dort sur le toit de la maison. il regarde les oiseaux qui "
  "passent dans le ciel. la petite fille ouvre la porte et appelle son "
  "chat. elle ne veut pas qu il reste dehors pendant la nuit. le chat "
  "descend du toit et vient pres d elle. ils rentrent tous les deux dans "
  "la cuisine. la mere a prepare une soupe pour le repas du soir. le pere "
  "lit un livre au coin du feu. tout est calme dans la maison et la nuit "
  "tombe sur le village.";

static const char *const TEXTE_ANGLAIS =
  "the old man walked to the river with his dog. he had not seen the water "
  "for many years. it was cold and the wind was strong. she said that they "
  "would come back in the morning. there was a small house on the hill and "
  "it was empty. we have been here for a long time but nobody came. what "
  "will you do when the rain stops. i think that he is right about this "
  "and that we should go.";

static const char *const TEXTE_ESPAGNOL =
  "el nino juega en la calle con su perro. la madre lo llama para que "
  "venga a comer. no hay nadie en la plaza porque hace mucho calor. los "
  "hombres trabajan en el campo desde la manana hasta la noche. ella dice "
  "que no quiere salir de la casa. el padre ha comprado un libro para su "
  "hija. todos los dias es lo mismo en este pueblo. cuando llega el "
  "invierno las calles estan vacias y el viento es muy frio.";

static const char *const TEXTE_ALLEMAND =
  "der alte mann geht mit seinem hund in den wald. er hat das haus am "
  "morgen verlassen und ist noch nicht wieder da. die frau wartet auf ihn "
  "und sieht aus dem fenster. es ist kalt und der wind ist stark. das kind "
  "spielt mit einem ball vor der tur. wir haben den ganzen tag auf euch "
  "gewartet aber ihr seid nicht gekommen. ich weiss nicht was sie von mir "
  "will. wenn der regen aufhort werden wir durch die stadt gehen.";

static const char *const TEXTE_ITALIEN =
  "il bambino gioca nella strada con il suo cane. la madre lo chiama "
  "perche e ora di mangiare. non c e nessuno nella piazza perche fa molto "
  "caldo. gli uomini lavorano nei campi dalla mattina alla sera. lei dice "
  "che non vuole uscire di casa. il padre ha comprato un libro per sua "
  "figlia. tutti i giorni sono uguali in questo paese. quando arriva l "
  "inverno le strade sono vuote e il vento e molto freddo.";

static const char *const TEXTE_PORTUGAIS =
  "o menino brinca na rua com o seu cao. a mae chama o filho para que ele "
  "venha comer. nao ha ninguem na praca porque faz muito calor. os homens "
  "trabalham no campo desde a manha ate a noite. ela diz que nao quer sair "
  "de casa. o pai comprou um livro para a sua filha. todos os dias sao "
  "iguais nesta terra. quando chega o inverno as ruas ficam vazias e o "
  "vento e muito frio.";

static const char *const TEXTE_NEERLANDAIS =
  "de oude man loopt met zijn hond naar het bos. hij heeft het huis in de "
  "morgen verlaten en is nog niet terug. de vrouw wacht op hem en kijkt "
  "uit het raam. het is koud en de wind is sterk. het kind speelt met een "
  "bal voor de deur. wij hebben de hele dag op jullie gewacht maar jullie "
  "zijn niet gekomen. ik weet niet wat zij van mij wil. als de regen "
  "ophoudt gaan we door de stad lopen.";

static const char *const TEXTE_LATIN =
  "puer in horto ambulat et cum amicis suis ludit sed mater eum vocat "
  "quia cena iam parata est. non est facile semper bene agere nam homines "
  "saepe errant tamen qui laborat et sperat ad finem perveniet. ego te "
  "video et tu me audis et nos omnes in hac urbe magna vivimus quae inter "
  "montes et mare posita est. si vis pacem para bellum ut dicunt antiqui "
  "sed ego pacem sine bello malo.";

static const char *const TEXTE_CATALAN =
  "el meu germa viu a barcelona amb la seva dona i els seus dos fills. "
  "cada mati agafa el tren per anar a la feina pero a vegades prefereix "
  "caminar perque li agrada molt passejar pel barri. quan arriba a casa "
  "sopen tots junts i parlen del que han fet durant el dia. tambe els "
  "agrada llegir llibres i escoltar musica abans d anar a dormir i els "
  "caps de setmana sovint van a la muntanya.";

static const char *const TEXTE_SUEDOIS =
  "det var en gang en liten pojke som bodde i ett rott hus vid sjon. "
  "varje morgon gick han ner till vattnet for att se pa faglarna och hans "
  "hund foljde alltid med honom. han tyckte mycket om att fiska men han "
  "hade inte sa mycket talamod. nar solen gick ner kom hans mor och "
  "kallade pa honom och da sprang de hem tillsammans for att ata middag "
  "med resten av familjen.";

static const char *const TEXTE_DANOIS =
  "der var engang en gammel mand som boede i et lille hus ved havet. hver "
  "dag gik han ned til stranden for at se pa bolgerne og han havde altid "
  "sin hund med sig. om aftenen sad han ved vinduet og laeste i en bog "
  "mens hans kone lavede mad. de havde ikke mange penge men de var glade "
  "og deres born kom ofte og besogte dem om sondagen nar vejret var godt.";

static const char *const TEXTE_NORVEGIEN =
  "det var en gang en gammel mann som bodde i et lite hus ved sjoen. hver "
  "dag gikk han ned til stranden for a se pa bolgene og han hadde alltid "
  "hunden sin med seg. om kvelden satt han ved vinduet og leste i en bok "
  "mens kona hans laget middag. de hadde ikke mye penger men de var glade "
  "og barna deres kom ofte for a besoke dem pa sondager nar vaeret var "
  "fint.";

static const char *const TEXTE_FINNOIS =
  "olipa kerran pieni tytto joka asui metsan laidalla punaisessa talossa. "
  "joka aamu han meni jarvelle katsomaan lintuja ja hanen koiransa "
  "seurasi hanta aina. han piti kovasti kalastamisesta mutta hanella ei "
  "ollut paljon karsivallisyytta. kun aurinko laski aiti kutsui hanet "
  "kotiin ja sitten he soivat yhdessa illallista. illalla isa luki heille "
  "kirjaa ja lapset kuuntelivat hiljaa ennen kuin menivat nukkumaan.";

static const char *const TEXTE_POLONAIS =
  "pewnego dnia maly chlopiec poszedl do lasu zeby zbierac grzyby. jego "
  "matka powiedziala mu ze nie moze wracac zbyt pozno bo wieczorem robi "
  "sie zimno. chlopiec szedl dlugo miedzy drzewami i nagle zobaczyl "
  "starego czlowieka ktory siedzial pod debem. starzec usmiechnal sie do "
  "niego i zapytal czy szuka czegos. chlopiec odpowiedzial ze tak ale "
  "jeszcze nic nie znalazl wiec starzec pokazal mu droge.";

static const char *const TEXTE_TCHEQUE =
  "jednoho dne sel maly chlapec do lesa aby sbiral houby. jeho matka mu "
  "rekla ze se nesmi vracet prilis pozde protoze vecer je zima. chlapec "
  "sel dlouho mezi stromy a najednou uvidel stareho muze ktery sedel pod "
  "dubem. starec se na nej usmal a zeptal se ho jestli neco hleda. "
  "chlapec odpovedel ze ano ale jeste nic nenasel a tak mu starec ukazal "
  "cestu k velke louce.";

static const char *const TEXTE_HONGROIS =
  "egyszer volt hol nem volt elt egy oreg halasz a tenger partjan. minden "
  "reggel kiment a csonakjaval es csak este tert haza. a felesege mindig "
  "varta ot a kis hazban ahol egyutt laktak mar nagyon regota. egy napon "
  "a halasz egy kulonos aranyhalat fogott amely emberi hangon szolalt meg "
  "es azt kerte hogy engedje szabadon. az oreg megsajnalta es visszadobta "
  "a vizbe mert jo szive volt.";

static const char *const TEXTE_ROUMAIN =
  "a fost odata un baiat care locuia intr un sat mic langa un rau mare. "
  "in fiecare dimineata mergea la scoala cu prietenii lui si se intorcea "
  "acasa dupa amiaza. ii placea foarte mult sa citeasca si sa asculte "
  "povestile bunicului care stia multe lucruri despre lume. cand se facea "
  "seara toata familia se aduna in jurul mesei si vorbeau despre ce s a "
  "intamplat in timpul zilei.";

static const char *const TEXTE_TURC =
  "bir zamanlar kucuk bir koyde yasli bir adam yasardi. her sabah "
  "erkenden kalkar bahcesine gider ve agaclarina su verirdi. koyun "
  "cocuklari onu cok severdi cunku ona ne zaman gitseler onlara guzel "
  "hikayeler anlatirdi. bir gun cocuklardan biri ona neden bu kadar mutlu "
  "oldugunu sordu. yasli adam gulumsedi ve hayatin kucuk seylerde sakli "
  "oldugunu bunu anlamak icin sabirli olmak gerektigini soyledi.";

static const char *const TEXTE_ESPERANTO =
  "iam estis knabo kiu logis en malgranda domo apud la arbaro. ciun "
  "matenon li iris al la lago por rigardi la birdojn kaj lia hundo ciam "
  "akompanis lin. li tre satis fiskapti sed li ne havis multe da "
  "pacienco. kiam la suno subiris lia patrino vokis lin hejmen kaj tiam "
  "ili kune mangis la vespermangon kun la tuta familio. poste la patro "
  "legis al ili libron antau ol ili iris dormi.";


/* ======================================================================== */
/*                                  Outils                                  */
/* ======================================================================== */

/**
 * Ajoute au detecteur les mots d'un texte : les mots sont separes par des
 * espaces, un mot suivi d'un point termine sa phrase. Renvoie le nombre de
 * mots ajoutes.
 */
static size_t ajouter_texte(langue_detecteur_t *d, const char *texte)
{
  char mot[64];
  size_t longueur = 0u;
  size_t nb = 0u;
  const char *c = texte;

  while (*c != '\0') {
    if (*c >= 'a' && *c <= 'z') {
      assert(longueur + 1u < sizeof(mot));
      mot[longueur] = *c;
      longueur++;
    }
    if (longueur > 0u && (*c == ' ' || *c == '.' || c[1] == '\0')) {
      mot[longueur] = '\0';
      assert(langue_detecteur_ajouter_mot(d, mot, *c == '.'));
      longueur = 0u;
      nb++;
    }
    c++;
  }
  return nb;
}

/** Nombre de phrases gardees parmi les phrases [debut, fin[. */
static size_t nb_gardees(const langue_detecteur_t *d, size_t debut, size_t fin)
{
  size_t phrase;
  size_t nb = 0u;

  for (phrase = debut; phrase < fin; phrase++) {
    if (langue_detecteur_phrase_gardee(d, phrase)) {
      nb++;
    }
  }
  return nb;
}

/** Langue majoritaire d'un corpus forme d'un seul texte. */
static langue_t langue_du_texte(const char *texte)
{
  langue_detecteur_t *d = langue_detecteur_creer();
  langue_t langue;

  assert(d != NULL);
  ajouter_texte(d, texte);
  assert(langue_detecteur_conclure(d));
  langue = langue_detecteur_majoritaire(d);
  /* un corpus d'une seule langue est garde en entier */
  assert(langue_detecteur_nb_phrases_ecartees(d) == 0u);
  assert(langue_detecteur_nb_mots_ecartes(d) == 0u);
  langue_detecteur_detruire(&d);
  return langue;
}


/* ======================================================================== */
/*                           Profils de langues                             */
/* ======================================================================== */

static void test_nom_des_langues(void)
{
  unsigned int a;
  unsigned int b;

  assert(strcmp(langue_nom(LANGUE_INCONNUE), "inconnue") == 0);
  assert(strcmp(langue_nom(LANGUE_FRANCAIS), "francais") == 0);
  assert(strcmp(langue_nom(LANGUE_ANGLAIS), "anglais") == 0);
  assert(strcmp(langue_nom(LANGUE_NEERLANDAIS), "neerlandais") == 0);
  assert(strcmp(langue_nom(LANGUE_LATIN), "latin") == 0);
  assert(strcmp(langue_nom(LANGUE_ESPERANTO), "esperanto") == 0);
  /* noms non vides et tous differents */
  for (a = 0u; a < (unsigned int)NB_LANGUES; a++) {
    assert(langue_nom((langue_t)a)[0] != '\0');
    for (b = a + 1u; b < (unsigned int)NB_LANGUES; b++) {
      assert(strcmp(langue_nom((langue_t)a), langue_nom((langue_t)b)) != 0);
    }
  }
}

static void test_profils(void)
{
  const unsigned int fr = 1u << LANGUE_FRANCAIS;
  const unsigned int en = 1u << LANGUE_ANGLAIS;
  const unsigned int es = 1u << LANGUE_ESPAGNOL;
  const unsigned int de = 1u << LANGUE_ALLEMAND;
  const unsigned int it = 1u << LANGUE_ITALIEN;
  const unsigned int pt = 1u << LANGUE_PORTUGAIS;
  const unsigned int nl = 1u << LANGUE_NEERLANDAIS;
  const unsigned int la = 1u << LANGUE_LATIN;
  const unsigned int ca = 1u << LANGUE_CATALAN;
  const unsigned int sv = 1u << LANGUE_SUEDOIS;
  const unsigned int da = 1u << LANGUE_DANOIS;
  const unsigned int no = 1u << LANGUE_NORVEGIEN;
  const unsigned int fi = 1u << LANGUE_FINNOIS;
  const unsigned int pl = 1u << LANGUE_POLONAIS;
  const unsigned int cs = 1u << LANGUE_TCHEQUE;
  const unsigned int hu = 1u << LANGUE_HONGROIS;
  const unsigned int ro = 1u << LANGUE_ROUMAIN;
  const unsigned int tr = 1u << LANGUE_TURC;
  const unsigned int eo = 1u << LANGUE_ESPERANTO;

  /* mots propres a une langue */
  assert(langue_profils("avec") == fr);
  assert(langue_profils("the") == en);
  assert(langue_profils("hasta") == es);
  assert(langue_profils("pero") == (es | ca));
  assert(langue_profils("nicht") == de);
  assert(langue_profils("gli") == it);
  assert(langue_profils("nao") == pt);
  assert(langue_profils("het") == nl);
  assert(langue_profils("quod") == la);
  assert(langue_profils("aquest") == ca);
  assert(langue_profils("och") == sv);
  assert(langue_profils("af") == da);
  assert(langue_profils("hadde") == no);
  assert(langue_profils("etta") == fi);
  assert(langue_profils("nie") == pl);
  assert(langue_profils("jsem") == cs);
  assert(langue_profils("hogy") == hu);
  assert(langue_profils("pentru") == ro);
  assert(langue_profils("bir") == tr);
  assert(langue_profils("kaj") == eo);
  /* mots communs a plusieurs langues (ecrits sans accents : "pa" est le
     "pa" scandinave, avec un a rond) */
  assert(langue_profils("la") == (fr | es | it | ca | ro | eo));
  assert(langue_profils("que") == (fr | es | pt | ca));
  assert(langue_profils("die") == (de | nl));
  assert(langue_profils("in") == (en | de | it | nl | la | sv | ro));
  assert(langue_profils("ikke") == (da | no));
  assert(langue_profils("pa") == (sv | da | no));
  assert(langue_profils("ze") == (nl | pl | cs));
  /* premier et dernier mots de la table, mots elides */
  assert((langue_profils("a") & fr) != 0u);
  assert(langue_profils("zur") == de);
  assert((langue_profils("qu") & fr) != 0u);
  assert((langue_profils("s") & en) != 0u);
  /* mots absents de tous les profils */
  assert(langue_profils("") == 0u);
  assert(langue_profils("maison") == 0u);
  assert(langue_profils("zzzz") == 0u);
  assert(langue_profils("aa") == 0u);
  assert(langue_profils("th") == 0u);          /* prefixe d'un mot-outil */
  assert(langue_profils("thee") == 0u);        /* mot-outil prolonge */
}


/* ======================================================================== */
/*                           Langue majoritaire                             */
/* ======================================================================== */

static void test_detecteur_vide(void)
{
  langue_detecteur_t *d = langue_detecteur_creer();

  assert(d != NULL);
  assert(!langue_detecteur_est_conclu(d));
  assert(langue_detecteur_nb_phrases(d) == 0u);
  assert(langue_detecteur_nb_mots(d) == 0u);
  langue_detecteur_fin_texte(d);               /* texte vide : sans effet */
  assert(langue_detecteur_conclure(d));
  assert(langue_detecteur_est_conclu(d));
  assert(langue_detecteur_majoritaire(d) == LANGUE_INCONNUE);
  assert(langue_detecteur_nb_phrases(d) == 0u);
  assert(langue_detecteur_nb_phrases_ecartees(d) == 0u);
  assert(langue_detecteur_nb_mots_ecartes(d) == 0u);
  langue_detecteur_detruire(&d);
  assert(d == NULL);
  langue_detecteur_detruire(&d);               /* sans effet sur NULL */
}

static void test_chaque_langue(void)
{
  assert(langue_du_texte(TEXTE_FRANCAIS) == LANGUE_FRANCAIS);
  assert(langue_du_texte(TEXTE_ANGLAIS) == LANGUE_ANGLAIS);
  assert(langue_du_texte(TEXTE_ESPAGNOL) == LANGUE_ESPAGNOL);
  assert(langue_du_texte(TEXTE_ALLEMAND) == LANGUE_ALLEMAND);
  assert(langue_du_texte(TEXTE_ITALIEN) == LANGUE_ITALIEN);
  assert(langue_du_texte(TEXTE_PORTUGAIS) == LANGUE_PORTUGAIS);
  assert(langue_du_texte(TEXTE_NEERLANDAIS) == LANGUE_NEERLANDAIS);
  assert(langue_du_texte(TEXTE_LATIN) == LANGUE_LATIN);
  assert(langue_du_texte(TEXTE_CATALAN) == LANGUE_CATALAN);
  assert(langue_du_texte(TEXTE_SUEDOIS) == LANGUE_SUEDOIS);
  assert(langue_du_texte(TEXTE_DANOIS) == LANGUE_DANOIS);
  assert(langue_du_texte(TEXTE_NORVEGIEN) == LANGUE_NORVEGIEN);
  assert(langue_du_texte(TEXTE_FINNOIS) == LANGUE_FINNOIS);
  assert(langue_du_texte(TEXTE_POLONAIS) == LANGUE_POLONAIS);
  assert(langue_du_texte(TEXTE_TCHEQUE) == LANGUE_TCHEQUE);
  assert(langue_du_texte(TEXTE_HONGROIS) == LANGUE_HONGROIS);
  assert(langue_du_texte(TEXTE_ROUMAIN) == LANGUE_ROUMAIN);
  assert(langue_du_texte(TEXTE_TURC) == LANGUE_TURC);
  assert(langue_du_texte(TEXTE_ESPERANTO) == LANGUE_ESPERANTO);
}

/**
 * Corpus de trois textes dans une langue et d'un texte dans une autre :
 * verifie la langue majoritaire, qu'aucune phrase des trois textes n'est
 * ecartee et que le texte etranger l'est en entier (ou au moins pour
 * moitie si partiel : la fin d'un texte dans une langue proche contient
 * parfois trop peu de mots qui la distinguent).
 */
static void verifier_texte_etranger(const char *texte, langue_t langue,
                                    const char *etranger, bool partiel)
{
  langue_detecteur_t *d = langue_detecteur_creer();
  size_t avant;
  size_t mots_etrangers;
  unsigned int i;

  for (i = 0u; i < 2u; i++) {
    ajouter_texte(d, texte);
    langue_detecteur_fin_texte(d);
  }
  avant = langue_detecteur_nb_phrases(d);
  mots_etrangers = ajouter_texte(d, etranger);
  langue_detecteur_fin_texte(d);
  ajouter_texte(d, texte);
  assert(langue_detecteur_conclure(d));
  assert(langue_detecteur_majoritaire(d) == langue);
  assert(nb_gardees(d, 0u, avant) == avant);
  assert(langue_detecteur_nb_mots_ecartes(d) <= mots_etrangers);
  assert(langue_detecteur_nb_mots_ecartes(d) >= (partiel ? mots_etrangers / 2u : mots_etrangers));
  langue_detecteur_detruire(&d);
}

static void test_langues_proches(void)
{
  /* danois et norvegien, espagnol et catalan, polonais et tcheque : les
     mots qu'elles ne partagent pas les distinguent */
  verifier_texte_etranger(TEXTE_NORVEGIEN, LANGUE_NORVEGIEN, TEXTE_DANOIS, true);
  verifier_texte_etranger(TEXTE_DANOIS, LANGUE_DANOIS, TEXTE_NORVEGIEN, true);
  verifier_texte_etranger(TEXTE_SUEDOIS, LANGUE_SUEDOIS, TEXTE_DANOIS, true);
  verifier_texte_etranger(TEXTE_ESPAGNOL, LANGUE_ESPAGNOL, TEXTE_CATALAN, false);
  verifier_texte_etranger(TEXTE_CATALAN, LANGUE_CATALAN, TEXTE_ESPAGNOL, false);
  verifier_texte_etranger(TEXTE_POLONAIS, LANGUE_POLONAIS, TEXTE_TCHEQUE, false);
  verifier_texte_etranger(TEXTE_TCHEQUE, LANGUE_TCHEQUE, TEXTE_POLONAIS, false);
  /* un texte dans chacune des nouvelles langues est ecarte d'un corpus francais, et inversement */
  verifier_texte_etranger(TEXTE_FRANCAIS, LANGUE_FRANCAIS, TEXTE_LATIN, false);
  verifier_texte_etranger(TEXTE_FRANCAIS, LANGUE_FRANCAIS, TEXTE_CATALAN, false);
  verifier_texte_etranger(TEXTE_FRANCAIS, LANGUE_FRANCAIS, TEXTE_FINNOIS, false);
  verifier_texte_etranger(TEXTE_FRANCAIS, LANGUE_FRANCAIS, TEXTE_HONGROIS, false);
  verifier_texte_etranger(TEXTE_FRANCAIS, LANGUE_FRANCAIS, TEXTE_ROUMAIN, false);
  verifier_texte_etranger(TEXTE_FRANCAIS, LANGUE_FRANCAIS, TEXTE_TURC, false);
  verifier_texte_etranger(TEXTE_FRANCAIS, LANGUE_FRANCAIS, TEXTE_ESPERANTO, false);
  verifier_texte_etranger(TEXTE_LATIN, LANGUE_LATIN, TEXTE_FRANCAIS, false);
  verifier_texte_etranger(TEXTE_HONGROIS, LANGUE_HONGROIS, TEXTE_ANGLAIS, false);
}

static void test_langue_inconnue(void)
{
  /* aucun mot-outil : langue inconnue, tout est garde */
  langue_detecteur_t *d = langue_detecteur_creer();
  size_t nb = ajouter_texte(d, "bla blo bli. zorg zarg zurg zirg. blo bla.");

  assert(nb == 9u);
  assert(langue_detecteur_nb_mots(d) == 9u);
  assert(langue_detecteur_nb_phrases(d) == 3u);
  assert(langue_detecteur_conclure(d));
  assert(langue_detecteur_majoritaire(d) == LANGUE_INCONNUE);
  assert(nb_gardees(d, 0u, 3u) == 3u);
  assert(langue_detecteur_nb_phrases_ecartees(d) == 0u);
  langue_detecteur_detruire(&d);

  /* trop peu de mots-outils (moins du quart des mots) : inconnue aussi */
  d = langue_detecteur_creer();
  ajouter_texte(d, "le zorg zarg zurg zirg blo bla bli blu.");
  assert(langue_detecteur_conclure(d));
  assert(langue_detecteur_majoritaire(d) == LANGUE_INCONNUE);
  assert(langue_detecteur_phrase_gardee(d, 0u));
  langue_detecteur_detruire(&d);
}


/* ======================================================================== */
/*                            Passages ecartes                              */
/* ======================================================================== */

static void test_texte_etranger_ecarte(void)
{
  /* trois textes francais et un texte anglais : l'anglais est ecarte */
  langue_detecteur_t *d = langue_detecteur_creer();
  size_t phrases_francaises;
  size_t phrases_anglaises;
  size_t mots_anglais;

  ajouter_texte(d, TEXTE_FRANCAIS);
  langue_detecteur_fin_texte(d);
  ajouter_texte(d, TEXTE_FRANCAIS);
  langue_detecteur_fin_texte(d);
  phrases_francaises = langue_detecteur_nb_phrases(d);
  mots_anglais = ajouter_texte(d, TEXTE_ANGLAIS);
  langue_detecteur_fin_texte(d);
  phrases_anglaises = langue_detecteur_nb_phrases(d) - phrases_francaises;
  ajouter_texte(d, TEXTE_FRANCAIS);
  assert(langue_detecteur_conclure(d));

  assert(langue_detecteur_majoritaire(d) == LANGUE_FRANCAIS);
  assert(nb_gardees(d, 0u, phrases_francaises) == phrases_francaises);
  assert(nb_gardees(d, phrases_francaises, phrases_francaises + phrases_anglaises) == 0u);
  assert(nb_gardees(d, phrases_francaises + phrases_anglaises, langue_detecteur_nb_phrases(d))
         == langue_detecteur_nb_phrases(d) - phrases_francaises - phrases_anglaises);
  assert(langue_detecteur_nb_phrases_ecartees(d) == phrases_anglaises);
  assert(langue_detecteur_nb_mots_ecartes(d) == mots_anglais);
  langue_detecteur_detruire(&d);
}

static void test_langue_majoritaire_anglaise(void)
{
  /* le meme module ecarte le francais d'un corpus anglais */
  langue_detecteur_t *d = langue_detecteur_creer();
  size_t phrases_anglaises;

  ajouter_texte(d, TEXTE_ANGLAIS);
  langue_detecteur_fin_texte(d);
  ajouter_texte(d, TEXTE_ANGLAIS);
  langue_detecteur_fin_texte(d);
  phrases_anglaises = langue_detecteur_nb_phrases(d);
  ajouter_texte(d, TEXTE_FRANCAIS);
  assert(langue_detecteur_conclure(d));
  assert(langue_detecteur_majoritaire(d) == LANGUE_ANGLAIS);
  assert(nb_gardees(d, 0u, phrases_anglaises) == phrases_anglaises);
  assert(nb_gardees(d, phrases_anglaises, langue_detecteur_nb_phrases(d)) == 0u);
  langue_detecteur_detruire(&d);
}

static void test_passage_etranger_dans_un_texte(void)
{
  /* un seul texte : francais, puis anglais, puis francais. Les passages
     ont au moins LANGUE_TAILLE_PASSAGE mots : les phrases anglaises sont
     ecartees, sauf eventuellement celles qui partagent un passage avec du
     francais, aux frontieres */
  langue_detecteur_t *d = langue_detecteur_creer();
  size_t debut_anglais;
  size_t fin_anglais;
  size_t total;

  ajouter_texte(d, TEXTE_FRANCAIS);
  ajouter_texte(d, TEXTE_FRANCAIS);
  debut_anglais = langue_detecteur_nb_phrases(d);
  ajouter_texte(d, TEXTE_ANGLAIS);
  ajouter_texte(d, TEXTE_ANGLAIS);
  fin_anglais = langue_detecteur_nb_phrases(d);
  ajouter_texte(d, TEXTE_FRANCAIS);
  ajouter_texte(d, TEXTE_FRANCAIS);
  total = langue_detecteur_nb_phrases(d);
  assert(langue_detecteur_conclure(d));

  assert(langue_detecteur_majoritaire(d) == LANGUE_FRANCAIS);
  /* le coeur du passage anglais est ecarte (frontieres : 5 phrases) */
  assert(nb_gardees(d, debut_anglais + 5u, fin_anglais - 5u) == 0u);
  /* le francais loin des frontieres est garde */
  assert(nb_gardees(d, 0u, debut_anglais - 5u) == debut_anglais - 5u);
  assert(nb_gardees(d, fin_anglais + 5u, total) == total - fin_anglais - 5u);
  assert(langue_detecteur_nb_phrases_ecartees(d) == total - nb_gardees(d, 0u, total));
  langue_detecteur_detruire(&d);
}

static void test_lexique_bilingue_ecarte(void)
{
  /* regle des mots connus : un lexique bilingue n'a presque pas de
     mots-outils, mais ses mots n'apparaissent nulle part ailleurs ; une
     liste de mots du texte, elle, est gardee */
  langue_detecteur_t *d = langue_detecteur_creer();
  size_t debut_liste;
  size_t debut_lexique;
  size_t total;
  unsigned int i;

  /* le texte courant domine : un mot est connu si la moitie au moins de
     ses occurrences sont dans des passages de reference */
  for (i = 0u; i < 4u; i++) {
    ajouter_texte(d, TEXTE_FRANCAIS);
  }
  langue_detecteur_fin_texte(d);
  debut_liste = langue_detecteur_nb_phrases(d);
  ajouter_texte(d, "chat toit maison oiseaux ciel fille porte nuit cuisine "
                   "mere soupe repas soir pere livre feu village. chat "
                   "maison porte nuit soupe livre feu village ciel toit.");
  langue_detecteur_fin_texte(d);
  debut_lexique = langue_detecteur_nb_phrases(d);
  ajouter_texte(d, "chat cat. toit roof. maison house. oiseau bird. ciel "
                   "sky. fille girl. porte door. nuit night. cuisine "
                   "kitchen. soupe soup. repas meal. livre book. feu fire.");
  total = langue_detecteur_nb_phrases(d);
  assert(langue_detecteur_conclure(d));

  assert(langue_detecteur_majoritaire(d) == LANGUE_FRANCAIS);
  assert(nb_gardees(d, 0u, debut_liste) == debut_liste);
  assert(nb_gardees(d, debut_liste, debut_lexique) == debut_lexique - debut_liste);
  assert(nb_gardees(d, debut_lexique, total) == 0u);
  assert(langue_detecteur_nb_phrases_ecartees(d) == total - debut_lexique);
  assert(langue_detecteur_nb_mots_ecartes(d) == 26u);
  langue_detecteur_detruire(&d);
}

static void test_fin_de_texte_courte(void)
{
  /* quelques mots anglais a la fin d'un texte francais rejoignent le
     dernier passage du texte (gardes avec lui) ; les memes mots, seuls
     dans leur texte, forment un passage a part (ecarte) */
  const char *const anglais = "the cat is on the mat.";
  langue_detecteur_t *d = langue_detecteur_creer();
  size_t n;

  ajouter_texte(d, TEXTE_FRANCAIS);
  ajouter_texte(d, anglais);
  langue_detecteur_fin_texte(d);
  n = langue_detecteur_nb_phrases(d);
  ajouter_texte(d, anglais);
  assert(langue_detecteur_conclure(d));
  assert(langue_detecteur_majoritaire(d) == LANGUE_FRANCAIS);
  assert(nb_gardees(d, 0u, n) == n);
  assert(!langue_detecteur_phrase_gardee(d, n));
  assert(langue_detecteur_nb_phrases_ecartees(d) == 1u);
  assert(langue_detecteur_nb_mots_ecartes(d) == 6u);
  langue_detecteur_detruire(&d);
}

static void test_phrases_d_un_meme_passage(void)
{
  /* un passage n'est ferme qu'a la fin d'une phrase, quand il a au moins
     LANGUE_TAILLE_PASSAGE mots : deux phrases courtes qui se suivent
     partagent le meme passage, donc le meme sort */
  langue_detecteur_t *d = langue_detecteur_creer();
  size_t phrase;
  size_t n;
  unsigned int i;

  ajouter_texte(d, TEXTE_FRANCAIS);
  langue_detecteur_fin_texte(d);
  n = langue_detecteur_nb_phrases(d);
  /* 12 phrases de 4 mots : la francaise et l'anglaise alternent */
  for (i = 0u; i < 6u; i++) {
    ajouter_texte(d, "le chat dort ici. the dog was there.");
  }
  assert(langue_detecteur_conclure(d));
  for (phrase = n; phrase + 1u < n + 10u; phrase += 2u) {
    assert(langue_detecteur_phrase_gardee(d, phrase) == langue_detecteur_phrase_gardee(d, phrase + 1u));
  }
  langue_detecteur_detruire(&d);
}

static void test_phrase_inachevee(void)
{
  /* la fin du texte termine la phrase en cours */
  langue_detecteur_t *d = langue_detecteur_creer();

  assert(langue_detecteur_ajouter_mot(d, "le", false));
  assert(langue_detecteur_ajouter_mot(d, "chat", false));
  assert(langue_detecteur_nb_phrases(d) == 0u);
  langue_detecteur_fin_texte(d);
  assert(langue_detecteur_nb_phrases(d) == 1u);
  langue_detecteur_fin_texte(d);               /* deja termine */
  assert(langue_detecteur_nb_phrases(d) == 1u);
  assert(langue_detecteur_ajouter_mot(d, "il", false));
  assert(langue_detecteur_ajouter_mot(d, "dort", true));
  assert(langue_detecteur_ajouter_mot(d, "la", false));
  /* conclure termine aussi le texte en cours */
  assert(langue_detecteur_conclure(d));
  assert(langue_detecteur_nb_phrases(d) == 3u);
  assert(langue_detecteur_nb_mots(d) == 5u);
  assert(langue_detecteur_majoritaire(d) == LANGUE_FRANCAIS);
  assert(nb_gardees(d, 0u, 3u) == 3u);
  langue_detecteur_detruire(&d);
}

static void test_grand_corpus(void)
{
  /* 300 textes : les tableaux s'agrandissent ; un texte sur dix est en
     allemand et il est ecarte en entier */
  langue_detecteur_t *d = langue_detecteur_creer();
  size_t debut[301];
  size_t mots_allemands = 0u;
  size_t gardees;
  unsigned int i;

  for (i = 0u; i < 300u; i++) {
    debut[i] = langue_detecteur_nb_phrases(d);
    if (i % 10u == 9u) {
      mots_allemands += ajouter_texte(d, TEXTE_ALLEMAND);
    } else {
      ajouter_texte(d, TEXTE_FRANCAIS);
    }
    langue_detecteur_fin_texte(d);
  }
  debut[300] = langue_detecteur_nb_phrases(d);
  assert(langue_detecteur_conclure(d));
  assert(langue_detecteur_majoritaire(d) == LANGUE_FRANCAIS);
  for (i = 0u; i < 300u; i++) {
    gardees = nb_gardees(d, debut[i], debut[i + 1u]);
    assert(gardees == ((i % 10u == 9u) ? 0u : debut[i + 1u] - debut[i]));
  }
  assert(langue_detecteur_nb_mots_ecartes(d) == mots_allemands);
  langue_detecteur_detruire(&d);
}


/* ======================================================================== */
/*                        Verification des contrats                         */
/* ======================================================================== */

static void violation_nom_langue_invalide(void)
{
  langue_nom(NB_LANGUES);
}

static void violation_profils_mot_nul(void)
{
  langue_profils(NULL);
}

static void violation_profils_mot_majuscule(void)
{
  langue_profils("Le");
}

static void violation_detecteur_nul(void)
{
  langue_detecteur_nb_mots(NULL);
}

static void violation_mot_vide(void)
{
  langue_detecteur_t *d = langue_detecteur_creer();

  langue_detecteur_ajouter_mot(d, "", true);
}

static void violation_mot_invalide(void)
{
  langue_detecteur_t *d = langue_detecteur_creer();

  langue_detecteur_ajouter_mot(d, "l'eau", true);
}

static void violation_ajout_apres_conclusion(void)
{
  langue_detecteur_t *d = langue_detecteur_creer();

  langue_detecteur_conclure(d);
  langue_detecteur_ajouter_mot(d, "le", true);
}

static void violation_conclure_deux_fois(void)
{
  langue_detecteur_t *d = langue_detecteur_creer();

  langue_detecteur_conclure(d);
  langue_detecteur_conclure(d);
}

static void violation_majoritaire_avant_conclusion(void)
{
  langue_detecteur_t *d = langue_detecteur_creer();

  langue_detecteur_majoritaire(d);
}

static void violation_phrase_inexistante(void)
{
  langue_detecteur_t *d = langue_detecteur_creer();

  langue_detecteur_ajouter_mot(d, "le", true);
  langue_detecteur_conclure(d);
  langue_detecteur_phrase_gardee(d, 1u);       /* seule la phrase 0 existe */
}

static void sans_violation(void)
{
  langue_detecteur_t *d = langue_detecteur_creer();

  langue_detecteur_ajouter_mot(d, "le", true);
  langue_detecteur_conclure(d);
  langue_detecteur_phrase_gardee(d, 0u);
  langue_detecteur_detruire(&d);
}

static void test_contrats(void)
{
  assert(!viole_contrat(sans_violation));
  assert(viole_contrat(violation_nom_langue_invalide));
  assert(viole_contrat(violation_profils_mot_nul));
  assert(viole_contrat(violation_profils_mot_majuscule));
  assert(viole_contrat(violation_detecteur_nul));
  assert(viole_contrat(violation_mot_vide));
  assert(viole_contrat(violation_mot_invalide));
  assert(viole_contrat(violation_ajout_apres_conclusion));
  assert(viole_contrat(violation_conclure_deux_fois));
  assert(viole_contrat(violation_majoritaire_avant_conclusion));
  assert(viole_contrat(violation_phrase_inexistante));
}


int main(void)
{
  printf("Tests du module langue :\n");
  LANCER_TEST(test_nom_des_langues);
  LANCER_TEST(test_profils);
  LANCER_TEST(test_detecteur_vide);
  LANCER_TEST(test_chaque_langue);
  LANCER_TEST(test_langues_proches);
  LANCER_TEST(test_langue_inconnue);
  LANCER_TEST(test_texte_etranger_ecarte);
  LANCER_TEST(test_langue_majoritaire_anglaise);
  LANCER_TEST(test_passage_etranger_dans_un_texte);
  LANCER_TEST(test_lexique_bilingue_ecarte);
  LANCER_TEST(test_fin_de_texte_courte);
  LANCER_TEST(test_phrases_d_un_meme_passage);
  LANCER_TEST(test_phrase_inachevee);
  LANCER_TEST(test_grand_corpus);
  LANCER_TEST(test_contrats);
  afficher_bilan("langue");
  return EXIT_SUCCESS;
}
