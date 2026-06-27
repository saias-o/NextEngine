# NextEngine Web UI Guide

> Guide pratique pour assistants IA et contributeurs. Lis ceci avant de creer
> ou debugger une UI Web dans NextEngine.

## Objectif

NextEngine veut une UI auteur aussi proche que possible de Chrome: HTML-like,
CSS-like, JavaScript, `querySelector`, `addEventListener`, `classList`,
`innerHTML`, layout responsive, hot reload. Le backend actuel n'est pas un
navigateur complet: il s'appuie sur RmlUi pour le DOM/layout/style et QuickJS
pour JavaScript. Le travail du moteur est donc d'absorber les differences autant
que possible, pas de forcer les jeux a ecrire une UI "speciale moteur".

Quand tu codes une UI:

- Ecris d'abord du HTML/CSS/JS naturel.
- Si ca casse, ameliore le support moteur ou documente une vraie limite.
- Evite les contournements fragiles sauf pour debloquer temporairement une scene.
- Garde les exemples propres: ils deviennent le modele des prochains LLM.

## Architecture mentale

Un `WebCanvasNode` est une page RmlUi rendue dans une texture Vulkan.

- `url`: fichier `.rml` / HTML-like a charger.
- `.rcss` / `.css`: styles charges par `<link type="text/rcss" href="..."/>`.
- `<script src="..."></script>`: execute par QuickJS, relatif au document.
- `mode = ScreenSpace`: overlay 2D dans le viewport de jeu/editor.
- `mode = WorldSpace`: quad 3D texturé, raycastable, utile VR/XR.
- `width` / `height`: resolution logique du canvas RmlUi.
- `renderOrder`: ordre entre overlays.
- `interactive`: route souris/touch/clavier vers le document.

Le renderer CPU RmlUi produit des pixels RGBA transparents, uploades dirty-only
dans une texture. La transparence est vraie: un `body` transparent laisse voir la
scene 3D.

## Structure recommandee

```text
GTAClone/ui/hud/
  hud.rml
  hud.rcss
  hud.js
  star.png
```

Utilise des chemins relatifs au document:

```html
<rml>
  <head>
    <link type="text/rcss" href="hud.rcss"/>
  </head>
  <body>
    <div id="hud-root">
      <button id="cash-button" class="ui-hit">$000250350</button>
      <div id="transactions" class="panel hidden">
        <div class="title">Transactions</div>
        <div id="transaction-list"></div>
        <button id="back-button" class="ui-hit">Retour</button>
      </div>
    </div>
    <script src="hud.js"></script>
  </body>
</rml>
```

## JavaScript recommande

Garde un etat simple, des fonctions de rendu pures, puis le binding des events.

```js
const state = {
  cash: 250350,
  transactions: [
    { label: "Vente: Sentinel XS", amount: 18500 },
    { label: "Achat: munitions", amount: -1250 }
  ]
};

function money(value) {
  const sign = value < 0 ? "-" : "";
  return sign + "$" + String(Math.abs(Math.floor(value))).padStart(8, "0");
}

function show(id, visible) {
  document.getElementById(id).classList.toggle("hidden", !visible);
}

function render() {
  document.getElementById("cash-button").textContent = money(state.cash);
  document.getElementById("transaction-list").innerHTML =
    state.transactions.map(tx => {
      const cls = tx.amount >= 0 ? "sale" : "purchase";
      return `<div class="row"><div class="label">${tx.label}</div><div class="amount ${cls}">${money(tx.amount)}</div></div>`;
    }).join("");
}

render();
show("transactions", false);

document.getElementById("cash-button").addEventListener("click", () => show("transactions", true));
document.getElementById("back-button").addEventListener("click", () => show("transactions", false));
```

Important:

- `click` doit fonctionner. Si ce n'est pas le cas, corrige le pont input moteur.
- `mousedown` peut etre ajoute pour feedback immediat, mais ne doit pas remplacer
  `click` comme pattern principal.
