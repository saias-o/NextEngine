#!/usr/bin/env bash
# E2E du chemin « ship » : exporte WitnessGame avec le VRAI BuildExporter
# (saida_tool export-game — le même code que le bouton Build), ajoute le
# pilote e2e_driver.js en autoload, lance le runtime standalone et greppe le
# verdict. Nécessite une machine avec GPU (le runtime ouvre une fenêtre).
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

OUT="${1:-$ROOT/build/witness-e2e}"
rm -rf "$OUT"

./build/bin/saida_tool.exe export-game WitnessGame/WitnessGame.saidaproj \
    --out "$OUT" --version 0.1.0 --company Saida > /dev/null

python - "$OUT/WitnessGame.saidaproj" <<'EOF'
import json, sys
p = json.load(open(sys.argv[1]))
p.setdefault("autoloads", {})["E2EDriver"] = "scripts/e2e_driver.js"
json.dump(p, open(sys.argv[1], "w"), indent=2)
EOF

cd "$OUT"
"./Witness Game.exe" > e2e.log 2>&1 || true

if ! grep -q "\[E2E\] PASS" e2e.log; then
    echo "WITNESS E2E: FAIL"
    grep -E "\[E2E\]|\[JS\]|error" e2e.log | tail -20
    exit 1
fi

# Second lancement, même dossier : la progression sauvegardée par le run 1
# doit être restaurée depuis saves/ au boot (save/load après redémarrage).
"./Witness Game.exe" > e2e_restart.log 2>&1 || true

if ! grep -q "\[E2E\] RESTART PASS" e2e_restart.log; then
    echo "WITNESS E2E: FAIL (redémarrage : progression non restaurée)"
    grep -E "\[E2E\]|\[JS\]|error" e2e_restart.log | tail -20
    exit 1
fi

echo "WITNESS E2E: PASS (run + restart)"
exit 0
