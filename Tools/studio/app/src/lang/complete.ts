/*
 * Completion, hover and signature help, as plain data: no editor is involved here, so every answer
 * can be tested against a string and an offset.
 */

import { Field, Schema } from '../data/schema';
import { findRules, Range } from '../edit/rcss';
import { parseRml } from '../edit/rml';
import { Alias, callAt, classify, Context, RcssContext, Span } from './context';
import { attributeInfo, attributesFor, ELEMENTS, EVENTS } from './markup';
import { describeProperty, functionsFor, keywordsFor, propertyNames, vocabulary } from './vocabulary';

export type ItemKind = 'property' | 'keyword' | 'function' | 'tag' | 'attribute' | 'field' | 'command' | 'variable' | 'class' | 'id' | 'model' | 'value';

export interface Item {
	label: string;
	kind: ItemKind;
	/** Monaco snippet syntax when `snippet` is set: `$1`, `${1:name}`, `$0`. */
	insert: string;
	snippet?: boolean;
	range: Span;
	detail?: string;
	doc?: string;
	/** Sorts before everything without one. */
	preferred?: boolean;
	/** Opens completion again once inserted, for a value that is about to be typed. */
	retrigger?: boolean;
}

/** What completion needs from outside the text: the models' schemas and the stylesheets in reach. */
export interface Sources {
	schema(model: string): Schema | null;
	/** Every model with a schema or a mock. */
	models: string[];
	/** Every stylesheet the document links, its own <style> blocks aside. A linked template gives the ranges of its own. */
	stylesheets: { text: string; ranges?: Range[] }[];
}

export const NO_SOURCES: Sources = { schema: () => null, models: [], stylesheets: [] };

const TRANSFORMS = ['format', 'round', 'ceil', 'floor', 'abs', 'min', 'max', 'clamp', 'to_lower', 'to_upper'];
const EVENT_MEMBERS = ['mouse_x', 'mouse_y', 'mouse_delta_x', 'mouse_delta_y', 'button', 'key_identifier', 'value'];
/** The descriptors an `@font-face` block takes, with the keywords each accepts. */
const FONT_FACE_DESCRIPTORS: Record<string, string[]> = { 'font-family': [], src: ['url()'], 'font-style': ['normal', 'italic'], 'font-weight': ['normal', 'bold'] };
const PSEUDO_CLASSES = ['hover', 'active', 'focus', 'focus-visible', 'checked', 'selected', 'disabled', 'first-child', 'last-child', 'only-child', 'first-of-type', 'last-of-type', 'nth-child()', 'nth-of-type()', 'not()', 'empty'];

/** A type an expression path leads to. */
type Resolved =
	| { kind: 'root' }
	| { kind: 'struct'; name: string; fields: Record<string, Field> }
	| { kind: 'array'; field: Field }
	| { kind: 'scalar'; type: string };

function typeOf(schema: Schema, field: Field): Resolved {
	return field.type === 'array' ? { kind: 'array', field } : { kind: 'scalar', type: field.type };
}

function elementOf(schema: Schema, array: Resolved | null): Resolved | null {
	if (!array || array.kind !== 'array') return null;
	const struct = array.field.of ? schema.structs?.[array.field.of] : undefined;
	if (struct && array.field.of) return { kind: 'struct', name: array.field.of, fields: struct.fields };
	return { kind: 'scalar', type: array.field.of ?? 'string' };
}

/** What a dotted path names, following loop variables back to the arrays they walk. */
export function resolvePath(schema: Schema, aliases: Alias[], path: string[]): Resolved | null {
	if (!path.length) return { kind: 'root' };
	const [head, ...rest] = path;

	let current: Resolved | null = null;
	const index = aliases.map((alias) => alias.name).lastIndexOf(head);
	if (index !== -1) {
		const alias = aliases[index];
		current = alias.path.length ? elementOf(schema, resolvePath(schema, aliases.slice(0, index), alias.path)) : { kind: 'scalar', type: 'int' };
	} else if (schema.fields[head]) {
		current = typeOf(schema, schema.fields[head]);
	}

	for (const member of rest) {
		if (!current) return null;
		if (current.kind === 'struct') {
			const field: Field | undefined = current.fields[member];
			current = field ? typeOf(schema, field) : null;
		} else if (current.kind === 'array' && member === 'size') {
			current = { kind: 'scalar', type: 'int' };
		} else {
			return null;
		}
	}
	return current;
}

