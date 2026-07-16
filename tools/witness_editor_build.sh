#!/usr/bin/env bash
# E2E du clic Build de l'ÉDITEUR : lance SaidaEngine.exe --build (exactement le
# code du bouton Build du dialogue, pas saida_tool), ajoute le pilote
# e2e_driver.js en autoload dans l'artefact produit, exécute le jeu et greppe
# le verdict. Nécessite une machine avec GPU (l'éditeur crée sa fenêtre).
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

OUT="${1:-$ROOT/build/witness-editor-build}"
rm -rf "$OUT"

./build/bin/SaidaEngine.exe --project WitnessGame/WitnessGame.saidaproj \
    --build "$OUT" > "$ROOT/build/editor_build.log" 2>&1 || true

if ! grep -q "\[BUILD\] PASS" "$ROOT/build/editor_build.log"; then
    echo "EDITOR BUILD: FAIL (pas de [BUILD] PASS)"
    tail -20 "$ROOT/build/editor_build.log"
    exit 1
fi

python - "$OUT/WitnessGame.saidaproj" <<'EOF'
import json, sys
p = json.load(open(sys.argv[1]))
p.setdefault("autoloads", {})["E2EDriver"] = "scripts/e2e_driver.js"
json.dump(p, open(sys.argv[1], "w"), indent=2)
EOF

cd "$OUT"
"./Witness Game.exe" > e2e.log 2>&1 || true

if grep -q "\[E2E\] PASS" e2e.log; then
    echo "WITNESS EDITOR-BUILD E2E: PASS"
    exit 0
fi
echo "WITNESS EDITOR-BUILD E2E: FAIL"
grep -E "\[E2E\]|\[JS\]|error" e2e.log | tail -20
exit 1
