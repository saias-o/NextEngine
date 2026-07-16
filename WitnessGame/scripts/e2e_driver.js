// Pilote E2E (jamais référencé par le jeu : le harnais l'ajoute comme
// autoload dans la copie packagée).
//
// Phase 1 — gameplay : marche tout droit, traverse la porte du hub puis
// Relic2 alignée dans l'arène ; validé dès qu'une relique est sauvegardée.
// Phase 2 — chantier 3 : N cycles hub↔arena par tree.changeScene, puis
// vérifie que la mémoire résidente de l'AssetLoader est stable (pas de
// croissance entre le début et la fin des cycles) et sous le budget, et que
// la mémoire GPU des ressources chargées (gpuResidentBytes : textures/meshes
// du ResourceManager, évincées par trimUnused au changement de scène) est
// elle aussi stable — le vrai critère « déchargement réel » du chantier 3.
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
let gpuAtStart = -1;
let gameState = null;
let relicSignalArmed = false;
let relicSignalSeen = false;

function finish(verdict) {
    finished = true;
    input.inject("MoveForward", 0);
    console.log("[E2E] " + verdict);
    tree.quit();
}

function onReady() {
    gameState = tree.autoload("GameState");
    if (gameState === null) return finish("FAIL: GameState autoload missing");
    gameState.call("reset");
    console.log("[E2E] driver armed");
}

function relicsCollected() {
    return Number(gameState.call("getRelics")) || 0;
}

function armRelicSignal() {
    if (relicSignalArmed) return true;
    const relic = tree.firstInGroup("relic");
    if (relic === null) return true;
    const relics = tree.nodesInGroup("relic");
    const sameRelic = tree.nodeById(relic.id);
    if (relics.length !== 3 || sameRelic === null ||
        sameRelic.getName() !== relic.getName()) {
        finish("FAIL: cross-node group/id lookup mismatch");
        return false;
    }
    for (const candidate of relics) {
        if (!candidate.on("bodyEntered", function (who) {
            if (who === "Player") relicSignalSeen = true;
        })) {
            finish("FAIL: cross-node signal subscription rejected");
            return false;
        }
    }
    relicSignalArmed = true;
    return true;
}

function onUpdate(dt) {
    if (finished) return;
    t += dt;

    if (phase === 1) {
        if (!armRelicSignal()) return;
        if (t > 1.0) input.inject("MoveForward", 1);
        if (relicsCollected() >= 1) {
            if (!relicSignalSeen)
                return finish("FAIL: relic collected without cross-node signal");
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
        const s = assets.stats();
        residentAtStart = s.residentBytes;
        gpuAtStart = s.gpuResidentBytes;
    }
    if (cycles >= CYCLES) {
        const s = assets.stats();
        if (s.residentBytes > s.budgetBytes)
            return finish("FAIL: resident " + s.residentBytes + " over budget " + s.budgetBytes);
        if (residentAtStart >= 0 && s.residentBytes > residentAtStart)
            return finish("FAIL: loader memory grew across cycles (" +
                          residentAtStart + " -> " + s.residentBytes + ")");
        if (gpuAtStart >= 0 && s.gpuResidentBytes > gpuAtStart)
            return finish("FAIL: GPU resident memory grew across cycles (" +
                          gpuAtStart + " -> " + s.gpuResidentBytes + ")");
        return finish("PASS (cycles=" + CYCLES + ", resident=" + s.residentBytes +
                      "/" + s.budgetBytes + ", gpu=" + s.gpuResidentBytes + ")");
    }
    cycles++;
    tree.changeScene(cycles % 2 ? "scenes/hub.scene" : "scenes/arena.scene");
}
