#!/usr/bin/env bash
# E2E du clic Build de l'ÉDITEUR : lance SaidaEngine.exe --build (exactement le
# code du bouton Build du dialogue, pas saida_tool), exécute l'artefact exact
# avec un autoload de test éphémère et greppe le verdict. Nécessite une machine
# avec GPU (l'éditeur crée sa fenêtre).
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

OUT="${1:-$ROOT/build/witness-editor-build}"
case "$OUT" in
    /* | [A-Za-z]:[\\/]* ) ;;
    * ) OUT="$ROOT/$OUT" ;;
esac
rm -rf "$OUT"
rm -rf "$ROOT/build/witness-editor-build-saves"

./build/bin/SaidaEngine.exe --project WitnessGame/WitnessGame.saidaproj \
    --build "$OUT" > "$ROOT/build/editor_build.log" 2>&1 || true

if ! grep -q "\[BUILD\] PASS" "$ROOT/build/editor_build.log"; then
    echo "EDITOR BUILD: FAIL (pas de [BUILD] PASS)"
    tail -20 "$ROOT/build/editor_build.log"
    exit 1
fi

cd "$OUT"
export SAIDA_SAVE_DIR="$ROOT/build/witness-editor-build-saves"

"./Witness Game.exe" --test-autoload \
    "E2EDriver=scripts/e2e_driver.js" > e2e.log 2>&1 || true

if ! grep -q "\[E2E\] PASS" e2e.log; then
    echo "WITNESS EDITOR-BUILD E2E: FAIL"
    grep -E "\[E2E\]|\[JS\]|error" e2e.log | tail -20
    exit 1
fi

"./Witness Game.exe" --test-autoload \
    "E2EDriver=scripts/e2e_driver.js" > e2e_restart.log 2>&1 || true

if ! grep -q "\[E2E\] RESTART PASS" e2e_restart.log; then
    echo "WITNESS EDITOR-BUILD E2E: FAIL (restart)"
    grep -E "\[E2E\]|\[JS\]|error" e2e_restart.log | tail -20
    exit 1
fi

echo "WITNESS EDITOR-BUILD E2E: PASS (run + restart)"
exit 0