function describeField(name: string, field: Field): { detail: string; doc?: string } {
	const type = field.type === 'array' ? `array of ${field.of ?? 'string'}` : field.type === 'enum' && field.enum ? `enum ${field.enum}` : field.type;
	const lines = [field.doc, field.writable ? 'Writable.' : '', field.sample !== undefined && field.type !== 'array' ? `Sample: \`${JSON.stringify(field.sample)}\`` : ''].filter(Boolean);
	return { detail: type, doc: lines.join('\n\n') || undefined };
}

function commandSnippet(name: string, parameters: string[]): string {
	if (!parameters.length) return name;
	return `${name}(${parameters.map((parameter, index) => `\${${index + 1}:${parameter.split(/\s+/).pop()}}`).join(', ')})`;
}

/** Every class a stylesheet's selectors name. */
function classesIn(text: string, ranges?: Range[]): string[] {
	const names = new Set<string>();
	for (const range of ranges ?? [{ from: 0, to: text.length }]) {
		for (const rule of findRules(text, range)) for (const match of rule.bareSelector.matchAll(/\.([-\w]+)/g)) names.add(match[1]);
	}
	return [...names];
}

function idsIn(text: string): string[] {
	return [...new Set([...text.matchAll(/\sid\s*=\s*["']([^"']+)["']/g)].map((match) => match[1]))];
}

/** Classes and ids the document can select, from its own stylesheet and every one it links. */
function selectables(text: string, language: 'rml' | 'rcss', sources: Sources): { classes: string[]; ids: string[] } {
	const own = language === 'rml' ? classesIn(text, parseRml(text).styles) : classesIn(text);
	const classes = new Set([...own, ...sources.stylesheets.flatMap((sheet) => classesIn(sheet.text, sheet.ranges))]);
	return { classes: [...classes].sort(), ids: language === 'rml' ? idsIn(text).sort() : [] };
}

export function complete(text: string, offset: number, language: 'rml' | 'rcss', sources: Sources = NO_SOURCES): Item[] {
	const context = classify(text, offset, language);
	switch (context.kind) {
		case 'tag': return completeTag(text, context);
		case 'attribute': return completeAttribute(context);
		case 'value': return completeValue(text, context, sources);
		case 'expression': return completeExpression(text, context, sources);
		case 'rcss': return completeRcss(text, context.context, language, sources);
		default: return [];
	}
}

function completeTag(text: string, context: Extract<Context, { kind: 'tag' }>): Item[] {
	const range = context.word;
	const closed = text[range.to] === '>';
	if (context.closing) {
		const innermost = context.open[context.open.length - 1];
		return innermost ? [{ label: innermost, kind: 'tag', insert: closed ? innermost : innermost + '>', range, preferred: true }] : [];
	}
	const names = new Set([...Object.keys(ELEMENTS), ...vocabulary.elements]);
	return [...names].sort().map((name) => ({ label: name, kind: 'tag', insert: name, range, doc: ELEMENTS[name]?.doc }));
}

function completeAttribute(context: Extract<Context, { kind: 'attribute' }>): Item[] {
	const range = context.word;
	const items: Item[] = [];
	for (const info of attributesFor(context.tag)) {
		if (info.name.endsWith('-')) {
			if (info.name === 'data-event-') {
				for (const event of EVENTS) items.push({ label: info.name + event, kind: 'attribute', insert: `${info.name}${event}="$1"`, snippet: true, range, doc: info.doc, retrigger: true });
			} else {
				items.push({ label: info.name + '…', kind: 'attribute', insert: `${info.name}\${1:name}="$2"`, snippet: true, range, doc: info.doc });
			}
			continue;
		}
		if (context.present.includes(info.name)) continue;
		const own = !!ELEMENTS[context.tag]?.attributes?.includes(info);
		items.push({
			label: info.name,
			kind: 'attribute',
			insert: info.bare ? info.name : `${info.name}="$1"`,
			snippet: !info.bare,
			range,
			doc: info.doc,
			preferred: own,
			retrigger: !info.bare && (!!info.values || !!info.value),
		});
	}
	return items;
}

function completeValue(text: string, context: Extract<Context, { kind: 'value' }>, sources: Sources): Item[] {
	const range = context.word;
	const info = attributeInfo(context.tag, context.attribute);
	if (info?.values) return info.values.map((value) => ({ label: value, kind: 'value', insert: value, range }));
	if (info?.value === 'model') return sources.models.map((model) => ({ label: model, kind: 'model', insert: model, range }));
	if (info?.value === 'class') {
		const present = new Set(text.slice(context.valueStart, context.valueEnd).split(/\s+/));
		return selectables(text, 'rml', sources).classes.filter((name) => !present.has(name) || text.slice(range.from, range.to) === name).map((name) => ({ label: name, kind: 'class', insert: name, range }));
	}
	if (info?.value === 'id') return idsIn(text).map((id) => ({ label: id, kind: 'id', insert: id, range }));
	return [];
}

