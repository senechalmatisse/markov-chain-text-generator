/**
@file langue.c
Fichier langue.c

Implementation de la detection de langue (cf. langue.h).

Representation du detecteur, pendant les ajouts :
  - mots : table de hachage des mots distincts (identifiants 0, 1, ...) ;
  - profils[id] : masque des langues dont le profil contient le mot id ;
  - occurrences[i] : identifiant du i-eme mot ajoute ;
  - passages[p] : premiere phrase, premiere occurrence et score dans chaque
    langue du passage p ; le passage en cours (ouvert) n'y figure pas
    encore, ses compteurs sont dans la structure.
Apres la conclusion, il ne reste que gardees[phrase] (un octet par phrase)
et les totaux : la table des mots et les occurrences sont liberees.

Invariants (pendant les ajouts) :
  - capacite_passages > nb_passages des que le passage ouvert contient un
    mot : le fermer ne demande jamais d'allocation ;
  - les passages se suivent : le passage p couvre les phrases
    [passages[p].premiere_phrase, passages[p + 1].premiere_phrase[ et les
    occurrences correspondantes ; le passage ouvert couvre la suite.
*/
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include "contrat.h"
#include "hachage.h"
#include "langue.h"

/** Capacite initiale des tableaux dynamiques. */
#define CAPACITE_INITIALE ((size_t)64)

/** Un passage de n mots a une langue si son score atteint n / DIVISEUR. */
#define DIVISEUR_SCORE 5u

/** Ecart minimal de score pour qu'une langue en domine une autre. */
#define ECART_MINIMAL 2u

/** La langue majoritaire doit couvrir 1 / DIVISEUR_MAJORITE des mots. */
#define DIVISEUR_MAJORITE 4u

/** Un passage est garde si NUMERATEUR / DENOMINATEUR de ses mots sont connus. */
#define NUMERATEUR_CONNUS 7u
#define DENOMINATEUR_CONNUS 10u

/** Bit d'une langue dans un masque. */
#define BIT(langue) (1u << (unsigned int)(langue))

#define FR BIT(LANGUE_FRANCAIS)
#define EN BIT(LANGUE_ANGLAIS)
#define ES BIT(LANGUE_ESPAGNOL)
#define DE BIT(LANGUE_ALLEMAND)
#define IT BIT(LANGUE_ITALIEN)
#define PT BIT(LANGUE_PORTUGAIS)
#define NL BIT(LANGUE_NEERLANDAIS)
#define LA BIT(LANGUE_LATIN)
#define CA BIT(LANGUE_CATALAN)
#define SV BIT(LANGUE_SUEDOIS)
#define DA BIT(LANGUE_DANOIS)
#define NO BIT(LANGUE_NORVEGIEN)
#define FI BIT(LANGUE_FINNOIS)
#define PL BIT(LANGUE_POLONAIS)
#define CS BIT(LANGUE_TCHEQUE)
#define HU BIT(LANGUE_HONGROIS)
#define RO BIT(LANGUE_ROUMAIN)
#define TR BIT(LANGUE_TURC)
#define EO BIT(LANGUE_ESPERANTO)

/* chaque langue a son bit dans un masque de type unsigned int */
typedef char verification_masque[(NB_LANGUES <= 32) ? 1 : -1];

/** Mot-outil et langues dont il fait partie du profil. */
typedef struct {
  const char *mot;
  unsigned int langues;
} mot_outil_t;

/**
 * Profils des langues : mots-outils les plus frequents de chaque langue
 * (de 67 a 112 par langue), ecrits comme les mots de l'iterateur
 * (minuscules sans accents : "ar" pour le suedois "ar" ecrit avec un a
 * trema, "vaere" pour le "vaere" danois et norvegien ecrit avec un "ae" lie),
 * tries dans l'ordre de strcmp (recherche dichotomique).
 * Les lettres seules sont les mots elides ("l'", "d'", "qu'", "'s", "'t")
 * et les mots d'une lettre (polonais "w", tcheque "v").
 */
