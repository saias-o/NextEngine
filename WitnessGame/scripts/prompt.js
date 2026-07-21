// Prompt adaptatif attaché à PromptText : le label du binding de mouvement
// suit le dernier périphérique actif (input.lastActiveDevice). Avant toute
// activité ("none"), le prompt clavier sert de défaut.

const LABELS = {
    "keyboard-mouse": "Move: WASD",
    "gamepad": "Move: Left Stick",
    "touch": "Move: Swipe",
};
const DEFAULT_LABEL = LABELS["keyboard-mouse"];

let last = null;

function refresh() {
    const device = String(input.lastActiveDevice());
    if (device === last) return;
    last = device;
    node.setText(LABELS[device] || DEFAULT_LABEL);
}

function onReady() {
    refresh();
    time.every(0.1, refresh);
}
