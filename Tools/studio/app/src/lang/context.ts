/*
 * What the text cursor is in: a tag name, an attribute, a binding expression, a stylesheet.
 *
 * Read by scanning from the start of the text to the cursor, so a tag still being typed classifies
 * as well as a finished one. Everything that completes, describes or checks starts here.
 */

import { parseRml, RmlElement } from '../edit/rml';

export interface Span {
	from: number;
	to: number;
}

/** A loop variable in reach, and the path of the array it walks. */
export interface Alias {
	name: string;
	/** Empty for an index variable, which is a number. */
	path: string[];
}

export interface Scope {
	/** The nearest data-model, or null outside every one. */
	model: string | null;
	aliases: Alias[];
}

export type RcssContext =
	| { kind: 'selector'; word: Span; prefix: string }
	| { kind: 'property'; word: Span; prefix: string; atRule: string | null; present: string[] }
	| { kind: 'value'; property: string; word: Span; prefix: string; valueStart: number; fn: string | null; atRule: string | null }
	| { kind: 'none' };

export type Context =
	| { kind: 'tag'; word: Span; prefix: string; closing: boolean; open: string[] }
	| { kind: 'attribute'; tag: string; word: Span; prefix: string; present: string[] }
	| { kind: 'value'; tag: string; attribute: string; word: Span; prefix: string; valueStart: number; valueEnd: number; scope: Scope }
	| { kind: 'expression'; source: 'text' | 'attribute' | 'event' | 'loop'; tag: string; attribute: string | null; expressionStart: number; word: Span; prefix: string; path: string[]; piped: boolean; scope: Scope }
	| { kind: 'rcss'; inline: boolean; context: RcssContext }
	| { kind: 'none' };

const NAME = /[-\w:.]/;
const WORD = /[-\w]/;

/** The identifier around an offset, as far as it runs either way. */
export function wordAt(text: string, offset: number, pattern: RegExp = WORD, from = 0, to = text.length): Span {
	let start = offset;
	while (start > from && pattern.test(text[start - 1])) start--;
	let end = offset;
	while (end < to && pattern.test(text[end])) end++;
	return { from: start, to: end };
}

const RAW = new Set(['style', 'script', 'text']);

/** Attributes whose values are binding expressions. */
export const EXPRESSION_ATTRIBUTE = /^data-(?:text|value|checked|if|visible|rml|attr-[\w-]+|attrif-[\w-]+|class-[\w-]+|style-[\w-]+|alias-[\w-]+)$/;

/** The model and loop variables in reach of an element's contents, outermost first. */
export function scopeOf(element: RmlElement | null, includeOwnLoop = true): Scope {
	const chain: RmlElement[] = [];
	for (let at = element; at; at = at.parent) chain.unshift(at);
	const scope: Scope = { model: null, aliases: [] };
	for (const entry of chain) {
		const model = entry.attributes.find((attribute) => attribute.name === 'data-model')?.value;
		if (model) {
			scope.model = model;
			scope.aliases = [];
		}
		const loop = entry.attributes.find((attribute) => attribute.name === 'data-for')?.value;
		if (loop !== undefined && (includeOwnLoop || entry !== element)) scope.aliases.push(...loopAliases(loop));
		for (const attribute of entry.attributes) {
			if (attribute.name.startsWith('data-alias-')) scope.aliases.push({ name: attribute.name.slice('data-alias-'.length), path: pathOf(attribute.value) });
		}
	}
	return scope;
}

/** `row : Rows`, `row, i : Rows`, or `Rows`, which names the row `it` and the index `it_index`. */
export function loopAliases(loop: string): Alias[] {
	const [left, right] = loop.includes(':') ? loop.split(':') : ['', loop];
	const names = left.split(',').map((part) => part.trim()).filter(Boolean);
	const path = pathOf(right);
	return [{ name: names[0] ?? 'it', path }, { name: names[1] ?? 'it_index', path: [] }];
}

