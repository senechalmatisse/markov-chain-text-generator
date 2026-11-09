# Fichier resume_couverture.awk
#
# Resume la sortie de "gcov -b" (cible couverture du Makefile) : pour chaque
# fichier source du projet, pourcentage de lignes executees et de branches
# prises au moins une fois, puis total pondere.
# Usage : awk -f tests/resume_couverture.awk couverture/gcov.txt

/^File '/ {
  fichier = $0
  sub(/^File '/, "", fichier)
  sub(/'$/, "", fichier)
  sub(/^(\.\.\/)+/, "", fichier)
  retenu = (fichier ~ /^src\// || fichier == "main.c")
  next
}

retenu && /^Lines executed:/ {
  split($0, morceaux, /[:% ]+/)
  lignes_pourcent[fichier] = morceaux[3]
  lignes_total[fichier] = morceaux[5]
  if (!(fichier in vu)) {
    vu[fichier] = 1
    ordre[++nb_fichiers] = fichier
  }
  next
}

retenu && /^Taken at least once:/ {
  split($0, morceaux, /[:% ]+/)
  branches_pourcent[fichier] = morceaux[5]
  branches_total[fichier] = morceaux[7]
  next
}

END {
  printf "Couverture des tests (gcov) :\n"
  printf "  %-20s %20s %26s\n", "Fichier", "Lignes executees", "Branches prises"
  for (i = 1; i <= nb_fichiers; i++) {
    f = ordre[i]
    printf "  %-20s %10.1f %% de %5d %16.1f %% de %5d\n", f, \
           lignes_pourcent[f], lignes_total[f], \
           branches_pourcent[f], branches_total[f]
    lignes_couvertes += lignes_pourcent[f] * lignes_total[f] / 100
    lignes += lignes_total[f]
    branches_couvertes += branches_pourcent[f] * branches_total[f] / 100
    branches += branches_total[f]
  }
  if (lignes > 0 && branches > 0) {
    printf "  %-20s %10.1f %% de %5d %16.1f %% de %5d\n", "Total", \
           100 * lignes_couvertes / lignes, lignes, \
           100 * branches_couvertes / branches, branches
    printf "Couverture totale des lignes : %.1f%%\n", \
           100 * lignes_couvertes / lignes
  }
}