static const mot_outil_t MOTS_OUTILS[] = {
  { "a", FR | EN | ES | IT | PT | LA | CA | NO | PL | CS | HU | RO },
  { "aan", NL },
  { "ab", LA },
  { "abans", CA },
  { "aber", DE },
  { "about", EN },
  { "aby", PL | CS },
  { "ac", LA },
  { "aceasta", RO },
  { "acest", RO },
  { "acum", RO },
  { "ad", LA | DA },
  { "af", DA },
  { "after", EN },
  { "ai", FR | IT | RO },
  { "aici", RO },
  { "aina", FI },
  { "ainsi", FR },
  { "aki", HU },
  { "akkor", HU },
  { "al", ES | IT | NL | CA | RO | EO },
  { "alatt", HU },
  { "aldri", NO },
  { "aldrig", DA },
  { "ale", PL | CS | RO },
  { "all", EN | IT },
  { "alla", IT | CA | SV },
  { "alle", DE | IT | DA | NO },
  { "allo", IT },
  { "allt", SV },
  { "alltid", NO },
  { "alors", FR },
  { "als", DE | NL | CA },
  { "alt", DA | NO },
  { "altid", DA },
  { "am", DE | RO },
  { "ama", TR },
  { "amb", CA },
  { "amely", HU },
  { "ami", HU },
  { "an", EN | DE | SV },
  { "ancak", TR },
  { "anche", IT },
  { "ancora", IT },
  { "and", EN },
  { "anden", DA },
  { "andra", SV },
  { "ani", PL | CS },
  { "ankau", EO },
  { "ankorau", EO },
  { "annat", SV },
  { "annen", NO },
  { "antau", EO },
  { "ante", ES | PT | LA },
  { "any", EN },
  { "ao", PT },
  { "aos", PT },
  { "apres", FR },
  { "apud", LA },
  { "aquest", CA },
  { "aquesta", CA },
  { "aquestes", CA },
  { "aquests", CA },
  { "aqui", CA },
  { "ar", SV | RO },
  { "ara", CA },
  { "arasinda", TR },
  { "are", EN },
  { "artik", TR },
  { "as", FR | EN | PT },
  { "asa", RO },
  { "at", EN | DA | NO },
  { "ate", PT },
  { "ati", RO },
  { "atque", LA },
  { "att", SV },
  { "atunci", RO },
  { "au", FR | RO | EO },
  { "auch", DE },
  { "auf", DE },
  { "aus", DE },
  { "aussi", FR },
  { "aut", LA },
  { "autem", LA },
  { "aux", FR },
  { "av", SV | NO },
  { "avait", FR },
  { "avea", RO },
  { "avec", FR },
  { "aven", SV },
  { "avoir", FR },
  { "ayni", TR },
  { "ayrica", TR },
  { "az", HU | TR },
  { "azok", HU },
  { "azt", HU },
  { "bade", SV },
  { "bana", TR },
  { "bara", SV },
  { "bardzo", PL },
  { "bazi", TR },
  { "be", EN | HU },
  { "been", EN },
  { "bei", DE },
  { "belki", TR },
  { "ben", TR },
  { "beni", TR },
  { "benim", TR },
  { "beri", TR },
  { "bez", PL },
  { "bien", FR },
  { "bij", NL },
  { "bile", TR },
  { "bir", TR },
  { "biri", TR },
  { "bis", DE },
  { "biz", TR },
  { "bize", TR },
  { "bland", SV },
  { "ble", NO },
  { "blev", SV | DA },
  { "bli", SV | NO },
  { "blir", SV | NO },
  { "blive", DA },
  { "bliver", DA },
  { "bo", PL },
  { "boyle", TR },
  { "bu", TR },
  { "bunlar", TR },
  { "bunu", TR },
  { "bunun", TR },
  { "burada", TR },
  { "but", EN },
  { "butun", TR },
  { "by", EN | PL | CS },
  { "byc", PL },
  { "byl", PL | CS },
  { "byla", PL | CS },
  { "bylo", PL | CS },
  { "byt", CS },
  { "c", FR | IT },
  { "ca", FR | RO },
  { "can", EN },
  { "cand", RO },
  { "cap", CA },
  { "car", FR | EO },
  { "care", RO },
  { "ce", FR | RO | EO },
  { "cela", FR },
  { "ces", FR },
  { "cet", FR },
  { "cette", FR },
  { "che", IT },
  { "chez", FR },
  { "chi", IT },
  { "ci", IT | EO },
  { "cine", RO },
  { "cio", EO },
  { "ciu", EO },
  { "co", PL | CS },
  { "cok", TR },
  { "com", PT | CA },
  { "come", IT },
  { "comme", FR },
  { "como", ES | PT },
  { "con", ES | IT },
  { "contra", ES | PT | LA },
  { "cos", PL },
  { "cosi", IT },
  { "could", EN },
  { "csak", HU },
  { "cu", RO | EO },
  { "cual", ES },
  { "cuando", ES },
  { "cui", IT },
  { "cum", LA | RO },
  { "cunku", TR },
  { "czy", PL },
  { "d", FR | EN | IT | CA },
  { "da", DE | IT | PT | SV | DA | NO | TR | EO },
  { "daar", NL },
  { "daha", TR },
  { "dal", IT },
  { "dalla", IT },
  { "dan", NL },
  { "dans", FR },
  { "dar", SV | RO },
  { "das", DE | PT },
  { "dass", DE },
  { "dat", NL },
  { "de", FR | ES | PT | NL | LA | CA | SV | DA | NO | HU | RO | TR | EO },
  { "dedi", TR },
  { "deg", NO },
  { "degil", TR },
  { "degli", IT },
  { "dei", IT },
  { "deinde", LA },
  { "del", ES | IT | CA },
  { "dell", IT },
  { "della", IT },
  { "delle", IT },
  { "dello", IT },
  { "dels", CA },
  { "dem", DE | SV | DA | NO },
  { "den", DE | NL | SV | DA },
  { "denna", SV },
  { "denne", DA | NO },
  { "depois", PT },
  { "der", DE | NL | DA | NO },
  { "deras", SV },
  { "dere", NO },
  { "deres", DA | NO },
  { "des", FR | DE },
  { "desde", ES | PT },
  { "despres", CA },
  { "dess", SV },
  { "dessa", SV },
  { "det", SV | DA | NO },
  { "detta", SV },
  { "dette", DA | NO },
  { "deux", FR },
  { "devas", EO },
  { "deze", NL },
  { "di", IT },
  { "did", EN },
  { "die", DE | NL },
  { "dig", DA },
  { "diger", TR },
  { "din", DA | NO | RO },
  { "disse", DA | NO },
  { "dit", NL },
  { "diye", TR },
  { "dla", PL },
  { "do", EN | PT | PL | CS | EO },
  { "doar", RO },
  { "doch", DE },
  { "dock", SV },
  { "dog", DA },
  { "dogru", TR },
  { "donc", FR },
  { "donde", ES },
  { "dont", FR },
  { "door", NL },
  { "dos", PT },
  { "dove", IT },
  { "du", FR | DE | SV | DA | NO | EO },
  { "dum", LA },
  { "dupa", RO },
  { "durante", ES | IT | PT },
  { "durch", DE },
  { "e", ES | IT | PT },
  { "ea", LA | RO },
  { "eam", LA },
  { "ec", EO },
  { "ed", IT },
  { "een", NL },
  { "efter", SV | DA },
  { "eg", NO },
  { "eger", TR },
  { "egy", HU },
  { "ehka", FI },
  { "ei", FI | RO },
  { "eika", FI },
  { "ein", DE },
  { "eine", DE },
  { "einem", DE },
  { "einen", DE },
  { "einer", DE },
  { "eines", DE },
  { "eius", LA },
  { "eivat", FI },
  { "el", ES | CA | HU | RO | EO },
  { "ela", PT },
  { "elas", PT },
  { "ele", PT },
  { "eles", PT },
  { "eli", FI },
  { "ell", CA },
  { "ella", ES | CA },
  { "elle", FR },
  { "eller", SV | DA | NO },
  { "elles", FR | CA },
  { "ellos", ES },
  { "ells", CA },
  { "els", CA },
  { "em", PT | CA },
  { "emme", FI },
  { "en", FR | ES | NL | CA | SV | DA | NO | FI | HU | TR | EO },
  { "encara", CA },
  { "encore", FR },
  { "end", DA },
  { "enemman", FI },
  { "enim", LA },
  { "enn", NO },
  { "ens", CA },
  { "entre", FR | ES | PT | CA },
  { "er", DE | NL | DA | NO },
  { "era", ES | IT | PT | CA | RO },
  { "erat", LA },
  { "ergo", LA },
  { "es", FR | ES | DE | CA | HU },
  { "esa", ES },
  { "ese", ES },
  { "eso", ES },
  { "essa", PT },
  { "esse", PT | LA },
  { "essere", IT },
  { "esset", LA },
  { "est", FR | LA },
  { "esta", ES | PT },
  { "estas", ES | EO },
  { "este", ES | PT | RO },
  { "esti", EO },
  { "estis", EO },
  { "esto", ES },
  { "estos", ES | EO },
  { "et", FR | LA | DA | NO | FI },
  { "etait", FR },
  { "ete", FR },
  { "etiam", LA },
  { "etre", FR },
  { "ett", SV },
  { "etta", FI },
  { "etter", NO },
  { "eu", PT | RO },
  { "eum", LA },
  { "evet", TR },
  { "ex", LA },
  { "ez", HU },
  { "ezek", HU },
  { "ezt", HU },
  { "fa", SV },
  { "fait", FR },
  { "fakat", TR },
  { "far", SV },
  { "fel", HU },
  { "fer", CA },
  { "fi", RO },
  { "fick", SV },
  { "fie", RO },
  { "fik", DA },
  { "fikk", NO },
  { "fins", CA },
  { "foarte", RO },
  { "foi", PT },
  { "for", EN | SV | DA },
  { "fost", RO },
  { "fra", DA | NO },
  { "fram", SV },
  { "fran", SV },
  { "from", EN },
  { "fue", ES },
  { "fuit", LA },
  { "fur", DE },
  { "gar", SV },
  { "gdy", PL },
  { "gdzie", PL },
  { "geen", NL },
  { "genom", SV },
  { "gi", EO },
  { "gia", IT },
  { "gibi", TR },
  { "gik", DA },
  { "gikk", NO },
  { "gin", EO },
  { "gis", EO },
  { "gli", IT },
  { "go", PL },
  { "gor", SV },
  { "gore", TR },
  { "ha", ES | IT | PT | CA | SV | NO | HU },
  { "haar", NL },
  { "haben", DE },
  { "habia", ES },
  { "had", EN | NL },
  { "hadde", NO },
  { "hade", SV },
  { "haec", LA },
  { "ham", DA | NO },
  { "han", ES | CA | SV | DA | NO | FI },
  { "hanella", FI },
  { "hanelle", FI },
  { "hanen", FI },
  { "hanet", FI },
  { "hangi", TR },
  { "hanno", IT },
  { "hans", SV | DA | NO },
  { "hanta", FI },
  { "har", SV | DA | NO },
  { "has", EN },
  { "hasta", ES },
  { "hat", DE },
  { "havas", EO },
  { "havde", DA },
  { "have", EN | DA },
  { "havis", EO },
  { "hay", ES },
  { "hayir", TR },
  { "he", EN | FI },
  { "hebben", NL },
  { "heeft", NL },
  { "heidan", FI },
  { "heita", FI },
  { "hela", SV },
  { "helt", SV },
  { "hem", NL | TR },
  { "hemen", TR },
  { "hende", DA },
  { "hendes", DA },
  { "henne", SV | NO },
  { "hennes", NO },
  { "hep", TR },
  { "her", EN | DA | NO | TR },
  { "het", NL },
  { "hic", LA | TR },
  { "hier", DE | NL },
  { "hij", NL },
  { "him", EN },
  { "his", EN },
  { "ho", IT | CA | CS },
  { "hoc", LA },
  { "hogy", HU },
  { "hol", HU },
  { "hon", SV },
  { "honom", SV },
  { "hos", DA | NO },
  { "hun", NL | DA | NO },
  { "hur", SV },
  { "hva", NO },
  { "hvad", DA },
  { "hvis", DA | NO },
  { "hvor", DA | NO },
  { "hyvin", FI },
  { "i", EN | IT | CA | SV | DA | NO | PL | CS | RO },
  { "iam", LA },
  { "ich", DE | PL },
  { "icin", TR },
  { "icinde", TR },
  { "id", LA },
  { "idi", TR },
  { "if", EN },
  { "igen", HU },
  { "igitur", LA },
  { "ihm", DE },
  { "ihn", DE },
  { "ihr", DE },
  { "ihre", DE },
  { "ii", RO },
  { "ik", NL },
  { "ikke", DA | NO },
  { "ikkje", NO },
  { "il", FR | IT },
  { "ile", TR },
  { "ili", EO },
  { "ilia", EO },
  { "ilin", EO },
  { "illa", LA },
  { "ille", LA },
  { "illud", LA },
  { "ilman", FI },
  { "ils", FR },
  { "ilyen", HU },
  { "im", DE },
  { "in", EN | DE | IT | NL | LA | SV | RO },
  { "inca", RO },
  { "ind", DA },
  { "ingen", SV },
  { "inn", NO },
  { "inom", SV },
  { "inte", SV },
  { "inter", LA | EO },
  { "into", EN },
  { "io", IT },
  { "is", EN | NL | LA | HU },
  { "ise", TR },
  { "isso", PT },
  { "ist", DE },
  { "iste", TR },
  { "isto", PT },
  { "it", EN },
  { "ita", LA },
  { "its", EN },
  { "itt", HU },
  { "j", FR },
  { "ja", PT | CA | FI | PL | CS | EO },
  { "jag", SV },
  { "jak", PL | CS },
  { "jako", PL | CS },
  { "jam", EO },
  { "je", FR | NL | CS | EO },
  { "jednak", PL },
  { "jego", PL },
  { "jeho", CS },
  { "jej", PL },
  { "jejich", CS },
  { "jen", CS | EO },
  { "jer", DA },
  { "jeres", DA },
  { "jesli", PL },
  { "jest", PL },
  { "jeste", CS },
  { "jeszcze", PL },
  { "ji", CS },
  { "jiz", CS },
  { "jo", CA | DA | NO | FI },
  { "joka", FI },
  { "jokin", FI },
  { "jonka", FI },
  { "jos", FI },
  { "jossa", FI },
  { "jota", FI },
  { "jotain", FI },
  { "jotka", FI },
  { "jotta", FI },
  { "jsem", CS },
  { "jsi", CS },
  { "jsme", CS },
  { "jsou", CS },
  { "ju", SV },
  { "juz", PL },
  { "k", CS },
  { "kadar", TR },
  { "kaikki", FI },
  { "kaj", EO },
  { "kan", NL | SV | NO },
  { "kann", DE },
  { "kanske", SV },
  { "kanskje", NO },
  { "kanssa", FI },
  { "karsi", TR },
  { "kde", CS },
  { "kdyz", CS },
  { "ke", EO },
  { "kell", HU },
  { "kendi", TR },
  { "ket", HU },
  { "ki", HU | TR },
  { "kiam", EO },
  { "kie", EO },
  { "kiedy", PL },
  { "kiel", EO },
  { "kim", TR },
  { "kio", EO },
  { "kiu", EO },
  { "kiuj", EO },
  { "kom", SV },
  { "kommer", SV },
  { "koska", FI },
  { "kozott", HU },
  { "ktera", CS },
  { "ktere", CS },
  { "ktery", CS },
  { "ktora", PL },
  { "ktore", PL },
  { "ktory", PL },
  { "kuin", FI },
  { "kuitenkin", FI },
  { "kuka", FI },
  { "kun", FI | EO },
  { "kunne", DA | NO },
  { "kuten", FI },
  { "l", FR | IT | CA },
  { "la", FR | ES | IT | CA | RO | EO },
  { "las", ES },
  { "le", FR | ES | IT | HU | RO },
  { "lehet", HU },
  { "lei", IT },
  { "les", FR | ES | CA },
  { "lesz", HU },
  { "lett", HU },
  { "leur", FR },
  { "leurs", FR },
  { "lhe", PT },
  { "li", CA | EO },
  { "lia", EO },
  { "lidt", DA },
  { "liian", FI },
  { "lin", EO },
  { "litt", NO },
  { "ll", EN },
  { "lo", ES | IT },
  { "lor", RO },
  { "loro", IT },
  { "los", ES },
  { "lub", PL },
  { "lui", FR | IT | RO },
  { "m", FR | EN | RO },
  { "ma", FR | IT | PL | CS | RO },
  { "maar", NL },
  { "magis", LA },
  { "mai", CA | RO },
  { "mais", FR | PT },
  { "majd", HU },
  { "mam", PL },
  { "man", DE | SV | DA | NO },
  { "manga", SV },
  { "mange", DA | NO },
  { "mar", HU },
  { "mas", ES | PT | HU },
  { "maske", DA },
  { "maste", SV },
  { "me", FR | EN | ES | PT | LA | FI },
  { "med", SV | DA | NO },
  { "meer", NL },
  { "meg", NO | HU },
  { "meget", DA },
  { "mehr", DE },
  { "meidan", FI },
  { "meita", FI },
  { "mellan", SV },
  { "mellem", DA },
  { "mellom", NO },
  { "meme", FR },
  { "men", SV | DA | NO },
  { "mer", SV },
  { "mert", HU },
  { "mes", FR | CA },
  { "mesmo", PT },
  { "met", NL },
  { "meu", PT | CA },
  { "meva", CA },
  { "mezi", CS },
  { "mi", ES | IT | PL | CS | HU | TR | EO },
  { "mia", EO },
  { "mich", DE },
  { "miedzy", PL },
  { "miert", HU },
  { "mig", SV | DA },
  { "mihi", LA },
  { "mij", NL },
  { "mijn", NL },
  { "mika", FI },
  { "mikor", HU },
  { "miksi", FI },
  { "min", DA | NO | EO },
  { "mina", FI },
  { "minden", HU },
  { "mine", DA | NO },
  { "minha", PT },
  { "mint", HU },
  { "minua", FI },
  { "minulla", FI },
  { "minulle", FI },
  { "minun", FI },
  { "mir", DE },
  { "missa", FI },
  { "mista", FI },
  { "mit", DE | DA },
  { "mita", FI },
  { "mitaan", FI },
  { "miten", FI },
  { "mitt", NO },
  { "mnie", PL },
  { "mod", DA },
  { "moet", NL },
  { "moi", FR },
  { "molt", CA },
  { "molto", IT },
  { "mon", FR },
  { "more", EN },
  { "most", HU },
  { "mot", SV | NO },
  { "moze", PL },
  { "mu", PL | CS | TR },
  { "mucho", ES },
  { "muito", PT },
  { "mukaan", FI },
  { "mutta", FI },
  { "muy", ES },
  { "my", EN | CS },
  { "mycket", SV },
  { "mye", NO },
  { "myos", FI },
  { "n", FR },
  { "na", PT | NO | PL | CS },
  { "naar", NL },
  { "nach", DE },
  { "nad", PL },
  { "nada", ES | PT },
  { "nagon", SV },
  { "nagot", SV },
  { "nagra", SV },
  { "nagyon", HU },
  { "naiden", FI },
  { "nain", FI },
  { "nam", LA },
  { "nao", PT },
  { "nar", SV | DA | NO },
  { "nas", PT | PL | CS },
  { "nasil", TR },
  { "nawet", PL },
  { "ne", FR | IT | FI | RO | TR | EO },
  { "nebo", CS },
  { "nec", LA },
  { "ned", DA | NO },
  { "neden", TR },
  { "nei", IT },
  { "nekem", HU },
  { "neki", HU },
  { "nel", IT },
  { "nell", IT },
  { "nella", IT },
  { "nelle", IT },
  { "nem", HU },
  { "neni", CS },
  { "neque", LA },
  { "nerede", TR },
  { "nez", CS },
  { "ni", FR | ES | CA | EO },
  { "nia", EO },
  { "nic", PL },
  { "nicht", DE },
  { "nie", PL },
  { "niet", NL },
  { "niin", FI },
  { "niita", FI },
  { "nincs", HU },
  { "no", EN | ES | IT | PT | CA },
  { "noch", DE },
  { "noe", NO },
  { "noen", NO },
  { "nog", NL },
  { "nogen", DA },
  { "noget", DA },
  { "nogle", DA },
  { "noi", IT | RO },
  { "noin", FI },
  { "non", FR | IT | LA },
  { "nos", FR | ES | PT | LA },
  { "nosaltres", CA },
  { "not", EN },
  { "notre", FR },
  { "nous", FR },
  { "nu", NL | SV | DA | RO },
  { "num", PT },
  { "numa", PT },
  { "nun", EO },
  { "nunc", LA },
  { "nur", DE | EO },
  { "nyt", FI },
  { "o", ES | IT | PT | PL | CS | HU | RO | TR },
  { "ob", LA },
  { "och", SV },
  { "ocksa", SV },
  { "od", PL | CS },
  { "oder", DE },
  { "of", EN | NL },
  { "og", DA | NO },
  { "ogsa", DA | NO },
  { "ok", HU },
  { "ol", EO },
  { "olan", TR },
  { "olarak", TR },
  { "oldu", TR },
  { "oldugu", TR },
  { "ole", FI },
  { "olemme", FI },
  { "olen", FI },
  { "olet", FI },
  { "oli", FI },
  { "olisi", FI },
  { "olivat", FI },
  { "olla", FI },
  { "ollut", FI },
  { "olmak", TR },
  { "olur", TR },
  { "olyan", HU },
  { "om", NL | SV | DA | NO },
  { "omnes", LA },
  { "omnia", LA },
  { "on", FR | EN | CA | FI },
  { "ona", TR },
  { "once", TR },
  { "onde", PT },
  { "one", EN },
  { "oni", CS },
  { "onlar", TR },
  { "only", EN },
  { "ont", FR },
  { "onu", TR },
  { "onun", TR },
  { "onze", NL },
  { "ook", NL },
  { "op", NL | DA },
  { "opp", NO },
  { "or", FR | EN },
  { "orada", TR },
  { "oraz", PL },
  { "os", PT | DA },
  { "oss", SV | NO },
  { "ot", HU },
  { "other", EN },
  { "otra", ES },
  { "otro", ES },
  { "otros", ES },
  { "ott", HU },
  { "ou", FR | PT },
  { "oui", FR },
  { "our", EN },
  { "out", EN },
  { "ovat", FI },
  { "over", NL | SV | DA | NO },
  { "oyle", TR },
  { "pa", SV | DA | NO },
  { "pak", CS },
  { "paljon", FI },
  { "pan", PL },
  { "par", FR },
  { "para", ES | PT },
  { "pas", FR },
  { "pe", RO },
  { "pedig", HU },
  { "pel", CA },
  { "pela", PT },
  { "pelo", PT },
  { "pels", CA },
  { "pentru", RO },
  { "per", IT | LA | CA },
  { "perche", IT },
  { "pero", ES | CA },
  { "perque", CA },
  { "peu", FR },
  { "piu", IT },
  { "plej", EO },
  { "pli", EO },
  { "plus", FR },
  { "po", PL | CS },
  { "poate", RO },
  { "poco", ES | IT },
  { "pod", PL },
  { "poi", IT },
  { "por", ES | PT | EO },
  { "porque", ES | PT },
  { "post", LA | EO },
  { "pour", FR },
  { "povas", EO },
  { "pri", CS | EO },
  { "prin", RO },
  { "pro", LA | CS },
  { "propter", LA },
  { "proto", CS },
  { "przed", PL },
  { "przez", PL },
  { "przy", PL },
  { "puis", FR },
  { "qu", FR },
  { "qua", LA },
  { "quae", LA },
  { "quam", LA },
  { "quan", CA },
  { "quand", FR },
  { "quando", IT | PT },
  { "que", FR | ES | PT | CA },
  { "quella", IT },
  { "quello", IT },
  { "quem", PT | LA },
  { "questa", IT },
  { "questo", IT },
  { "qui", FR | LA | CA },
  { "quia", LA },
  { "quibus", LA },
  { "quid", LA },
  { "quien", ES },
  { "quo", LA },
  { "quod", LA },
  { "quoque", LA },
  { "ragmen", TR },
  { "re", EN },
  { "redan", SV },
  { "res", CA },
  { "rien", FR },
  { "s", FR | EN | CA | CS | RO },
  { "sa", FR | SV | NO | PL },
  { "sadan", DA },
  { "sadece", TR },
  { "said", EN },
  { "samma", SV },
  { "sana", TR },
  { "sans", FR },
  { "sao", PT },
  { "sau", RO },
  { "schon", DE },
  { "se", FR | ES | IT | PT | LA | FI | CS | HU | RO | EO },
  { "sed", LA | EO },
  { "sedan", SV },
  { "seg", NO },
  { "sei", DE },
  { "sein", DE },
  { "seine", DE },
  { "seka", FI },
  { "selv", DA | NO },
  { "sem", PT | HU },
  { "sen", FI | TR },
  { "seni", TR },
  { "senin", TR },
  { "sense", CA },
  { "senza", IT },
  { "ser", ES | PT | CA },
  { "ses", FR },
  { "seu", PT | CA },
  { "seus", PT },
  { "seva", CA },
  { "sey", TR },
  { "she", EN },
  { "si", FR | ES | IT | LA | CA | CS | RO | EO },
  { "sia", EO },
  { "sibi", LA },
  { "sic", LA },
  { "sich", DE },
  { "sie", DE | PL },
  { "siebie", PL },
  { "siella", FI },
  { "sig", SV | DA },
  { "siina", FI },
  { "siis", FI },
  { "siita", FI },
  { "siksi", FI },
  { "silla", FI },
  { "sille", FI },
  { "simdi", TR },
  { "sin", ES | SV | DA | NO },
  { "sina", SV | FI },
  { "sind", DE },
  { "sine", LA | DA | NO },
  { "sinun", FI },
  { "sit", DA },
  { "sita", FI },
  { "sitt", SV | NO },
  { "sitten", FI },
  { "siz", TR },
  { "size", TR },
  { "sjalv", SV },
  { "ska", SV },
  { "skal", DA | NO },
  { "skulle", SV | DA | NO },
  { "slik", NO },
  { "so", EN | DE | PT },
  { "sobie", PL },
  { "sobre", ES | PT | CA },
  { "soi", FR },
  { "sok", HU },
  { "som", SV | DA | NO },
  { "some", EN },
  { "son", FR | ES | CA },
  { "sono", IT },
  { "sonra", TR },
  { "sont", FR },
  { "sous", FR },
  { "soyle", TR },
  { "su", ES | IT | TR },
  { "sua", IT | PT | LA },
  { "suas", PT },
  { "sub", LA | EO },
  { "such", EN },
  { "sue", IT },
  { "sui", LA },
  { "sull", IT },
  { "sunt", LA | RO },
  { "suo", IT | LA },
  { "suoi", IT },
  { "super", LA },
  { "sur", FR | EO },
  { "sus", ES },
  { "suum", LA },
  { "sve", CS },
  { "svou", CS },
  { "t", FR | EN },
  { "taalla", FI },
  { "tai", FI },
  { "tak", PL | CS },
  { "take", CS },
  { "takze", PL },
  { "tam", LA },
  { "tama", FI },
  { "taman", FI },
  { "tambe", CA },
  { "tambem", PT },
  { "tambien", ES },
  { "tamen", LA | EO },
  { "tanto", ES | IT | PT },
  { "tassa", FI },
  { "tata", FI },
  { "tato", CS },
  { "te", FR | ES | PT | NL | LA | FI | HU | RO },
  { "tego", PL },
  { "tej", PL },
  { "tem", PT },
  { "ten", PL | CS },
  { "tento", CS },
  { "ter", PT },
  { "teraz", PL },
  { "teu", CA },
  { "teva", CA },
  { "tez", PL },
  { "than", EN },
  { "that", EN },
  { "the", EN },
  { "their", EN },
  { "them", EN },
  { "then", EN },
  { "there", EN },
  { "these", EN },
  { "they", EN },
  { "thi", DA },
  { "this", EN },
  { "ti", IT | HU },
  { "tiam", EO },
  { "tibi", LA },
  { "tie", EO },
  { "tiel", EO },
  { "til", DA | NO },
  { "till", SV },
  { "tim", CS },
  { "tinha", PT },
  { "tio", EO },
  { "tiu", EO },
  { "tiuj", EO },
  { "to", EN | PL | CS },
  { "todo", ES | PT },
  { "todos", ES | PT },
  { "toen", NL },
  { "toho", CS },
  { "toi", FR },
  { "tom", CS },
  { "tot", NL | CA },
  { "toujours", FR },
  { "tous", FR },
  { "tout", FR },
  { "toute", FR },
  { "toutes", FR },
  { "tre", EO },
  { "tres", FR },
  { "tu", FR | ES | IT | PT | CA | PL | CS | RO },
  { "tum", LA | TR },
  { "tuo", FI },
  { "tuon", FI },
  { "tutti", IT },
  { "tutto", IT },
  { "ty", PL | CS },
  { "tylko", PL },
  { "tym", PL },
  { "u", NL | PL },
  { "uber", DE },
  { "ubi", LA },
  { "ud", DA },
  { "uit", NL },
  { "um", DE | PT },
  { "uma", PT },
  { "un", FR | ES | IT | CA | RO },
  { "una", ES | IT | CA },
  { "unas", ES },
  { "und", DE },
  { "unde", RO },
  { "under", SV | DA | NO },
  { "une", FR },
  { "unei", RO },
  { "unes", CA },
  { "uno", ES | IT },
  { "unos", ES },
  { "uns", PT | CA },
  { "unu", EO },
  { "unui", RO },
  { "up", EN },
  { "upon", EN },
  { "upp", SV },
  { "us", EN | CA },
  { "usein", FI },
  { "ut", LA | SV | NO },
  { "utan", SV | HU },
  { "uz", CS },
  { "uzere", TR },
  { "uzerine", TR },
  { "v", CS },
  { "va", CA | RO },
  { "vaan", FI },
  { "vad", SV },
  { "vaere", DA | NO },
  { "vaeret", DA },
  { "vaert", NO },
  { "vagy", HU },
  { "vahan", FI },
  { "vaikka", FI },
  { "vain", FI },
  { "vam", CS },
  { "van", NL | HU },
  { "var", SV | DA | NO | TR },
  { "vara", SV },
  { "ve", EN | CS | TR },
  { "veel", NL },
  { "vel", LA },
  { "veldig", NO },
  { "vers", FR },
  { "very", EN },
  { "veya", TR },
  { "vi", IT | SV | DA | NO | EO },
  { "via", EO },
  { "vid", SV },
  { "viela", FI },
  { "vil", DA | NO },
  { "vill", SV },
  { "ville", DA | NO },
  { "vin", EO },
  { "voi", IT | RO },
  { "volt", HU },
  { "voltak", HU },
  { "von", DE },
  { "voor", NL },
  { "vor", DE | DA },
  { "vores", DA },
  { "vos", LA },
  { "votre", FR },
  { "vous", FR },
  { "vy", CS },
  { "w", PL },
  { "waar", NL },
  { "war", DE },
  { "was", EN | DE | NL },
  { "wat", NL },
  { "we", EN | NL },
  { "wel", NL },
  { "wenn", DE },
  { "werd", NL },
  { "werden", DE },
  { "were", EN },
  { "what", EN },
  { "when", EN },
  { "which", EN },
  { "who", EN },
  { "wie", DE | NL },
  { "wiec", PL },
  { "wieder", DE },
  { "wij", NL },
  { "will", EN },
  { "wir", DE },
  { "wird", DE },
  { "with", EN },
  { "wo", DE },
  { "worden", NL },
  { "wordt", NL },
  { "would", EN },
  { "wszystko", PL },
  { "wurde", DE },
  { "y", FR | ES },
  { "ya", ES | TR },
  { "yani", TR },
  { "yine", TR },
  { "yli", FI },
  { "yo", ES },
  { "yok", TR },
  { "you", EN },
  { "your", EN },
  { "z", PL | CS },
  { "za", PL | CS },
  { "zaten", TR },
  { "ze", NL | PL | CS },
  { "zich", NL },
  { "zij", NL },
  { "zijn", NL },
  { "zo", NL },
  { "zou", NL },
  { "zu", DE },
  { "zum", DE },
  { "zur", DE },
};

