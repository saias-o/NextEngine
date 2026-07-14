#!/usr/bin/env bash
# E2E du chemin « ship » : packe WitnessGame comme BuildExporter, ajoute le
# pilote e2e_driver.js en autoload, lance le runtime standalone et greppe le
# verdict. Nécessite une machine avec GPU (le runtime ouvre une fenêtre).
set -e
cd "$(dirname "$0")/.."

OUT="${1:-build/witness-e2e}"
rm -rf "$OUT"
mkdir -p "$OUT"

cp build/bin/SaidaEngineRuntime.exe "$OUT/WitnessGame.exe"
cp build/bin/glfw3.dll "$OUT/" 2>/dev/null || true
cp -r build/shaders "$OUT/shaders"
cp -r WitnessGame/scenes WitnessGame/scripts WitnessGame/assets WitnessGame/anim "$OUT/"
cp WitnessGame/WitnessGame.saidaproj "$OUT/"

python - "$OUT/WitnessGame.saidaproj" <<'EOF'
import json, sys
p = json.load(open(sys.argv[1]))
p.setdefault("autoloads", {})["E2EDriver"] = "scripts/e2e_driver.js"
json.dump(p, open(sys.argv[1], "w"), indent=2)
EOF

printf '# SaidaEngine game boot manifest\nschema=1\nproject=WitnessGame.saidaproj\nmain_scene=scenes/hub.scene\n' > "$OUT/game.saida"

cd "$OUT"
./WitnessGame.exe > e2e.log 2>&1 || true

if grep -q "\[E2E\] PASS" e2e.log; then
    echo "WITNESS E2E: PASS"
    exit 0
fi
echo "WITNESS E2E: FAIL"
grep -E "\[E2E\]|\[JS\]|error" e2e.log | tail -20
exit 1
