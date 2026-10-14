// Drives the parts of the app around the editing commands: menus, keys, panels, zoom and the library.
const { store, session, edit, commands, engine, host, log, wait } = studio;
const fail = (message) => { throw new Error(message); };
const expect = (condition, message) => { if (!condition) fail(message); log('ok   ' + message); };

await wait(2000);
const path = store.get().document;
const model = session.peekModel(path);
const original = model.getValue();
const count = (needle) => model.getValue().split(needle).length - 1;
const key = (target, init) => target.dispatchEvent(new KeyboardEvent('keydown', { bubbles: true, cancelable: true, ...init }));
const find = (name, value, node = store.get().tree) => {
	if (node.attributes[name] === value) return node;
	for (const child of node.children) {
		const found = find(name, value, child);
		if (found) return found;
	}
	return null;
};

// An element added from the palette is undone from the keyboard wherever the focus was left.
await edit.insertNear('<div id="added">Added</div>');
expect(count('id="added"') === 1 && store.get().selection !== null, 'palette insert lands selected');
document.querySelector('.library') ?? commands.runCommand('panel:library');
await wait(100);
key(document.body, { key: 'z', ctrlKey: true });
expect(model.getValue() === original, 'Ctrl+Z undoes it with the focus outside the canvas');
expect(find('id', 'added') === null, 'and the canvas follows');
key(document.body, { key: 'z', ctrlKey: true, shiftKey: true });
expect(count('id="added"') === 1 && find('id', 'added') !== null, 'Ctrl+Shift+Z redoes it');

// Typing in a text field keeps undo for the typing.
session.select(find('id', 'added').id);
commands.runCommand('panel:inspector');
await wait(300);
const field = document.querySelector('.inspector input');
field.focus();
field.dispatchEvent(new InputEvent('input', { bubbles: true }));
key(field, { key: 'z', ctrlKey: true });
expect(count('id="added"') === 1, 'Ctrl+Z in a field being typed in leaves the document alone');
field.blur();

expect(await host.testMenu(['Edit', 'Undo']), 'the menu bar has Edit > Undo');
await wait(200);
expect(model.getValue() === original, 'which undoes the document');

// Clipboard, through the same commands the menus run.
session.select(find('id', 'quit').id);
commands.runCommand('copy');
commands.runCommand('paste');
await wait(300);
expect(count('id="quit"') === 2, 'copy then paste adds the element after itself');
commands.runCommand('undo');
session.select(find('id', 'quit').id);
commands.runCommand('cut');
await wait(300);
expect(count('id="quit"') === 0, 'cut removes it');
commands.runCommand('undo');
expect(model.getValue() === original, 'and undo restores it');

// A closed panel comes back from the View menu, the canvas included.
const tab = [...document.querySelectorAll('.dv-default-tab')].find((entry) => entry.textContent.trim() === 'Canvas');
tab.querySelector('.dv-default-tab-action').click();
await wait(300);
expect(!document.querySelector('.canvas-panel'), 'closing the Canvas tab removes the panel');
expect(await host.testMenu(['View', 'Canvas']), 'the menu bar has View > Canvas');
await wait(500);
expect(!!document.querySelector('.canvas-panel canvas') && store.get().documentOpen, 'View > Canvas brings it back with the document');
await edit.insertNear('<div id="after">After</div>');
expect(find('id', 'after') !== null, 'and it still edits');
commands.runCommand('undo');
commands.runCommand('reset-layout');
await wait(500);
expect(document.querySelectorAll('.dv-default-tab').length === 10 && !!document.querySelector('.canvas-panel canvas'), 'Reset Layout restores every panel');

// A layout is saved to a file and loaded back.
const layoutFile = studio.shots + '/narrow.json';
const names = () => [...document.querySelectorAll('.dv-default-tab')].map((entry) => entry.textContent.trim()).sort().join();
const allPanels = names();
[...document.querySelectorAll('.dv-default-tab')].find((entry) => entry.textContent.trim() === 'Docs').querySelector('.dv-default-tab-action').click();
await wait(300);
const withoutDocs = names();
expect(await host.saveLayout(studio.dock.serialiseLayout(), layoutFile) === 'narrow', 'a layout is saved under the name given');
commands.runCommand('reset-layout');
await wait(500);
expect(names() === allPanels, 'the default layout has every panel again');
commands.runCommand('load-layout', layoutFile);
await wait(700);
expect(names() === withoutDocs && !!document.querySelector('.canvas-panel canvas') && store.get().documentOpen, 'loading the layout brings back the saved arrangement, canvas intact');
await host.writeFile(store.get().project.root + '/UI/not-a-layout.json', '{"no": "layout"}');
commands.runCommand('load-layout', store.get().project.root + '/UI/not-a-layout.json');
await wait(700);
expect(names() === allPanels && !!document.querySelector('.canvas-panel canvas'), 'a file that is not a layout leaves the default one');
expect(await host.testMenu(['View', 'Layout', 'Reset Layout']), 'the menu bar has View > Layout');
expect(await host.testMenuChecked(['Help', 'About Folio Studio']) !== null, 'and Help > About');
await wait(500);

