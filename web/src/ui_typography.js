// DOM 排版与 Canvas 测量共用字体栈，避免中文回退字体不同导致宽度漂移。
export const UI_FONT_FAMILY = globalThis.document && globalThis.getComputedStyle
  ? getComputedStyle(document.documentElement).getPropertyValue('--ui-font-family').trim() || 'sans-serif'
  : 'sans-serif';
