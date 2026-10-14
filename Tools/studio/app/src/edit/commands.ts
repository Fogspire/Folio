import type { Inspection, RuleProperty } from '../engine';
import { monaco } from '../monaco';
import { extension, isUnder } from '../paths';
import { engine, findNode, flushPush, getModel, openDocument, peekModel, resolveSourcePath, select, store } from '../session';
import { appendRule, findRule, Range, removeDeclaration, setDeclaration, setInlineStyle, TextEdit } from './rcss';
import {
	attributeOf, duplicateElement, elementAt, extractMarkup, insertMarkup, moveElement, ownStyleBlock, parseRml, removeElement,
	RmlDocument, RmlElement, setAttribute, setText, unescapeText,
} from './rml';
import { mapTree, SourceMap } from './sourcemap';
import type { Target } from './target';

export type { Target };

export interface Source {
	path: string;
	text: string;
	document: RmlDocument;
	map: SourceMap;
}

/** Elements that hold no children, so something added at one lands beside it. */
export const LEAF_TAGS = new Set(['img', 'input', 'textarea', 'select', 'progress', 'progressbar', 'handle', 'text']);

let cached: { version: number; revision: number; path: string; source: Source } | null = null;
/** The file each visual edit landed in, newest last, so undo reverses them in order across files. */
const undoStack: string[] = [];
const redoStack: string[] = [];

/** The open document's markup, parsed and matched against the live tree. */
export function currentSource(): Source | null {
	const state = store.get();
	if (!state.document || !state.tree) return null;
	const model = peekModel(state.document);
	if (!model) return null;

	const version = model.getVersionId();
	if (cached && cached.version === version && cached.revision === state.revision && cached.path === state.document) return cached.source;

	const text = model.getValue();
	const document = parseRml(text);
	if (!document.body) return null;
	const source: Source = { path: state.document, text, document, map: mapTree(state.tree, document.body) };
	cached = { version, revision: state.revision, path: state.document, source };
	return source;
}

export function sourceElement(id: string | null): RmlElement | null {
	if (id === null) return null;
	return currentSource()?.map.byId.get(id) ?? null;
}

/** Whether an element can be edited visually: it has to be one the markup actually contains. */
export function isEditable(id: string | null): boolean {
	return sourceElement(id) !== null;
}

export function dpRatio(): number {
	const state = store.get();
	return state.dpRatio ?? Math.min(state.width, state.height) / 1080;
}

function tidy(value: number): string {
	return String(Math.round(value * 10) / 10);
}

/** A pointer has no finer grain than a pixel, so a length dragged to is a whole number of its unit. */
function whole(value: number): string {
	return String(Math.round(value));
}

/**
 * A length moved by a number of pixels, kept in the unit it was written in.
 *
 * `basis` is what a percentage is a percentage of. A value with no usable unit, such as `auto`, is
 * rewritten in dp from where the element currently is.
 */
export function shiftLength(current: string | undefined, deltaPixels: number, basis: number, fallbackPixels: number): string {
	const state = store.get();
	const match = /^(-?\d*\.?\d+)(dp|px|%|vw|vh)$/.exec((current ?? '').trim());
	if (match) {
		const amount = Number(match[1]);
		switch (match[2]) {
			case 'dp': return whole(amount + deltaPixels / dpRatio()) + 'dp';
			case 'px': return whole(amount + deltaPixels) + 'px';
			case '%': return basis > 0 ? tidy(amount + (deltaPixels / basis) * 100) + '%' : current!;
			case 'vw': return tidy(amount + (deltaPixels / state.width) * 100) + 'vw';
			case 'vh': return tidy(amount + (deltaPixels / state.height) * 100) + 'vh';
		}
	}
	return whole((fallbackPixels + deltaPixels) / dpRatio()) + 'dp';
}

function applyToModel(model: monaco.editor.ITextModel, edits: TextEdit[]) {
	model.pushStackElement();
	model.pushEditOperations(
		null,
		edits.map((edit) => ({ range: monaco.Range.fromPositions(model.getPositionAt(edit.from), model.getPositionAt(edit.to)), text: edit.text })),
		() => null,
	);
	model.pushStackElement();
}

/** Where an offset in the old text ends up once edits have been applied. */
function shiftOffset(edits: TextEdit[], offset: number): number {
	let shifted = offset;
	for (const edit of edits) {
		if (edit.to <= offset) shifted += edit.text.length - (edit.to - edit.from);
	}
	return shifted;
}

/** Where the first element of an inserted edit begins in the new text. */
function insertedOffset(edits: TextEdit[], inserted: TextEdit): number {
	return shiftOffset(edits.filter((edit) => edit !== inserted), inserted.from) + Math.max(0, inserted.text.indexOf('<'));
}

