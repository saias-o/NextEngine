// Point de sauvegarde : l'autoload est l'unique propriétaire de la progression
// et de sa représentation durable.

function onReady() {
    const gameState = tree.autoload("GameState");
    if (gameState === null) throw new Error("GameState autoload is missing");
    node.on("bodyEntered", function (who) {
        if (who !== "Player") return;
        const state = gameState.call("saveProgress");
        audio.play("save");
        console.log("[SavePoint] saved — relics=" + state.relics +
                    " saves=" + state.saves);
    });
}
