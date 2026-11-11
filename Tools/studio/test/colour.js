// The Inspector's colour picker: it previews without writing, writes once on close, and Escape takes it back.
const { store, session, edit, commands, log, wait, capture } = studio;
const fail = (message) => { throw new Error(message); };
const expect = (condition, message) => { if (!condition) fail(message); log('ok   ' + message); };

await wait(2000);
const path = store.get().document;
const model = session.peekModel(path);
const original = model.getValue();
const text = () => model.getValue();

const byAttribute = (name, value, node = store.get().tree) => {
	if (node.attributes[name] === value) return node;
	for (const child of node.children) {
		const found = byAttribute(name, value, child);
		if (found) return found;
	}
	return null;
};

session.select(byAttribute('id', 'new_game').id);
commands.runCommand('panel:inspector');
await wait(500);

const well = () => [...document.querySelectorAll('.inspector .prop')].find((row) => row.querySelector('label')?.title === 'color')?.querySelector('.well');
const type = (input, value) => {
	Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(input, value);
	input.dispatchEvent(new Event('input', { bubbles: true }));
	input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
};
const outside = () => document.body.dispatchEvent(new MouseEvent('mousedown', { bubbles: true }));

expect(well(), 'the color row has a colour well');
well().scrollIntoView({ block: 'center' });
await wait(200);
well().click();
await wait(300);
const picker = document.querySelector('.colour-picker');
expect(picker, 'clicking it opens the picker');
expect(picker.querySelectorAll('.cp-channel').length === 4, 'with three channels and alpha');
await capture(studio.shots + '/studio-colour.png');

type(picker.querySelector('.cp-hex-field'), '3c8dbc');
await wait(300);
expect(text() === original, 'a change is previewed without writing the document');
outside();
await wait(600);
expect(!document.querySelector('.colour-picker'), 'clicking outside closes it');
expect(/#3c8dbc/i.test(text()), 'and writes the colour');
edit.undo();
await wait(300);
expect(text() === original, 'one undo takes the whole pick back');

well().click();
await wait(300);
type(document.querySelector('.colour-picker .cp-hex-field'), '#ff000080');
await wait(200);
document.querySelector('.colour-picker').dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
await wait(400);
expect(!document.querySelector('.colour-picker') && text() === original, 'Escape closes it without writing');

well().click();
await wait(300);
const alphaField = [...document.querySelectorAll('.colour-picker .cp-number')].find((field) => field.title === 'Alpha').querySelector('input');
type(alphaField, '50');
await wait(200);
outside();
await wait(600);
expect(/#[0-9a-f]{6}80/i.test(text()), 'alpha is written as the hex alpha pair');
edit.undo();
await wait(300);

well().click();
await wait(300);
const recent = document.querySelectorAll('.colour-picker .cp-section')[0].querySelectorAll('.cp-swatch');
expect(recent.length >= 2, 'the colours picked are offered as recent');
expect(document.querySelectorAll('.colour-picker .cp-section')[1].querySelectorAll('.cp-swatch').length > 0, 'and the document\'s own colours are offered');
await capture(studio.shots + '/studio-colour-recent.png');
outside();
await wait(300);