- Evite de stocker l'etat dans le DOM quand un objet JS simple suffit.
- Les hooks moteur disponibles incluent `tree.changeScene`, `tree.reloadScene`,
  `tree.setPaused`, `tree.paused`, `tree.quit`.

## CSS fiable

RmlUi supporte beaucoup plus que du positionnement absolu. Utilise le flow CSS:

- `display: block`
- `display: inline-block`
- `display: flex`
- `flex-direction`
- `flex`, `align-items`, `justify-content`
- `gap`, `row-gap`, `column-gap`
- `margin`, `padding`
- `width`, `height`, `%`, `px`
- `position: absolute` pour les HUD fixes
- `:hover`, `:active`, classes
- couleurs, bordures, backgrounds, font sizes, line heights

Exemple:

```css
body {
  margin: 0;
  width: 100%;
  height: 100%;
  background-color: transparent;
  font-family: LatoLatin;
}

.panel {
  display: block;
  width: 448px;
  padding: 14px;
  background-color: #07101c;
  border-width: 2px;
  border-color: #9fe87a;
}

.row {
  display: flex;
  flex-direction: row;
  align-items: center;
  gap: 12px;
  padding: 6px 10px;
  margin-bottom: 6px;
  background-color: #121d2b;
}

.row .label { flex: 1 1 auto; }
.row .amount { flex: 0 0 128px; text-align: right; }

.hidden { display: none; }
```

Cascade warning: if a more specific rule sets `display`, `.hidden` may lose.
Prefer specific hidden selectors for panels:

```css
#transactions.hidden {
  display: none;
}
```

Avoid unless verified:

- `text-shadow` is currently sanitized out.
- Browser-only CSS vendor properties are ignored.
- Complex CSS transforms, masks, filters, and advanced compositing may be missing
  in the CPU renderer.
- Do not assume every Chrome feature exists. When a normal feature is missing,
  prefer improving the bridge/backend and updating this guide.

## DOM / JS support expected

Current bridge should support:

- `document.getElementById(id)`
- `document.querySelector(selector)`
- `document.querySelectorAll(selector)`
- `document.body`
- `document.documentElement`
- `element.querySelector(selector)`
- `element.querySelectorAll(selector)`
- `element.textContent`
- `element.innerHTML` / `element.innerRML`
- `element.id`
- `element.classList.add/remove/toggle/contains`
- `element.style.setProperty/removeProperty`
- `element.addEventListener(type, callback)`
- `element.removeEventListener(type, callback)`
- `element.click()`, `focus()`, `blur()`
- `element.getBoundingClientRect()`
- `offsetLeft`, `offsetTop`, `offsetWidth`, `offsetHeight`
- `clientWidth`, `clientHeight`

If a natural browser API is missing and needed for a normal UI, add it in
`WebCanvasNode` instead of rewriting the UI into awkward C++-style code.

## Interaction et hit-test

For transparent full-screen HUDs, only real UI controls should capture input.
Mark interactive regions with `ui-hit`:

```html
<button id="cash-button" class="ui-hit">...</button>
<div id="transactions" class="panel ui-hit hidden">...</div>
```

The engine hit-tests RmlUi elements and only routes pointer events to elements
marked `.ui-hit` or native controls such as `button`, `input`, `select`,
`textarea`. This prevents a transparent fullscreen canvas from stealing all game
clicks.

Screen-space input is local to the editor/game viewport, not the whole window.
World-space input uses raycast into the canvas plane. XR controller rays route
to world-space canvases.

## Scene setup checklist

In the `.scene`, a HUD-like WebCanvas should look conceptually like:

```json
{
  "type": "WebCanvasNode",
  "name": "GTA HUD",
  "enabled": true,
  "transform": {
    "position": [0.0, 0.0, 0.0],
    "rotation": [0.0, 0.0, 0.0, 1.0],
    "scale": [1.0, 1.0, 1.0]
  },
  "width": 1280,
  "height": 720,
  "mode": 0,
  "url": "GTAClone/ui/hud/hud.rml",
  "hotReload": true,
  "interactive": true,
  "renderOrder": 1000
}
```

