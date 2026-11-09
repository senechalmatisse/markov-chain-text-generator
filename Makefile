CFLAGS = -O3 --std=c99 -Wall -lm

all: main.o src/hachage.o src/markov.o src/repertoire.o src/sauvegarde.o
	gcc main.o src/hachage.o src/markov.o src/repertoire.o src/sauvegarde.o -o projet $(CFLAGS) 

test_hachage: tests/test_hachage.o src/hachage.o
	gcc tests/test_hachage.o src/hachage.o -o tests/test_hachage $(CFLAGS)
	./tests/test_hachage
	rm tests/test_hachage

test_markov: tests/test_markov.o src/markov.o
	gcc tests/test_markov.o src/markov.o -o tests/test_markov $(CFLAGS)
	./tests/test_markov
	rm tests/test_markov

test_repertoire: tests/test_repertoire.o src/repertoire.o
	gcc tests/test_repertoire.o src/repertoire.o -o tests/test_repertoire $(CFLAGS)
	./tests/test_repertoire
	rm tests/test_repertoire

test_sauvegarde: tests/test_sauvegarde.o src/sauvegarde.o
	gcc tests/test_sauvegarde.o src/sauvegarde.o -o tests/test_sauvegarde $(CFLAGS)
	./tests/test_sauvegarde
	rm tests/test_sauvegarde