function pathOf(expression: string): string[] {
	const match = /[A-Za-z_]\w*(?:\s*\.\s*[A-Za-z_]\w*)*/.exec(expression);
	return match ? match[0].split('.').map((part) => part.trim()) : [];
}

/** The dotted path leading up to the word at the cursor, and whether it follows a `|` transform. */
function expressionAt(text: string, start: number, offset: number): { path: string[]; piped: boolean } {
	const before = text.slice(start, offset).replace(/'[^']*'|"[^"]*"/g, (literal) => ' '.repeat(literal.length));
	const match = /((?:[A-Za-z_]\w*\s*\.\s*)*)[A-Za-z_]?\w*$/.exec(before)!;
	const path = match[1].split('.').map((part) => part.trim()).filter(Boolean);
	const piped = !path.length && /\|\s*$/.test(before.slice(0, match.index));
	return { path, piped };
}

export function classify(text: string, offset: number, language: 'rml' | 'rcss'): Context {
	if (language === 'rcss') return { kind: 'rcss', inline: false, context: classifyRcss(text, offset, 0, text.length) };
	return classifyRml(text, offset);
}

interface OpenTag {
	tag: string;
	start: number;
	attributes: string[];
}

function classifyRml(text: string, offset: number): Context {
	let index = 0;
	let open: OpenTag | null = null;
	const stack: string[] = [];

	const elementScope = (includeOwnLoop: boolean) => {
		const document = parseRml(text);
		let found: RmlElement | null = null;
		const search = (elements: RmlElement[]) => {
			for (const element of elements) {
				if (offset > element.start && offset <= element.end) {
					found = element;
					search(element.children);
					return;
				}
			}
		};
		search(document.roots);
		return scopeOf(found, includeOwnLoop);
	};

	while (index < offset) {
		if (!open) {
			if (text.startsWith('<!--', index)) {
				const end = text.indexOf('-->', index + 4);
				if (end === -1 || end + 3 > offset) return { kind: 'none' };
				index = end + 3;
				continue;
			}
			if (text.startsWith('{{', index)) {
				const end = text.indexOf('}}', index + 2);
				if (end === -1 || end >= offset) {
					const word = wordAt(text, offset, /\w/);
					const { path, piped } = expressionAt(text, index + 2, offset);
					return { kind: 'expression', source: 'text', tag: stack[stack.length - 1] ?? '', attribute: null, expressionStart: index + 2, word, prefix: text.slice(word.from, offset), path, piped, scope: elementScope(true) };
				}
				index = end + 2;
				continue;
			}
			if (text[index] === '<') {
				if (text[index + 1] === '/') {
					const end = text.indexOf('>', index);
					if (end === -1 || end >= offset) {
						const word = wordAt(text, offset, NAME, index + 2);
						return { kind: 'tag', word, prefix: text.slice(word.from, offset), closing: true, open: [...stack] };
					}
					const tag = text.slice(index + 2, end).trim().toLowerCase();
					const at = stack.lastIndexOf(tag);
					if (at !== -1) stack.length = at;
					index = end + 1;
					continue;
				}
				if (text[index + 1] === '!' || text[index + 1] === '?') {
					const end = text.indexOf('>', index);
					if (end === -1 || end >= offset) return { kind: 'none' };
					index = end + 1;
					continue;
				}
				const name = /^[A-Za-z_][\w:.-]*/.exec(text.slice(index + 1, index + 80));
				if (index + 1 === offset || (name && index + 1 + name[0].length >= offset)) {
					const word = wordAt(text, offset, NAME, index + 1);
					return { kind: 'tag', word, prefix: text.slice(word.from, offset), closing: false, open: [...stack] };
				}
				if (!name) {
					index += 1;
					continue;
				}
				open = { tag: name[0].toLowerCase(), start: index, attributes: [] };
				index += 1 + name[0].length;
				continue;
			}
			index += 1;
			continue;
		}

		// Inside an opening tag.
		const character = text[index];
		if (character === '>' || (character === '/' && text[index + 1] === '>')) {
			const selfClosing = character === '/';
			index += selfClosing ? 2 : 1;
			const tag = open.tag;
			open = null;
			if (selfClosing) continue;
			if (RAW.has(tag)) {
				const close = new RegExp(`</${tag}\\s*>`, 'i').exec(text.slice(index));
				const closeStart = close ? index + close.index : text.length;
				if (closeStart >= offset) {
					if (tag === 'style') return { kind: 'rcss', inline: false, context: classifyRcss(text, offset, index, closeStart) };
					return { kind: 'none' };
				}
				index = closeStart + close![0].length;
				continue;
			}
			stack.push(tag);
			continue;
		}
		if (/\s/.test(character)) {
			index += 1;
			continue;
		}

		const nameStart = index;
		while (index < text.length && !/[\s=>]/.test(text[index]) && !(text[index] === '/' && text[index + 1] === '>')) index++;
		const name = text.slice(nameStart, index);
		if (!name) {
			index += 1;
			continue;
		}
		if (index >= offset) {
			const word = wordAt(text, offset, NAME, nameStart);
			return { kind: 'attribute', tag: open.tag, word, prefix: text.slice(word.from, offset), present: open.attributes };
		}
		open.attributes.push(name);

		let at = index;
		while (at < text.length && /\s/.test(text[at])) at++;
		if (text[at] !== '=') continue;
		at += 1;
		while (at < text.length && /\s/.test(text[at])) at++;
		const quote = text[at];
		if (quote !== '"' && quote !== "'") {
			index = at;
			continue;
		}
		const valueStart = at + 1;
		const close = text.indexOf(quote, valueStart);
		const valueEnd = close === -1 ? text.length : close;
		if (valueEnd >= offset && valueStart <= offset) return classifyValue(text, offset, open.tag, name, valueStart, valueEnd, elementScope);
		index = Math.min(text.length, valueEnd + 1);
	}

	if (open) {
		// The cursor sits in an opening tag, between attributes.
		return { kind: 'attribute', tag: open.tag, word: { from: offset, to: offset }, prefix: '', present: open.attributes };
	}
	return { kind: 'none' };
}