Rules:

- For fullscreen screen-space HUDs, position `(0,0)` and scale `(1,1)` lets the
  canvas follow the actual viewport size.
- Keep `body` transparent unless you intentionally want an opaque menu.
- Set `interactive: true` for clickable UI.
- Use `.ui-hit` so transparent areas do not capture input.
- Use `renderOrder` for layering between multiple web canvases.

## Editor viewport caveat

The editor has Scene Tree / Inspector / File Browser over a docked viewport.
The renderer must use the real visible viewport rectangle, not the entire
swapchain. This has already bitten the GTA HUD: the UI was correct in screen
coordinates but hidden under ImGui panels. If Web UI appears cropped or offset in
editor, inspect:

- `EditorUI::viewportPosition()`
- `EditorUI::viewportSize()`
- `Engine::setRenderViewport`
- `Renderer::activeRenderRect`
- `UIRenderer::gatherUI`
- `UIRenderer::recordCommands`

Never "fix" this by padding the HUD away from editor panels.

## Debug checklist

When an UI does not appear:

1. Check logs for RmlUi/QuickJS warnings.
2. In inspector, check `lastError`.
3. Verify script path resolution. `<script src="hud.js">` is relative to the RML
   document.
4. Verify texture dependency paths: images should be relative, e.g. `star.png`,
   not duplicated project paths.
5. Check the WebCanvas render stat log:
   - `rendered 0 visible pixels`: DOM/CSS/render issue.
   - nonzero pixels but invisible: GPU composition / viewport / alpha issue.
6. For transparent UIs, remember that invisible transparent pixels are correct.
7. For click bugs:
   - hover works? input reaches RmlUi.
   - `click` not firing? inspect `WebCanvasNode::fireMouseEvent`.
   - callback exception? logs should show `[WebCanvas JS] event listener`.
8. For layout bugs:
   - first try normal CSS flow/flex.
   - inspect CSS cascade specificity, especially `.hidden`.
   - use `getBoundingClientRect()` from JS if needed.

Useful log examples:

```text
UIRenderer: gathered 1 WebCanvas node(s), 1 screen draw command(s)
[WebCanvas] 'GTA HUD' rendered 13444 visible pixel(s), bbox=25,28 -> 303,148
```

If the bbox suddenly includes a hidden panel, check CSS cascade: hidden probably
does not win.

## Quality bar for examples

An example UI is acceptable only if:

- it is authored like ordinary web UI;
- JS is readable and state-driven;
- CSS uses normal layout primitives where possible;
- transparent areas do not steal input;
- it works inside the editor viewport and in runtime viewport;
- no RmlUi/QuickJS warnings appear on load;
- hot reload preserves the old UI on failed reload;
- it demonstrates at least one event (`click`) and one DOM mutation.

Bad examples teach future LLMs bad habits. If a workaround is necessary, leave a
comment explaining what engine feature is missing and where to fix it.

## Files to inspect for engine-side work

- `src/scene/WebCanvasNode.*`: DOM bridge, JS execution, input events, RmlUi
  context, texture update.
- `src/ui/RmlUiRuntime.*`: RmlUi init, fonts, file interface, CSS sanitization.
- `src/ui/RmlUiRenderInterface.*`: CPU rasterizer, textures, scissor, transforms.
- `src/ui/UIInteractionSystem.*`: mouse/touch/key routing and hit-test.
- `src/graphics/UIRenderer.*`: screen-space draw commands and composition.
- `src/render/Renderer.*`: world-space canvases, viewport rect, tonemap.
- `src/xr/toolkit/XRRayInteractor.*`: XR controller ray routing.

## Current philosophy

RmlUi is the lightweight renderer/layout engine; QuickJS is the single JS
runtime. We do not embed Chromium. But the authoring experience should move
toward browser expectations wherever practical. If a future LLM has to choose
between:

1. writing weird RmlUi-only markup, or
2. adding a small missing DOM/CSS compatibility layer,

prefer option 2 when it is clean, bounded, and useful to more than one UI.
