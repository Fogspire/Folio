/*
 * Formatting for RML and RCSS: indentation by structure, and spacing normalised inside tags and
 * declarations.
 *
 * It never reflows. A rule written on one line stays on one line, a brace stays on the line its
 * author put it, attributes stay on the lines they were wrapped onto, and comments and the contents
 * of <text> and <script> keep their own internal layout, moved as a block. Formatting a formatted
 * file changes nothing.
 */

import { applyEdits, findDeclarations, findRules, TextEdit } from './rcss';
import { parseRml, RmlElement } from './rml';

export interface FormatOptions {
	/** One level of indentation. */
	unit: string;
	tabSize: number;
}

const DEFAULTS: FormatOptions = { unit: '\t', tabSize: 4 };

function columns(indent: string, tabSize: number): number {
	let width = 0;
	for (const character of indent) width = character === '\t' ? width + tabSize - (width % tabSize) : width + 1;
	return width;
}

function indentOfWidth(width: number, options: FormatOptions): string {
	if (options.unit !== '\t') return ' '.repeat(Math.max(0, width));
	return '\t'.repeat(Math.floor(Math.max(0, width) / options.tabSize)) + ' '.repeat(Math.max(0, width) % options.tabSize);
}

/** Moves a line's indent by a number of columns, which keeps a block's own alignment as it moves. */
function shiftIndent(indent: string, delta: number, options: FormatOptions): string {
	if (delta === 0) return indent;
	// Added or taken at the front, so spaces that align text after a tab stay spaces.
	if (delta > 0) return indentOfWidth(delta, options) + indent;
	let at = 0;
	let removed = 0;
	while (at < indent.length && removed < -delta) removed += indent[at++] === '\t' ? options.tabSize : 1;
	if (removed === -delta) return indent.slice(at);
	return indentOfWidth(columns(indent, options.tabSize) + delta, options);
}

function leadingWhitespace(line: string): string {
	return /^[ \t]*/.exec(line)![0];
}

/** Collapses runs of blank lines to one and strips trailing whitespace. */
function tidyLines(lines: string[]): string[] {
	const out: string[] = [];
	for (const line of lines) {
		const trimmed = line.replace(/[ \t]+$/, '');
		if (trimmed === '' && out.length && out[out.length - 1] === '') continue;
		out.push(trimmed);
	}
	return out;
}

/* ---------------------------------------------------------------- RCSS */

/** Spacing inside rules: `name: value;`, and a space inside a one-line rule's braces. */
function normaliseRules(text: string): string {
	const edits: TextEdit[] = [];

	for (const rule of findRules(text)) {
		const body = text.slice(rule.bodyStart, rule.bodyEnd);
		const declarations = findDeclarations(text, rule.bodyStart, rule.bodyEnd);
		const brace = rule.bodyStart - 1;

		// Runs of spaces are left alone throughout: a sheet that lines its rules up in columns means it.
		const selectorEnd = rule.selectorStart + text.slice(rule.selectorStart, brace).trimEnd().length;
		if (selectorEnd === brace) edits.push({ from: brace, to: brace, text: ' ' });

		const oneLine = !body.includes('\n');
		if (oneLine && declarations.length) {
			if (!/^\s/.test(body)) edits.push({ from: rule.bodyStart, to: rule.bodyStart, text: ' ' });
			if (!/\s$/.test(body)) edits.push({ from: rule.bodyEnd, to: rule.bodyEnd, text: ' ' });
		}

		for (const declaration of declarations) {
			if (text[declaration.start - 1] === ';') edits.push({ from: declaration.start, to: declaration.start, text: ' ' });
			const colon = declaration.valueStart - 1;
			const nameEnd = declaration.start + declaration.name.length;
			if (!text.slice(nameEnd, colon).includes('\n') && nameEnd !== colon) edits.push({ from: nameEnd, to: colon, text: '' });

			const raw = text.slice(declaration.valueStart, declaration.valueEnd);
			if (raw.trim() && !/^\s/.test(raw)) edits.push({ from: declaration.valueStart, to: declaration.valueStart, text: ' ' });

			const terminated = text[declaration.end - 1] === ';';
			const trailing = /\s*$/.exec(raw)![0];
			if (terminated && trailing && !trailing.includes('\n')) edits.push({ from: declaration.valueEnd - trailing.length, to: declaration.valueEnd, text: '' });
			if (!terminated && raw.trim()) edits.push({ from: declaration.valueEnd, to: declaration.valueEnd, text: ';' });
		}
	}

	return applyEdits(text, edits);
}