function completeExpression(text: string, context: Extract<Context, { kind: 'expression' }>, sources: Sources): Item[] {
	const range = context.word;
	if (context.piped) return TRANSFORMS.map((name) => ({ label: name, kind: 'function', insert: name, range }));

	const schema = context.scope.model ? sources.schema(context.scope.model) : null;
	const items: Item[] = [];

	if (context.path.length === 1 && context.path[0] === 'ev' && context.source === 'event') {
		return EVENT_MEMBERS.map((name) => ({ label: name, kind: 'variable', insert: name, range }));
	}

	if (!context.path.length) {
		const seen = new Set<string>();
		for (const alias of [...context.scope.aliases].reverse()) {
			if (seen.has(alias.name)) continue;
			seen.add(alias.name);
			items.push({ label: alias.name, kind: 'variable', insert: alias.name, range, detail: alias.path.length ? `row of ${alias.path.join('.')}` : 'index', preferred: true });
		}
		if (schema) {
			for (const [name, field] of Object.entries(schema.fields)) items.push({ label: name, kind: 'field', insert: name, range, ...describeField(name, field) });
			// A command is named only where a statement begins.
			if (context.source === 'event' && /(^|;)\s*$/.test(text.slice(context.expressionStart, range.from))) {
				for (const [name, parameters] of Object.entries(schema.commands ?? {})) {
					items.push({ label: name, kind: 'command', insert: commandSnippet(name, parameters), snippet: true, range, detail: `(${parameters.join(', ')})`, preferred: true });
				}
			}
		}
		if (context.source === 'event') items.push({ label: 'ev', kind: 'variable', insert: 'ev', range, detail: 'the event' });
		return items;
	}

	if (!schema) return [];
	const resolved = resolvePath(schema, context.scope.aliases, context.path);
	if (resolved?.kind === 'struct') {
		for (const [name, field] of Object.entries(resolved.fields)) items.push({ label: name, kind: 'field', insert: name, range, ...describeField(name, field) });
	} else if (resolved?.kind === 'array') {
		items.push({ label: 'size', kind: 'field', insert: 'size', range, detail: 'int' });
	}
	return items;
}

function completeRcss(text: string, context: RcssContext, language: 'rml' | 'rcss', sources: Sources): Item[] {
	switch (context.kind) {
		case 'property': {
			if (context.atRule === '@spritesheet') {
				return ['src', 'resolution'].map((name) => ({ label: name, kind: 'property', insert: `${name}: $0;`, snippet: true, range: context.word }));
			}
			if (context.atRule === '@font-face') {
				return Object.keys(FONT_FACE_DESCRIPTORS).map((name) => ({ label: name, kind: 'property', insert: `${name}: $0;`, snippet: true, range: context.word }));
			}
			if (context.atRule && context.atRule !== '@keyframes') return [];
			const followed = /^\s*:/.test(text.slice(context.word.to));
			return propertyNames().map((name) => ({
				label: name,
				kind: 'property',
				insert: followed ? name : `${name}: $0;`,
				snippet: !followed,
				range: context.word,
				doc: describeProperty(name) ?? undefined,
				retrigger: !followed && (keywordsFor(name).length > 0 || functionsFor(name).length > 0),
			}));
		}
		case 'value': {
			if (context.atRule === '@font-face') {
				return (FONT_FACE_DESCRIPTORS[context.property] ?? []).map((word) => ({ label: word, kind: 'keyword', insert: word, range: context.word }));
			}
			if (context.atRule && context.atRule !== '@keyframes') return [];
			const items: Item[] = keywordsFor(context.property).map((word) => ({ label: word, kind: 'keyword', insert: word, range: context.word }));
			if (!context.fn) {
				for (const fn of functionsFor(context.property)) items.push({ label: fn, kind: 'function', insert: `${fn}($1)`, snippet: true, range: context.word });
			}
			return items;
		}
		case 'selector': {
			const prefix = context.prefix;
			const segment = Math.max(prefix.lastIndexOf('.'), prefix.lastIndexOf('#'));
			const before = text[context.word.from - 1];
			const { classes, ids } = selectables(text, language, sources);
			if (before === ':' && segment === -1) {
				return PSEUDO_CLASSES.map((name) => ({ label: name, kind: 'keyword', insert: name.endsWith('()') ? name.slice(0, -1) + '$1)' : name, snippet: name.endsWith('()'), range: context.word }));
			}
			if (segment !== -1) {
				const range = { from: context.word.from + segment + 1, to: context.word.to };
				const names = prefix[segment] === '.' ? classes : ids;
				return names.map((name) => ({ label: name, kind: prefix[segment] === '.' ? 'class' : 'id', insert: name, range }));
			}
			const tags = [...new Set([...Object.keys(ELEMENTS), ...vocabulary.elements])].sort();
			return [
				...tags.map((name): Item => ({ label: name, kind: 'tag', insert: name, range: context.word })),
				...classes.map((name): Item => ({ label: '.' + name, kind: 'class', insert: '.' + name, range: context.word })),
			];
		}
		default:
			return [];
	}
}

