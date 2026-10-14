/*
 * Applying one property to one rule, in place.
 *
 * An edit is a request - set this property on this rule to this value - applied by something that
 * understands RCSS well enough to honour it without reformatting. It finds the smallest span it can
 * replace, a value or an insertion point inside a block, and touches nothing else: a tool that
 * reflowed a stylesheet to move a button would be abandoned within a day, and rightly.
 *
 * It is deliberately not a full RCSS parser. It understands blocks, strings, comments and at-rules
 * well enough to find a rule and its declarations, and it refuses rather than guesses when it cannot:
 * an edit that lands in the wrong place is far worse than one that does not happen.
 *
 * Every function takes a range, because a document's own <style> block is a stylesheet too and its
 * rules are edited where they sit, inside the markup.
 */

export interface TextEdit {
	from: number;
	to: number;
	text: string;
}

export interface Range {
	from: number;
	to: number;
}

export interface Rule {
	/** The selector with any enclosing at-rule prefixed, which is what tells two `.btn` rules apart. */
	selector: string;
	bareSelector: string;
	selectorStart: number;
	bodyStart: number;
	bodyEnd: number;
	/** One-based line of the selector within the whole text. */
	line: number;
}

export interface Declaration {
	name: string;
	value: string;
	valueStart: number;
	valueEnd: number;
	start: number;
	end: number;
}

export function applyEdits(text: string, edits: TextEdit[]): string {
	let out = text;
	for (const edit of [...edits].sort((a, b) => b.from - a.from)) out = out.slice(0, edit.from) + edit.text + out.slice(edit.to);
	return out;
}

export function lineOf(text: string, offset: number): number {
	let line = 1;
	for (let index = text.indexOf('\n'); index !== -1 && index < offset; index = text.indexOf('\n', index + 1)) line += 1;
	return line;
}

export function newlineOf(text: string): string {
	return text.includes('\r\n') ? '\r\n' : '\n';
}

/** Steps over a comment or a string, so a brace inside one is not read as structure. */
function scanSkippable(text: string, index: number): number {
	if (text.startsWith('/*', index)) {
		const end = text.indexOf('*/', index + 2);
		return end === -1 ? text.length : end + 2;
	}

	const quote = text[index];
	if (quote === '"' || quote === "'") {
		let at = index + 1;
		while (at < text.length && text[at] !== quote) at += text[at] === '\\' ? 2 : 1;
		return at + 1;
	}

	return index;
}

/**
 * Every rule in a range, with the span of its selector and of its body.
 *
 * Block at-rules are descended into rather than skipped, so a rule inside a theme is addressable.
 */
export function findRules(text: string, range: Range = { from: 0, to: text.length }, prefix = ''): Rule[] {
	const rules: Rule[] = [];
	let index = range.from;
	let selectorStart = range.from;

	while (index < range.to) {
		const skipped = scanSkippable(text, index);
		if (skipped !== index) {
			// A comment before a rule belongs to the file, not to the selector.
			if (text.slice(selectorStart, index).trim() === '') selectorStart = skipped;
			index = skipped;
			continue;
		}

		const character = text[index];

		if (character === '{') {
			const selector = text.slice(selectorStart, index).trim();

			let depth = 1;
			let at = index + 1;
			while (at < range.to && depth > 0) {
				const inner = scanSkippable(text, at);
				if (inner !== at) {
					at = inner;
					continue;
				}
				if (text[at] === '{') depth += 1;
				else if (text[at] === '}') depth -= 1;
				at += 1;
			}

			const bodyStart = index + 1;
			const bodyEnd = depth === 0 ? at - 1 : at;

			if (selector.startsWith('@')) {
				// A block at-rule contains rules; anything else with a block, such as @spritesheet, does not.
				if (/^@(media|supports)/i.test(selector)) rules.push(...findRules(text, { from: bodyStart, to: bodyEnd }, `${prefix}${selector} `));
			} else if (selector) {
				const start = selectorStart + /^\s*/.exec(text.slice(selectorStart, index))![0].length;
				rules.push({ selector: prefix + selector, bareSelector: selector, selectorStart: start, bodyStart, bodyEnd, line: lineOf(text, start) });
			}

			index = at;
			selectorStart = index;
			continue;
		}

		if (character === '}' || character === ';') {
			index += 1;
			selectorStart = index;
			continue;
		}

		index += 1;
	}

	return rules;
}

