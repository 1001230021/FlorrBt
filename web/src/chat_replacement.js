import { getCurrentLocale } from "./i18n.js";

// Per-locale replacement entries. Longer keys win: at every position the
// longest matching key is replaced first, so "eat apple" becomes the whole
// phrase translation instead of two word-level translations.
const replacementEntries = {
  "zh-Hans": [
    ["eat apple", "吃苹果"],
    ["apple", "苹果"],
    ["eat", "食用"],
  ],
  "zh-Hant": [
    ["eat apple", "吃蘋果"],
    ["apple", "蘋果"],
    ["eat", "食用"],
  ],
};

const sortedEntries = new Map();

function entriesForLocale(locale) {
  if (!sortedEntries.has(locale)) {
    const entries = (replacementEntries[locale] || [])
      .map(([from, to]) => [String(from).toLowerCase(), String(to)])
      .filter(([from]) => from.length > 0)
      .sort((a, b) => b[0].length - a[0].length);
    sortedEntries.set(locale, entries);
  }
  return sortedEntries.get(locale);
}

export function applyChatReplacement(text) {
  const source = String(text || "");
  const entries = entriesForLocale(getCurrentLocale());
  if (!entries.length) return source;

  const lowered = source.toLowerCase();
  let result = "";
  let index = 0;
  while (index < source.length) {
    let matched = false;
    for (const [from, to] of entries) {
      if (lowered.startsWith(from, index)) {
        result += to;
        index += from.length;
        matched = true;
        break;
      }
    }
    if (!matched) {
      result += source[index];
      index += 1;
    }
  }
  return result;
}
