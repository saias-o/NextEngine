// Relique à ramasser. Attaché à un nœud Area : réagit au signal réfléchi
// "bodyEntered" (payload = nom du nœud entrant), incrémente le compteur
// partagé via storage, puis se libère (queueFree différé).

exportProperty("points", 1);

const SLOT = "witness";
let collected = false;

function bump(field, amount) {
    let state = { relics: 0, saves: 0 };
    const raw = storage.load(SLOT);
    if (raw !== null) {
        try { state = JSON.parse(raw); } catch (_) {}
    }
    state[field] = (Number(state[field]) || 0) + amount;
    storage.save(SLOT, JSON.stringify(state));
    return state[field];
}

function onReady() {
    node.on("bodyEntered", function (who) {
        if (collected || who !== "Player") return;
        collected = true;
        audio.play("pickup");
        const total = bump("relics", props.points);
        console.log("[Pickup] " + node.getName() + " collected — relics=" + total);
        node.queueFree();
    });
}
