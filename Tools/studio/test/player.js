// Player settings, the accessibility checks, the screen reader and the flashing check: everything uipreview's
// command line can check, from Studio.
const { store, session, commands, engine, host, log, wait, capture } = studio;
const fail = (message) => { throw new Error(message); };
const expect = (condition, message) => { if (!condition) fail(message); log('ok   ' + message); };

await wait(2000);
const root = store.get().project.root;
const tree = await session.openDocument(root + '/UI/screens/skill_tree.rml');
await wait(800);

// The recipe passes --a11y, so its checks find nothing to warn about.
let checks = store.get().checks;
expect(Array.isArray(checks) && !checks.some((entry) => entry.severity !== 'info'), 'the recipe has no accessibility or performance warnings');

const reader = engine.accessibilityTree();
const names = [];
const walk = (node) => { names.push(node.name); node.children.forEach(walk); };
walk(reader);
expect(names.includes('Toughness') && names.includes('Bulwark'), 'the screen reader reads every skill by name');
commands.runCommand('panel:reader');
await wait(400);
expect(document.querySelectorAll('.reader-row').length > 7, 'and the Screen Reader panel lists them');
await capture(studio.shots + '/studio-reader.png');

// A document with an unnamed control and faint text: the checks must report both.
const bad = root + '/UI/screens/bad.rml';
await host.writeFile(bad, '<rml><head><style>body { width: 100%; height: 100%; background-color: #202020; nav: auto; } .faint { color: #2a2a2a; font-size: 16dp; } .icon { display: block; tab-index: auto; nav: auto; width: 40dp; height: 40dp; background-color: #888888; }</style></head><body><p class="faint" data-noloc>Hard to read</p><div class="icon"></div></body></rml>');
await wait(1000);
await session.openDocument(bad);
await wait(800);
checks = store.get().checks;
expect(checks.some((entry) => entry.code === 'W-A11Y-CONTRAST'), 'faint text is reported for its contrast');
expect(checks.some((entry) => entry.code === 'W-A11Y-NONAME'), 'a control with no name is reported');
commands.runCommand('panel:problems');
await wait(400);
expect([...document.querySelectorAll('.problem .dim')].some((row) => row.textContent.includes('W-A11Y-NONAME')), 'and Problems lists them');
await capture(studio.shots + '/studio-problems.png');

// Player settings reach the renderer.
session.setPlayer({ textScale: 1.5 });
await wait(400);
expect(store.get().documentOpen, 'the document reopens at 150% text');
session.setPlayer({ inputDevice: 'gamepad', vision: 'deuteranopia' });
await wait(400);
const surface = document.querySelector('.canvas-surface');
expect(surface && surface.style.filter.includes('vision-deuteranopia'), 'colour vision draws the canvas through its filter');
const themes = await session.discoverThemes();
expect(themes.includes('high-contrast'), 'the shipped high-contrast theme is offered');

await session.openDocument(root + '/UI/screens/skill_tree.rml');
await wait(800);
commands.runCommand('panel:canvas');
await wait(200);
document.querySelector('.player-picker .button').click();
await wait(600);
expect(document.querySelector('.player-popover'), 'the Player button opens its settings');
expect(document.querySelector('.player-picker .button').textContent.includes('3'), 'and counts the three changed settings');
await capture(studio.shots + '/studio-player.png');
session.setPlayer({ textScale: 1, inputDevice: 'keyboard', vision: 'none' });
await wait(400);

// The flashing check plays the document and reports what it found.
session.checkFlashing();
const flash = store.get().flash;
expect(flash && flash.result.code.includes('A11Y-FLASH'), 'Check for Flashing reports a result');
log('     ' + flash.result.message);