/** Nombre de mots-outils. */
#define NB_MOTS_OUTILS (sizeof(MOTS_OUTILS) / sizeof(MOTS_OUTILS[0]))

/** Nom de chaque langue (cf. langue_t). */
static const char *const NOMS_LANGUES[NB_LANGUES] = {
  "inconnue", "francais", "anglais", "espagnol", "allemand", "italien",
  "portugais", "neerlandais", "latin", "catalan", "suedois", "danois",
  "norvegien", "finnois", "polonais", "tcheque", "hongrois", "roumain",
  "turc", "esperanto"
};

/** Passage : suite de phrases consecutives d'un meme texte. */
typedef struct {
  size_t premiere_phrase;
  size_t premiere_occurrence;
  unsigned int scores[NB_LANGUES];
} passage_t;

struct langue_detecteur_s {
  hachage_t *mots;               /* NULL apres la conclusion */
  unsigned int *profils;         /* par identifiant de mot */
  size_t capacite_profils;
  unsigned int *occurrences;     /* identifiant de chaque mot ajoute */
  size_t nb_occurrences;
  size_t capacite_occurrences;
  passage_t *passages;           /* passages fermes */
  size_t nb_passages;
  size_t capacite_passages;
  passage_t ouvert;              /* passage en cours */
  size_t nb_phrases;
  size_t premier_passage_texte;  /* premier passage du texte en cours */
  bool phrase_en_cours;          /* le dernier mot ne finissait pas sa phrase */
  /* resultats (apres la conclusion) */
  bool conclu;
  langue_t majoritaire;
  unsigned char *gardees;        /* par phrase : 1 si gardee */
  size_t nb_phrases_ecartees;
  size_t nb_mots_ecartes;
};