export interface Hover {
	markdown: string;
	range: Span;
}

export function hover(text: string, offset: number, language: 'rml' | 'rcss', sources: Sources = NO_SOURCES): Hover | null {
	const context = classify(text, offset, language);
	const word = (span: Span) => text.slice(span.from, span.to);

	switch (context.kind) {
		case 'tag': {
			const name = word(context.word).toLowerCase();
			const info = ELEMENTS[name];
			if (info) return { markdown: `**<${name}>**\n\n${info.doc}`, range: context.word };
			return name ? { markdown: `**<${name}>**\n\nA generic element: it does what the stylesheet says and nothing more.`, range: context.word } : null;
		}
		case 'attribute': {
			const name = word(context.word);
			const info = attributeInfo(context.tag, name);
			return info ? { markdown: `**${name}**\n\n${info.doc}`, range: context.word } : null;
		}
		case 'expression': {
			const name = word(context.word);
			if (!name || !context.scope.model) return null;
			const schema = sources.schema(context.scope.model);
			if (!schema) return null;
			const command = schema.commands?.[name];
			if (command && context.source === 'event' && !context.path.length) return { markdown: `\`${name}(${command.join(', ')})\`\n\nA command on \`${context.scope.model}\`.`, range: context.word };
			const parent = resolvePath(schema, context.scope.aliases, context.path);
			let field: Field | undefined;
			if (parent?.kind === 'root') field = schema.fields[name];
			else if (parent?.kind === 'struct') field = parent.fields[name];
			if (field) {
				const { detail, doc } = describeField(name, field);
				return { markdown: `\`${name}: ${detail}\`${doc ? '\n\n' + doc : ''}`, range: context.word };
			}
			const alias = [...context.scope.aliases].reverse().find((entry) => entry.name === name);
			if (alias && parent?.kind === 'root') return { markdown: alias.path.length ? `\`${name}\`: a row of \`${alias.path.join('.')}\`` : `\`${name}\`: the row's index`, range: context.word };
			return null;
		}
		case 'rcss': {
			const inner = context.context;
			if (inner.kind === 'property') {
				const doc = describeProperty(word(inner.word));
				return doc ? { markdown: doc, range: inner.word } : null;
			}
			if (inner.kind === 'value') {
				const name = word(inner.word);
				if (text[inner.word.to] === '(' && functionsFor(inner.property).includes(name)) return { markdown: `**${name}()**: ${effectKind(inner.property)}.`, range: inner.word };
				return null;
			}
			return null;
		}
		default:
			return null;
	}
}

function effectKind(property: string): string {
	if (property === 'decorator' || property === 'mask-image') return 'a decorator';
	if (property === 'font-effect') return 'a font effect';
	if (property === 'filter' || property === 'backdrop-filter') return 'a filter';
	return 'a function';
}

export interface Signature {
	label: string;
	parameters: string[];
	active: number;
}

/** The command being called in a `data-event-*` value, with the argument the cursor is in. */
export function signature(text: string, offset: number, sources: Sources = NO_SOURCES): Signature | null {
	const context = classify(text, offset, 'rml');
	if (context.kind !== 'expression' || context.source !== 'event' || !context.scope.model) return null;
	const call = callAt(text, offset, context.expressionStart);
	if (!call) return null;
	const parameters = sources.schema(context.scope.model)?.commands?.[call.command];
	if (!parameters) return null;
	return { label: `${call.command}(${parameters.join(', ')})`, parameters, active: Math.min(call.argument, Math.max(0, parameters.length - 1)) };
}
