/** Where a command's key applies. */
export type Scope =
	/** Everywhere, a text editor included. */
	| 'app'
	/** Everywhere but a text editor or a text field. */
	| 'edit'
	/** Only with the canvas or the outline focused, since it acts on the selected element. */
	| 'element';

export interface CommandInfo {
	id: string;
	label: string;
	group: string;
	scope: Scope;
	/** Keys out of the box, as "Ctrl+Shift+Z". Ctrl stands for Cmd on a Mac. */
	keys: string[];
}

const command = (group: string, id: string, label: string, scope: Scope, ...keys: string[]): CommandInfo => ({ id, label, group, scope, keys });

/** Every command a key can be given to, in the order the shortcuts dialog lists them. */
export const COMMANDS: CommandInfo[] = [
	command('File', 'open-project', 'Open Project', 'app', 'Ctrl+O'),
	command('File', 'go-to-file', 'Go to File', 'app', 'Ctrl+T'),
	command('File', 'new-from-recipe', 'New Document from Recipe', 'app', 'Ctrl+N'),
	command('File', 'save-all', 'Save All', 'app', 'Ctrl+S'),
	command('File', 'revert-all', 'Revert Unsaved Changes', 'app'),
	command('File', 'create-shortcuts', 'Create Shortcuts', 'app'),

	command('Edit', 'undo', 'Undo', 'edit', 'Ctrl+Z'),
	command('Edit', 'redo', 'Redo', 'edit', 'Ctrl+Shift+Z', 'Ctrl+Y'),
	command('Edit', 'cut', 'Cut', 'element', 'Ctrl+X'),
	command('Edit', 'copy', 'Copy', 'element', 'Ctrl+C'),
	command('Edit', 'paste', 'Paste', 'element', 'Ctrl+V'),
	command('Edit', 'duplicate', 'Duplicate', 'element', 'Ctrl+D'),
	command('Edit', 'delete', 'Delete', 'element', 'Delete', 'Backspace'),
	command('Edit', 'wrap', 'Wrap in a Div', 'element'),
	command('Edit', 'edit-text', 'Edit Text', 'element', 'Enter'),
	command('Edit', 'show-in-code', 'Show in Code', 'element'),
	command('Edit', 'bake', 'Bake to PNG', 'element'),
	command('Edit', 'format-document', 'Format Document', 'app', 'Alt+Shift+F'),
	command('Edit', 'preferences', 'Preferences', 'app', 'Ctrl+,'),
	command('Edit', 'keybindings', 'Keyboard Shortcuts', 'app', 'Ctrl+Shift+,'),

	command('Select', 'select-parent', 'Select Parent', 'element', 'Alt+ArrowUp'),
	command('Select', 'select-first-child', 'Select First Child', 'element', 'Alt+ArrowDown'),
	command('Select', 'select-previous', 'Select Previous Sibling', 'element', 'Alt+ArrowLeft'),
	command('Select', 'select-next', 'Select Next Sibling', 'element', 'Alt+ArrowRight'),
	command('Select', 'deselect', 'Deselect', 'edit', 'Escape'),

	command('View', 'panel:canvas', 'Show Canvas', 'app'),
	command('View', 'panel:code', 'Show Code', 'app'),
	command('View', 'panel:files', 'Show Files', 'app'),
	command('View', 'panel:library', 'Show Library', 'app'),
	command('View', 'panel:outline', 'Show Outline', 'app'),
	command('View', 'panel:inspector', 'Show Properties', 'app'),
	command('View', 'panel:data', 'Show Data', 'app'),
	command('View', 'panel:problems', 'Show Problems', 'app'),
	command('View', 'panel:activity', 'Show Activity', 'app'),
	command('View', 'panel:docs', 'Show Docs', 'app', 'F1'),
	command('View', 'panel:terminal', 'Show Terminal', 'app'),
	command('View', 'show-messages', 'Messages', 'app'),
	command('View', 'save-layout', 'Save Layout As', 'app'),
	command('View', 'load-layout', 'Load Layout', 'app'),
	command('View', 'reset-layout', 'Reset Layout', 'app', 'F10'),
	command('View', 'zoom-in', 'Zoom In', 'app', 'Ctrl+=', 'Ctrl+Shift++', 'Ctrl++'),
	command('View', 'zoom-out', 'Zoom Out', 'app', 'Ctrl+-'),
	command('View', 'zoom-fit', 'Zoom to Fit', 'app', 'Ctrl+0'),
	command('View', 'zoom-100', 'Actual Size', 'app', 'Ctrl+1'),
	command('View', 'toggle-timeline', 'Timeline', 'app', 'Ctrl+Shift+T'),
	command('View', 'mode-edit', 'Edit Mode', 'app'),
	command('View', 'mode-play', 'Play Mode', 'app'),

	command('Help', 'check-updates', 'Check for Updates', 'app'),
	command('Help', 'open-repo', 'Folio on GitHub', 'app'),
];

