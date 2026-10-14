// Drives the visual editing commands against a scratch copy of a recipe and checks the text they leave.
const { store, session, edit, host, log, wait, capture } = studio;
const fail = (message) => { throw new Error(message); };
const expect = (condition, message) => { if (!condition) fail(message); log('ok   ' + message); };

await wait(2000);
const path = store.get().document;
const model = session.peekModel(path);
const original = model.getValue();
const text = () => model.getValue();

const byAttribute = (name, value, node = store.get().tree) => {
	if (node.attributes[name] === value || (name === 'class' && (node.attributes.class || '').split(' ').includes(value))) return node;
	for (const child of node.children) {
		const found = byAttribute(name, value, child);
		if (found) return found;
	}
	return null;
};
const idOf = (name, value) => byAttribute(name, value).id;

expect(store.get().documentOpen, 'document opened');
expect(edit.isEditable(idOf('id', 'new_game')), 'a button is editable');

session.select(idOf('id', 'new_game'));
await edit.setElementText(idOf('id', 'new_game'), 'Begin & Play');
expect(text().includes('>Begin &amp; Play</button>'), 'text edit lands escaped');
expect(byAttribute('id', 'new_game').text.includes('Begin & Play'), 'canvas shows the new text');
expect(store.get().selection === idOf('id', 'new_game'), 'selection survives a text edit');

// A value that a rule of the document already sets is changed where it is written.
session.select(idOf('class', 'menu-shell'));
let inspection = store.get().inspection;
let target = edit.defaultTarget(inspection, 'width');
expect(target.kind === 'rule' && target.rule === '.menu-shell', 'width targets the rule that sets it');
await edit.setStyle(inspection.id, 'width', '520dp', target);
expect(text().includes('.menu-shell { width: 520dp; }'), 'rule value changed in place');
expect(Math.round(byAttribute('class', 'menu-shell').rect[2]) === 520, 'canvas reflects the new width');

// A property nothing of the project's sets yet goes to a new rule in the document's own style block.
session.select(idOf('id', 'continue'));
inspection = store.get().inspection;
target = edit.defaultTarget(inspection, 'margin-top');
expect(target.kind === 'new-rule' && target.selector === '#continue', 'a shipped rule is never the target: ' + JSON.stringify(target));
await edit.setStyle(inspection.id, 'margin-top', '12dp', target);
expect(/#continue \{ margin-top: 12dp; \}\s*<\/style>/.test(text()), 'new rule appended inside the style block');

// Now that rule exists, it is the target for the next property.
session.select(idOf('id', 'continue'));
inspection = store.get().inspection;
target = edit.defaultTarget(inspection, 'opacity');
expect(target.kind === 'rule' && target.rule === '#continue', 'the new rule becomes the target');
await edit.setStyle(inspection.id, 'opacity', '0.9', target);
expect(text().includes('#continue { margin-top: 12dp; opacity: 0.9; }'), 'second property joins the same rule');

// Reorder: Quit to the top of its stack.
const stack = idOf('class', 'btn-stack');
await edit.moveInto(idOf('id', 'quit'), stack, 0);
expect(text().indexOf('id="quit"') < text().indexOf('id="continue"'), 'element moved before its siblings');
expect(byAttribute('class', 'btn-stack').children[0].attributes.id === 'quit', 'canvas order follows');
expect(store.get().selection === idOf('id', 'quit'), 'the moved element is selected');

await edit.insertInto(stack, 1, '<button class="btn focusable">Inserted</button>');
expect(byAttribute('class', 'btn-stack').children[1].text === 'Inserted', 'snippet inserted at index');
expect(/\n\t\t\t<button class="btn focusable">Inserted<\/button>\n/.test(text()), 'inserted at the siblings indent');

await edit.duplicate(store.get().selection);
expect(byAttribute('class', 'btn-stack').children.filter((child) => child.text === 'Inserted').length === 2, 'duplicated');
await edit.deleteElement(store.get().selection);
expect(byAttribute('class', 'btn-stack').children.filter((child) => child.text === 'Inserted').length === 1, 'deleted');

await edit.setElementAttribute(idOf('id', 'load'), 'data-if', 'saves > 0');
expect(text().includes('data-if="saves > 0"') || text().includes('data-if="saves &gt; 0"'), 'attribute added');

// A drag on the canvas: the build label is pinned from the right and bottom, and stays so.
const build = byAttribute('class', 'build');
session.select(build.id);
await wait(100);
const overlay = document.querySelector('.overlay');
const bounds = overlay.getBoundingClientRect();
const scale = bounds.width / store.get().width;
const at = (x, y) => ({ clientX: bounds.left + x * scale, clientY: bounds.top + y * scale, button: 0, buttons: 1, bubbles: true });
const cx = build.rect[0] + build.rect[2] / 2;
const cy = build.rect[1] + build.rect[3] / 2;
overlay.dispatchEvent(new MouseEvent('mousedown', at(cx, cy)));
window.dispatchEvent(new MouseEvent('mousemove', at(cx - 30, cy - 20)));
window.dispatchEvent(new MouseEvent('mousemove', at(cx - 100, cy - 50)));
await wait(100);
await capture(studio.shots + '/studio-drag.png');
window.dispatchEvent(new MouseEvent('mouseup', at(cx - 100, cy - 50)));
await wait(300);
const rule = /\.build \{([^}]*)\}/.exec(text())[1];
log('     .build {' + rule + '}');
const right = Number(/right: (\d+)dp;/.exec(rule)?.[1]);
const bottom = Number(/bottom: (\d+)dp;/.exec(rule)?.[1]);
// A screen pixel is about two document pixels at this zoom, which is the grain a drag has.
expect(Math.abs(right - 124) <= 2 && Math.abs(bottom - 70) <= 2, 'drag moved right and bottom by the distance dragged, in whole dp');
expect(!/left:|top:/.test(rule), 'and did not pin the element from a second side');
const moved = byAttribute('class', 'build');
expect(Math.abs(moved.rect[0] - (build.rect[0] - 100)) <= 2.5 && Math.abs(moved.rect[1] - (build.rect[1] - 50)) <= 2.5, 'the element is where it was dropped');

