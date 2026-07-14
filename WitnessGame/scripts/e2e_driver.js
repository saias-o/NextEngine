// Pilote E2E (jamais référencé par le jeu : le harnais l'ajoute comme
// autoload dans la copie packagée).
//
// Phase 1 — gameplay : marche tout droit, traverse la porte du hub puis
// Relic2 alignée dans l'arène ; validé dès qu'une relique est sauvegardée.
// Phase 2 — chantier 3 : N cycles hub↔arena par tree.changeScene, puis
// vérifie que la mémoire résidente de l'AssetLoader est stable (pas de
// croissance entre le début et la fin des cycles) et sous le budget.
// PASS/FAIL dans les logs ([E2E] …) ; le code de sortie du process reste 0.

const TIMEOUT = 25;
const CYCLES = 16;
const CYCLE_INTERVAL = 0.5;

let t = 0;
let finished = false;
let phase = 1;
let cycles = 0;
let sinceSwap = 0;
let residentAtStart = -1;

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

function relicsCollected() {
    const raw = storage.load("witness");
    if (raw === null) return 0;
    try { return Number(JSON.parse(raw).relics) || 0; } catch (_) { return 0; }
}

function onUpdate(dt) {
    if (finished) return;
    t += dt;

    if (phase === 1) {
        if (t > 1.0) input.inject("MoveForward", 1);
        if (relicsCollected() >= 1) {
            input.inject("MoveForward", 0);
            phase = 2;
            console.log("[E2E] phase 1 ok — relic collected, starting scene cycles");
            return;
        }
        if (t > TIMEOUT) finish("FAIL: no relic collected within " + TIMEOUT + "s");
        return;
    }

    sinceSwap += dt;
    if (sinceSwap < CYCLE_INTERVAL) return;
    sinceSwap = 0;

    if (cycles === 2) {
        // Référence après quelques cycles (les caches chauds sont remplis).
        residentAtStart = assets.stats().residentBytes;
    }
    if (cycles >= CYCLES) {
        const s = assets.stats();
        if (s.residentBytes > s.budgetBytes)
            return finish("FAIL: resident " + s.residentBytes + " over budget " + s.budgetBytes);
        if (residentAtStart >= 0 && s.residentBytes > residentAtStart)
            return finish("FAIL: loader memory grew across cycles (" +
                          residentAtStart + " -> " + s.residentBytes + ")");
        return finish("PASS (cycles=" + CYCLES + ", resident=" + s.residentBytes +
                      "/" + s.budgetBytes + ")");
    }
    cycles++;
    tree.changeScene(cycles % 2 ? "scenes/hub.scene" : "scenes/arena.scene");
}
