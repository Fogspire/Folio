export interface FuzzyMatch {
	score: number;
	/** Indices into the text of the characters the query matched, for highlighting. */
	indices: number[];
}

const SEPARATORS = '/\\._- ';

/**
 * Matches a query against a path as a subsequence, the way an editor's quick open does: "alti" finds
 * altitude.rml and "scrmen" finds screens/menu. Null when the query's characters do not all appear in order.
 *
 * The file name is tried first and scores above the folders, so typing a name finds that file ahead of a
 * longer path that merely spells it out across directories.
 */
export function fuzzyMatch(query: string, path: string): FuzzyMatch | null {
	const wanted = query.toLowerCase().replace(/\s+/g, '');
	if (!wanted) return { score: 0, indices: [] };
	const text = path.toLowerCase();
	const nameStart = Math.max(text.lastIndexOf('/'), text.lastIndexOf('\\')) + 1;

	const inName = subsequence(wanted, text, nameStart);
	if (inName) return { score: inName.score + 1000, indices: inName.indices };
	return subsequence(wanted, text, 0);
}

/** Matches greedily from the left, preferring a character that starts a word over one inside a word. */
function subsequence(wanted: string, text: string, from: number): FuzzyMatch | null {
	const indices: number[] = [];
	let score = 0;
	let at = from;
	for (let position = 0; position < wanted.length; position++) {
		const character = wanted[position];
		let found = -1;
		// A word start further on beats the next plain occurrence, so "mm" lands on main_menu's two words,
		// as long as the rest of the query still fits after it.
		for (let index = at; index < text.length; index++) {
			if (text[index] !== character) continue;
			if (found === -1) found = index;
			if ((index === from || SEPARATORS.includes(text[index - 1])) && fits(wanted, position + 1, text, index + 1)) {
				found = index;
				break;
			}
		}
		if (found === -1) return null;

		const previous = indices[indices.length - 1];
		if (previous !== undefined && found === previous + 1) score += 8;
		if (found === from || SEPARATORS.includes(text[found - 1])) score += 10;
		score -= Math.min(found - at, 10);
		indices.push(found);
		at = found + 1;
	}
	// Shorter paths first among equals, so the obvious file is on top.
	return { score: score - text.length * 0.01, indices };
}

function fits(wanted: string, position: number, text: string, at: number): boolean {
	for (; position < wanted.length; position++) {
		at = text.indexOf(wanted[position], at);
		if (at === -1) return false;
		at++;
	}
	return true;
}
