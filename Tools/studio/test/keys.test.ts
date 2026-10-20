import { describe, expect, it } from 'vitest';
import { comboOf, commandForKey } from '../app/src/keys';

const press = (key: string, code: string, modifiers: Partial<KeyboardEvent> = {}) =>
	comboOf({ key, code, ctrlKey: false, metaKey: false, altKey: false, shiftKey: false, ...modifiers } as KeyboardEvent);

describe('comboOf', () => {
	it('reads shifted punctuation by the key pressed', () => {
		expect(press('<', 'Comma', { ctrlKey: true, shiftKey: true })).toBe('Ctrl+Shift+,');
		expect(commandForKey('Ctrl+Shift+,', {})).toBe('keybindings');
	});

	it('keeps Ctrl+Shift++ for zoom', () => {
		expect(commandForKey(press('+', 'Equal', { ctrlKey: true, shiftKey: true })!, {})).toBe('zoom-in');
	});
});
