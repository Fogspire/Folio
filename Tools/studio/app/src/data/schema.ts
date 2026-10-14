/*
 * The typed description of a data model: what a document may bind, and what the game must supply.
 *
 * One file, UI/schema/<model>.json, from which both sides are produced. The mock a document previews
 * against is generated from it, and so is the C++ base class the game's view model derives from, so
 * the two cannot drift the way a hand-kept mock and a hand-kept header do.
 */

import { parseRml, RmlElement } from '../edit/rml';

export type FieldType = 'bool' | 'int' | 'float' | 'string' | 'text' | 'name' | 'enum' | 'array';
export const FIELD_TYPES: FieldType[] = ['bool', 'int', 'float', 'string', 'text', 'name', 'enum', 'array'];

export interface Field {
	type: FieldType;
	/** For an array: a struct named in `structs`, or a scalar type. */
	of?: string;
	/** For an enum: the C++ enum's name. */
	enum?: string;
	/** What the mock holds, which is what the document previews with. */
	sample?: unknown;
	/** The C++ initial value, when it should differ from the type's zero. */
	default?: unknown;
	doc?: string;
	/** Whether the document may write it back, as a `data-value` on an input does. */
	writable?: boolean;
}

export interface Schema {
	model: string;
	/** The C++ class. Its generated base is this name with Base appended. */
	class?: string;
	api?: string;
	category?: string;
	includes?: string[];
	/** Documents that bind this model, for checking the markup against it. */
	documents?: string[];
	/** Set when Unreal wrote the file from an existing class, which is then the source of truth. */
	source?: 'reflected';
	structs?: Record<string, { fields: Record<string, Field> }>;
	fields: Record<string, Field>;
	/** Each command's parameters, as `type Name`. */
	commands?: Record<string, string[]>;
}

export interface ModelUse {
	/** Root names the markup reads. */
	values: Set<string>;
	/** For each array iterated with `data-for`, the members of a row the markup reads. */
	members: Map<string, Set<string>>;
	/** Each command called, with the number of arguments at the call. */
	commands: Map<string, number>;
}

export interface Issue {
	severity: 'error' | 'warning';
	name: string;
	message: string;
}

/** Names an expression may contain that are not bindings: RmlUi's own, and the transform functions. */
const RESERVED = new Set([
	'true', 'false', 'it', 'it_index', 'ev', 'size',
	'format', 'round', 'ceil', 'floor', 'abs', 'min', 'max', 'clamp', 'to_lower', 'to_upper',
]);

/** Every data attribute that carries an expression. `data-loc` is a translation key and `data-model` a name. */
const EXPRESSION_ATTRIBUTE = /^data-(?:text|value|checked|if|visible|rml|attr-[a-z-]+|attrif-[a-z-]+|class-[a-z-]+|style-[a-z-]+)$/;

function pascal(name: string): string {
	return name.split(/[_\-\s]+/).filter(Boolean).map((part) => part[0].toUpperCase() + part.slice(1)).join('');
}

function decodeEntities(value: string): string {
	return value.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&amp;/g, '&');
}

/** The dotted paths an expression reads, with string literals and calls removed. */
function paths(expression: string): string[][] {
	const text = decodeEntities(expression).replace(/'[^']*'|"[^"]*"/g, ' ');
	const out: string[][] = [];
	for (const match of text.matchAll(/[A-Za-z_]\w*(?:\s*\.\s*[A-Za-z_]\w*)*/g)) {
		const before = text.slice(0, match.index).trimEnd();
		// A path that continues one already taken, such as the tail after an index: a[0].b
		if (before.endsWith('.')) continue;
		// A name followed by ( is a call, and expressions have no user-defined ones.
		if (text.slice(match.index + match[0].length).trimStart().startsWith('(') && !match[0].includes('.')) continue;
		out.push(match[0].split('.').map((part) => part.trim()));
	}
	return out;
}

interface Scope {
	use: ModelUse | null;
	/** Loop variables in reach, each naming the array it walks. */
	aliases: Map<string, string>;
}

