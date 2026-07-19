#!/usr/bin/env bash
# Stage web exact du jeu témoin via le VRAI BuildExporter (saida_tool
# export-game --platform web, le même code que le bouton Build). Le pilote E2E
# est passé au runtime par l'URL et ne modifie aucun fichier du package.
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

EXPORT="$ROOT/build/witness-web-export"
OUT="${1:-$ROOT/build/witness-web}"
rm -rf "$EXPORT" "$OUT"

"${SAIDA_TOOL:-$(
    if [[ -x "$ROOT/build/bin/saida_tool.exe" ]]; then
        printf '%s' "$ROOT/build/bin/saida_tool.exe"
    else
        printf '%s' "$ROOT/build/bin/saida_tool"
    fi
)}" export-game WitnessGame/WitnessGame.saidaproj \
    --platform web --out "$EXPORT" > /dev/null

# exportWebBuild écrit <out>/web ; on publie ce dossier tel quel.
mv "$EXPORT/web" "$OUT"
rm -rf "$EXPORT"

echo "staged: $OUT"
echo "serve: python web/serve.py $OUT 8080"
echo "test:  http://localhost:8080/?smoke&test-autoload=E2EDriver%3Dscripts%2Fe2e_driver.js"