/* ======================================================================== */
/*                           Profils de langues                             */
/* ======================================================================== */

const char *langue_nom(langue_t langue)
{
  PRECONDITION((unsigned int)langue < (unsigned int)NB_LANGUES);
  return NOMS_LANGUES[langue];
}

unsigned int langue_profils(const char *mot)
{
  size_t gauche = 0u;
  size_t droite = NB_MOTS_OUTILS;
  size_t milieu;
  unsigned int masque = 0u;
  int comparaison;

  PRECONDITION(mot != NULL);
  PRECONDITION(hachage_mot_valide(mot));

  /* recherche dichotomique dans [gauche, droite[ */
  while (masque == 0u && gauche < droite) {
    milieu = gauche + ((droite - gauche) / 2u);
    comparaison = strcmp(mot, MOTS_OUTILS[milieu].mot);
    if (comparaison == 0) {
      masque = MOTS_OUTILS[milieu].langues;
    } else if (comparaison < 0) {
      droite = milieu;
    } else {
      gauche = milieu + 1u;
    }
  }
  POSTCONDITION((masque & BIT(LANGUE_INCONNUE)) == 0u);
  return masque;
}


/* ======================================================================== */
/*                        Detecteur : creation, ajouts                      */
/* ======================================================================== */

/**
 * Garantit qu'un tableau dynamique peut recevoir un element de plus que
 * nb (doublement de la capacite). Renvoie false si la memoire manque (le
 * tableau est alors inchange).
 */
