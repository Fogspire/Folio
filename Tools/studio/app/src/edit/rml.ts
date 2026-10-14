/*
 * A parse of RML that remembers where everything was, and edits that change only what they must.
 *
 * The markup is the source of truth and people keep writing it by hand, so a visual edit is a text
 * edit: one attribute value, one inserted element, one removed line. Nothing is re-serialised, which
 * is what keeps comments, indentation and attribute order exactly as their author left them.
 */

import { newlineOf, Range, TextEdit } from './rcss';

export interface RmlAttribute {
	name: string;
	value: string;
	/** The whole attribute, name through closing quote. */
	start: number;
	end: number;
	/** The value between its quotes, or an empty range at `end` for a bare attribute. */
	valueStart: number;
	valueEnd: number;
}

export interface RmlElement {
	tag: string;
	start: number;
	end: number;
	/** Just past the `>` of the opening tag. */
	openEnd: number;
	/** Where `</tag>` begins. Equal to `end` for a self-closing element. */
	closeStart: number;
	selfClosing: boolean;
	attributes: RmlAttribute[];
	children: RmlElement[];
	parent: RmlElement | null;
	/** Whether anything but whitespace sits directly inside, outside any child. */
	hasText: boolean;
}

export interface RmlDocument {
	roots: RmlElement[];
	head: RmlElement | null;
	body: RmlElement | null;
	/** The contents of each <style> element, which are stylesheets edited where they sit. */
	styles: Range[];
}

/**
 * Elements whose contents are not RML. A `<text>` holds Folio's own markup, in which `<angry>` is a
 * style and `</>` closes it, so reading it as elements would be reading a different language.
 */
const RAW_CONTENT = new Set(['style', 'script', 'text']);

export function parseRml(text: string): RmlDocument {
	const roots: RmlElement[] = [];
	const stack: RmlElement[] = [];
	const styles: Range[] = [];
	let index = 0;

	const markText = (from: number, to: number) => {
		const parent = stack[stack.length - 1];
		if (parent && !parent.hasText && text.slice(from, to).trim() !== '') parent.hasText = true;
	};

	while (index < text.length) {
		const open = text.indexOf('<', index);
		if (open === -1) {
			markText(index, text.length);
			break;
		}
		markText(index, open);

		if (text.startsWith('<!--', open)) {
			const end = text.indexOf('-->', open + 4);
			index = end === -1 ? text.length : end + 3;
			continue;
		}
		if (text[open + 1] === '!' || text[open + 1] === '?') {
			const end = text.indexOf('>', open);
			index = end === -1 ? text.length : end + 1;
			continue;
		}

		if (text[open + 1] === '/') {
			const end = text.indexOf('>', open);
			const tag = text.slice(open + 2, end === -1 ? text.length : end).trim().toLowerCase();
			// Close the nearest open element of that name, and anything left open inside it with it.
			for (let depth = stack.length - 1; depth >= 0; depth--) {
				if (stack[depth].tag !== tag) continue;
				for (let closing = stack.length - 1; closing > depth; closing--) {
					stack[closing].closeStart = open;
					stack[closing].end = open;
				}
				stack[depth].closeStart = open;
				stack[depth].end = end === -1 ? text.length : end + 1;
				stack.length = depth;
				break;
			}
			index = end === -1 ? text.length : end + 1;
			continue;
		}

		const nameMatch = /^[A-Za-z_][\w:.-]*/.exec(text.slice(open + 1, open + 80));
		if (!nameMatch) {
			// A stray `<` in text.
			markText(open, open + 1);
			index = open + 1;
			continue;
		}

		const element: RmlElement = {
			tag: nameMatch[0].toLowerCase(),
			start: open,
			end: text.length,
			openEnd: text.length,
			closeStart: text.length,
			selfClosing: false,
			attributes: [],
			children: [],
			parent: stack[stack.length - 1] ?? null,
			hasText: false,
		};

		let at = open + 1 + nameMatch[0].length;
		for (;;) {
			while (at < text.length && /\s/.test(text[at])) at++;
			if (at >= text.length) break;
			if (text[at] === '>') {
				at += 1;
				break;
			}
			if (text[at] === '/' && text[at + 1] === '>') {
				element.selfClosing = true;
				at += 2;
				break;
			}

			const start = at;
			while (at < text.length && !/[\s=>]/.test(text[at]) && !(text[at] === '/' && text[at + 1] === '>')) at++;
			const name = text.slice(start, at);
			if (!name) {
				at += 1;
				continue;
			}

			let lookahead = at;
			while (lookahead < text.length && /\s/.test(text[lookahead])) lookahead++;
			if (text[lookahead] !== '=') {
				element.attributes.push({ name, value: '', start, end: at, valueStart: at, valueEnd: at });
				continue;
			}

			at = lookahead + 1;
			while (at < text.length && /\s/.test(text[at])) at++;
			const quote = text[at];
			if (quote === '"' || quote === "'") {
				const close = text.indexOf(quote, at + 1);
				const valueEnd = close === -1 ? text.length : close;
				element.attributes.push({ name, value: text.slice(at + 1, valueEnd), start, end: Math.min(text.length, valueEnd + 1), valueStart: at + 1, valueEnd });
				at = Math.min(text.length, valueEnd + 1);
			} else {
				const valueStart = at;
				while (at < text.length && !/[\s>]/.test(text[at])) at++;
				element.attributes.push({ name, value: text.slice(valueStart, at), start, end: at, valueStart, valueEnd: at });
			}
		}

		element.openEnd = at;
		(element.parent ? element.parent.children : roots).push(element);

		if (element.selfClosing) {
			element.closeStart = at;
			element.end = at;
			index = at;
			continue;
		}

		if (RAW_CONTENT.has(element.tag)) {
			const close = new RegExp(`</${element.tag}\\s*>`, 'i').exec(text.slice(at));
			const closeStart = close ? at + close.index : text.length;
			element.closeStart = closeStart;
			element.end = close ? closeStart + close[0].length : text.length;
			element.hasText = text.slice(at, closeStart).trim() !== '';
			if (element.tag === 'style') styles.push({ from: at, to: closeStart });
			index = element.end;
			continue;
		}

		stack.push(element);
		index = at;
	}

	const find = (elements: RmlElement[], tag: string): RmlElement | null => {
		for (const element of elements) {
			if (element.tag === tag) return element;
			const inner = find(element.children, tag);
			if (inner) return inner;
		}
		return null;
	};

	return { roots, head: find(roots, 'head'), body: find(roots, 'body'), styles };
}

