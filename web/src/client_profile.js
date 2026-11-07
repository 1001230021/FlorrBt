// 参考入口只切换表现参数，账号、物品和玩法仍由同一协议维护。
export const referenceClient = globalThis.document?.documentElement?.dataset.client === "reference";
