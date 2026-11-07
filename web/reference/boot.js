// 共用客户端入口与 DOM 契约，避免另抄一套会漂移的鉴权和交易协议。
async function boot() {
  const response = await fetch(new URL("../index.html", import.meta.url));
  if (!response.ok) throw new Error(`Client template: HTTP ${response.status}`);
  const template = new DOMParser().parseFromString(await response.text(), "text/html");
  const styles = [...template.head.querySelectorAll('link[rel="stylesheet"], link[rel="preload"]')];
  const ready = styles.map(source => {
    const link = document.importNode(source, true);
    const wait = link.rel === "stylesheet" ? new Promise((resolve, reject) => {
      link.onload = resolve; link.onerror = () => reject(new Error(`Stylesheet: ${link.getAttribute("href")}`));
    }) : Promise.resolve();
    document.head.append(link);
    return wait;
  });
  await Promise.all(ready);
  template.body.querySelectorAll("script").forEach(script => script.remove());
  document.body.replaceChildren(...[...template.body.childNodes].map(node => document.importNode(node, true)));
  document.body.classList.add("florr-ui", "reference-client");
  const switcher = document.createElement("a");
  switcher.className = "reference-switch"; switcher.href = "./index.html";
  switcher.textContent = "Original client / 原客户端";
  document.getElementById("authPanel").append(switcher);
  document.getElementById("settingsPanel").append(switcher.cloneNode(true));
  document.querySelectorAll(".panel-close").forEach(button => { button.textContent = "×"; });
  await import("../src/game.js");
  document.title = "FlorrBt · Video reference";
  document.documentElement.dataset.referenceReady = "true";
}

boot().catch(error => {
  console.error("Reference client startup failed", error);
  const message = document.createElement("div"); message.id = "referenceLoading";
  message.textContent = `Unable to load client / 客户端加载失败：${error.message || "network error"}`;
  const back = document.createElement("a"); back.href = "./index.html"; back.textContent = "Original client / 原客户端";
  message.append(back); document.body.append(message);
});
