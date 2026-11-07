export function defaultSocketEndpoint(location = globalThis.location) {
  return `${location?.protocol === "https:" ? "wss" : "ws"}://${location?.host || "127.0.0.1:8080"}/ws`;
}