static bool reserver(void **tableau, size_t nb, size_t *capacite, size_t taille_element)
{
  size_t nouvelle_capacite;
  void *agrandi = NULL;
  bool succes = true;

  if (nb >= *capacite) {
    nouvelle_capacite = (*capacite == 0u) ? CAPACITE_INITIALE : 2u * *capacite;
    if (nouvelle_capacite > *capacite
        && nouvelle_capacite <= SIZE_MAX / taille_element) {
      agrandi = realloc(*tableau, nouvelle_capacite * taille_element);
    }
    if (agrandi == NULL) {
      succes = false;
    } else {
      *tableau = agrandi;
      *capacite = nouvelle_capacite;
    }
  }
  return succes;
}

/** Commence un passage vide a la position courante. */
static void ouvrir_passage(langue_detecteur_t *d)
{
  unsigned int langue;

  d->ouvert.premiere_phrase = d->nb_phrases;
  d->ouvert.premiere_occurrence = d->nb_occurrences;
  for (langue = 0u; langue < (unsigned int)NB_LANGUES; langue++) {
    d->ouvert.scores[langue] = 0u;
  }
}

/** Nombre de mots du passage ouvert. */
static size_t taille_passage_ouvert(const langue_detecteur_t *d)
{
  return d->nb_occurrences - d->ouvert.premiere_occurrence;
}