// A resize writes each side where it is set: the width in its rule, the height in the style attribute.
const shell = idOf('class', 'menu-shell');
await edit.setStyle(shell, 'height', '400dp', { kind: 'inline' });
session.select(idOf('class', 'menu-shell'));
await wait(150);
const corner = document.querySelector('.handle-se').getBoundingClientRect();
const grab = (x, y) => ({ clientX: x, clientY: y, button: 0, buttons: 1, bubbles: true });
const [hx, hy] = [corner.left + corner.width / 2, corner.top + corner.height / 2];
document.querySelector('.handle-se').dispatchEvent(new MouseEvent('mousedown', grab(hx, hy)));
window.dispatchEvent(new MouseEvent('mousemove', grab(hx + 20 * scale, hy + 30 * scale)));
window.dispatchEvent(new MouseEvent('mouseup', grab(hx + 20 * scale, hy + 30 * scale)));
await wait(400);
expect(/\.menu-shell \{ width: 5(3[7-9]|4[0-3])dp; \}/.test(text()), 'resized width stays in the rule that sets it: ' + /\.menu-shell \{[^}]*\}/.exec(text())[0]);
expect(/class="menu-shell" style="height: 4(2[7-9]|3[0-3])dp;"/.test(text()), 'resized height lands in the style attribute that sets it: ' + /<div class="menu-shell"[^>]*>/.exec(text())[0]);

// Shift writes the size to this element alone, leaving the rule it shares untouched.
const ruleBefore = /\.menu-shell \{[^}]*\}/.exec(text())[0];
const corner2 = document.querySelector('.handle-se').getBoundingClientRect();
const [sx, sy] = [corner2.left + corner2.width / 2, corner2.top + corner2.height / 2];
document.querySelector('.handle-se').dispatchEvent(new MouseEvent('mousedown', { ...grab(sx, sy), shiftKey: true }));
window.dispatchEvent(new MouseEvent('mousemove', { ...grab(sx + 20 * scale, sy), shiftKey: true }));
window.dispatchEvent(new MouseEvent('mouseup', grab(sx + 20 * scale, sy)));
await wait(400);
expect(/\.menu-shell \{[^}]*\}/.exec(text())[0] === ruleBefore, 'a Shift resize leaves the shared rule alone');
expect(/class="menu-shell" style="height: 4(2[7-9]|3[0-3])dp; width: 5(5[6-9]|6[0-4])dp;"/.test(text()), 'and writes the width to the element: ' + /<div class="menu-shell"[^>]*>/.exec(text())[0]);

