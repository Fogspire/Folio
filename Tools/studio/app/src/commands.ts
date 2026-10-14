import { resetLayout, restoreLayout, serialiseLayout, showPanel } from './dock';
import {
	currentSource, deleteElement, duplicate, elementMarkup, elementText, insertNear, isEditable, redo, sourceElement, undo, wrapElement,
} from './edit/commands';
import { lineOf } from './edit/rcss';
import { host } from './host';
import { commandForKey, commandInfo, comboOf, keysFor, toAccelerator } from './keys';
import { monaco } from './monaco';
import {
	canvasScale, checkForUpdatesNow, findNode, openFile, openProject, pickProject, saveAll, select, setEditing, setMode, setZoom, store, toggleUpdateCheck,
} from './session';

export interface MenuItem {
	id?: string;
	label?: string;
	accelerator?: string;
	enabled?: boolean;
	separator?: boolean;
	submenu?: MenuItem[];
}

/** Markup last cut or copied here, which survives the system clipboard being unreadable. */
let copied: string | null = null;
/** Text fields typed into since they took focus. In one of those, undo belongs to the typing. */
const touched = new WeakSet<EventTarget>();

document.addEventListener('input', (event) => { if (event.target) touched.add(event.target); }, true);
document.addEventListener('focusout', (event) => { if (event.target) touched.delete(event.target); }, true);

function parentOf(id: string): string | null {
	if (id === '') return null;
	const slash = id.lastIndexOf('/');
	return slash === -1 ? '' : id.slice(0, slash);
}

/** The element one step from the selection: its first child, or the sibling before or after it. */
function neighbour(id: string, step: 'child' | -1 | 1): string | null {
	const { tree } = store.get();
	let target: string;
	if (step === 'child') target = id === '' ? '0' : id + '/0';
	else {
		if (id === '') return null;
		const parent = parentOf(id)!;
		const index = Number(id.slice(id.lastIndexOf('/') + 1)) + step;
		if (index < 0) return null;
		target = parent === '' ? String(index) : parent + '/' + index;
	}
	return findNode(tree, target) ? target : null;
}

function selectNeighbour(step: 'child' | -1 | 1) {
	const { selection } = store.get();
	const target = selection !== null ? neighbour(selection, step) : null;
	if (target !== null) select(target);
}

/** The key a menu shows beside a command. */
function accelerator(id: string): string | undefined {
	return toAccelerator(keysFor(id, store.get().settings.keys)[0]);
}

function focusedEditor() {
	return monaco.editor.getEditors().find((editor) => editor.hasTextFocus()) ?? null;
}

function textField(target: EventTarget | null = document.activeElement): HTMLElement | null {
	if (!(target instanceof HTMLElement)) return null;
	return target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement || target.isContentEditable ? target : null;
}

export function revealInCode(id: string) {
	const source = currentSource();
	const element = sourceElement(id);
	if (!source || !element) return;
	openFile(source.path, lineOf(source.text, element.start));
	showPanel('code');
}

function copySelection(): string | null {
	const { selection } = store.get();
	const markup = selection !== null ? elementMarkup(selection) : null;
	if (markup === null) return null;
	copied = markup;
	navigator.clipboard.writeText(markup).catch(() => undefined);
	return markup;
}

async function paste() {
	let markup = copied;
	try {
		const text = (await navigator.clipboard.readText()).trim();
		if (text.startsWith('<')) markup = text;
	} catch {
		// The clipboard can refuse to be read; what was copied here still pastes.
	}
	if (markup) await insertNear(markup);
}

function zoomBy(factor: number) {
	const { zoom } = store.get();
	setZoom((zoom === 'fit' ? canvasScale.value : zoom) * factor);
}

