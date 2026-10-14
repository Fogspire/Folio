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
const at = (x, y) => ({ clientX: bounds.left + x * scale, clientY: bounds.top + y * scale, button: 0, bubbles: true });
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
