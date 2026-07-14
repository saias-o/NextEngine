#!/usr/bin/env bash
# Met en scène le package web du jeu témoin (miroir de exportWebBuild) dans
# build/witness-web : player compilé (build-web-player) + données projet +
# project-files.json. Servir ensuite avec : python web/serve.py build/witness-web
set -e
cd "$(dirname "$0")/.."

OUT="${1:-build/witness-web}"
[ -f build-web-player/index.html ] || { echo "build-web-player manquant — lancer web/build_web_player.sh"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/project"

for f in index.html index.js index.wasm index.data; do
    [ -f "build-web-player/$f" ] && cp "build-web-player/$f" "$OUT/"
done

cp -r WitnessGame/scenes WitnessGame/scripts WitnessGame/assets WitnessGame/anim "$OUT/project/"
cp WitnessGame/WitnessGame.saidaproj "$OUT/project/"

# Pilote E2E en autoload (comme tools/witness_e2e.sh côté desktop).
python - "$OUT/project/WitnessGame.saidaproj" <<'EOF'
import json, sys
p = json.load(open(sys.argv[1]))
p.setdefault("autoloads", {})["E2EDriver"] = "scripts/e2e_driver.js"
json.dump(p, open(sys.argv[1], "w"), indent=2)
EOF

printf '# SaidaEngine game boot manifest\nschema=1\nproject=WitnessGame.saidaproj\nmain_scene=scenes/hub.scene\n' > "$OUT/project/game.saida"

python - "$OUT" <<'EOF'
import json, os, sys
root = os.path.join(sys.argv[1], "project")
files = []
for base, _, names in os.walk(root):
    for n in names:
        files.append(os.path.relpath(os.path.join(base, n), root).replace(os.sep, "/"))
files.sort()
with open(os.path.join(sys.argv[1], "project-files.json"), "w") as f:
    json.dump({"schema": 1, "files": files}, f, indent=2)
EOF

echo "staged: $OUT (servir: python web/serve.py $OUT)"