interface Block {
	kind: 'rules' | 'body';
	/**
	 * Set when the block's first declaration shares the brace's line, as in `.a { b: c;`. The lines
	 * after it are usually aligned under that declaration, so they move with the brace's line instead.
	 */
	hanging: number | null;
}

/** Reindents a stylesheet by brace depth, every line at `base` plus one unit per level. */
function reindentSheet(text: string, base: string, options: FormatOptions): string {
	const lines = text.split('\n');
	const out: string[] = [];
	const stack: Block[] = [];
	let inComment = false;
	// How far the last line that was not inside a comment moved, which a comment continuing from it moves too.
	let delta = 0;
	// The last character that was not whitespace or comment, which tells a continued value from a new one.
	let last = '';
	let statement = '';

	for (const line of lines) {
		const old = leadingWhitespace(line);
		const content = line.slice(old.length);
		const top = stack[stack.length - 1];
		let indent: string;

		if (!content) {
			indent = '';
		} else if (inComment) {
			indent = shiftIndent(old, delta, options);
		} else if (top?.hanging != null) {
			indent = shiftIndent(old, top.hanging, options);
			delta = top.hanging;
		} else if (top?.kind === 'body' && last && !';{}'.includes(last) && !content.startsWith('{') && !content.startsWith('}')) {
			// A value continued from the line above, often aligned under where the value began.
			indent = shiftIndent(old, delta, options);
		} else {
			const level = content.startsWith('}') ? stack.length - 1 : stack.length;
			indent = base + options.unit.repeat(Math.max(0, level));
			delta = columns(indent, options.tabSize) - columns(old, options.tabSize);
		}
		out.push(content ? indent + content : '');

		for (let index = 0; index < content.length; index++) {
			if (inComment) {
				if (content.startsWith('*/', index)) {
					inComment = false;
					index += 1;
				}
				continue;
			}
			if (content.startsWith('/*', index)) {
				inComment = true;
				index += 1;
				continue;
			}
			const character = content[index];
			if (character === '"' || character === "'") {
				const close = content.indexOf(character, index + 1);
				index = close === -1 ? content.length : close;
				last = character;
				continue;
			}
			if (character === '{') {
				const prelude = statement.trim();
				const rest = content.slice(index + 1).replace(/\/\*.*?\*\//g, '').trim();
				const hanging = rest !== '' && !rest.includes('}') ? delta : null;
				stack.push({ kind: /^@(media|supports|keyframes)\b/.test(prelude) ? 'rules' : 'body', hanging });
				statement = '';
			} else if (character === '}') {
				stack.pop();
				statement = '';
			} else if (character === ';') {
				statement = '';
			} else {
				statement += character;
			}
			if (!/\s/.test(character)) last = character;
		}
		statement += '\n';
	}

	return tidyLines(out).join('\n');
}

/** Formats a whole stylesheet, every line indented from `base`. */
export function formatSheet(text: string, base = '', options: FormatOptions = DEFAULTS): string {
	return reindentSheet(normaliseRules(text), base, options);
}

export function formatRcss(text: string, options: FormatOptions = DEFAULTS): string {
	const newline = text.includes('\r\n') ? '\r\n' : '\n';
	const body = formatSheet(text.replace(/\r\n/g, '\n'), '', options).replace(/\n+$/, '');
	return (body ? body + '\n' : '').replace(/\n/g, newline);
}

/* ---------------------------------------------------------------- RML */

function walk(elements: RmlElement[], visit: (element: RmlElement) => void) {
	for (const element of elements) {
		visit(element);
		walk(element.children, visit);
	}
}

/** Spacing inside tags: one space between attributes, none around `=`, before `>` or inside `</tag>`. */
function normaliseTags(text: string): string {
	const edits: TextEdit[] = [];
	walk(parseRml(text).roots, (element) => {
		const nameEnd = element.start + 1 + element.tag.length;
		const closing = element.selfClosing ? element.openEnd - 2 : element.openEnd - 1;
		if (closing < nameEnd || (text[closing] !== '>' && text[closing] !== '/')) return;

		let previous = nameEnd;
		for (const attribute of element.attributes) {
			const gap = text.slice(previous, attribute.start);
			if (!gap.includes('\n') && gap !== ' ' && /^\s+$/.test(gap)) edits.push({ from: previous, to: attribute.start, text: ' ' });

			const quoted = attribute.valueStart > attribute.start && (text[attribute.valueStart - 1] === '"' || text[attribute.valueStart - 1] === "'");
			const equals = attribute.start + attribute.name.length;
			if (quoted) {
				const between = text.slice(equals, attribute.valueStart - 1);
				if (between !== '=' && !between.includes('\n')) edits.push({ from: equals, to: attribute.valueStart - 1, text: '=' });
			}
			previous = attribute.end;
		}

		const tail = text.slice(previous, closing);
		if (/^\s+$/.test(tail) && !tail.includes('\n')) edits.push({ from: previous, to: closing, text: element.selfClosing ? ' ' : '' });

		if (!element.selfClosing && text.startsWith('</', element.closeStart) && text[element.end - 1] === '>') {
			const name = text.slice(element.closeStart + 2, element.end - 1);
			if (name !== name.trim() && !name.includes('\n')) edits.push({ from: element.closeStart + 2, to: element.end - 1, text: name.trim() });
		}
	});
	return applyEdits(text, edits);
}

const FRAMES = new Set(['rml', 'template', 'body']);

/**
 * Whether a frame element indents its children, read from how its first child is written. Documents
 * differ on whether <rml>, <template> and <body> indent what they hold, and each is left as it is.
 */
function indentsChildren(text: string, root: RmlElement): boolean {
	const first = root.children.find((child) => text.slice(text.lastIndexOf('\n', child.start - 1) + 1, child.start).trim() === '');
	if (!first) return true;
	const lineOf = (offset: number) => text.slice(text.lastIndexOf('\n', offset - 1) + 1, offset);
	return lineOf(first.start).length > leadingWhitespace(lineOf(root.start)).length;
}

/**
 * Spans whose lines keep their own layout and move as a block with the line they begin on: comments,
 * the contents of <text> and <script>, attribute values written over several lines, and prose that
 * starts on its element's own line and wraps, as in `<p>Arrows move focus,` with the rest below.
 */
function verbatimBlocks(source: string, elements: RmlElement[]): { from: number; to: number }[] {
	const raw = elements.filter((element) => ['style', 'script', 'text'].includes(element.tag));
	const blocks: { from: number; to: number }[] = [];
	for (let at = source.indexOf('<!--'); at !== -1; at = source.indexOf('<!--', at + 4)) {
		if (raw.some((element) => at > element.openEnd && at < element.closeStart)) continue;
		const end = source.indexOf('-->', at + 4);
		blocks.push({ from: at, to: end === -1 ? source.length : end + 3 });
		if (end === -1) break;
		at = end;
	}
	for (const element of raw) if (element.tag !== 'style') blocks.push({ from: element.openEnd, to: element.closeStart });
	for (const element of elements) {
		for (const attribute of element.attributes) {
			if (source.slice(attribute.valueStart, attribute.valueEnd).includes('\n')) blocks.push({ from: attribute.valueStart, to: attribute.valueEnd });
		}
		if (raw.includes(element) || FRAMES.has(element.tag) || element.selfClosing) continue;
		const lineEnd = source.indexOf('\n', element.openEnd);
		if (lineEnd === -1 || lineEnd >= element.closeStart) continue;
		if (source.slice(element.openEnd, lineEnd).replace(/<[^>]*>/g, '').trim() !== '') blocks.push({ from: element.openEnd, to: element.closeStart });
	}
	return blocks;
}

/**
 * Indents by lines rather than by elements: everything opened on one line and left open indents what
 * follows by one level together, so `<div><div>` wraps its contents once, as it was written.
 */
export function formatRml(text: string, options: FormatOptions = DEFAULTS): string {
	const newline = text.includes('\r\n') ? '\r\n' : '\n';
	const source = normaliseTags(text.replace(/\r\n/g, '\n'));
	const document = parseRml(source);
	const elements: RmlElement[] = [];
	walk(document.roots, (element) => elements.push(element));
	const blocks = verbatimBlocks(source, elements);
	const flat = new Set(elements.filter((element) => (!element.parent || FRAMES.has(element.tag)) && !indentsChildren(source, element)));

	// The level of the line each element opens on, filled in as lines are placed.
	const openLevel = new Map<RmlElement, number>();
	const innerLevel = (element: RmlElement) => (openLevel.get(element) ?? 0) + (flat.has(element) ? 0 : 1);

	const lines = source.split('\n');
	const out: string[] = [];
	const levels: number[] = [];
	const deltas: number[] = [];
	const sheets: { open: number; from: number; to: number }[] = [];
	let offset = 0;
	let next = 0;

	const levelAt = (first: number): number => {
		const starting = elements.find((element) => element.start === first);
		if (starting) return starting.parent ? innerLevel(starting.parent) : 0;
		// Only an explicit close: an element closed implicitly ends where something else's tag begins.
		const closing = elements.find((element) => element.closeStart === first && element.closeStart < element.end);
		if (closing) return openLevel.get(closing) ?? 0;
		const wrapped = elements.find((element) => first > element.start && first < element.openEnd);
		if (wrapped) return (openLevel.get(wrapped) ?? 0) + 1;
		let container: RmlElement | null = null;
		for (const element of elements) if (first >= element.openEnd && first < element.closeStart) container = element;
		return container ? innerLevel(container) : 0;
	};

	for (let index = 0; index < lines.length; index++) {
		const line = lines[index];
		const lineStart = offset;
		offset += line.length + 1;
		const old = leadingWhitespace(line);
		const first = lineStart + old.length;
		const content = line.slice(old.length);
		const style = elements.find((element) => element.tag === 'style' && first > element.openEnd && first < element.closeStart);
		const block = blocks.find((range) => lineStart > range.from && first < range.to);

		let level = 0;
		// The level what opens on this line is counted from: a wrapped tag's own line, not the wrap.
		let base: number | null = null;
		if (!content) {
			out.push('');
			deltas.push(0);
		} else if (style) {
			// Placed with the rest of its stylesheet once the <style> line's own level is known.
			out.push(line);
			deltas.push(0);
		} else if (block) {
			const startLine = source.slice(0, block.from).split('\n').length - 1;
			const delta = deltas[startLine] ?? 0;
			out.push(shiftIndent(old, delta, options) + content);
			deltas.push(delta);
			level = levels[startLine] ?? 0;
		} else {
			level = levelAt(first);
			const wrapped = elements.find((element) => first > element.start && first < element.openEnd);
			if (wrapped) base = openLevel.get(wrapped) ?? 0;
			const indent = options.unit.repeat(level);
			out.push(indent + content);
			deltas.push(columns(indent, options.tabSize) - columns(old, options.tabSize));
		}
		levels.push(level);

		while (next < elements.length && elements[next].start < offset) {
			if (!openLevel.has(elements[next])) openLevel.set(elements[next], base ?? level);
			next++;
		}

		// A <style> whose contents have lines to themselves is formatted as a stylesheet.
		for (const element of elements) {
			if (element.tag !== 'style' || element.openEnd < lineStart || element.openEnd >= offset) continue;
			if (source.slice(element.openEnd, offset - 1).trim() !== '') continue;
			const closeLine = source.slice(0, element.closeStart).split('\n').length - 1;
			const closeLineStart = source.lastIndexOf('\n', element.closeStart - 1) + 1;
			if (source.slice(closeLineStart, element.closeStart).trim() !== '' || closeLine <= index + 1) continue;
			sheets.push({ open: index, from: index + 1, to: closeLine });
		}
	}

	for (const sheet of [...sheets].reverse()) {
		const inner = lines.slice(sheet.from, sheet.to).join('\n');
		const formatted = formatSheet(inner, options.unit.repeat(levels[sheet.open] + 1), options).replace(/^\n+|\n+$/g, '');
		out.splice(sheet.from, sheet.to - sheet.from, ...(formatted ? formatted.split('\n') : []));
	}

	const body = tidyLines(out).join('\n').replace(/\n+$/, '');
	return (body ? body + '\n' : '').replace(/\n/g, newline);
}

/* ---------------------------------------------------------------- Edits */

/**
 * Pairs the lines of two texts that hold the same content once indentation is set aside, as index
 * pairs in order. Lines outside the shared head and tail are matched by longest common subsequence.
 */
function matchLines(a: string[], b: string[]): [number, number][] {
	let head = 0;
	while (head < a.length && head < b.length && a[head].trim() === b[head].trim()) head++;
	let tail = 0;
	while (tail < a.length - head && tail < b.length - head && a[a.length - 1 - tail].trim() === b[b.length - 1 - tail].trim()) tail++;

	const pairs: [number, number][] = [];
	for (let index = 0; index < head; index++) pairs.push([index, index]);
	const n = a.length - head - tail;
	const m = b.length - head - tail;
	if (n > 0 && m > 0 && n * m <= 4_000_000) {
		const width = m + 1;
		const lengths = new Uint32Array((n + 1) * width);
		for (let i = n - 1; i >= 0; i--) {
			for (let j = m - 1; j >= 0; j--) {
				lengths[i * width + j] = a[head + i].trim() === b[head + j].trim()
					? lengths[(i + 1) * width + j + 1] + 1
					: Math.max(lengths[(i + 1) * width + j], lengths[i * width + j + 1]);
			}
		}
		for (let i = 0, j = 0; i < n && j < m;) {
			if (a[head + i].trim() === b[head + j].trim()) pairs.push([head + i++, head + j++]);
			else if (lengths[(i + 1) * width + j] >= lengths[i * width + j + 1]) i++;
			else j++;
		}
	}
	for (let index = tail; index > 0; index--) pairs.push([a.length - index, b.length - index]);
	return pairs;
}

/**
 * The edits that turn one text into another. A line that only changed is edited in place, and lines
 * added or removed are edits of their own, so no edit carries a line break into a line it did not change.
 */
export function minimalEdits(before: string, after: string): TextEdit[] {
	if (before === after) return [];
	const a = before.split('\n');
	const b = after.split('\n');
	const starts: number[] = [];
	for (let index = 0, offset = 0; index <= a.length; offset += (a[index++]?.length ?? 0) + 1) starts.push(offset);

	const edits: TextEdit[] = [];
	/** Replaces old lines [i0, i1) with new lines [j0, j1), pairing them from the end so a line rewritten in place stays an edit of that line. */
	const replace = (i0: number, i1: number, j0: number, j1: number) => {
		const paired = Math.min(i1 - i0, j1 - j0);
		for (let index = 1; index <= paired; index++) {
			const [ai, bj] = [i1 - index, j1 - index];
			if (a[ai] !== b[bj]) edits.push({ from: starts[ai], to: starts[ai] + a[ai].length, text: b[bj] });
		}
		i1 -= paired;
		j1 -= paired;
		if (i0 === i1 && j0 === j1) return;
		const lines = b.slice(j0, j1);
		if (i1 < a.length) edits.push({ from: starts[i0], to: starts[i1], text: lines.map((line) => line + '\n').join('') });
		else if (i0 > 0) edits.push({ from: starts[i0] - 1, to: before.length, text: lines.map((line) => '\n' + line).join('') });
		else edits.push({ from: 0, to: before.length, text: lines.join('\n') });
	};

	let i = 0;
	let j = 0;
	for (const [ai, bj] of [...matchLines(a, b), [a.length, b.length] as [number, number]]) {
		replace(i, ai, j, bj);
		if (ai < a.length && a[ai] !== b[bj]) edits.push({ from: starts[ai], to: starts[ai] + a[ai].length, text: b[bj] });
		i = ai + 1;
		j = bj + 1;
	}
	return edits;
}

export function formatterFor(path: string): ((text: string, options?: FormatOptions) => string) | null {
	const lower = path.toLowerCase();
	if (lower.endsWith('.rml')) return formatRml;
	if (lower.endsWith('.rcss')) return formatRcss;
	return null;
}
