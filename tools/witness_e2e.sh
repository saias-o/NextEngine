#!/usr/bin/env bash
# E2E du chemin « ship » : exporte WitnessGame avec le VRAI BuildExporter
# (saida_tool export-game — le même code que le bouton Build), lance le runtime
# standalone avec un autoload de test éphémère et greppe le verdict. L'artefact
# exporté n'est jamais réécrit. Nécessite une machine avec GPU.
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

OUT="${1:-$ROOT/build/witness-e2e}"
rm -rf "$OUT"

./build/bin/saida_tool.exe export-game WitnessGame/WitnessGame.saidaproj \
    --out "$OUT" --version 1.0.0 --company Saida > /dev/null

cd "$OUT"
# Les jeux packagés persistent leurs saves sous le dossier utilisateur de l'OS.
# On les épingle à un dossier neuf par invocation (partagé par les deux runs) :
# la preuve de redémarrage reste hermétique, insensible aux saves d'un run
# précédent dans %APPDATA%/~/.local/share.
export SAIDA_SAVE_DIR="$OUT/.saves"

"./Witness Game.exe" --test-autoload \
    "E2EDriver=scripts/e2e_driver.js" > e2e.log 2>&1 || true

if ! grep -q "\[E2E\] PASS" e2e.log; then
    echo "WITNESS E2E: FAIL"
    grep -E "\[E2E\]|\[JS\]|error" e2e.log | tail -20
    exit 1
fi

# Second lancement, même dossier : la progression sauvegardée par le run 1
# doit être restaurée depuis saves/ au boot (save/load après redémarrage).
"./Witness Game.exe" --test-autoload \
    "E2EDriver=scripts/e2e_driver.js" > e2e_restart.log 2>&1 || true

if ! grep -q "\[E2E\] RESTART PASS" e2e_restart.log; then
    echo "WITNESS E2E: FAIL (redémarrage : progression non restaurée)"
    grep -E "\[E2E\]|\[JS\]|error" e2e_restart.log | tail -20
    exit 1
fi

echo "WITNESS E2E: PASS (run + restart)"
exit 0
