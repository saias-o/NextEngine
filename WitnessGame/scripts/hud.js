// HUD minimal. Attaché au UITextNode "ScoreText" : rafraîchit le compteur de
// reliques depuis storage (pas d'accès inter-nœuds en JS — friction V1
// consignée ; le polling à intervalle est le contournement assumé).

const SLOT = "witness";
let last = null;

function refresh() {
    const raw = storage.load(SLOT);
    if (raw === last) return;
    last = raw;
    let relics = 0;
    if (raw !== null) {
        try { relics = Number(JSON.parse(raw).relics) || 0; } catch (_) {}
    }
    // Pas encore de binding JS pour muter le texte d'un UITextNode : consigné
    // comme friction V1. En attendant, le HUD trace en console.
    console.log("[HUD] relics=" + relics);
}

function onReady() {
    refresh();
    time.every(0.5, refresh);
}