/** Selectors compare on their text with runs of whitespace flattened, so formatting does not matter. */
export function normalise(selector: string): string {
	return selector.replace(/\s+/g, ' ').replace(/\s*,\s*/g, ', ').trim();
}

/**
 * The declarations in a body, as spans.
 *
 * Only top-level ones. Comments are skipped rather than parsed, so a commented-out declaration stays
 * commented out. A last declaration with no semicolon is still one.
 */
export function findDeclarations(text: string, bodyStart: number, bodyEnd: number): Declaration[] {
	const declarations: Declaration[] = [];
	let index = bodyStart;
	let start = bodyStart;
	let depth = 0;

	const take = (end: number, terminated: boolean) => {
		const chunk = text.slice(start, end);
		const colon = chunk.indexOf(':');
		if (colon === -1) return;
		const name = chunk.slice(0, colon).trim();
		// A name with a brace or a comment in it is not a declaration this understands.
		if (!/^[-a-zA-Z][-a-zA-Z0-9]*$/.test(name)) return;

		// An unterminated value runs to the end of the body, less the whitespace before the brace.
		const valueEnd = terminated ? end : start + chunk.replace(/\s+$/, '').length;
		declarations.push({
			name,
			value: text.slice(start + colon + 1, valueEnd).trim(),
			valueStart: start + colon + 1,
			valueEnd,
			start: start + /^\s*/.exec(chunk)![0].length,
			end: terminated ? end + 1 : valueEnd,
		});
	};

	while (index < bodyEnd) {
		const skipped = scanSkippable(text, index);
		if (skipped !== index) {
			// A comment trailing one declaration must not become part of the next one's name.
			if (text.slice(start, index).trim() === '') start = skipped;
			index = skipped;
			continue;
		}

		const character = text[index];
		if (character === '{') depth += 1;
		else if (character === '}') depth -= 1;
		else if (character === ';' && depth === 0) {
			take(index, true);
			start = index + 1;
		}
		index += 1;
	}

	if (text.slice(start, bodyEnd).trim() !== '') take(bodyEnd, false);
	return declarations;
}

/** The indent used inside a body, so an inserted declaration matches what is already there. */
function bodyIndent(text: string, bodyStart: number, bodyEnd: number): string {
	const match = /\n([ \t]+)\S/.exec(text.slice(bodyStart, bodyEnd));
	if (match) return match[1];

	const before = text.lastIndexOf('\n', bodyStart) + 1;
	const outer = /^[ \t]*/.exec(text.slice(before))![0];
	return outer + (outer.includes(' ') ? '    ' : '\t');
}

/**
 * Picks the rule an edit names.
 *
 * By selector, then by line when one is given, because the same selector can appear twice and the
 * renderer reports which one it meant. Without a line the last wins, as it does in the cascade.
 */
export function findRule(text: string, selector: string, line?: number, range?: Range): Rule | null {
	const wanted = normalise(selector);
	const rules = findRules(text, range).filter((rule) => normalise(rule.selector) === wanted || normalise(rule.bareSelector) === wanted);
	if (!rules.length) return null;
	if (line !== undefined) {
		const exact = rules.find((rule) => rule.line === line);
		if (exact) return exact;
	}
	return rules[rules.length - 1];
}

