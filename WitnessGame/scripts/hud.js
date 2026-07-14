// HUD : attaché au UITextNode "ScoreText". Rafraîchit le compteur de reliques
// depuis storage (pas encore de comms inter-nœuds en JS — friction n°6).

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
    node.setText("Relics: " + relics);
}

function onReady() {
    refresh();
    time.every(0.25, refresh);
}