/**
 * Writes edits to a file, redraws, and selects whatever now starts at `selectOffset` in the markup.
 *
 * An edit can change how many elements there are, so the selection cannot be carried over by its
 * position in the tree. It is carried by where its tag starts in the text, which the edit knows.
 */
async function commit(path: string, edits: TextEdit[], selectOffset: number | null = null) {
	const model = await getModel(path);
	if (!model || !edits.length) return;

	applyToModel(model, edits);
	undoStack.push(path);
	redoStack.length = 0;
	flushPush();

	if (selectOffset !== null) {
		const source = currentSource();
		const element = source ? elementAt(source.document, selectOffset) : null;
		const ids = element && source ? source.map.ids.get(element) : null;
		select(ids?.[0] ?? null);
	}
}

/** The file an undo with no visual edit behind it applies to: the one in Code, or else the document. */
function fallbackPath(): string | null {
	const state = store.get();
	return state.activeFile ?? state.document;
}

/** An edit undone in a document that is not on the canvas would not be seen, so that document is shown. */
function follow(path: string) {
	if (/\.rml$/i.test(path) && path !== store.get().document && store.get().openFiles.includes(path)) openDocument(path);
}

export function undo() {
	const path = undoStack.pop() ?? fallbackPath();
	const model = path ? peekModel(path) : null;
	if (!path || !model || !model.canUndo()) return;
	model.undo();
	redoStack.push(path);
	flushPush();
	follow(path);
}

export function redo() {
	const path = redoStack.pop() ?? fallbackPath();
	const model = path ? peekModel(path) : null;
	if (!path || !model || !model.canRedo()) return;
	model.redo();
	undoStack.push(path);
	flushPush();
	follow(path);
}

function withElement(id: string): { source: Source; element: RmlElement } | null {
	const source = currentSource();
	const element = source?.map.byId.get(id);
	return source && element ? { source, element } : null;
}

export async function setElementAttribute(id: string, name: string, value: string | null) {
	const found = withElement(id);
	if (!found) return;
	const edit = setAttribute(found.source.text, found.element, name, value);
	if (edit) await commit(found.source.path, [edit], found.element.start);
}

export async function renameElementAttribute(id: string, from: string, to: string) {
	const found = withElement(id);
	const attribute = found ? attributeOf(found.element, from) : null;
	if (!found || !attribute || !to || from === to) return;
	await commit(found.source.path, [{ from: attribute.start, to: attribute.start + from.length, text: to }], found.element.start);
}

/** The text an element holds, when text is all it holds. */
export function elementText(id: string | null): string | null {
	const source = currentSource();
	const element = id !== null ? source?.map.byId.get(id) : null;
	if (!source || !element || element.children.length || element.tag === 'body') return null;
	if (element.selfClosing) return '';
	const raw = source.text.slice(element.openEnd, element.closeStart);
	return element.tag === 'text' ? raw : unescapeText(raw);
}

export async function setElementText(id: string, value: string) {
	const found = withElement(id);
	if (!found || found.element.children.length) return;
	await commit(found.source.path, [setText(found.source.text, found.element, value)], found.element.start);
}

export async function deleteElement(id: string) {
	const found = withElement(id);
	if (!found || found.element.tag === 'body') return;
	await commit(found.source.path, [removeElement(found.source.text, found.element)]);
	select(null);
}

export async function duplicate(id: string) {
	const found = withElement(id);
	if (!found || found.element.tag === 'body') return;
	const edit = duplicateElement(found.source.text, found.element);
	if (edit) await commit(found.source.path, [edit], insertedOffset([edit], edit));
}

/** Turns a position among a live element's children into one among the markup's. */
function sourceIndex(source: Source, parentId: string, parent: RmlElement, liveIndex: number): number {
	const sibling = source.map.byId.get(parentId === '' ? String(liveIndex) : `${parentId}/${liveIndex}`);
	const index = sibling ? parent.children.indexOf(sibling) : -1;
	return index === -1 ? parent.children.length : index;
}

/** Inserts markup as a child of a live element, before its child at `liveIndex`. */
export async function insertInto(parentId: string, liveIndex: number, markup: string) {
	const found = withElement(parentId);
	if (!found) return;
	const edit = insertMarkup(found.source.text, found.element, sourceIndex(found.source, parentId, found.element, liveIndex), markup);
	await commit(found.source.path, [edit], insertedOffset([edit], edit));
}

export async function moveInto(id: string, parentId: string, liveIndex: number) {
	const found = withElement(id);
	const parent = withElement(parentId);
	if (!found || !parent || found.element.tag === 'body') return;
	const edits = moveElement(found.source.text, found.element, parent.element, sourceIndex(found.source, parentId, parent.element, liveIndex));
	if (edits) await commit(found.source.path, edits, insertedOffset(edits, edits[1]));
}