/** Runs a command by name. The menu bar, the context menus and the keyboard all end up here. */
export function runCommand(command: string, argument?: string) {
	const { selection } = store.get();
	const editor = focusedEditor();
	const field = textField();
	// With the caret in text, the clipboard and undo commands mean the text.
	const native = editor || field;

	if (command.startsWith('panel:')) return showPanel(command.slice('panel:'.length));

	switch (command) {
		case 'open-project': return void pickProject();
		case 'open-recent': return void (argument && openProject(argument));
		case 'new-from-recipe':
			showPanel('library');
			store.set({ status: 'Pick a recipe in Library, then Use it' });
			return;
		case 'save-all': return void saveAll();
		case 'undo':
			if (editor) editor.trigger('menu', 'undo', null);
			else if (field && touched.has(field)) host.native('undo');
			else undo();
			return;
		case 'redo':
			if (editor) editor.trigger('menu', 'redo', null);
			else if (field && touched.has(field)) host.native('redo');
			else redo();
			return;
		case 'cut':
			if (native) host.native('cut');
			else if (selection !== null && copySelection() !== null) deleteElement(selection);
			return;
		case 'copy':
			if (native || window.getSelection()?.toString()) host.native('copy');
			else copySelection();
			return;
		case 'paste':
			if (native) host.native('paste');
			else paste();
			return;
		case 'duplicate': return void (selection !== null && duplicate(selection));
		case 'delete':
			if (native) host.native('delete');
			else if (selection !== null) deleteElement(selection);
			return;
		case 'wrap': return void (selection !== null && wrapElement(selection));
		case 'edit-text':
			if (selection !== null && isEditable(selection) && elementText(selection) !== null) {
				showPanel('canvas');
				setEditing(selection);
			}
			return;
		case 'show-in-code': return void (selection !== null && revealInCode(selection));
		case 'select-first-child': return selectNeighbour('child');
		case 'select-previous': return selectNeighbour(-1);
		case 'select-next': return selectNeighbour(1);
		case 'preferences': return store.set({ dialog: { kind: 'preferences' } });
		case 'keybindings': return store.set({ dialog: { kind: 'keys' } });
		case 'assign-key': return void (argument && store.set({ dialog: { kind: 'assign', command: argument } }));
		case 'open-repo': return void host.openExternal('https://github.com/Fogspire/Folio');
		case 'select-parent': return void (selection !== null && parentOf(selection) !== null && select(parentOf(selection)));
		case 'deselect': return select(null);
		case 'reset-layout': return resetLayout();
		case 'save-layout':
			host.saveLayout(serialiseLayout()).then((name) => { if (name) store.set({ status: 'Saved layout ' + name }); });
			return;
		case 'load-layout':
			host.loadLayout(argument).then((text) => {
				if (text !== null && !restoreLayout(text)) store.set({ status: 'That file is not a layout' });
			});
			return;
		case 'zoom-in': return zoomBy(1.25);
		case 'zoom-out': return zoomBy(0.8);
		case 'zoom-fit': return setZoom('fit');
		case 'zoom-100': return setZoom(1);
		case 'mode-edit': return setMode('edit');
		case 'mode-play': return setMode('play');
		case 'check-updates': return void checkForUpdatesNow();
		case 'toggle-update-check': return toggleUpdateCheck();
	}
}

/**
 * The keyboard. Which command a key runs is the person's to choose; where it applies is the command's.
 *
 * Commands on the selected element only apply while the canvas or the outline has the focus, so
 * Delete in some other panel never removes an element out of sight.
 */
export function handleKey(event: KeyboardEvent): boolean {
	const state = store.get();
	if (state.dialog) {
		if (event.key !== 'Escape') return false;
		store.set({ dialog: null });
		return true;
	}

	const target = event.target instanceof HTMLElement ? event.target : null;
	if (state.mode === 'play' && target?.closest('.canvas-surface')) return false;
	// Every key in the terminal is the shell's.
	if (target?.closest('.terminal-panel')) return false;

	const combo = comboOf(event);
	const command = combo ? commandForKey(combo, state.settings.keys) : null;
	const info = command ? commandInfo(command) : null;
	if (!command || !info) return false;

	const field = textField(target);
	if (info.scope !== 'app') {
		if (target?.closest('.monaco-editor')) return false;
		// A field being typed in keeps undo for the typing, and every other key for itself.
		if (field && (touched.has(field) || (command !== 'undo' && command !== 'redo'))) return false;
	}
	if (info.scope === 'element') {
		if (!target?.closest('[data-element-keys]')) return false;
		if (command === 'copy' && window.getSelection()?.toString()) return false;
		if (command === 'edit-text' && target.tagName === 'BUTTON') return false;
	}

	runCommand(command);
	return true;
}

/** The context menu for an element, on the canvas or in the outline. */
export async function elementMenu(id: string) {
	if (store.get().selection !== id) select(id);
	const editable = isEditable(id) && id !== '';
	const items: MenuItem[] = [
		{ id: 'edit-text', label: 'Edit Text', accelerator: accelerator('edit-text'), enabled: isEditable(id) && elementText(id) !== null },
		{ id: 'show-in-code', label: 'Show in Code', enabled: isEditable(id) },
		{ id: 'select-parent', label: 'Select Parent', accelerator: accelerator('select-parent'), enabled: id !== '' },
		{ separator: true },
		{ id: 'cut', label: 'Cut', accelerator: accelerator('cut'), enabled: editable },
		{ id: 'copy', label: 'Copy', accelerator: accelerator('copy'), enabled: editable },
		{ id: 'paste', label: 'Paste', accelerator: accelerator('paste'), enabled: isEditable(id) },
		{ id: 'duplicate', label: 'Duplicate', accelerator: accelerator('duplicate'), enabled: editable },
		{ id: 'delete', label: 'Delete', accelerator: accelerator('delete'), enabled: editable },
		{ separator: true },
		{ id: 'wrap', label: 'Wrap in a Div', enabled: editable },
	];
	const chosen: string | null = await host.contextMenu(items);
	if (chosen) runCommand(chosen);
}

/** Shows a context menu and returns the id chosen. */
export function popup(items: MenuItem[]): Promise<string | null> {
	return host.contextMenu(items);
}