// Keys: the defaults, walking the tree, and giving a command a key of its own.
const tabCount = () => document.querySelectorAll('.dv-default-tab').length;
[...document.querySelectorAll('.dv-default-tab')].find((entry) => entry.textContent.trim() === 'Docs').querySelector('.dv-default-tab-action').click();
await wait(300);
key(document.body, { key: 'F10' });
await wait(500);
expect(tabCount() === 10, 'F10 resets the layout');
expect(await host.testMenuKey(['View', 'Layout', 'Reset Layout']) === 'F10', 'and the menu shows the key');

const surface = document.querySelector('.canvas-panel [data-element-keys]');
surface.focus();
const quitId = find('id', 'quit').id;
const up = quitId.slice(0, quitId.lastIndexOf('/'));
session.select(quitId);
key(surface, { key: 'ArrowUp', altKey: true });
expect(store.get().selection === up, 'Alt+Up selects the parent');
key(surface, { key: 'ArrowDown', altKey: true });
expect(store.get().selection === up + '/0', 'Alt+Down selects the first child');
key(surface, { key: 'ArrowRight', altKey: true });
expect(store.get().selection === up + '/1', 'Alt+Right selects the next sibling');
key(surface, { key: 'ArrowLeft', altKey: true });
key(surface, { key: 'ArrowLeft', altKey: true });
expect(store.get().selection === up + '/0', 'Alt+Left selects the previous one, and stops at the first');

expect(await host.testMenu(['View', 'Zoom In'], { ctrlKey: true, shiftKey: true }), 'a Ctrl+Shift+click on a menu item');
await wait(300);
expect(store.get().dialog?.kind === 'assign' && store.get().dialog.command === 'zoom-in' && store.get().zoom === 'fit', 'asks for its key instead of running it');
window.dispatchEvent(new KeyboardEvent('keydown', { key: 'F9', bubbles: true, cancelable: true }));
await wait(300);
expect(store.get().dialog === null && store.get().settings.keys['zoom-in'].join() === 'F9', 'the key pressed becomes its shortcut');
expect(await host.testMenuKey(['View', 'Zoom In']) === 'F9', 'the menu shows it');
key(document.body, { key: 'F9' });
expect(store.get().zoom !== 'fit', 'and it runs the command');
key(document.body, { key: '0', ctrlKey: true });
key(document.body, { key: '=', ctrlKey: true });
expect(store.get().zoom === 'fit', 'while the key it had before no longer does');
commands.runCommand('assign-key', 'zoom-in');
await wait(200);
window.dispatchEvent(new KeyboardEvent('keydown', { key: 'F10', bubbles: true, cancelable: true }));
await wait(200);
expect(store.get().settings.keys['reset-layout'].length === 0, 'a key already in use is taken from the command that had it');
commands.runCommand('keybindings');
await wait(300);
expect(document.querySelectorAll('.key-table tr').length > 30 && !!document.querySelector('tr[data-command="zoom-in"] .key'), 'Keyboard Shortcuts lists every command with its key');
[...document.querySelectorAll('tr[data-command="zoom-in"] button')].find((button) => button.textContent === 'Remove').click();
await wait(200);
expect(store.get().settings.keys['zoom-in'].length === 0, 'and removes one');
session.updateSettings({ keys: {} });
key(document.body, { key: 'Escape' });
await wait(200);
expect(store.get().dialog === null && await host.testMenuKey(['View', 'Zoom In']) === 'CmdOrCtrl+=', 'restoring the defaults puts the menu back');
commands.runCommand('preferences');
await wait(300);
expect(document.querySelectorAll('.dialog .field-row').length === 4 && document.querySelector('.dialog .field-row input').placeholder.length > 0, 'Preferences opens, and names the shell it would use');
key(document.body, { key: 'Escape' });
expect(await host.testMenuChecked(['Help', 'Folio on GitHub']) !== null, 'Help links to the repository');