/** Ferme le passage ouvert (non vide) : il devient un passage a part. */
static void fermer_passage(langue_detecteur_t *d)
{
  INVARIANT(d->nb_passages < d->capacite_passages);
  d->passages[d->nb_passages] = d->ouvert;
  d->nb_passages++;
  ouvrir_passage(d);
}

langue_detecteur_t *langue_detecteur_creer(void)
{
  langue_detecteur_t *d = malloc(sizeof(langue_detecteur_t));

  if (d != NULL) {
    d->mots = hachage_creer(0u);
    d->profils = NULL;
    d->capacite_profils = 0u;
    d->occurrences = NULL;
    d->nb_occurrences = 0u;
    d->capacite_occurrences = 0u;
    d->passages = NULL;
    d->nb_passages = 0u;
    d->capacite_passages = 0u;
    d->nb_phrases = 0u;
    d->premier_passage_texte = 0u;
    d->phrase_en_cours = false;
    d->conclu = false;
    d->majoritaire = LANGUE_INCONNUE;
    d->gardees = NULL;
    d->nb_phrases_ecartees = 0u;
    d->nb_mots_ecartes = 0u;
    ouvrir_passage(d);
    if (d->mots == NULL) {
      free(d);
      d = NULL;
    }
  }
  if (d == NULL) {
    errno = ENOMEM;
  }
  POSTCONDITION(d == NULL || (!d->conclu && d->nb_phrases == 0u
                              && d->nb_occurrences == 0u));
  return d;
}

