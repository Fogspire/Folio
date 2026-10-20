// Drives completion, squiggles and formatting in the code editor against a scratch copy of a game document.
const { store, session, commands, monaco, host, log, wait, capture } = studio;
const expect = (condition, message) => { if (!condition) throw new Error(message); log('ok   ' + message); };
const read = async (path) => { const bytes = await host.readFile(path); return bytes ? new TextDecoder().decode(bytes) : null; };

await wait(2500);
const path = store.get().document;
const model = session.peekModel(path);
expect(store.get().documentOpen && !!model, 'the game document opened');
commands.runCommand('panel:code');
await wait(300);
const editor = monaco.editor.getEditors().find((entry) => entry.getModel() === model);
expect(!!editor, 'the code editor shows it');

/** Puts the cursor just after `marker`, opens completion and reads what it offers. */
const suggest = async (marker, typed = '') => {
	const offset = model.getValue().indexOf(marker) + marker.length;
	const position = model.getPositionAt(offset);
	editor.focus();
	editor.setPosition(position);
	if (typed) editor.trigger('test', 'type', { text: typed });
	editor.trigger('test', 'editor.action.triggerSuggest', {});
	await wait(800);
	const labels = [...document.querySelectorAll('.suggest-widget .monaco-list-row .label-name')].map((node) => node.textContent);
	editor.trigger('test', 'hideSuggestWidget', {});
	return labels;
};

let labels = await suggest('<div class="crumb" data-if="Page != 0">{{ ');
expect(labels.includes('Title') && labels.includes('Rows'), 'a binding offers the model\'s fields from its mock: ' + labels.slice(0, 6).join(', '));

labels = await suggest('<div class="name">{{ r.');
expect(labels.includes('Label') && labels.includes('Fraction'), 'a row offers its members: ' + labels.slice(0, 6).join(', '));

labels = await suggest('.row { ', 'jus');
expect(labels.includes('justify-content'), 'a style block offers properties: ' + labels.slice(0, 4).join(', '));
await capture(studio.shots + '/studio-intellisense.png');
model.setValue(model.getValue().replace('.row { jus', '.row { '));

labels = await suggest('<div class="panel', ' ');
expect(labels.includes('btn') || labels.includes('panel'), 'a class attribute offers classes from the linked library: ' + labels.slice(0, 6).join(', '));
model.setValue(model.getValue().replace('<div class="panel ', '<div class="panel'));

// What the text alone shows is wrong is underlined while typing.
model.setValue(model.getValue().replace('.row { display: flex; }', '.row { display: flex; text-shadow: none; }'));
await wait(600);
const markers = monaco.editor.getModelMarkers({ resource: model.uri, owner: 'folio-text' });
expect(markers.some((marker) => marker.message.includes('text-shadow')), 'a banned property is underlined: ' + markers.map((marker) => marker.message).join(' | '));

// Format Document puts a mangled line back, as one step Undo takes back.
const tidy = model.getValue();
model.setValue(tidy.replace('\t\t\t<div class="title">', '<div class="title">').replace('<div class="crumb" data-if', '<div class="crumb"   data-if'));
const mangled = model.getValue();
commands.runCommand('format-document');
await wait(200);
expect(model.getValue() === tidy, 'formatting restores the indentation');
editor.trigger('test', 'undo', null);
expect(model.getValue() === mangled, 'undo takes the formatting back');

// With format on type, Enter fixes the line it finishes and nothing else.
store.set({ settings: { ...store.get().settings, formatOnType: true } });
await wait(100);
model.setValue(tidy.replace('\t\t\t<div class="title">', '<div class="title">').replace('<div class="crumb" data-if', '<div class="crumb"   data-if'));
const titleLine = model.getValue().split('\n').findIndex((line) => line.startsWith('<div class="title">')) + 1;
editor.focus();
editor.setPosition({ lineNumber: titleLine, column: model.getLineMaxColumn(titleLine) });
editor.trigger('keyboard', 'type', { text: '\n' });
await wait(300);
expect(model.getLineContent(titleLine).startsWith('\t\t\t<div class="title">'), 'Enter formats the line it finishes');
expect(model.getValue().includes('<div class="crumb"   data-if'), 'and leaves the other lines alone');
editor.trigger('test', 'undo', null);
editor.trigger('test', 'undo', null);

// Typing > with blank lines to tidy elsewhere keeps the caret where it was.
model.setValue(tidy.replace('<head>', '<head>\n\t<title>T</title>\n') + '\n\n');
const headLine = model.getValue().split('\n').findIndex((line) => line.includes('<title>T</title>')) + 1;
editor.setPosition({ lineNumber: headLine, column: model.getLineMaxColumn(headLine) });
editor.trigger('keyboard', 'type', { text: '\n' });
for (const character of '<title>') editor.trigger('keyboard', 'type', { text: character });
await wait(300);
const caret = editor.getPosition();
expect(caret.lineNumber === headLine + 1 && model.getLineContent(caret.lineNumber).trim() === '<title>' && caret.column === model.getLineMaxColumn(caret.lineNumber), 'typing > leaves the caret after it: ' + JSON.stringify(model.getLineContent(caret.lineNumber)) + ' at ' + caret.column);
for (const character of 'X</title >') editor.trigger('keyboard', 'type', { text: character });
await wait(300);
expect(model.getLineContent(caret.lineNumber).trim() === '<title>X</title>', 'typing > tidies a closing tag: ' + JSON.stringify(model.getLineContent(caret.lineNumber)));

// The same with CRLF line ends, which most files in a Windows checkout have.
model.setValue(tidy.replace('<head>', '<head>\n\t\t<title>T</title>\n') + '\n\n');
model.setEOL(monaco.editor.EndOfLineSequence.CRLF);
const lineCount = model.getLineCount();
editor.setPosition({ lineNumber: headLine, column: model.getLineMaxColumn(headLine) });
editor.trigger('keyboard', 'type', { text: '\n' });
for (const character of '<title >') { editor.trigger('keyboard', 'type', { text: character }); await wait(30); }
await wait(300);
const crlf = editor.getPosition();
expect(model.getLineCount() === lineCount + 1 && crlf.lineNumber === headLine + 1 && model.getLineContent(crlf.lineNumber).trim() === '<title>' && crlf.column === model.getLineMaxColumn(crlf.lineNumber), 'with CRLF, typing > tidies the tag and leaves the caret after it: ' + JSON.stringify(model.getLineContent(crlf.lineNumber)) + ' at ' + crlf.column);
commands.runCommand('format-document');
expect(model.getLineCount() === lineCount - 1 && model.getEOL() === '\r\n', 'with CRLF, Format Document adds no lines and keeps CRLF');
model.setEOL(monaco.editor.EndOfLineSequence.LF);

// With format on focus lost, leaving the editor formats the file.
store.set({ settings: { ...store.get().settings, formatOnType: false, formatOnBlur: true } });
model.setValue(mangled);
editor.focus();
// The test window has no OS focus, so a real blur() fires no event.
document.activeElement.dispatchEvent(new FocusEvent('blur'));
await wait(200);
expect(model.getValue() === tidy, 'leaving the editor formats the file');
store.set({ settings: { ...store.get().settings, formatOnBlur: false } });

// With format on save, what reaches the disk is formatted.
model.setValue(mangled);
store.set({ settings: { ...store.get().settings, formatOnSave: true } });
await session.save(path);
expect((await read(path)).includes('\t\t\t<div class="title">'), 'saving formatted the file first');

log('ok   intellisense done');