// With the preference off, Shift is ignored and the size goes to the rule as usual.
studio.session.updateSettings({ shiftResize: false });
await edit.setStyle(shell, 'width', null, { kind: 'inline' });
session.select(idOf('class', 'menu-shell'));
await wait(150);
const corner3 = document.querySelector('.handle-se').getBoundingClientRect();
const [tx, ty] = [corner3.left + corner3.width / 2, corner3.top + corner3.height / 2];
document.querySelector('.handle-se').dispatchEvent(new MouseEvent('mousedown', { ...grab(tx, ty), shiftKey: true }));
window.dispatchEvent(new MouseEvent('mousemove', { ...grab(tx + 20 * scale, ty), shiftKey: true }));
window.dispatchEvent(new MouseEvent('mouseup', grab(tx + 20 * scale, ty)));
await wait(400);
expect(/\.menu-shell \{ width: 5(5[6-9]|6[0-4])dp; \}/.test(text()) && !/class="menu-shell" style="[^"]*width/.test(text()), 'with Shift resizing turned off, Shift resizes through the rule as usual: ' + /\.menu-shell \{[^}]*\}/.exec(text())[0]);
studio.session.updateSettings({ shiftResize: true });

// A resize that lands in a rule other elements share says how many it changed.
await edit.setStyle(idOf('id', 'new_game'), 'height', '48dp', { kind: 'new-rule', selector: '.btn-wide' });
session.select(idOf('id', 'new_game'));
await wait(150);
const corner4 = document.querySelector('.handle-s').getBoundingClientRect();
const [ux, uy] = [corner4.left + corner4.width / 2, corner4.top + corner4.height / 2];
document.querySelector('.handle-s').dispatchEvent(new MouseEvent('mousedown', grab(ux, uy)));
window.dispatchEvent(new MouseEvent('mousemove', grab(ux, uy + 10 * scale)));
window.dispatchEvent(new MouseEvent('mouseup', grab(ux, uy + 10 * scale)));
await wait(600);
expect(/\.btn-wide \{ height: 5[789]dp; \}/.test(text()), 'the shared rule took the new height');
expect(store.get().status.startsWith('Resized 5 elements that share .btn-wide'), 'and the status bar says what else changed: ' + store.get().status);

// Shift-resizing one of them says it has left the shared rule.
const corner5 = document.querySelector('.handle-s').getBoundingClientRect();
const [vx, vy] = [corner5.left + corner5.width / 2, corner5.top + corner5.height / 2];
document.querySelector('.handle-s').dispatchEvent(new MouseEvent('mousedown', { ...grab(vx, vy), shiftKey: true }));
window.dispatchEvent(new MouseEvent('mousemove', { ...grab(vx, vy + 10 * scale), shiftKey: true }));
window.dispatchEvent(new MouseEvent('mouseup', grab(vx, vy + 10 * scale)));
await wait(600);
expect(store.get().status === 'Resized this element alone: its height no longer follows .btn-wide, which 4 other elements still share', 'a Shift resize says which shared rule it left: ' + store.get().status);

// A move with the button no longer held ends the gesture rather than dragging on.
session.select(idOf('class', 'build'));
await wait(150);
const before = text();
const label = byAttribute('class', 'build');
overlay.dispatchEvent(new MouseEvent('mousedown', at(label.rect[0] + 4, label.rect[1] + 4)));
window.dispatchEvent(new MouseEvent('mousemove', { ...at(label.rect[0] - 60, label.rect[1] - 60), buttons: 0 }));
window.dispatchEvent(new MouseEvent('mousemove', at(label.rect[0] - 90, label.rect[1] - 90)));
window.dispatchEvent(new MouseEvent('mouseup', at(label.rect[0] - 90, label.rect[1] - 90)));
await wait(300);
expect(text() === before, 'a lost release does not leave the element following the pointer');

session.select(idOf('id', 'settings'));
await wait(150);
await capture(studio.shots + '/studio-edit.png');

// Undo everything, and the text is exactly what it started as.
for (let i = 0; i < 40; i++) edit.undo();
expect(text() === original, 'undoing every edit restores the file byte for byte');
expect(Object.keys(store.get().dirty).length === 0, 'and it is no longer dirty');

edit.redo();
expect(text() !== original, 'redo reapplies');
await session.saveAll();
const saved = new TextDecoder().decode(await host.readFile(path));
expect(saved === text(), 'save writes the buffer to disk');
