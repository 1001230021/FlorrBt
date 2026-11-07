// 乐观布局的寿命由操作回执决定，不能凭相同卡片排列或经过的时间推断成功。
export function createLoadoutPreview() {
  let latest = null;
  return {
    stage(transaction) {
      latest = { ...transaction, inventoryTouchedKeys: [...new Set([
        ...(latest?.inventoryTouchedKeys || []), ...(transaction.inventoryTouchedKeys || []),
      ])] };
    },
    acknowledge(id) {
      if (!latest || latest.id !== id) return false;
      latest = null;
      return true;
    },
    clear() { latest = null; },
    get current() { return latest; },
  };
}
