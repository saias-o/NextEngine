const playerState = {
  cash: 250350,
  transactions: [
    { kind: "sale", label: "Vente: Sentinel XS", amount: 18500 },
    { kind: "purchase", label: "Achat: munitions", amount: -1250 },
    { kind: "purchase", label: "Achat: Pay'n'Spray", amount: -500 },
    { kind: "sale", label: "Vente: composants", amount: 3200 },
    { kind: "purchase", label: "Achat: planque", amount: -15000 }
  ]
};

function formatCash(value) {
  const sign = value < 0 ? "-" : "";
  const whole = Math.max(0, Math.floor(Math.abs(value)));
  return sign + "$" + String(whole).padStart(8, "0");
}

function setText(id, value) {
  const element = document.getElementById(id);
  if (element) element.textContent = value;
}

function showLedger(visible) {
  const panel = document.getElementById("ledger-panel");
  if (!panel) return;
  panel.classList.toggle("hidden", !visible);
}

function renderCash() {
  const cash = formatCash(playerState.cash);
  setText("cash-shadow", cash);
  setText("cash", cash);
}

function renderLedger() {
  const list = document.getElementById("ledger-list");
  if (!list) return;

  let html = "";
  for (const tx of playerState.transactions) {
    const amountClass = tx.amount >= 0 ? "sale" : "purchase";
    html += ""
      + "<div class=\"ledger-row\">"
      + "<div class=\"ledger-label\">" + tx.label + "</div>"
      + "<div class=\"ledger-amount " + amountClass + "\">" + formatCash(tx.amount) + "</div>"
      + "</div>";
  }
  list.innerHTML = html;
}

function bindHud() {
  const cashButton = document.getElementById("cash-button");
  const backButton = document.getElementById("back-button");
  let ledgerVisible = false;

  function openLedger() {
    if (ledgerVisible) return;
    ledgerVisible = true;
    showLedger(true);
  }

  function closeLedger() {
    if (!ledgerVisible) return;
    ledgerVisible = false;
    showLedger(false);
  }

  if (cashButton) {
    cashButton.addEventListener("click", openLedger);
    cashButton.addEventListener("mousedown", openLedger);
  }

  if (backButton) {
    backButton.addEventListener("click", closeLedger);
    backButton.addEventListener("mousedown", closeLedger);
  }
}

renderCash();
renderLedger();
showLedger(false);
bindHud();
