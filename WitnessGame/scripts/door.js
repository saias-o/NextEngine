// Porte de changement de scène. Attaché à un nœud Area : quand le joueur
// entre, bascule la sous-scène (le World et les autoloads survivent).

exportProperty("targetScene", "scenes/hub.scene");

let used = false;

function onReady() {
    node.on("bodyEntered", function (who) {
        if (used || who !== "Player") return;
        used = true;  // changeScene est différé : ne pas le déclencher deux fois
        console.log("[Door] -> " + props.targetScene);
        tree.changeScene(props.targetScene);
    });
}