function readExpression(expression: string, scope: Scope) {
	if (!scope.use) return;
	for (const path of paths(expression)) {
		const [root, member] = path;
		const array = scope.aliases.get(root);
		if (array !== undefined) {
			// `r.Label` reads a member of a row. A bare `r` is a row of scalars and reads nothing more.
			if (member && member !== 'size') {
				if (!scope.use.members.has(array)) scope.use.members.set(array, new Set());
				scope.use.members.get(array)!.add(member);
			}
			continue;
		}
		if (!RESERVED.has(root)) scope.use.values.add(root);
	}
}

/** Reads a document's binding surface: every model it names, and what it asks of each. */
export function extractBindings(text: string): Map<string, ModelUse> {
	const models = new Map<string, ModelUse>();
	const document = parseRml(text);

	const walk = (element: RmlElement, inherited: Scope) => {
		let scope = inherited;
		const attribute = (name: string) => element.attributes.find((entry) => entry.name === name)?.value;

		const model = attribute('data-model');
		if (model) {
			if (!models.has(model)) models.set(model, { values: new Set(), members: new Map(), commands: new Map() });
			scope = { use: models.get(model)!, aliases: new Map() };
		}

		// `data-for="row : Rows"`, `data-for="row, index : Rows"`, or just `data-for="Rows"`, which names the row `it`.
		const loop = attribute('data-for');
		if (loop !== undefined && scope.use) {
			const [left, right] = loop.includes(':') ? loop.split(':') : ['it', loop];
			const names = left.split(',').map((part) => part.trim()).filter(Boolean);
			const source = paths(right)[0];
			if (source) {
				readExpression(right, scope);
				// A loop over a member of an outer row, such as `cell : row.Cells`, is not a root array;
				// its rows are then not something a flat schema can describe, so they are left unnamed.
				const array = scope.aliases.has(source[0]) ? '' : source[0];
				const aliases = new Map(scope.aliases);
				aliases.set(names[0] ?? 'it', array);
				if (names[1]) aliases.set(names[1], '');
				scope = { use: scope.use, aliases };
			}
		}

		for (const entry of element.attributes) {
			if (EXPRESSION_ATTRIBUTE.test(entry.name)) {
				readExpression(entry.value, scope);
			} else if (entry.name.startsWith('data-event-') && scope.use) {
				const call = decodeEntities(entry.value).trim();
				const match = /^([A-Za-z_]\w*)\s*(?:\((.*)\))?$/s.exec(call);
				if (match) {
					const inner = (match[2] ?? '').trim();
					scope.use.commands.set(match[1], inner ? inner.split(',').length : 0);
					readExpression(inner, scope);
				} else {
					// An assignment, such as `bOpen = false`, reads and writes values rather than calling anything.
					readExpression(call, scope);
				}
			}
		}

		// Text directly inside the element, between its children, is where {{ }} lives.
		if (!element.selfClosing && element.tag !== 'style' && element.tag !== 'script') {
			let from = element.openEnd;
			for (const child of [...element.children, null]) {
				const to = child ? child.start : element.closeStart;
				for (const match of text.slice(from, to).matchAll(/\{\{(.+?)\}\}/gs)) readExpression(match[1], scope);
				if (child) from = child.end;
			}
		}

		for (const child of element.children) walk(child, scope);
	};

	for (const root of document.roots) walk(root, { use: null, aliases: new Map() });
	return models;
}

function inferScalar(value: unknown, name: string): Field {
	if (typeof value === 'boolean') return { type: 'bool', sample: value };
	if (typeof value === 'number') {
		// A whole number is an int unless the name says it measures something continuous.
		const fractional = !Number.isInteger(value) || /(Fraction|Ratio|Alpha|Opacity|Angle|Scale|Percent|Time|Speed|Delta)$/.test(name);
		return { type: fractional ? 'float' : 'int', sample: value };
	}
	return { type: 'string', sample: typeof value === 'string' ? value : String(value ?? '') };
}