/** The markup of an element as written, for the clipboard. */
export function elementMarkup(id: string): string | null {
	const found = withElement(id);
	if (!found || found.element.tag === 'body') return null;
	return extractMarkup(found.source.text, found.element);
}

/**
 * Inserts markup where the selection suggests: inside it when it can hold children, after it when
 * it cannot, and at the end of the document when nothing is selected.
 */
export async function insertNear(markup: string) {
	const { selection, tree } = store.get();
	const node = selection !== null && isEditable(selection) ? findNode(tree, selection) : null;
	if (!node || !tree) return insertInto('', tree?.children.length ?? 0, markup);

	const element = sourceElement(node.id);
	const leaf = node.id !== '' && (LEAF_TAGS.has(node.tag) || (element?.hasText ?? false));
	if (!leaf) return insertInto(node.id, node.children.length, markup);

	const slash = node.id.lastIndexOf('/');
	const parent = slash === -1 ? '' : node.id.slice(0, slash);
	return insertInto(parent, Number(node.id.slice(slash + 1)) + 1, markup);
}

export async function wrapElement(id: string, tag = 'div') {
	const found = withElement(id);
	if (!found || found.element.tag === 'body' || !found.element.parent) return;
	const { source, element } = found;
	const inner = extractMarkup(source.text, element).split('\n').map((line) => '\t' + line).join('\n');
	const index = element.parent!.children.indexOf(element);
	// Inserted where the element was, then the element removed, so the wrapper takes its place.
	const insert = insertMarkup(source.text, element.parent!, index, `<${tag}>\n${inner}\n</${tag}>`);
	await commit(source.path, [insert, removeElement(source.text, element)], insertedOffset([insert], insert));
}

// Styles

/** Whether a stylesheet belongs to the project, rather than being one Folio ships to every project. */
export function isProjectFile(reported: string): boolean {
	const { project } = store.get();
	const path = resolveSourcePath(reported);
	if (!project || !path) return false;
	return project.root === project.folioRoot || !isUnder(project.folioRoot, path);
}

function sameRule(a: { rule: string; file: string; line: number }, b: { rule: string; file: string; line: number }): boolean {
	return a.rule === b.rule && a.file === b.file && a.line === b.line;
}

/** The rules that matched this element itself and can be edited, most recently listed last. */
export function ownRules(inspection: Inspection): RuleProperty[] {
	const out: RuleProperty[] = [];
	for (const property of inspection.rules) {
		if (property.inherited || !isProjectFile(property.file)) continue;
		if (!out.some((entry) => sameRule(entry, property))) out.push(property);
	}
	return out;
}

/** Everywhere a new property on this element could be written. */
export function candidateTargets(inspection: Inspection): Target[] {
	const targets: Target[] = ownRules(inspection).map((property) => ({ kind: 'rule', rule: property.rule, file: property.file, line: property.line }));
	const taken = new Set(targets.map((target) => (target.kind === 'rule' ? target.rule : '')));

	const id = inspection.attributes.id;
	const firstClass = (inspection.attributes.class ?? '').split(/\s+/).filter(Boolean)[0];
	if (id && !taken.has('#' + id)) targets.push({ kind: 'new-rule', selector: '#' + id });
	if (firstClass && !taken.has('.' + firstClass)) targets.push({ kind: 'new-rule', selector: '.' + firstClass });
	targets.push({ kind: 'inline' });
	return targets;
}

/**
 * Where a property is written when nothing has been chosen.
 *
 * The rule or style attribute that already sets it, since changing a value where it is written is never a
 * surprise. Otherwise the element's own id rule, then any rule of its own, then its style attribute.
 */
export function defaultTarget(inspection: Inspection, property: string): Target {
	if (property in inspection.inline) return { kind: 'inline' };
	const setter = inspection.rules.find((entry) => entry.name === property && !entry.inherited && isProjectFile(entry.file));
	if (setter) return { kind: 'rule', rule: setter.rule, file: setter.file, line: setter.line };

	const chosen = store.get().writeTarget;
	if (chosen) return chosen;

	const candidates = candidateTargets(inspection);
	const byId = candidates.find((target) => target.kind === 'rule' && target.rule.includes('#'));
	return byId ?? candidates[0];
}

export function describeTarget(target: Target): string {
	if (target.kind === 'inline') return 'this element (style attribute)';
	if (target.kind === 'new-rule') return `new rule ${target.selector}`;
	return target.rule;
}

function styleRanges(path: string, text: string): Range[] | null {
	return extension(path) === '.rml' ? parseRml(text).styles : null;
}

