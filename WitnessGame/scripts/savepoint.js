// Point de sauvegarde. La progression est déjà écrite au fil de l'eau par les
// pickups ; entrer ici incrémente le compteur de sauvegardes et le consigne —
// il matérialise le critère save/load du jeu témoin.

const SLOT = "witness";

function onReady() {
    node.on("bodyEntered", function (who) {
        if (who !== "Player") return;
        let state = { relics: 0, saves: 0 };
        const raw = storage.load(SLOT);
        if (raw !== null) {
            try { state = JSON.parse(raw); } catch (_) {}
        }
        state.saves = (Number(state.saves) || 0) + 1;
        storage.save(SLOT, JSON.stringify(state));
        console.log("[SavePoint] saved — relics=" + state.relics +
                    " saves=" + state.saves);
    });
}