/** A first schema for a model that only has a mock: the mock's values, typed by what they are. */
export function inferSchema(model: string, mock: Record<string, unknown>, documents: string[] = []): Schema {
	const schema: Schema = { model, category: pascal(model), documents, fields: {} };

	for (const [name, value] of Object.entries(mock)) {
		if (Array.isArray(value)) {
			const first = value[0];
			if (first && typeof first === 'object' && !Array.isArray(first)) {
				const struct = `F${pascal(model)}${pascal(name).replace(/s$/, '')}`;
				const fields: Record<string, Field> = {};
				// Every row contributes, since any one of them may leave a member out.
				for (const row of value) {
					for (const [member, entry] of Object.entries(row as Record<string, unknown>)) {
						if (!(member in fields)) {
							const { sample: _sample, ...field } = inferScalar(entry, member);
							fields[member] = field;
						}
					}
				}
				schema.structs = { ...(schema.structs ?? {}), [struct]: { fields } };
				schema.fields[name] = { type: 'array', of: struct, sample: value };
			} else {
				schema.fields[name] = { type: 'array', of: first === undefined ? 'string' : inferScalar(first, name).type, sample: value };
			}
		} else if (value && typeof value === 'object') {
			// A nested object has no counterpart in a reflected class, so it is carried as a sample only.
			schema.fields[name] = { type: 'string', sample: value };
		} else {
			schema.fields[name] = inferScalar(value, name);
		}
	}
	return schema;
}

function zero(field: Field): unknown {
	switch (field.type) {
		case 'bool': return false;
		case 'int': case 'float': case 'enum': return 0;
		case 'array': return [];
		default: return '';
	}
}

/** The mock a schema describes: every field's sample, or its type's zero where none is given. */
export function mockFromSchema(schema: Schema): Record<string, unknown> {
	const mock: Record<string, unknown> = {};
	for (const [name, field] of Object.entries(schema.fields)) mock[name] = field.sample !== undefined ? field.sample : zero(field);
	return mock;
}

/** Checks what documents bind against what a schema declares, in both directions. */
export function validate(schema: Schema, use: ModelUse): Issue[] {
	const issues: Issue[] = [];

	for (const name of [...use.values].sort()) {
		if (!(name in schema.fields)) issues.push({ severity: 'error', name, message: `'${name}' is bound in the markup but the model has no such field.` });
	}

	for (const [array, members] of use.members) {
		const field = schema.fields[array];
		if (!field || field.type !== 'array' || !field.of) continue;
		const struct = schema.structs?.[field.of];
		if (!struct) continue;
		for (const member of [...members].sort()) {
			if (!(member in struct.fields)) issues.push({ severity: 'error', name: `${array}.${member}`, message: `A row of '${array}' has no '${member}'.` });
		}
	}

	for (const [name, count] of use.commands) {
		const declared = schema.commands?.[name];
		if (!declared) issues.push({ severity: 'error', name, message: `'${name}' is called by the markup but the model declares no such command.` });
		else if (declared.length !== count) issues.push({ severity: 'error', name, message: `'${name}' is called with ${count} argument(s) but takes ${declared.length}.` });
	}

	for (const name of Object.keys(schema.fields).sort()) {
		if (!use.values.has(name)) issues.push({ severity: 'warning', name, message: `'${name}' is declared but nothing binds it.` });
	}

	return issues;
}

/** Folds the uses of one model across several documents into one, since a model can serve many. */
export function mergeUses(uses: ModelUse[]): ModelUse {
	const merged: ModelUse = { values: new Set(), members: new Map(), commands: new Map() };
	for (const use of uses) {
		for (const value of use.values) merged.values.add(value);
		for (const [array, members] of use.members) {
			if (!merged.members.has(array)) merged.members.set(array, new Set());
			for (const member of members) merged.members.get(array)!.add(member);
		}
		for (const [name, count] of use.commands) merged.commands.set(name, count);
	}
	return merged;
}

/** Field names a document could bind at the root of a model, and in a row of each array. */
export function bindableNames(schema: Schema): string[] {
	const names = Object.keys(schema.fields);
	for (const field of Object.values(schema.fields)) {
		const struct = field.type === 'array' && field.of ? schema.structs?.[field.of] : undefined;
		if (struct) names.push(...Object.keys(struct.fields));
	}
	return [...new Set(names)].sort();
}

export function formatJson(value: unknown): string {
	return JSON.stringify(value, null, '\t') + '\n';
}
