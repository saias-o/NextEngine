// Pilote E2E (jamais référencé par le jeu : le harnais l'ajoute comme
// autoload dans la copie packagée). Marche tout droit : traverse la porte du
// hub, puis Relic2 alignée dans l'arène. PASS dès qu'une relique est
// sauvegardée, FAIL au timeout. Le code de sortie du process reste 0 : le
// harnais greppe [E2E] PASS/FAIL dans les logs.

const TIMEOUT = 25;
let t = 0;
let finished = false;

function finish(verdict) {
    finished = true;
    input.inject("MoveForward", 0);
    console.log("[E2E] " + verdict);
    tree.quit();
}

function onReady() {
    storage.remove("witness");
    console.log("[E2E] driver armed");
}

function onUpdate(dt) {
    if (finished) return;
    t += dt;

    if (t > 1.0) input.inject("MoveForward", 1);

    const raw = storage.load("witness");
    if (raw !== null) {
        try {
            if ((Number(JSON.parse(raw).relics) || 0) >= 1) { finish("PASS"); return; }
        } catch (_) {}
    }
    if (t > TIMEOUT) finish("FAIL: no relic collected within " + TIMEOUT + "s");
}
