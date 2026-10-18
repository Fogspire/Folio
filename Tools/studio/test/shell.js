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
expect(document.querySelectorAll('.dv-default-tab').length === studio.dock.PANEL_IDS.length && !!document.querySelector('.canvas-panel canvas'), 'Reset Layout restores every panel');

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
// On a Mac, About lives in the app menu instead.
const onMac = navigator.platform.startsWith('Mac');
expect((await host.testMenuChecked(['Help', 'About Folio Studio']) !== null) !== onMac, onMac ? 'and About is not in Help' : 'and Help > About');
await wait(500);

// Keys: the defaults, walking the tree, and giving a command a key of its own.
const tabCount = () => document.querySelectorAll('.dv-default-tab').length;
[...document.querySelectorAll('.dv-default-tab')].find((entry) => entry.textContent.trim() === 'Docs').querySelector('.dv-default-tab-action').click();
await wait(300);
key(document.body, { key: 'F10' });
await wait(500);
expect(tabCount() === studio.dock.PANEL_IDS.length, 'F10 resets the layout');
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
const made = await host.createShortcuts(studio.shots);
expect(Array.isArray(made) && made.length === 1, 'File > Create Shortcuts makes one: ' + made);

// Whatever playing did to the document is gone on the way back to editing.
session.setMode('play');
engine.setInlineProperty(quitId, 'margin-left', '80dp');
session.refresh();
const playedRect = find('id', 'quit').rect[0];
session.setMode('edit');
await wait(300);
expect(find('id', 'quit').rect[0] !== playedRect && !('margin-left' in engine.inspect(quitId).inline), 'leaving Play puts the document back as the source has it');

// A document without a declared size opens at its own last size, not the last document's.
session.setViewport(1280, 720, null);
const fadePath = store.get().project.root + '/UI/screens/fade.rml';
await host.writeFile(fadePath, '<rml><head><style>@keyframes fade { from { opacity: 0; } to { opacity: 1; } } body { width: 100%; height: 100%; } #fade { width: 100dp; height: 100dp; background-color: #ffffff; animation: 1s linear fade; }</style></head><body><div id="fade"></div></body></rml>');
await wait(1000);
await session.openDocument(fadePath);
await wait(300);
expect(store.get().width === 1920 && store.get().height === 1080, 'a document with no size of its own opens at the default');
await session.openDocument(path);
expect(store.get().width === 1280 && store.get().height === 720, 'and one shown at another size before comes back at it');
session.setViewport(1920, 1080, null);

// Baking an element writes it alone to a PNG.
const baked = store.get().project.root + '/UI/art/quit.png';
await edit.insertNear('<div id="tile" style="width: 64dp; height: 32dp; background-color: #ff0000;"></div>');
await session.bakeElement(find('id', 'tile').id, baked, 2);
const bakedData = await host.readFile(baked);
expect(!!bakedData && bakedData[1] === 0x50 && bakedData[2] === 0x4e && bakedData[3] === 0x47, 'Bake to PNG writes a PNG: ' + store.get().status);
const bakedWidth = (bakedData[16] << 24) | (bakedData[17] << 16) | (bakedData[18] << 8) | bakedData[19];
expect(bakedWidth === 64, 'cropped to the element, drawn at twice the pixels and averaged back to its size: ' + bakedWidth);
commands.runCommand('undo');
await edit.insertNear('<div id="shadowed" style="width: 64dp; height: 32dp; box-shadow: #000000 0 0 8dp;"></div>');
const refused = store.get().project.root + '/UI/art/shadowed.png';
await session.bakeElement(find('id', 'shadowed').id, refused);
expect(!(await host.exists(refused)), 'a layer effect is refused rather than baked wrong');
commands.runCommand('undo');

// The timeline holds the clock, plays it, and goes back.
await session.openDocument(fadePath);
await wait(300);
const backed = store.get().project.root + '/UI/art/backed.png';
await session.bakeElement('', backed, 1, '#808080');
const backedImage = await createImageBitmap(new Blob([await host.readFile(backed)], { type: 'image/png' }));
const backedCanvas = new OffscreenCanvas(backedImage.width, backedImage.height);
backedCanvas.getContext('2d').drawImage(backedImage, 0, 0);
const corner = backedCanvas.getContext('2d').getImageData(1, 1, 1, 1).data;
expect(corner[3] === 255 && Math.abs(corner[0] - 128) <= 1, 'a background fills what the element leaves transparent: ' + [...corner].join());
const wholeTarget = store.get().project.root + '/UI/art/whole.png';
await session.bakeElement('', wholeTarget);
const wholeData = await host.readFile(wholeTarget);
expect(!!wholeData && ((wholeData[16] << 24) | (wholeData[17] << 16) | (wholeData[18] << 8) | wholeData[19]) === store.get().width, 'a whole document bakes at its size');
const fadeId = find('id', 'fade').id;
const opacity = () => Number(engine.inspect(fadeId, ['opacity']).computed.opacity);
key(document.body, { key: 'T', code: 'KeyT', ctrlKey: true, shiftKey: true });
await wait(100);
expect(!!store.get().timeline && !!document.querySelector('.timeline .timeline-scrub'), 'Ctrl+Shift+T opens the timeline');
session.setTimelinePlaying(false);
session.seekTimeline(0.5);
expect(Math.abs(opacity() - 0.5) < 0.02, 'scrubbing forward shows the animation at that moment: ' + opacity().toFixed(2));
session.seekTimeline(0.25);
expect(Math.abs(opacity() - 0.25) < 0.02, 'and back: ' + opacity().toFixed(2));
session.seekTimeline(0);
session.setTimelinePlaying(true);
await wait(700);
expect(store.get().timeline.time > 0.3, 'Play runs it on: ' + store.get().timeline.time.toFixed(2));
await wait(2000);
expect(!store.get().timeline.playing && store.get().timeline.time === store.get().settings.timelineDuration && opacity() > 0.95, 'and it stops at the end');
commands.runCommand('toggle-timeline');
expect(store.get().timeline === null, 'Timeline again closes it');
await session.openDocument(path);