/** Sets a property on a rule: the value alone when it is already there, one new line when it is not. */
export function setDeclaration(text: string, rule: Rule, property: string, value: string): TextEdit {
	const declarations = findDeclarations(text, rule.bodyStart, rule.bodyEnd);
	const existing = declarations.filter((declaration) => declaration.name === property).pop();

	if (existing) {
		// The value alone, so any comment after it and the whitespace around it survive.
		const before = text.slice(existing.valueStart, existing.valueEnd);
		const leading = /^\s*/.exec(before)![0];
		const trailing = /\s*$/.exec(before)![0];
		return { from: existing.valueStart, to: existing.valueEnd, text: leading + value + trailing };
	}

	const body = text.slice(rule.bodyStart, rule.bodyEnd);
	const newline = newlineOf(text);

	// A rule written on one line stays on one line.
	if (!body.includes('\n')) {
		const last = declarations[declarations.length - 1];
		if (!last) return { from: rule.bodyStart, to: rule.bodyEnd, text: ` ${property}: ${value}; ` };
		const terminated = text[last.end - 1] === ';';
		return { from: last.end, to: last.end, text: `${terminated ? '' : ';'} ${property}: ${value};` };
	}

	// After the last declaration rather than at the end of the body, so a trailing comment inside the
	// rule stays at the bottom where its author put it.
	const indent = bodyIndent(text, rule.bodyStart, rule.bodyEnd);
	const last = declarations[declarations.length - 1];
	if (!last) return { from: rule.bodyStart, to: rule.bodyStart, text: `${newline}${indent}${property}: ${value};` };
	const terminated = text[last.end - 1] === ';';
	return { from: last.end, to: last.end, text: `${terminated ? '' : ';'}${newline}${indent}${property}: ${value};` };
}

/** Removes a declaration, and the line it had to itself. */
export function removeDeclaration(text: string, rule: Rule, property: string): TextEdit | null {
	const existing = findDeclarations(text, rule.bodyStart, rule.bodyEnd).filter((declaration) => declaration.name === property).pop();
	if (!existing) return null;

	const lineStart = text.lastIndexOf('\n', existing.start - 1) + 1;
	let lineEnd = text.indexOf('\n', existing.end);
	if (lineEnd === -1) lineEnd = text.length;
	const alone = text.slice(lineStart, existing.start).trim() === '' && text.slice(existing.end, lineEnd).trim() === '';
	if (alone && lineEnd < rule.bodyEnd) return { from: lineStart, to: lineEnd + 1, text: '' };

	// Sharing a line: take the space that separated it from its neighbour too.
	const trailing = /^[ \t]*/.exec(text.slice(existing.end))![0].length;
	return { from: existing.start, to: existing.end + trailing, text: '' };
}

/** Appends a rule at the end of a sheet, or of a <style> block's range. */
export function appendRule(text: string, range: Range, selector: string, declarations: [string, string][]): TextEdit {
	const newline = newlineOf(text);
	const inside = text.slice(range.from, range.to);
	// Match the indent the block's existing rules use, which inside a <style> element is not zero.
	const existing = findRules(text, range);
	let indent = '';
	if (existing.length) {
		const first = existing[0];
		indent = /^[ \t]*/.exec(text.slice(text.lastIndexOf('\n', first.selectorStart - 1) + 1))![0];
	} else if (range.from > 0) {
		const closing = text.lastIndexOf('\n', range.to - 1) + 1;
		const outer = /^[ \t]*/.exec(text.slice(closing))![0];
		indent = outer + '\t';
	}

	const body = declarations.map(([name, value]) => `${name}: ${value};`).join(' ');
	const rule = `${indent}${selector} { ${body} }`;

	// Before the whitespace that leads up to the end, so a closing </style> keeps its own line.
	const tail = /\s*$/.exec(inside)![0].length;
	const at = range.to - tail;
	const blank = inside.trim() === '' ? '' : newline;
	return { from: at, to: at, text: `${newline}${blank}${rule}${tail ? '' : newline}` };
}

/** Parses the contents of a style attribute. */
export function parseInlineStyle(value: string): Declaration[] {
	return findDeclarations(value, 0, value.length);
}

/** A style attribute's contents with one property set, replaced in place or added at the end. */
export function setInlineStyle(value: string, property: string, next: string | null): string {
	const declarations = parseInlineStyle(value);
	const existing = declarations.filter((declaration) => declaration.name === property).pop();

	if (next === null) {
		if (!existing) return value;
		return (value.slice(0, existing.start) + value.slice(existing.end)).replace(/\s+/g, ' ').trim();
	}
	if (existing) return value.slice(0, existing.valueStart) + ' ' + next + value.slice(existing.valueEnd);

	const trimmed = value.trim();
	if (!trimmed) return `${property}: ${next};`;
	return `${trimmed}${trimmed.endsWith(';') ? '' : ';'} ${property}: ${next};`;
}
