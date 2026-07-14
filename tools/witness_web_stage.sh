#!/usr/bin/env bash
# Stage web du jeu témoin via le VRAI BuildExporter (saida_tool export-game
# --platform web, le même code que le bouton Build) + pilote E2E en autoload.
# Servir ensuite : python web/serve.py build/witness-web  →  http://localhost:8081/?smoke
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

EXPORT="$ROOT/build/witness-web-export"
OUT="${1:-$ROOT/build/witness-web}"
rm -rf "$EXPORT" "$OUT"

./build/bin/saida_tool.exe export-game WitnessGame/WitnessGame.saidaproj \
    --platform web --out "$EXPORT" > /dev/null

# exportWebBuild écrit <out>/web ; on publie ce dossier tel quel.
mv "$EXPORT/web" "$OUT"
rm -rf "$EXPORT"

# Pilote E2E en autoload (comme tools/witness_e2e.sh côté desktop).
python - "$OUT/project/WitnessGame.saidaproj" <<'EOF'
import json, sys
p = json.load(open(sys.argv[1]))
p.setdefault("autoloads", {})["E2EDriver"] = "scripts/e2e_driver.js"
json.dump(p, open(sys.argv[1], "w"), indent=2)
EOF

echo "staged: $OUT (servir: python web/serve.py $OUT)"