/** Sets a property, or with null removes it, at a target. Several at once land as one undo step. */
export async function setStyles(id: string, properties: [string, string | null][], target: Target) {
	if (target.kind === 'inline') {
		const found = withElement(id);
		if (!found) return;
		let value = attributeOf(found.element, 'style')?.value ?? '';
		for (const [name, next] of properties) value = setInlineStyle(value, name, next);
		const edit = setAttribute(found.source.text, found.element, 'style', value.trim() ? value : null);
		if (edit) await commit(found.source.path, [edit], found.element.start);
		return;
	}

	if (target.kind === 'new-rule') {
		const source = currentSource();
		const declarations = properties.filter((entry): entry is [string, string] => entry[1] !== null);
		if (!source || !declarations.length) return;
		const block = ownStyleBlock(source.text, source.document);
		if (!block) return;
		const edit = 'range' in block
			? appendRule(source.text, block.range, target.selector, declarations)
			: block.create(`${target.selector} { ${declarations.map(([name, value]) => `${name}: ${value};`).join(' ')} }`);
		await commit(source.path, [edit], withElement(id) ? shiftOffset([edit], withElement(id)!.element.start) : null);
		return;
	}

	const path = resolveSourcePath(target.file);
	const model = path ? await getModel(path) : null;
	if (!path || !model) return;

	// One declaration at a time against the text as it then stands, because each changes the offsets
	// the next is found by.
	let text = model.getValue();
	const original = text;
	for (const [name, next] of properties) {
		const ranges = styleRanges(path, text);
		const rule = ranges
			? ranges.map((range) => findRule(text, target.rule, target.line, range)).find((found) => found && found.line === target.line)
				?? ranges.map((range) => findRule(text, target.rule, undefined, range)).find(Boolean)
			: findRule(text, target.rule, target.line);
		if (!rule) continue;
		const edit = next === null ? removeDeclaration(text, rule, name) : setDeclaration(text, rule, name, next);
		if (edit) text = text.slice(0, edit.from) + edit.text + text.slice(edit.to);
	}
	if (text === original) return;

	// Narrowed to the span that actually differs, so the undo step and the diff are the edit and no more.
	let start = 0;
	while (start < original.length && start < text.length && original[start] === text[start]) start++;
	let endOld = original.length;
	let endNew = text.length;
	while (endOld > start && endNew > start && original[endOld - 1] === text[endNew - 1]) {
		endOld--;
		endNew--;
	}

	const found = path === store.get().document ? withElement(id) : null;
	const edit = { from: start, to: endOld, text: text.slice(start, endNew) };
	await commit(path, [edit], found ? shiftOffset([edit], found.element.start) : null);
	if (!found) select(id);
}

/** How many elements on the canvas a rule reaches, so an edit to it can say what else it changed. */
export function elementsReachedBy(target: Target): number {
	if (target.kind !== 'rule') return 1;
	let count = 0;
	const visit = (node: { id: string; children: { id: string }[] }) => {
		if (engine.inspect(node.id)?.rules.some((entry) => !entry.inherited && sameRule(entry, target))) count++;
		node.children.forEach((child) => visit(child as typeof node));
	};
	const tree = store.get().tree;
	if (tree) visit(tree);
	return count;
}

/**
 * Sets each property where `defaultTarget` puts it, so a value set by one rule is not written into another that loses to it.
 * Returns the rules written that reach other elements too, with how many they reach.
 */
export async function setStylesWhereWritten(id: string, properties: [string, string][], inspection: Inspection): Promise<{ rule: string; count: number }[]> {
	const groups = new Map<string, { target: Target; properties: [string, string][] }>();
	for (const entry of properties) {
		const target = defaultTarget(inspection, entry[0]);
		const key = JSON.stringify(target);
		if (!groups.has(key)) groups.set(key, { target, properties: [] });
		groups.get(key)!.properties.push(entry);
	}
	const shared = [...groups.values()]
		.map(({ target }) => ({ rule: target.kind === 'rule' ? target.rule : '', count: elementsReachedBy(target) }))
		.filter((entry) => entry.count > 1);
	for (const group of groups.values()) await setStyles(id, group.properties, group.target);
	return shared;
}

export function setStyle(id: string, property: string, value: string | null, target: Target) {
	return setStyles(id, [[property, value]], target);
}

/** Shows a change on the live element without writing it anywhere, for the length of a gesture. */
export function previewStyle(id: string, properties: [string, string][]) {
	for (const [name, value] of properties) engine.setInlineProperty(id, name, value);
}

export function clearPreview(id: string, names: string[]) {
	for (const name of names) engine.removeInlineProperty(id, name);
}