const byId = new Map(COMMANDS.map((entry) => [entry.id, entry]));

export function commandInfo(id: string): CommandInfo | null {
	return byId.get(id) ?? null;
}

const MODIFIER_KEYS = new Set(['Control', 'Shift', 'Alt', 'Meta', 'AltGraph', 'CapsLock', 'NumLock', 'ScrollLock']);

/** Punctuation by the key pressed, so Shift+, is not read as <. Equal is left out: Ctrl+Shift++ zooms in. */
const UNSHIFTED: Record<string, string> = {
	Comma: ',', Period: '.', Slash: '/', Semicolon: ';', Quote: "'", BracketLeft: '[', BracketRight: ']', Backslash: '\\', Minus: '-', Backquote: '`',
};

/** A key press as "Ctrl+Shift+Z", or null for a modifier on its own. */
export function comboOf(event: KeyboardEvent): string | null {
	if (MODIFIER_KEYS.has(event.key) || !event.key) return null;

	// Letters and digits by the key pressed, so Shift does not turn 1 into ! and lose the binding.
	const physical = /^Key([A-Z])$/.exec(event.code) ?? /^Digit(\d)$/.exec(event.code);
	let key = physical ? physical[1] : (event.shiftKey && UNSHIFTED[event.code]) || event.key;
	if (key === ' ') key = 'Space';
	else if (key.length === 1) key = key.toUpperCase();

	return (event.ctrlKey || event.metaKey ? 'Ctrl+' : '') + (event.altKey ? 'Alt+' : '') + (event.shiftKey ? 'Shift+' : '') + key;
}

/** The keys a command answers to, with the person's own choice in place of the defaults. */
export function keysFor(id: string, overrides: Record<string, string[]>): string[] {
	return overrides[id] ?? byId.get(id)?.keys ?? [];
}

export function commandForKey(combo: string, overrides: Record<string, string[]>): string | null {
	return COMMANDS.find((entry) => keysFor(entry.id, overrides).includes(combo))?.id ?? null;
}

/**
 * Gives a command a key, or none, taking that key from whichever command had it.
 * Returns the new overrides and the command that lost the key, if one did.
 */
export function assignKey(overrides: Record<string, string[]>, id: string, combo: string | null): { overrides: Record<string, string[]>; taken: string | null } {
	const next = { ...overrides };
	const taken = combo ? commandForKey(combo, overrides) : null;
	if (taken && taken !== id) next[taken] = keysFor(taken, overrides).filter((key) => key !== combo);
	next[id] = combo ? [combo] : [];
	return { overrides: next, taken: taken !== id ? taken : null };
}

/** Puts a command back to its default keys, taking them back from anything they were given to since. */
export function resetKey(overrides: Record<string, string[]>, id: string): Record<string, string[]> {
	const next = { ...overrides };
	delete next[id];
	for (const key of byId.get(id)?.keys ?? []) {
		const holder = commandForKey(key, next);
		if (holder && holder !== id) next[holder] = keysFor(holder, next).filter((entry) => entry !== key);
	}
	return next;
}

const ACCELERATOR_KEYS: Record<string, string> = {
	ArrowUp: 'Up', ArrowDown: 'Down', ArrowLeft: 'Left', ArrowRight: 'Right', Escape: 'Esc', '+': 'Plus',
	Enter: 'Enter', Delete: 'Delete', Backspace: 'Backspace', Tab: 'Tab', Space: 'Space',
	Home: 'Home', End: 'End', PageUp: 'PageUp', PageDown: 'PageDown', Insert: 'Insert',
};

/** A key as the menu bar writes it, or undefined for one it has no name for. */
export function toAccelerator(combo: string | undefined): string | undefined {
	if (!combo) return undefined;
	const parts = combo === '+' || combo.endsWith('++') ? [...combo.slice(0, -1).split('+').filter(Boolean), '+'] : combo.split('+');
	const key = parts.pop()!;
	const named = ACCELERATOR_KEYS[key] ?? (/^(F\d{1,2}|[A-Z0-9]|[=\-,.\/;'\[\]\\`])$/.test(key) ? key : null);
	if (!named) return undefined;
	return [...parts.map((part) => (part === 'Ctrl' ? 'CmdOrCtrl' : part)), named].join('+');
}

/** A key as a person reads it. */
export function displayKey(combo: string): string {
	return combo.replace(/Arrow/g, '').replace(/\+(?=.)/g, ' + ');
}
