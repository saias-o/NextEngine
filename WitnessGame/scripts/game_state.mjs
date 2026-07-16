// GameState — autoload du jeu témoin (déclaré dans WitnessGame.saidaproj).
//
// Il possède l'état persistant : reliques ramassées + meilleure série,
// sauvegardé via l'API `storage` (saves/witness.json). Les autres scripts ne
// peuvent pas l'atteindre directement car chaque ScriptBehaviour a son propre
// contexte JS : l'état
// partagé transite donc lui aussi par `storage`, sous le slot "witness".

const SLOT = "witness";

function readState() {
    const raw = storage.load(SLOT);
    if (raw === null) return { relics: 0, saves: 0 };
    try {
        const parsed = JSON.parse(raw);
        return {
            relics: Number(parsed.relics) || 0,
            saves: Number(parsed.saves) || 0,
        };
    } catch (_) {
        return { relics: 0, saves: 0 };
    }
}

export function onReady() {
    // Matérialise le slot au premier lancement pour que HUD/pickups aient
    // toujours un état lisible.
    if (!storage.has(SLOT)) {
        storage.save(SLOT, JSON.stringify({ relics: 0, saves: 0 }));
    }
    const state = readState();
    console.log(
        "[GameState] ready — relics=" + state.relics + " saves=" + state.saves);
}