export function attributeOf(element: RmlElement, name: string): RmlAttribute | null {
	return element.attributes.find((attribute) => attribute.name === name) ?? null;
}

export function escapeAttribute(value: string): string {
	return value.replace(/&(?!\w+;|#\d+;)/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');
}

export function escapeText(value: string): string {
	return value.replace(/&(?!\w+;|#\d+;)/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

export function unescapeText(value: string): string {
	return value.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&amp;/g, '&');
}

function lineStart(text: string, offset: number): number {
	return text.lastIndexOf('\n', offset - 1) + 1;
}

function lineEnd(text: string, offset: number): number {
	const end = text.indexOf('\n', offset);
	return end === -1 ? text.length : end + 1;
}

/** The whitespace a line begins with. */
export function indentAt(text: string, offset: number): string {
	return /^[ \t]*/.exec(text.slice(lineStart(text, offset)))![0];
}

/** Whether an element has its lines to itself, which decides whether removing it removes them. */
function ownsLines(text: string, element: RmlElement): boolean {
	const before = text.slice(lineStart(text, element.start), element.start);
	const after = text.slice(element.end, lineEnd(text, element.end));
	return before.trim() === '' && after.trim() === '';
}

/** Sets, adds or (with null) removes one attribute. */
export function setAttribute(text: string, element: RmlElement, name: string, value: string | null): TextEdit | null {
	const existing = attributeOf(element, name);

	if (value === null) {
		if (!existing) return null;
		// With the whitespace before it, so no double space or dangling line is left behind.
		let from = existing.start;
		while (from > element.start && /\s/.test(text[from - 1])) from--;
		return { from, to: existing.end, text: '' };
	}

	const escaped = escapeAttribute(value);
	if (existing) {
		if (existing.valueStart === existing.valueEnd && existing.end === existing.valueStart) return { from: existing.end, to: existing.end, text: `="${escaped}"` };
		return { from: existing.valueStart, to: existing.valueEnd, text: escaped };
	}

	// After the last attribute, or the tag name when there is none.
	const last = element.attributes[element.attributes.length - 1];
	const at = last ? last.end : element.start + 1 + element.tag.length;
	return { from: at, to: at, text: ` ${name}="${escaped}"` };
}

/** Replaces everything inside an element with text. */
export function setText(text: string, element: RmlElement, value: string): TextEdit {
	const escaped = element.tag === 'text' ? value : escapeText(value);
	if (element.selfClosing) {
		const slash = text.lastIndexOf('/>', element.end);
		return { from: slash, to: element.end, text: `>${escaped}</${element.tag}>` };
	}
	return { from: element.openEnd, to: element.closeStart, text: escaped };
}

export function removeElement(text: string, element: RmlElement): TextEdit {
	if (ownsLines(text, element)) return { from: lineStart(text, element.start), to: lineEnd(text, element.end), text: '' };
	return { from: element.start, to: element.end, text: '' };
}

/** An element's markup with its own indent taken off every line, ready to be put somewhere else. */
export function extractMarkup(text: string, element: RmlElement): string {
	const indent = indentAt(text, element.start);
	const lines = text.slice(element.start, element.end).split(/\r?\n/);
	return lines.map((line, index) => (index > 0 && line.startsWith(indent) ? line.slice(indent.length) : line)).join('\n');
}

function reindent(markup: string, indent: string, newline: string): string {
	return markup.split(/\r?\n/).map((line, index) => (index === 0 || line === '' ? line : indent + line)).join(newline);
}

/**
 * Inserts markup as the child at `index`, or as the last child when the index is past the end.
 *
 * `index` counts the parent's children as parsed, so an insertion before child 2 is index 2.
 */
export function insertMarkup(text: string, parent: RmlElement, index: number, markup: string): TextEdit {
	const newline = newlineOf(text);
	const reference = parent.children[index];

	if (reference) {
		if (ownsLines(text, reference)) {
			const indent = indentAt(text, reference.start);
			return { from: lineStart(text, reference.start), to: lineStart(text, reference.start), text: indent + reindent(markup, indent, newline) + newline };
		}
		return { from: reference.start, to: reference.start, text: reindent(markup, indentAt(text, reference.start), newline) };
	}

	const parentIndent = indentAt(text, parent.start);
	const last = parent.children[parent.children.length - 1];
	const childIndent = last && ownsLines(text, last) ? indentAt(text, last.start) : parentIndent + '\t';
	const body = childIndent + reindent(markup, childIndent, newline) + newline;

	if (parent.selfClosing) {
		const slash = text.lastIndexOf('/>', parent.end);
		let from = slash;
		while (from > parent.start && /\s/.test(text[from - 1])) from--;
		return { from, to: parent.end, text: `>${newline}${body}${parentIndent}</${parent.tag}>` };
	}

	// A closing tag on its own line takes the new child above it.
	if (text.slice(lineStart(text, parent.closeStart), parent.closeStart).trim() === '') {
		const at = lineStart(text, parent.closeStart);
		return { from: at, to: at, text: body };
	}

	// Otherwise the parent was written on one line, and gains lines to hold the child.
	return { from: parent.closeStart, to: parent.closeStart, text: `${newline}${body}${parentIndent}` };
}

/**
 * Moves an element to be the child at `index` of `parent`, with `index` counted before the move.
 *
 * Returns null when the move would put the element inside itself or leave it where it is.
 */
export function moveElement(text: string, element: RmlElement, parent: RmlElement, index: number): TextEdit[] | null {
	for (let ancestor: RmlElement | null = parent; ancestor; ancestor = ancestor.parent) {
		if (ancestor === element) return null;
	}
	if (parent === element.parent) {
		const current = parent.children.indexOf(element);
		if (index === current || index === current + 1) return null;
	}
	return [removeElement(text, element), insertMarkup(text, parent, index, extractMarkup(text, element))];
}

export function duplicateElement(text: string, element: RmlElement): TextEdit | null {
	if (!element.parent) return null;
	return insertMarkup(text, element.parent, element.parent.children.indexOf(element) + 1, extractMarkup(text, element));
}

/** The document's own stylesheet, or the edit that would create one, for a rule with nowhere else to go. */
export function ownStyleBlock(text: string, document: RmlDocument): { range: Range } | { create: (rules: string) => TextEdit } | null {
	const candidates = document.head ? document.head.children.filter((child) => child.tag === 'style' && !attributeOf(child, 'src')) : [];
	const style = candidates[candidates.length - 1];
	if (style) return { range: { from: style.openEnd, to: style.closeStart } };
	if (!document.head) return null;

	const head = document.head;
	return { create: (rules: string) => insertMarkup(text, head, head.children.length, `<style>
	${rules}
</style>`) };
}

/** The element whose opening tag begins at an offset, which is how a selection is found again after an edit. */
export function elementAt(document: RmlDocument, offset: number): RmlElement | null {
	const search = (elements: RmlElement[]): RmlElement | null => {
		for (const element of elements) {
			if (element.start === offset) return element;
			if (offset > element.start && offset < element.end) return search(element.children);
		}
		return null;
	};
	return search(document.roots);
}

/** The innermost element containing an offset, for following the text cursor onto the canvas. */
export function elementContaining(document: RmlDocument, offset: number): RmlElement | null {
	let found: RmlElement | null = null;
	const search = (elements: RmlElement[]) => {
		for (const element of elements) {
			if (offset >= element.start && offset <= element.end) {
				found = element;
				search(element.children);
				return;
			}
		}
	};
	search(document.roots);
	return found;
}