function classifyValue(text: string, offset: number, tag: string, attribute: string, valueStart: number, valueEnd: number, elementScope: (includeOwnLoop: boolean) => Scope): Context {
	if (attribute === 'style') return { kind: 'rcss', inline: true, context: classifyRcss(text, offset, valueStart, valueEnd, true) };

	const event = attribute.startsWith('data-event-');
	const loop = attribute === 'data-for';
	if (event || loop || EXPRESSION_ATTRIBUTE.test(attribute)) {
		// The array a loop walks is read in the scope outside the loop, and its alias names are new.
		let start = valueStart;
		if (loop) {
			const colon = text.indexOf(':', valueStart);
			if (colon !== -1 && colon < valueEnd) {
				if (offset <= colon) return { kind: 'none' };
				start = colon + 1;
			}
		}
		const word = wordAt(text, offset, /\w/, valueStart, valueEnd);
		const { path, piped } = expressionAt(text, start, offset);
		return {
			kind: 'expression',
			source: event ? 'event' : loop ? 'loop' : 'attribute',
			tag,
			attribute,
			expressionStart: start,
			word,
			prefix: text.slice(word.from, offset),
			path,
			piped,
			scope: elementScope(!loop),
		};
	}

	// Space-separated lists complete one word at a time.
	const word = attribute === 'class' ? wordAt(text, offset, WORD, valueStart, valueEnd) : { from: valueStart, to: valueEnd };
	return { kind: 'value', tag, attribute, word, prefix: text.slice(word.from, offset), valueStart, valueEnd, scope: elementScope(true) };
}

type Block = 'rules' | 'keyframes' | 'body' | 'at-body';