void langue_detecteur_detruire(langue_detecteur_t **d)
{
  PRECONDITION(d != NULL);
  if (*d != NULL) {
    hachage_detruire(&(*d)->mots);
    free((*d)->profils);
    free((*d)->occurrences);
    free((*d)->passages);
    free((*d)->gardees);
    free(*d);
    *d = NULL;
  }
  POSTCONDITION(*d == NULL);
}

bool langue_detecteur_ajouter_mot(langue_detecteur_t *d, const char *mot, bool fin_phrase)
{
  void *tableau;
  unsigned int identifiant = HACHAGE_ABSENT;
  unsigned int nb_mots;
  unsigned int langue;
  bool succes;

  PRECONDITION(d != NULL);
  PRECONDITION(!d->conclu);
  PRECONDITION(mot != NULL);
  PRECONDITION(mot[0] != '\0' && hachage_mot_valide(mot));

  /* 1. toutes les reservations d'abord : un echec ne modifie ni le nombre de mots ni les passages */
  tableau = d->occurrences;
  succes = reserver(&tableau, d->nb_occurrences, &d->capacite_occurrences, sizeof(unsigned int));
  d->occurrences = tableau;
  if (succes) {
    tableau = d->passages;
    succes = reserver(&tableau, d->nb_passages, &d->capacite_passages, sizeof(passage_t));
    d->passages = tableau;
  }
  if (succes) {
    nb_mots = hachage_nb_mots(d->mots);
    tableau = d->profils;
    succes = reserver(&tableau, nb_mots, &d->capacite_profils, sizeof(unsigned int));
    d->profils = tableau;
    if (succes) {
      identifiant = hachage_ajouter(d->mots, mot);
      succes = (identifiant != HACHAGE_ABSENT);
    }
    if (succes && identifiant == nb_mots) {
      d->profils[identifiant] = langue_profils(mot);
    }
  }
  /* 2. prise en compte du mot (plus aucune allocation) */
  if (succes) {
    d->occurrences[d->nb_occurrences] = identifiant;
    d->nb_occurrences++;
    for (langue = 1u; langue < (unsigned int)NB_LANGUES; langue++) {
      if ((d->profils[identifiant] & BIT(langue)) != 0u) {
        d->ouvert.scores[langue]++;
      }
    }
    d->phrase_en_cours = !fin_phrase;
    if (fin_phrase) {
      d->nb_phrases++;
      if (taille_passage_ouvert(d) >= LANGUE_TAILLE_PASSAGE) {
        fermer_passage(d);
      }
    }
  } else {
    errno = ENOMEM;
  }
  INVARIANT(taille_passage_ouvert(d) == 0u || d->capacite_passages > d->nb_passages);
  return succes;
}

void langue_detecteur_fin_texte(langue_detecteur_t *d)
{
  passage_t *dernier;
  unsigned int langue;

  PRECONDITION(d != NULL);
  PRECONDITION(!d->conclu);

  if (d->phrase_en_cours) {
    d->nb_phrases++;
    d->phrase_en_cours = false;
  }
  if (taille_passage_ouvert(d) > 0u) {
    if (taille_passage_ouvert(d) < LANGUE_TAILLE_PASSAGE / 2u
        && d->nb_passages > d->premier_passage_texte) {
      /* fin de texte trop courte pour etre jugee seule : elle rejoint le
         passage precedent du meme texte */
      dernier = &d->passages[d->nb_passages - 1u];
      for (langue = 0u; langue < (unsigned int)NB_LANGUES; langue++) {
        dernier->scores[langue] += d->ouvert.scores[langue];
      }
      ouvrir_passage(d);
    } else {
      fermer_passage(d);
    }
  }
  d->premier_passage_texte = d->nb_passages;
}


/* ======================================================================== */
/*                               Conclusion                                 */
/* ======================================================================== */

/** Nombre de mots du passage ferme p. */
static size_t taille_passage(const langue_detecteur_t *d, size_t p)
{
  size_t fin = (p + 1u < d->nb_passages) ? d->passages[p + 1u].premiere_occurrence : d->nb_occurrences;

  return fin - d->passages[p].premiere_occurrence;
}

/** Nombre de phrases du passage ferme p. */
static size_t phrases_passage(const langue_detecteur_t *d, size_t p)
{
  size_t fin = (p + 1u < d->nb_passages) ? d->passages[p + 1u].premiere_phrase : d->nb_phrases;

  return fin - d->passages[p].premiere_phrase;
}

/** Langue majoritaire : plus grande somme de scores, si elle couvre une
    part suffisante des mots (regle 2). */