// The terminal is a real shell in the project's folder.
let heard = '';
const stopHearing = host.onTerminalData((_id, data) => { heard += data; });
commands.runCommand('panel:terminal');
await wait(2500);
expect(!!document.querySelector('.terminal-panel .xterm') && !document.querySelector('.terminal-error'), 'the Terminal panel starts a shell');
const shellId = await host.terminalOpen(store.get().project.root, 80, 24);
await wait(1500);
heard = '';
host.terminalInput(shellId, 'echo folio-$((40+2))-$((40+2))' + String.fromCharCode(13));
await wait(1500);
expect(typeof shellId === 'number' && heard.includes('folio-42-42'), 'a shell runs what is typed into it');
host.terminalClose(shellId);
const shellInput = document.querySelector('.terminal-panel textarea');
shellInput.focus();
const beforeKey = model.getValue();
await edit.insertNear('<div id="typed">Typed</div>');
key(shellInput, { key: 'z', ctrlKey: true });
expect(model.getValue() !== beforeKey, 'and Ctrl+Z in the terminal goes to the shell, not to undo');
commands.runCommand('undo');
stopHearing();
commands.runCommand('panel:code');
await wait(300);

// Ctrl+wheel zooms about the pointer.
const viewport = document.querySelector('.canvas-viewport');
const stage = () => document.querySelector('.canvas-stage').getBoundingClientRect();
const before = stage();
const pointer = { clientX: before.left + before.width * 0.25, clientY: before.top + before.height * 0.25 };
const fraction = () => [(pointer.clientX - stage().left) / stage().width, (pointer.clientY - stage().top) / stage().height];
for (let i = 0; i < 8; i++) viewport.dispatchEvent(new WheelEvent('wheel', { ...pointer, deltaY: -100, ctrlKey: true, bubbles: true, cancelable: true }));
await wait(300);
expect(typeof store.get().zoom === 'number' && stage().width > before.width * 2, 'Ctrl+wheel zooms in: ' + store.get().zoom.toFixed(2));
expect(Math.abs(fraction()[0] - 0.25) < 0.02 && Math.abs(fraction()[1] - 0.25) < 0.02, 'keeping the point under the pointer where it was');
await wait(400);
const sharp = store.get().renderScale;
expect(sharp > 1 && engine.canvas.width === Math.round(store.get().width * sharp), 'the renderer draws at the zoomed resolution: ' + sharp);
const whole = store.get().tree.rect;
expect(Math.abs(whole[2] - store.get().width) < 0.5 && Math.abs(whole[3] - store.get().height) < 0.5, 'and still reports the document in its own pixels');
const quit = find('id', 'quit');
const middle = [quit.rect[0] + quit.rect[2] / 2, quit.rect[1] + quit.rect[3] / 2];
expect(engine.hitTest(middle[0], middle[1]).includes(quit.id), 'with a point still landing on the element drawn there');
viewport.dispatchEvent(new WheelEvent('wheel', { ...pointer, deltaY: 100, bubbles: true, cancelable: true }));
const zoomed = store.get().zoom;
expect(store.get().zoom === zoomed, 'a wheel without Ctrl scrolls and does not zoom');
key(document.body, { key: '0', ctrlKey: true });
expect(store.get().zoom === 'fit', 'Ctrl+0 fits');
await wait(400);
expect(store.get().renderScale < sharp, 'and the renderer goes back to fewer pixels');