/** Skips a comment or string starting at index, or returns index unchanged. */
function skip(text: string, index: number): number {
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
 * Classifies an offset inside a stylesheet, or inside a style attribute when `inline`, which is one
 * rule body with no braces.
 */
export function classifyRcss(text: string, offset: number, from: number, to: number, inline = false): RcssContext {
	const blocks: { kind: Block; atRule: string | null }[] = inline ? [{ kind: 'body', atRule: null }] : [];
	let statementStart = from;
	let present: string[] = [];
	let index = from;

	while (index < offset) {
		const skipped = skip(text, index);
		if (skipped !== index) {
			if (skipped > offset) return { kind: 'none' };
			if (text.slice(statementStart, index).trim() === '') statementStart = skipped;
			index = skipped;
			continue;
		}
		const character = text[index];
		if (character === '{') {
			const prelude = text.slice(statementStart, index).trim();
			const outer = blocks[blocks.length - 1]?.kind ?? 'rules';
			const atRule = /^@[\w-]+/.exec(prelude)?.[0] ?? null;
			let kind: Block = 'body';
			if (atRule === '@media' || atRule === '@supports') kind = 'rules';
			else if (atRule === '@keyframes') kind = 'keyframes';
			else if (atRule) kind = 'at-body';
			else if (outer === 'keyframes') kind = 'body';
			blocks.push({ kind, atRule: atRule ?? (outer === 'keyframes' ? '@keyframes' : null) });
			present = [];
			statementStart = index + 1;
		} else if (character === '}') {
			blocks.pop();
			statementStart = index + 1;
		} else if (character === ';') {
			const name = /^\s*([-\w]+)\s*:/.exec(text.slice(statementStart, index));
			if (name) present.push(name[1]);
			statementStart = index + 1;
		}
		index += 1;
	}

	const block = blocks[blocks.length - 1] ?? { kind: 'rules' as Block, atRule: null };
	const statement = text.slice(statementStart, offset);
	const word = wordAt(text, offset, WORD, from, to);
	const prefix = text.slice(word.from, offset);

	if (block.kind === 'rules' || block.kind === 'keyframes') {
		const selectorWord = wordAt(text, offset, /[-\w.#]/, from, to);
		return { kind: 'selector', word: selectorWord, prefix: text.slice(selectorWord.from, offset) };
	}

	const colon = statement.indexOf(':');
	if (colon === -1) return { kind: 'property', word, prefix, atRule: block.atRule, present };

	const property = statement.slice(0, colon).trim();
	const valueStart = statementStart + colon + 1;
	return { kind: 'value', property, word, prefix, valueStart, fn: enclosingFunction(text.slice(valueStart, offset)), atRule: block.atRule };
}

/** The function an open parenthesis before the cursor belongs to, as in `image(|`. */
function enclosingFunction(value: string): string | null {
	let depth = 0;
	for (let at = value.length - 1; at >= 0; at--) {
		if (value[at] === ')') depth++;
		else if (value[at] === '(') {
			if (depth === 0) return /([-\w]+)\s*$/.exec(value.slice(0, at))?.[1] ?? null;
			depth--;
		}
	}
	return null;
}

/** The command an event value calls, and which of its arguments the cursor is in. */
export function callAt(text: string, offset: number, valueStart: number): { command: string; argument: number } | null {
	const before = text.slice(valueStart, offset);
	const open = before.indexOf('(');
	if (open === -1 || before.indexOf(')', open) !== -1) return null;
	const command = /^\s*([A-Za-z_]\w*)\s*$/.exec(before.slice(0, open));
	if (!command) return null;
	let argument = 0;
	let depth = 0;
	for (const character of before.slice(open + 1).replace(/'[^']*'|"[^"]*"/g, '')) {
		if (character === '(') depth++;
		else if (character === ')') depth--;
		else if (character === ',' && depth === 0) argument++;
	}
	return { command: command[1], argument };
}