static langue_t langue_majoritaire(const langue_detecteur_t *d)
{
  size_t totaux[NB_LANGUES] = { 0u };
  size_t p;
  unsigned int langue;
  unsigned int meilleure = (unsigned int)LANGUE_INCONNUE;

  for (p = 0u; p < d->nb_passages; p++) {
    for (langue = 1u; langue < (unsigned int)NB_LANGUES; langue++) {
      totaux[langue] += d->passages[p].scores[langue];
    }
  }
  for (langue = 1u; langue < (unsigned int)NB_LANGUES; langue++) {
    if (totaux[langue] > totaux[meilleure]) {
      meilleure = langue;
    }
  }
  /* totaux[LANGUE_INCONNUE] == 0 : un corpus sans mot-outil est inconnu */
  if (totaux[meilleure] < (d->nb_occurrences + DIVISEUR_MAJORITE - 1u) / DIVISEUR_MAJORITE) {
    meilleure = (unsigned int)LANGUE_INCONNUE;
  }
  return (langue_t)meilleure;
}

/** Indique si une autre langue domine la langue m dans le passage p
    (regle 3). */
static bool est_etranger(const langue_detecteur_t *d, size_t p, langue_t m)
{
  const unsigned int *scores = d->passages[p].scores;
  size_t n = taille_passage(d, p);
  unsigned int langue;
  bool etranger = false;

  for (langue = 1u; !etranger && langue < (unsigned int)NB_LANGUES;
       langue++) {
    etranger = (langue != (unsigned int)m
                && (size_t)scores[langue] * DIVISEUR_SCORE >= n
                && scores[langue] > scores[m]
                && scores[langue] - scores[m] >= ECART_MINIMAL);
  }
  return etranger;
}

bool langue_detecteur_conclure(langue_detecteur_t *d)
{
  unsigned int nb_mots;
  unsigned int *total = NULL;      /* occurrences de chaque mot */
  unsigned int *reference = NULL;  /* ... dans les passages de reference */
  unsigned char *gardees;
  langue_t m;
  size_t p;
  size_t i;
  size_t debut;
  size_t n;
  size_t connus;
  unsigned int mot;
  bool garde;
  bool succes;

  PRECONDITION(d != NULL);
  PRECONDITION(!d->conclu);

  langue_detecteur_fin_texte(d);
  nb_mots = hachage_nb_mots(d->mots);
  /* toutes les allocations d'abord (calloc(0) peut renvoyer NULL) */
  gardees = malloc((d->nb_phrases > 0u) ? d->nb_phrases : 1u);
  total = calloc((size_t)nb_mots + 1u, sizeof(unsigned int));
  reference = calloc((size_t)nb_mots + 1u, sizeof(unsigned int));
  succes = (gardees != NULL && total != NULL && reference != NULL);
  if (succes) {
    memset(gardees, 1, (d->nb_phrases > 0u) ? d->nb_phrases : 1u);
    m = langue_majoritaire(d);
    if (m != LANGUE_INCONNUE) {
      /* regles 4 et 5 : occurrences de chaque mot, au total et dans les
         passages de reference */
      for (p = 0u; p < d->nb_passages; p++) {
        debut = d->passages[p].premiere_occurrence;
        n = taille_passage(d, p);
        garde = !est_etranger(d, p, m) && (size_t)d->passages[p].scores[m] * DIVISEUR_SCORE >= n;
        for (i = debut; i < debut + n; i++) {
          mot = d->occurrences[i];
          /* compteurs satures : reference[mot] <= total[mot] reste vrai */
          if (total[mot] < UINT_MAX) {
            total[mot]++;
            if (garde) {
              reference[mot]++;
            }
          }
        }
      }
      /* regle 6 : sort de chaque passage */
      for (p = 0u; p < d->nb_passages; p++) {
        debut = d->passages[p].premiere_occurrence;
        n = taille_passage(d, p);
        garde = !est_etranger(d, p, m);
        if (garde) {
          connus = 0u;
          for (i = debut; i < debut + n; i++) {
            mot = d->occurrences[i];
            /* total - reference <= reference : sans depassement */
            if (reference[mot] > 0u
                && total[mot] - reference[mot] <= reference[mot]) {
              connus++;
            }
          }
          garde = (connus * DENOMINATEUR_CONNUS >= n * NUMERATEUR_CONNUS);
        }
        if (!garde) {
          memset(gardees + d->passages[p].premiere_phrase, 0,
                 phrases_passage(d, p));
          d->nb_phrases_ecartees += phrases_passage(d, p);
          d->nb_mots_ecartes += n;
        }
      }
    }
    d->majoritaire = m;
    d->gardees = gardees;
    d->conclu = true;
    /* les mots ne servent plus : seuls restent les resultats */
    hachage_detruire(&d->mots);
    free(d->profils);
    free(d->occurrences);
    free(d->passages);
    d->profils = NULL;
    d->occurrences = NULL;
    d->passages = NULL;
    d->nb_passages = 0u;
  } else {
    free(gardees);
    errno = ENOMEM;
  }
  free(total);
  free(reference);
  POSTCONDITION(d->conclu == succes);
  return succes;
}


/* ======================================================================== */
/*                         Detecteur : consultation                         */
/* ======================================================================== */

bool langue_detecteur_est_conclu(const langue_detecteur_t *d)
{
  PRECONDITION(d != NULL);
  return d->conclu;
}

size_t langue_detecteur_nb_phrases(const langue_detecteur_t *d)
{
  PRECONDITION(d != NULL);
  return d->nb_phrases;
}

size_t langue_detecteur_nb_mots(const langue_detecteur_t *d)
{
  PRECONDITION(d != NULL);
  return d->nb_occurrences;
}

langue_t langue_detecteur_majoritaire(const langue_detecteur_t *d)
{
  PRECONDITION(d != NULL);
  PRECONDITION(d->conclu);
  return d->majoritaire;
}

bool langue_detecteur_phrase_gardee(const langue_detecteur_t *d, size_t phrase)
{
  PRECONDITION(d != NULL);
  PRECONDITION(d->conclu);
  PRECONDITION(phrase < d->nb_phrases);
  return d->gardees[phrase] != 0u;
}

size_t langue_detecteur_nb_phrases_ecartees(const langue_detecteur_t *d)
{
  PRECONDITION(d != NULL);
  PRECONDITION(d->conclu);
  POSTCONDITION(d->nb_phrases_ecartees <= d->nb_phrases);
  return d->nb_phrases_ecartees;
}

size_t langue_detecteur_nb_mots_ecartes(const langue_detecteur_t *d)
{
  PRECONDITION(d != NULL);
  PRECONDITION(d->conclu);
  POSTCONDITION(d->nb_mots_ecartes <= d->nb_occurrences);
  return d->nb_mots_ecartes;
}