// Several documents stay open as tabs, and the canvas shows whichever is in front.
const second = store.get().project.root + '/UI/screens/second.rml';
await host.writeFile(second, original.replace('<body', '<body id="second-body"'));
await wait(1000);
await session.openDocument(second);
await wait(500);
const tabs = (panel) => [...document.querySelectorAll(panel + ' .code-tab')];
const tabNamed = (panel, name) => tabs(panel).find((entry) => entry.textContent.includes(name));
expect(store.get().document === second && store.get().tree.attributes.id === 'second-body', 'a second document opens on the canvas');
expect(tabs('.canvas-panel').length === 2 && tabs('.code-panel').length === 2, 'and both documents have a tab over the canvas and in Code');
tabNamed('.code-panel', 'test.rml').click();
await wait(500);
expect(store.get().document === path && store.get().tree.attributes.id !== 'second-body', 'clicking a document tab in Code puts it on the canvas');
tabNamed('.canvas-panel', 'second.rml').click();
await wait(500);
expect(store.get().document === second && store.get().activeFile === second, 'and a tab over the canvas does the same, Code following');
tabNamed('.canvas-panel', 'second.rml').dispatchEvent(new MouseEvent('auxclick', { bubbles: true, button: 1 }));
await wait(500);
expect(!store.get().openFiles.includes(second) && store.get().document === path && store.get().documentOpen, 'a middle-click closes a tab, and the canvas falls back to the other document');
session.closeFiles([path]);
await wait(300);
expect(store.get().document === null && !store.get().documentOpen && !!document.querySelector('.canvas-empty'), 'closing the last document leaves the canvas empty');
// New files, and tabs that show an image or a font.
const screens = store.get().project.root + '/UI/screens';
const fresh = await session.createFile(screens, 'fresh', '.rml');
await wait(500);
expect(fresh === screens + '/fresh.rml' && store.get().document === fresh && store.get().documentOpen, 'a new document is created, and opens on the canvas');
expect(await session.createFile(screens, 'fresh.rml', '.rml') === null, 'a name already taken is refused');
const sheet = await session.createFile(screens, 'look', '.rcss');
expect(sheet === screens + '/look.rcss' && store.get().activeFile === sheet, 'a new stylesheet opens in Code');
const fromRecipe = await session.copyLibraryDocument(store.get().libraryFiles.find((file) => file.endsWith('/recipes/confirm.rml')), 'ask', screens + '/dialogs');
await wait(500);
expect(fromRecipe === screens + '/dialogs/ask.rml' && store.get().document === fromRecipe && store.get().documentOpen, 'a recipe is copied into the folder asked for');
const dot = store.get().project.root + '/UI/art/dot.png';
await host.writeFile(dot, Uint8Array.from(atob('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg=='), (c) => c.charCodeAt(0)));
await session.openFile(dot);
commands.runCommand('panel:code');
await wait(600);
expect(document.querySelector('.asset-preview img')?.naturalWidth === 1, 'an image opens as a picture');
await session.openFile(store.get().project.folioRoot + '/fonts/Inter-Variable.ttf');
await wait(600);
expect(document.querySelectorAll('.asset-preview .specimen').length === 5, 'a font opens as a specimen');
expect(await session.deleteFile(dot) && !(await host.exists(dot)) && !store.get().openFiles.includes(dot) && !store.get().projectFiles.includes(dot), 'deleting a file moves it to the trash and closes its tab');
tabNamed('.code-panel', 'look.rcss').click();
await wait(200);
expect(!document.querySelector('.asset-preview') && store.get().activeFile === sheet, 'and a text tab is the editor again');

await session.openDocument(second);
await session.openDocument(path);
await wait(500);
session.closeFiles(store.get().openFiles.filter((file) => file !== path));
expect(store.get().openFiles.length === 1 && store.get().document === path && store.get().documentOpen, 'closing the other tabs keeps this one on the canvas');

// A panel's tab closes on a middle-click too.
const docsTab = [...document.querySelectorAll('.dv-default-tab')].find((entry) => entry.textContent.trim() === 'Docs');
docsTab.dispatchEvent(new MouseEvent('auxclick', { bubbles: true, button: 1 }));
await wait(300);
expect(document.querySelectorAll('.dv-default-tab').length === 9, 'a middle-click closes a panel tab');
commands.runCommand('reset-layout');
await wait(500);

// The menu shows whether the startup check is on.
const startup = ['Help', 'Check for Updates at Startup'];
expect(await host.testMenuChecked(startup) === session.updateCheckEnabled(), 'the menu marks the startup update check as it is set');
const was = session.updateCheckEnabled();
commands.runCommand('toggle-update-check');
await wait(200);
expect(await host.testMenuChecked(startup) === !was, 'and the mark follows the setting');
commands.runCommand('toggle-update-check');
await wait(200);

// A recipe previews with a banner, and Use copies it into the project and opens the copy.
const recipe = store.get().libraryFiles.find((file) => file.endsWith('/recipes/pause.rml'));
await session.openDocument(recipe);
await wait(500);
const banner = document.querySelector('.library-banner');
expect(!!banner, 'a recipe on the canvas says it is a preview');
banner.querySelector('button').click();
await wait(1500);
const created = store.get().project.root + '/UI/screens/pause.rml';
expect(store.get().document === created && await host.exists(created), 'Use copies it to UI/screens and opens the copy');
expect(!document.querySelector('.library-banner') && store.get().documentOpen, 'which is then an ordinary document');
