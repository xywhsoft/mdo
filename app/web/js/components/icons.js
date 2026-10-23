const ICONS = Object.freeze({
  plus: '<path d="M12 4v16M4 12h16"/>',
  "chevron-down": '<path d="m7 10 5 5 5-5"/>',
  shield: '<path d="M12 3 4 6v6c0 5 8 9 8 9s8-4 8-9V6Z"/>',
  "shield-check": '<path d="M12 3 4 6v6c0 5 8 9 8 9s8-4 8-9V6Z"/><path d="m8 12 3 3 5-6"/>',
  "shield-alert": '<path d="M12 3 4 6v6c0 5 8 9 8 9s8-4 8-9V6Z"/><path d="M12 8v5m0 3h.01"/>',
  hand: '<path d="M8 12V6a2 2 0 0 1 4 0v6-8a2 2 0 0 1 4 0v8-6a2 2 0 0 1 4 0v9c0 4-2 6-6 6h-2c-2 0-3-1-4-2l-5-6a2 2 0 0 1 3-2l2 2"/>',
  menu: '<path d="M4 7h16M4 12h16M4 17h16"/>',
  close: '<path d="m6 6 12 12M18 6 6 18"/>',
  compose: '<path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L8 18l-4 1 1-4Z"/>',
  copy: '<rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h2"/>',
  retry: '<path d="M4 12a8 8 0 1 0 2.343-5.657"/><path d="M6.343 3.343v3h3"/><path d="M12 7v5l3 2"/>',
  branch: '<circle cx="6" cy="5" r="2"/><circle cx="18" cy="8" r="2"/><circle cx="18" cy="19" r="2"/><path d="M6 7v9a3 3 0 0 0 3 3h7M16 8h-5a5 5 0 0 0-5 5"/>',
  search: '<circle cx="11" cy="11" r="7"/><path d="m20 20-4-4"/>',
  download: '<path d="M12 3v12m-5-5 5 5 5-5M4 17v3h16v-3"/>',
  more: '<circle cx="5" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/>',
  clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
  help: '<circle cx="12" cy="12" r="9"/><path d="M9.5 9a2.5 2.5 0 1 1 4.2 1.8c-1.1.8-1.7 1.3-1.7 2.7M12 17h.01"/>',
  theme: '<path d="M20.5 13A8.5 8.5 0 0 1 11 3.5 8.5 8.5 0 1 0 20.5 13Z"/>',
  sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2m0 16v2M2 12h2m16 0h2M4.9 4.9l1.4 1.4m11.4 11.4 1.4 1.4M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>',
  settings: '<path d="M10.32 5.00 L10.26 2.15 L13.74 2.15 L13.68 5.00 L15.76 5.86 L17.74 3.81 L20.19 6.26 L18.14 8.24 L19.00 10.32 L21.85 10.26 L21.85 13.74 L19.00 13.68 L18.14 15.76 L20.19 17.74 L17.74 20.19 L15.76 18.14 L13.68 19.00 L13.74 21.85 L10.26 21.85 L10.32 19.00 L8.24 18.14 L6.26 20.19 L3.81 17.74 L5.86 15.76 L5.00 13.68 L2.15 13.74 L2.15 10.26 L5.00 10.32 L5.86 8.24 L3.81 6.26 L6.26 3.81 L8.24 5.86Z"/><circle cx="12" cy="12" r="3.5"/>',
  code: '<path d="m8 9-4 3 4 3M16 9l4 3-4 3M14 5l-4 14"/>',
  review: '<path d="M4 5h16v12H8l-4 3Z"/><path d="M8 9h8M8 13h5"/>',
  activity: '<path d="M3 12h4l3-7 4 14 3-7h4"/>',
  spark: '<path d="m12 3 1.5 4.5L18 9l-4.5 1.5L12 15l-1.5-4.5L6 9l4.5-1.5ZM5 15l.7 2.3L8 18l-2.3.7L5 21l-.7-2.3L2 18l2.3-.7Z"/>',
  folder: '<path d="M3 6h7l2 2h9v10a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z"/>',
  "folder-open": '<path d="M3 10V6h7l2 2h9v3M3 10h18l-3 9H5Z"/>',
  pin: '<path d="m8 3 8 0-1 6 3 3v2H6v-2l3-3Zm4 11v7"/>',
  brain: '<path d="M9.5 4A3.5 3.5 0 0 0 6 7.5v.3A3 3 0 0 0 4.5 13 3.5 3.5 0 0 0 8 18.5c.5 1 1.4 1.5 2.5 1.5V4Zm5 0A3.5 3.5 0 0 1 18 7.5v.3a3 3 0 0 1 1.5 5.2 3.5 3.5 0 0 1-3.5 5.5c-.5 1-1.4 1.5-2.5 1.5V4Z"/><path d="M7 9h3.5M14 13h4M8 16h2.5M13.5 8H17"/>',
  "arrow-up": '<path d="m12 19V5M6 11l6-6 6 6"/>',
  "arrow-left": '<path d="m19 12H5M11 18l-6-6 6-6"/>',
});

export function mountIcons(root = document) {
  for (const target of root.querySelectorAll("[data-icon]")) {
    const body = ICONS[target.dataset.icon];
    if (!body) continue;
    target.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`;
  }
}