test: test_hachage test_markov test_repertoire test_sauvegarde
	rm tests/*.o

clean:
	rm -f *.o *~ 
	rm -f src/*.o src/*~  
	rm -f tests/*.o test/*~ 

# ==========================================================================
# LIGNES AJOUTEES (rien n'a ete retire ni modifie au-dessus)
# ==========================================================================

.PHONY: all test clean test_hachage test_markov test_repertoire \
        test_sauvegarde test_lexique test_modele test_programme \
        test_defaillances test_aleatoire test_memoire couverture doc \
        installer_hook nettoyer_binaires

# Modules ajoutes au projet, et ensemble des modules
OBJETS_AJOUTES = src/lexique.o src/modele.o
OBJETS_MODULES = src/hachage.o src/markov.o src/repertoire.o \
                 src/sauvegarde.o $(OBJETS_AJOUTES)
SOURCES_MODULES = $(OBJETS_MODULES:.o=.c)
TESTS_UNITAIRES = hachage markov repertoire sauvegarde lexique modele

# --- Dependances vers les en-tetes : recompilation si un .h change --------
main.o: src/hachage.h src/markov.h src/repertoire.h src/sauvegarde.h \
        src/modele.h src/lexique.h src/texte.h
src/hachage.o: src/hachage.h src/contrat.h
src/markov.o: src/markov.h src/contrat.h
src/repertoire.o: src/repertoire.h src/hachage.h src/markov.h src/lexique.h \
                  src/texte.h src/contrat.h
src/sauvegarde.o: src/sauvegarde.h src/contrat.h
src/lexique.o: src/lexique.h src/texte.h src/contrat.h
src/modele.o: src/modele.h src/repertoire.h src/sauvegarde.h src/hachage.h \
              src/markov.h src/lexique.h src/texte.h src/contrat.h
tests/test_hachage.o: src/hachage.h tests/outils_tests.h
tests/test_markov.o: src/markov.h tests/outils_tests.h
tests/test_repertoire.o: src/repertoire.h src/hachage.h src/markov.h \
                         src/lexique.h src/texte.h tests/outils_tests.h
tests/test_sauvegarde.o: src/sauvegarde.h tests/outils_tests.h
tests/test_lexique.o: src/lexique.h src/texte.h tests/outils_tests.h
tests/test_modele.o tests/test_programme.o tests/test_defaillances.o: \
  src/modele.h src/repertoire.h src/hachage.h src/markov.h src/lexique.h \
  src/texte.h src/sauvegarde.h tests/outils_tests.h

# --- Edition de liens des regles d'origine --------------------------------
# Le programme utilise aussi les modules lexique et modele, et repertoire.c
# s'appuie sur markov, hachage et lexique : les regles d'origine ne les lient
# pas. On les ajoute a l'edition de liens de ces seules cibles (une variable
# "private" n'est pas transmise a la compilation des dependances).
all: $(OBJETS_AJOUTES)
all: private CFLAGS += $(OBJETS_AJOUTES) -lm
test_repertoire: src/hachage.o src/markov.o src/lexique.o
test_repertoire: private CFLAGS += src/hachage.o src/markov.o src/lexique.o -lm

# --- Nouveaux tests (meme forme que les regles d'origine) -----------------
test_lexique: tests/test_lexique.o src/lexique.o
	gcc tests/test_lexique.o src/lexique.o -o tests/test_lexique $(CFLAGS)
	./tests/test_lexique
	rm tests/test_lexique

test_modele: tests/test_modele.o $(OBJETS_MODULES)
	gcc tests/test_modele.o $(OBJETS_MODULES) -o tests/test_modele $(CFLAGS)
	./tests/test_modele
	rm tests/test_modele

# Programme principal : le test lance ./projet et verifie le texte produit
test_programme: all tests/test_programme.o $(OBJETS_MODULES)
	gcc tests/test_programme.o $(OBJETS_MODULES) -o tests/test_programme \
	  $(CFLAGS)
	./tests/test_programme ./projet
	rm tests/test_programme

# Defaillances d'allocation : malloc, calloc, realloc et free sont remplaces
# (option --wrap de l'editeur de liens GNU) pour faire echouer la 1re, la
# 2e, ..., la N-ieme allocation de chaque scenario.
EMBALLAGE = -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free
test_defaillances: tests/test_defaillances.o $(OBJETS_MODULES)
	gcc tests/test_defaillances.o $(OBJETS_MODULES) \
	  -o tests/test_defaillances $(CFLAGS) $(EMBALLAGE)
	./tests/test_defaillances
	rm tests/test_defaillances

# Octets au hasard, sous AddressSanitizer et UndefinedBehaviorSanitizer :
# tout acces memoire invalide interrompt le test (les fuites sont verifiees
# par valgrind dans test_memoire).
SANITISEURS = -O1 -g --std=c99 -Wall -fsanitize=address,undefined \
              -fno-omit-frame-pointer -fno-sanitize-recover=undefined
test_aleatoire: tests/test_aleatoire.c tests/outils_tests.h $(SOURCES_MODULES)
	gcc $(SANITISEURS) tests/test_aleatoire.c $(SOURCES_MODULES) \
	  -o tests/test_aleatoire
	ASAN_OPTIONS=detect_leaks=0 ./tests/test_aleatoire
	rm tests/test_aleatoire

test: test_lexique test_modele test_programme test_defaillances \
      test_aleatoire

# --- Verification de la memoire (valgrind) --------------------------------
# Le programme final (cas nominaux et cas d'erreur) puis chaque test sont
# executes sous valgrind. La moindre fuite (y compris un bloc encore
# accessible, "still reachable") ou le moindre acces memoire invalide fait
# echouer cette cible, donc "make test", donc la CI (.gitlab-ci.yml).
VALGRIND = valgrind --quiet --leak-check=full --show-leak-kinds=all \
           --errors-for-leak-kinds=all --error-exitcode=99 \
           --child-silent-after-fork=yes
# Repertoire de travail temporaire (les textes generes y sont ecrits)
REPERTOIRE_MEMOIRE = /tmp/projet_markov_test_memoire

test: test_memoire

test_memoire: all $(OBJETS_MODULES) tests/test_hachage.o tests/test_markov.o \
              tests/test_repertoire.o tests/test_sauvegarde.o \
              tests/test_lexique.o tests/test_modele.o \
              tests/test_programme.o tests/test_defaillances.o
	@command -v valgrind > /dev/null || \
	  { echo "ERREUR : valgrind est necessaire (apt install valgrind)"; exit 1; }
	@echo "=== Memoire du programme principal (valgrind) ==="
	@rm -rf $(REPERTOIRE_MEMOIRE) && mkdir -p $(REPERTOIRE_MEMOIRE)
	cd $(REPERTOIRE_MEMOIRE) && $(VALGRIND) $(CURDIR)/projet \
	  -g 42 -n 20 $(CURDIR)/dataset > /dev/null
	cd $(REPERTOIRE_MEMOIRE) && $(VALGRIND) $(CURDIR)/projet \
	  $(CURDIR)/dataset/en 42 5 > /dev/null
	$(VALGRIND) ./projet -k 2 -r -g 7 -n 20 \
	  -s $(REPERTOIRE_MEMOIRE)/modele.txt -o $(REPERTOIRE_MEMOIRE)/sortie.txt \
	  dataset > /dev/null
	$(VALGRIND) ./projet -m $(REPERTOIRE_MEMOIRE)/modele.txt -g 7 -n 20 \
	  -o $(REPERTOIRE_MEMOIRE)/recharge.txt > /dev/null
	@cmp -s $(REPERTOIRE_MEMOIRE)/sortie.txt $(REPERTOIRE_MEMOIRE)/recharge.txt \
	  || { echo "ERREUR : le modele recharge ne redonne pas le meme texte"; exit 1; }
	@for V_Vc_Fichier in texte_genere_42.txt texte_genere_42_2.txt sortie.txt; do \
	  [ -s "$(REPERTOIRE_MEMOIRE)/$${V_Vc_Fichier}" ] || \
	    { echo "ERREUR : $${V_Vc_Fichier} non genere"; exit 1; }; \
	done
	@rm -rf $(REPERTOIRE_MEMOIRE)
	$(VALGRIND) ./projet --aide > /dev/null
	@echo "(les messages d'erreur suivants sont attendus)"
	$(VALGRIND) ./projet dossier_inexistant > /dev/null; [ "$$?" -eq "1" ]
	$(VALGRIND) ./projet dataset graine_invalide > /dev/null; [ "$$?" -eq "1" ]
	$(VALGRIND) ./projet -m Makefile > /dev/null; [ "$$?" -eq "1" ]
	@echo "=== Memoire des tests (valgrind) ==="
	gcc tests/test_hachage.o src/hachage.o -o tests/memoire_hachage $(CFLAGS)
	gcc tests/test_markov.o src/markov.o -o tests/memoire_markov $(CFLAGS)
	gcc tests/test_repertoire.o src/repertoire.o src/hachage.o src/markov.o \
	  src/lexique.o -o tests/memoire_repertoire $(CFLAGS)
	gcc tests/test_sauvegarde.o src/sauvegarde.o -o tests/memoire_sauvegarde \
	  $(CFLAGS)
	gcc tests/test_lexique.o src/lexique.o -o tests/memoire_lexique $(CFLAGS)
	gcc tests/test_modele.o $(OBJETS_MODULES) -o tests/memoire_modele $(CFLAGS)
	gcc tests/test_programme.o $(OBJETS_MODULES) -o tests/memoire_programme \
	  $(CFLAGS)
	gcc tests/test_defaillances.o $(OBJETS_MODULES) \
	  -o tests/memoire_defaillances $(CFLAGS) $(EMBALLAGE)
	gcc tests/test_aleatoire.c $(SOURCES_MODULES) -o tests/memoire_aleatoire \
	  $(CFLAGS)
	@for V_Vc_Test in $(TESTS_UNITAIRES) defaillances; do \
	  echo "$(VALGRIND) ./tests/memoire_$${V_Vc_Test}"; \
	  $(VALGRIND) ./tests/memoire_$${V_Vc_Test} > /dev/null || exit 1; \
	done
	$(VALGRIND) ./tests/memoire_programme ./projet > /dev/null
	$(VALGRIND) ./tests/memoire_aleatoire 150 > /dev/null
	@rm -f tests/memoire_*
	@echo "=== Aucune fuite ni erreur memoire ==="

# --- Couverture des tests (gcov) ------------------------------------------
# Tout est recompile avec --coverage dans le repertoire couverture/, les
# tests et le programme y sont executes, puis gcov mesure, pour chaque
# fichier, les lignes executees et les branches prises au moins une fois.
# Les fichiers *.gcov du repertoire couverture/ montrent ligne par ligne ce
# que les tests n'atteignent pas ("#####"). Rapport HTML si lcov est
# installe (couverture/html/index.html).
COUVERTURE = couverture
OPTIONS_COUVERTURE = -O0 -g --std=c99 -Wall --coverage -DCOUVERTURE
OBJETS_COUVERTURE = $(addprefix $(COUVERTURE)/,$(notdir $(OBJETS_MODULES)))

couverture:
	@rm -rf $(COUVERTURE) && mkdir -p $(COUVERTURE)
	@for V_Vc_Source in $(SOURCES_MODULES) main.c tests/test_*.c; do \
	  gcc $(OPTIONS_COUVERTURE) -c "$${V_Vc_Source}" \
	    -o "$(COUVERTURE)/$$(basename "$${V_Vc_Source}" .c).o" || exit 1; \
	done
	gcc $(OPTIONS_COUVERTURE) $(COUVERTURE)/main.o $(OBJETS_COUVERTURE) \
	  -o $(COUVERTURE)/projet
	@for V_Vc_Test in $(TESTS_UNITAIRES) programme aleatoire; do \
	  gcc $(OPTIONS_COUVERTURE) $(COUVERTURE)/test_$${V_Vc_Test}.o \
	    $(OBJETS_COUVERTURE) -o $(COUVERTURE)/test_$${V_Vc_Test} || exit 1; \
	done
	gcc $(OPTIONS_COUVERTURE) $(COUVERTURE)/test_defaillances.o \
	  $(OBJETS_COUVERTURE) -o $(COUVERTURE)/test_defaillances $(EMBALLAGE)
	@for V_Vc_Test in $(TESTS_UNITAIRES) defaillances; do \
	  ./$(COUVERTURE)/test_$${V_Vc_Test} > /dev/null || exit 1; \
	done
	./$(COUVERTURE)/test_programme ./$(COUVERTURE)/projet > /dev/null
	./$(COUVERTURE)/test_aleatoire 300 > /dev/null
	@for V_Vc_Source in $(SOURCES_MODULES) main.c; do \
	  gcov -b -o $(COUVERTURE) "$${V_Vc_Source}" 2> /dev/null; \
	done > $(COUVERTURE)/gcov.txt
	@mv -f *.gcov $(COUVERTURE)/
	@awk -f tests/resume_couverture.awk $(COUVERTURE)/gcov.txt \
	  | tee $(COUVERTURE)/resume.txt
	@echo "Lignes jamais executees : marques ##### dans $(COUVERTURE)/*.gcov"
	@if command -v lcov > /dev/null && command -v genhtml > /dev/null; then \
	  lcov --quiet --capture --directory $(COUVERTURE) \
	    --output-file $(COUVERTURE)/couverture.info && \
	  genhtml --quiet $(COUVERTURE)/couverture.info \
	    --output-directory $(COUVERTURE)/html && \
	  echo "Rapport HTML : $(COUVERTURE)/html/index.html"; \
	fi

# --- Documentation (Doxygen) ----------------------------------------------
doc:
	@command -v doxygen > /dev/null || \
	  { echo "ERREUR : doxygen est necessaire (apt install doxygen)"; exit 1; }
	doxygen Doxyfile
	@echo "Documentation generee : doc/html/index.html"

# --- Hook git : "make test" avant chaque commit ---------------------------
# core.hooksPath designe le repertoire hooks/ du projet (chemin relatif a la
# racine du depot, le projet pouvant etre dans un sous-repertoire).
installer_hook:
	@git rev-parse --git-dir > /dev/null 2>&1 || \
	  { echo "ERREUR : ce repertoire n'est pas dans un depot git"; exit 1; }
	chmod +x hooks/pre-commit
	git config core.hooksPath "$$(git rev-parse --show-prefix)hooks"
	@echo "Hook installe : chaque commit lance make test (contournement :"
	@echo "git commit --no-verify)."

# --- Nettoyage des executables et des rapports (en plus de clean) ---------
clean: nettoyer_binaires

nettoyer_binaires:
	rm -f projet tests/test_hachage tests/test_markov tests/test_repertoire \
	  tests/test_sauvegarde tests/test_lexique tests/test_modele \
	  tests/test_programme tests/test_defaillances tests/test_aleatoire \
	  tests/memoire_*
	rm -rf $(COUVERTURE) doc
