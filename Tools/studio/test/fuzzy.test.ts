import { describe, expect, it } from 'vitest';
import { fuzzyMatch } from '../app/src/fuzzy';

const rank = (query: string, paths: string[]) => paths
	.map((path) => ({ path, match: fuzzyMatch(query, path) }))
	.filter((entry) => entry.match)
	.sort((a, b) => b.match!.score - a.match!.score)
	.map((entry) => entry.path);

describe('fuzzyMatch', () => {
	it('finds a file from the start of its name', () => {
		expect(rank('alti', ['UI/screens/menu.rml', 'UI/hud/altitude.rml', 'UI/lib/tiles.rcss'])).toEqual(['UI/hud/altitude.rml']);
	});

	it('puts a match in the name ahead of one spread across folders', () => {
		expect(rank('menu', ['UI/menu/pause.rml', 'UI/screens/main_menu.rml'])[0]).toBe('UI/screens/main_menu.rml');
	});

	it('prefers word starts', () => {
		expect(fuzzyMatch('mm', 'UI/main_menu.rml')!.indices).toEqual([3, 8]);
	});

	it('takes a later word start only when the rest still fits', () => {
		expect(fuzzyMatch('ab', 'xab_a')!.indices).toEqual([1, 2]);
	});

	it('matches across folders and ignores case and spaces', () => {
		expect(fuzzyMatch('Scr Men', 'UI/screens/menu.rml')).not.toBeNull();
		expect(fuzzyMatch('zz', 'UI/screens/menu.rml')).toBeNull();
	});
});