// Ctrl+T finds a file from part of its name, and Enter opens the one chosen.
key(document.body, { key: 't', code: 'KeyT', ctrlKey: true });
await wait(100);
const goTo = document.querySelector('.go-to-file input');
expect(store.get().dialog?.kind === 'go-to-file' && document.activeElement === goTo, 'Ctrl+T opens Go to File with the caret in it');
Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(goTo, 'fad');
goTo.dispatchEvent(new Event('input', { bubbles: true }));
await wait(100);
const goToRows = [...document.querySelectorAll('.go-to-file-row')];
expect(goToRows[0]?.classList.contains('selected') && goToRows[0].querySelector('.name').textContent === 'fade.rml', 'typing part of a name puts that file first');
key(goTo, { key: 'ArrowDown' });
key(goTo, { key: 'ArrowUp' });
await wait(50);
expect(document.querySelector('.go-to-file-row.selected') === document.querySelector('.go-to-file-row'), 'the arrows move the choice and come back');
key(goTo, { key: 'Enter' });
await wait(500);
expect(store.get().dialog === null && store.get().document === fadePath, 'Enter opens it');
await session.openDocument(path);
session.closeFiles([fadePath]);
await wait(300);

// Middle drag moves the document, even when it fits and there is nothing to scroll.
commands.runCommand('zoom-fit');
await wait(100);
const canvasViewport = document.querySelector('.canvas-viewport');
const stageBefore = document.querySelector('.canvas-stage').getBoundingClientRect();
canvasViewport.dispatchEvent(new MouseEvent('mousedown', { button: 1, buttons: 4, clientX: 100, clientY: 100, bubbles: true, cancelable: true }));
window.dispatchEvent(new MouseEvent('mousemove', { buttons: 4, clientX: 140, clientY: 125, bubbles: true }));
window.dispatchEvent(new MouseEvent('mouseup', { button: 1, clientX: 140, clientY: 125, bubbles: true }));
await wait(100);
const stageAfter = document.querySelector('.canvas-stage').getBoundingClientRect();
expect(Math.round(stageAfter.left - stageBefore.left) === 40 && Math.round(stageAfter.top - stageBefore.top) === 25, 'middle drag pans the canvas');
commands.runCommand('zoom-fit');
await wait(100);
expect(Math.round(document.querySelector('.canvas-stage').getBoundingClientRect().left) === Math.round(stageBefore.left), 'Zoom to Fit puts it back');

// Revert puts unsaved files back as they are on disk, and undo brings the changes back.
const onDisk = new TextDecoder().decode(await host.readFile(path));
await edit.insertNear('<div id="unsaved">Unsaved</div>');
expect(!!store.get().dirty[path], 'an edit leaves the file unsaved');
await session.revertAll();
expect(model.getValue() === onDisk && !store.get().dirty[path], 'Revert puts it back as it is on disk');
commands.runCommand('undo');
await wait(200);
expect(count('id="unsaved"') === 1 && !!store.get().dirty[path], 'and undo brings the change back');
commands.runCommand('undo');
await session.revertAll();

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
expect(document.querySelectorAll('.dv-default-tab').length === studio.dock.PANEL_IDS.length - 1, 'a middle-click closes a panel tab');
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

// The bar along the bottom explains what is under the pointer, and every status message is kept.
document.querySelector('.topbar .segmented button').dispatchEvent(new MouseEvent('mouseover', { bubbles: true }));
await wait(100);
expect(document.querySelector('.hintbar .hint').textContent === 'Select and edit elements', 'hovering a control shows its explanation in the hint bar: ' + store.get().hint);
store.set({ status: 'first message' });
store.set({ status: 'second message' });
commands.runCommand('show-messages');
await wait(100);
const logged = [...document.querySelectorAll('.dialog-body.messages li')].map((item) => item.textContent);
expect(logged[0].endsWith('second message') && logged.some((line) => line.endsWith('first message')), 'Messages lists the status messages, newest first');
document.querySelector('.dialog footer .button:last-child').click();
await wait(50);
expect(store.get().messages.length === 0, 'and Clear empties it');
store.set({ dialog: null });
store.set({ status: 'cleared by the next action' });
document.body.dispatchEvent(new MouseEvent('mousedown', { bubbles: true }));
expect(store.get().status === '', 'the next click clears the last message');
store.set({ status: 'cleared by a key' });
document.body.dispatchEvent(new KeyboardEvent('keydown', { key: 'Shift', bubbles: true }));
expect(store.get().status === 'cleared by a key', 'a modifier alone leaves it');
document.body.dispatchEvent(new KeyboardEvent('keydown', { key: 'a', bubbles: true }));
expect(store.get().status === '', 'and any other key clears it');
store.set({ status: 'goes away' });
await wait(8300);
expect(store.get().status === '', 'a message leaves the top bar after a few seconds');
store.set({ status: 'Working…' });
await wait(8300);
expect(store.get().status === 'Working…', 'but one for work in progress stays until replaced');
store.set({ status: '' });
