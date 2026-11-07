import { UiPanel } from "./ui_panel.js";
import { setAnimatedVisibility } from "./ui_motion.js";
import { t } from "./i18n.js";

// 两个入口共用同一份表单，避免账号、密码和注册模式出现不同步的副本。
export function createAccountUi({ state, manager, getAccount, onStart, onLogout, onOpen }) {
  const byId = id => document.getElementById(id);
  const home = byId("authPanel"), fields = byId("authFields"), formHome = byId("authFormHome");
  const formHost = byId("accountFormHost"), details = byId("accountDetails"), start = byId("startGameBtn");
  let visible = true, requireCredentials = false, restoring = false, busy = false;
  let bindingsOpen = false;
  const panel = new UiPanel({ id: "account", root: byId("accountPanel"), state, stateKey: "accountOpen", manager,
    trigger: byId("accountBtn"), focusTarget: byId("accountBtn"),
    onOpen: () => { onOpen(); render(); }, onChange: () => render(),
  });
  function render() {
    const account = getAccount();
    const signedIn = !!account && state.connected && state.accountSession && !requireCredentials;
    const pending = restoring && !signedIn && !requireCredentials;
    const ready = signedIn && state.inventoryLoaded;
    const showBindings = signedIn && bindingsOpen;
    const host = panel.isOpen() ? formHost : formHome;
    if (fields.parentElement !== host) host.append(fields);
    fields.classList.toggle("hidden", signedIn || pending);
    fields.inert = signedIn || pending;
    details.classList.toggle("hidden", !signedIn);
    byId('accountOverview').classList.toggle('hidden', showBindings);
    byId('accountBindings').classList.toggle('hidden', !showBindings);
    byId('accountBindings').inert = !showBindings;
    byId('accountBackBtn').classList.toggle('hidden', !showBindings);
    byId('accountBindingsBtn').setAttribute('aria-expanded', String(showBindings));
    const titleKey = showBindings ? 'ui.accountLinks.title' : 'ui.auth.account';
    byId('accountTitle').dataset.i18n = titleKey;
    byId('accountTitle').textContent = t(titleKey);
    byId("accountDisplayName").textContent = account;
    start.classList.toggle("hidden", !ready);
    start.disabled = busy || !ready;
    byId("startStatus").classList.toggle("hidden", !signedIn && !pending);
    if (signedIn && !ready) byId("startStatus").textContent = t('status.menuInventoryLoading');
    home.classList.toggle("remembered-account", signedIn || pending);
    setAnimatedVisibility(home, visible && !state.authenticated && !state.emailBindingPanelOpen, { duration: 220 });
  }
  function back() {
    if (!bindingsOpen) return false;
    bindingsOpen = false; render(); byId('accountBindingsBtn').focus(); return true;
  }
  byId("accountBtn").addEventListener("click", () => { if (!panel.isOpen()) bindingsOpen = false; panel.toggle(); });
  byId("accountCloseBtn").addEventListener("click", () => { bindingsOpen = false; panel.setOpen(false); });
  byId('accountBindingsBtn').addEventListener('click', () => { bindingsOpen = true; render(); byId('accountBackBtn').focus(); });
  byId('accountBackBtn').addEventListener('click', back);
  byId("logoutBtn").addEventListener("click", onLogout);
  start.addEventListener("click", onStart);
  return {
    panel, render, back,
    showHome(value) { visible = value; render(); },
    reset() { bindingsOpen = false; restoring = false; requireCredentials = false; byId("startStatus").textContent = ""; render(); },
    restoring() { restoring = true; requireCredentials = false; visible = true; render(); },
    signedIn() { bindingsOpen = false; restoring = false; requireCredentials = false; panel.setOpen(false); visible = true; byId("startStatus").textContent = ""; render(); },
    inventoryReady() { byId("startStatus").textContent = ""; render(); },
    requireLogin() { bindingsOpen = false; restoring = false; requireCredentials = true; visible = true; render(); },
    setBusy(value) { busy = value; byId("logoutBtn").disabled = busy; render(); },
  };
}
