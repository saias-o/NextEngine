#!/usr/bin/env bash
# E2E du mode Play de l'ÉDITEUR : copie WitnessGame, injecte le pilote comme
# autoload éphémère sans réécrire le projet, déclenche le vrai passage en Play
# via --play, puis vérifie gameplay, HUD et restauration au second lancement.
# Nécessite une machine avec GPU (l'éditeur ouvre une fenêtre).
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

PROJECT="${1:-$ROOT/build/witness-editor-play}"
LOG="$ROOT/build/editor_play.log"
RESTART_LOG="$ROOT/build/editor_play_restart.log"
rm -rf "$PROJECT"
cp -R WitnessGame "$PROJECT"
# La copie source peut contenir des saves locales ignorées par Git. Le premier
# lancement doit toujours partir d'un état vierge; seul le second réutilise la
# progression produite dans cette copie de test.
rm -rf "$PROJECT/saves"

./build/bin/SaidaEngine.exe --project "$PROJECT/WitnessGame.saidaproj" \
    --play --test-autoload "E2EDriver=scripts/e2e_driver.js" > "$LOG" 2>&1 || true

if ! grep -q "\[E2E\] PASS" "$LOG"; then
    echo "WITNESS EDITOR-PLAY E2E: FAIL"
    grep -E "\[E2E\]|\[JS\]|error" "$LOG" | tail -20
    exit 1
fi

./build/bin/SaidaEngine.exe --project "$PROJECT/WitnessGame.saidaproj" \
    --play --test-autoload "E2EDriver=scripts/e2e_driver.js" \
    > "$RESTART_LOG" 2>&1 || true

if ! grep -q "\[E2E\] RESTART PASS" "$RESTART_LOG"; then
    echo "WITNESS EDITOR-PLAY E2E: FAIL (redémarrage)"
    grep -E "\[E2E\]|\[JS\]|error" "$RESTART_LOG" | tail -20
    exit 1
fi

echo "WITNESS EDITOR-PLAY E2E: PASS (run + restart, UI incluse)"
