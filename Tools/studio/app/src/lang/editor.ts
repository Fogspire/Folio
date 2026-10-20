/*
 * The language features, wired into Monaco: completion, hover, signature help, go to definition,
 * formatting, and squiggles for what the text, the renderer and the schema each say is wrong.
 */

import { dataStore } from '../data/models';
import { inferSchema, Schema } from '../data/schema';
import { findRules, Range } from '../edit/rcss';
import { parseRml } from '../edit/rml';
import { monaco } from '../monaco';
import { basename, extension, isUnder, join } from '../paths';
import { documentRootPath, getModel, peekModel, resolveSourcePath, store } from '../session';
import { checkRcss, checkRml, Finding } from './check';
import { complete, hover, Item, ItemKind, signature, Sources } from './complete';
import { classify } from './context';
import { formatEdits, formatModel, toMonacoEdits } from './formatting';

type Language = 'rml' | 'rcss';

const KINDS: Record<ItemKind, monaco.languages.CompletionItemKind> = {
	property: monaco.languages.CompletionItemKind.Property,
	keyword: monaco.languages.CompletionItemKind.Keyword,
	function: monaco.languages.CompletionItemKind.Function,
	tag: monaco.languages.CompletionItemKind.Class,
	attribute: monaco.languages.CompletionItemKind.Field,
	field: monaco.languages.CompletionItemKind.Field,
	command: monaco.languages.CompletionItemKind.Method,
	variable: monaco.languages.CompletionItemKind.Variable,
	class: monaco.languages.CompletionItemKind.Reference,
	id: monaco.languages.CompletionItemKind.Reference,
	model: monaco.languages.CompletionItemKind.Module,
	value: monaco.languages.CompletionItemKind.Value,
};

/** The host path a model was opened from. Session keys its models by path, so this is read back from there. */
function pathOf(model: monaco.editor.ITextModel): string {
	const { openFiles, projectFiles, libraryFiles, document } = store.get();
	for (const path of [...(document ? [document] : []), ...openFiles, ...projectFiles, ...libraryFiles]) {
		if (peekModel(path) === model) return path;
	}
	return model.uri.fsPath.replace(/\\/g, '/');
}

function languageOf(model: monaco.editor.ITextModel): Language | null {
	const id = model.getLanguageId();
	return id === 'rml' || id === 'rcss' ? id : null;
}

/** Where a root-relative href points: `/folio/` is the plugin's stylesheets, anything else the project. */
function resolveHref(href: string): string | null {
	const resolved = resolveSourcePath(href);
	if (resolved) return resolved;
	const { project } = store.get();
	if (!project) return null;
	const path = href.replace(/^\/+/, '');
	if (path.startsWith('folio/')) return join(project.folioRoot, 'rcss', path.slice('folio/'.length));
	return join(project.root, path);
}

async function readText(path: string): Promise<string | null> {
	return (await getModel(path))?.getValue() ?? null;
}

interface Sheet {
	path: string;
	text: string;
	/** For a template, its <style> blocks. A stylesheet is all one range. */
	ranges?: Range[];
}

/** Every stylesheet a document links, through the templates it links, a few levels deep. */
async function linkedSheets(text: string, depth = 0, seen = new Set<string>()): Promise<Sheet[]> {
	if (depth > 3) return [];
	const sheets: Sheet[] = [];
	for (const element of flatten(parseRml(text).roots)) {
		if (element.tag !== 'link') continue;
		const type = element.attributes.find((attribute) => attribute.name === 'type')?.value;
		const href = element.attributes.find((attribute) => attribute.name === 'href')?.value;
		const path = href ? resolveHref(href) : null;
		if (!path || seen.has(path.toLowerCase())) continue;
		seen.add(path.toLowerCase());
		const linked = await readText(path);
		if (linked === null) continue;
		if (type === 'text/rcss') sheets.push({ path, text: linked });
		else if (type === 'text/template') {
			sheets.push({ path, text: linked, ranges: parseRml(linked).styles });
			sheets.push(...(await linkedSheets(linked, depth + 1, seen)));
		}
	}
	return sheets;
}

function flatten<T extends { children: T[] }>(elements: T[]): T[] {
	return elements.flatMap((element) => [element, ...flatten(element.children)]);
}

function modelRoot(): string | null {
	return documentRootPath() ?? store.get().project?.root ?? null;
}

/** A model's schema, or one inferred from its mock when it has no schema yet. */
async function loadSchema(model: string): Promise<{ schema: Schema; path: string } | null> {
	const known = dataStore.get().models.find((entry) => entry.name === model);
	if (known?.schema) return { schema: known.schema, path: known.schemaPath };
	if (known?.mock) return { schema: inferSchema(model, known.mock), path: known.mockPath };

	const root = modelRoot();
	if (!root) return null;
	for (const [directory, infer] of [['schema', false], ['mock', true]] as const) {
		const path = join(root, 'UI', directory, model + '.json');
		const text = await readText(path);
		if (text === null) continue;
		try {
			const value = JSON.parse(text);
			if (!infer && value && typeof value.fields === 'object') return { schema: value as Schema, path };
			if (infer && value && typeof value === 'object') return { schema: inferSchema(model, value), path };
		} catch {
			// A file that does not parse yet is being typed; completion goes without it.
		}
	}
	return null;
}

function modelNames(): string[] {
	const names = new Set<string>();
	for (const path of store.get().projectFiles) {
		const match = /\/UI\/(?:schema|mock)\/([^/]+)\.json$/i.exec(path);
		if (match) names.add(match[1]);
	}
	for (const model of dataStore.get().models) names.add(model.name);
	return [...names].sort();
}

interface Gathered {
	sources: Sources;
	sheets: Sheet[];
	schemaPaths: Map<string, string>;
}

/** Everything outside the text that completing it needs, read before answering since reads are async. */
async function gather(text: string, language: Language): Promise<Gathered> {
	const schemas = new Map<string, Schema>();
	const schemaPaths = new Map<string, string>();
	if (language === 'rml') {
		for (const match of text.matchAll(/data-model\s*=\s*["']([^"']+)["']/g)) {
			if (schemas.has(match[1])) continue;
			const loaded = await loadSchema(match[1]);
			if (loaded) {
				schemas.set(match[1], loaded.schema);
				schemaPaths.set(match[1], loaded.path);
			}
		}
	}
	const sheets = language === 'rml' ? await linkedSheets(text) : [];
	return {
		sources: { schema: (name) => schemas.get(name) ?? null, models: modelNames(), stylesheets: sheets },
		sheets,
		schemaPaths,
	};
}

function toRange(model: monaco.editor.ITextModel, from: number, to: number): monaco.Range {
	const start = model.getPositionAt(from);
	const end = model.getPositionAt(to);
	return new monaco.Range(start.lineNumber, start.column, end.lineNumber, end.column);
}

function toSuggestion(model: monaco.editor.ITextModel, position: monaco.Position, item: Item): monaco.languages.CompletionItem {
	const replace = toRange(model, item.range.from, item.range.to);
	// Monaco wants both ranges on the cursor's line and around it.
	const range = replace.startLineNumber === position.lineNumber && replace.endLineNumber === position.lineNumber
		? { insert: new monaco.Range(position.lineNumber, replace.startColumn, position.lineNumber, position.column), replace }
		: new monaco.Range(position.lineNumber, position.column, position.lineNumber, position.column);
	return {
		label: item.label,
		kind: KINDS[item.kind],
		insertText: item.insert,
		insertTextRules: item.snippet ? monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet : undefined,
		range,
		detail: item.detail,
		documentation: item.doc ? { value: item.doc } : undefined,
		sortText: (item.preferred ? '0' : '1') + item.label,
		command: item.retrigger ? { id: 'editor.action.triggerSuggest', title: '' } : undefined,
	};
}

function registerProviders(language: Language) {
	monaco.languages.registerCompletionItemProvider(language, {
		triggerCharacters: language === 'rml' ? ['<', '/', ' ', '"', "'", '.', '|', ':', '-'] : [':', ' ', '.', '#', '(', '-'],
		async provideCompletionItems(model, position) {
			const text = model.getValue();
			const { sources } = await gather(text, language);
			const items = complete(text, model.getOffsetAt(position), language, sources);
			return { suggestions: items.map((item) => toSuggestion(model, position, item)) };
		},
	});

	monaco.languages.registerHoverProvider(language, {
		async provideHover(model, position) {
			const text = model.getValue();
			const { sources } = await gather(text, language);
			const found = hover(text, model.getOffsetAt(position), language, sources);
			return found ? { range: toRange(model, found.range.from, found.range.to), contents: [{ value: found.markdown }] } : null;
		},
	});

	monaco.languages.registerDocumentFormattingEditProvider(language, {
		provideDocumentFormattingEdits: (model) => toMonacoEdits(model, formatEdits(model)),
	});

	monaco.languages.registerDocumentRangeFormattingEditProvider(language, {
		// Formatting depends on what encloses a line, so the whole document is formatted and the edits kept to the range.
		provideDocumentRangeFormattingEdits: (model, range) => toMonacoEdits(model, formatEdits(model)).filter((edit) => monaco.Range.areIntersectingOrTouching(edit.range, range)),
	});

	monaco.languages.registerOnTypeFormattingEditProvider(language, {
		autoFormatTriggerCharacters: ['\n', '}', '>'],
		provideOnTypeFormattingEdits(model, position, typed) {
			// Enter finishes the line above. The new line is left alone: trimming it would pull the caret off its indent.
			const line = typed === '\n' ? position.lineNumber - 1 : position.lineNumber;
			return toMonacoEdits(model, formatEdits(model)).filter((edit) => edit.range.startLineNumber === line && edit.range.endLineNumber === line && !edit.text.includes('\n'));
		},
	});

	monaco.languages.registerDefinitionProvider(language, {
		async provideDefinition(model, position) {
			const text = model.getValue();
			return definition(model, text, model.getOffsetAt(position), language, await gather(text, language));
		},
	});
}

/** Where a class, an id, a binding or a linked file is defined. */
async function definition(model: monaco.editor.ITextModel, text: string, offset: number, language: Language, gathered: Gathered): Promise<monaco.languages.Location[]> {
	const context = classify(text, offset, language);
	const locations: monaco.languages.Location[] = [];
	const at = async (path: string, from: number, to: number) => {
		const target = await getModel(path);
		if (target) locations.push({ uri: target.uri, range: toRange(target, from, to) });
	};

	if (context.kind === 'value' && context.attribute === 'class') {
		const name = text.slice(context.word.from, context.word.to);
		const pattern = new RegExp(`\\.${name.replace(/[-]/g, '\\-')}(?![-\\w])`);
		for (const range of parseRml(text).styles) {
			for (const rule of findRules(text, range)) if (pattern.test(rule.bareSelector)) locations.push({ uri: model.uri, range: toRange(model, rule.selectorStart, rule.selectorStart + rule.bareSelector.length) });
		}
		for (const sheet of gathered.sheets) {
			for (const rule of (sheet.ranges ?? [undefined]).flatMap((range) => findRules(sheet.text, range))) if (pattern.test(rule.bareSelector)) await at(sheet.path, rule.selectorStart, rule.selectorStart + rule.bareSelector.length);
		}
		return locations;
	}

	if (context.kind === 'value' && context.attribute === 'href') {
		const path = resolveHref(text.slice(context.valueStart, context.valueEnd));
		if (path) await at(path, 0, 0);
		return locations;
	}

	if (context.kind === 'value') {
		const id = text.slice(context.valueStart, context.valueEnd);
		const match = new RegExp(`\\sid\\s*=\\s*["']${id.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}["']`).exec(text);
		if (id && match) locations.push({ uri: model.uri, range: toRange(model, match.index + 1, match.index + match[0].length) });
		return locations;
	}

	if (context.kind === 'expression' && context.scope.model && !context.path.length) {
		const name = text.slice(context.word.from, context.word.to);
		const path = gathered.schemaPaths.get(context.scope.model);
		const schemaText = path ? await readText(path) : null;
		const index = schemaText?.indexOf(`"${name}"`) ?? -1;
		if (path && index !== -1) await at(path, index, index + name.length + 2);
	}
	return locations;
}

function registerSignatureHelp() {
	monaco.languages.registerSignatureHelpProvider('rml', {
		signatureHelpTriggerCharacters: ['(', ','],
		async provideSignatureHelp(model, position) {
			const text = model.getValue();
			const { sources } = await gather(text, 'rml');
			const found = signature(text, model.getOffsetAt(position), sources);
			if (!found) return null;
			return {
				value: {
					signatures: [{ label: found.label, parameters: found.parameters.map((parameter) => ({ label: parameter })) }],
					activeSignature: 0,
					activeParameter: found.active,
				},
				dispose: () => undefined,
			};
		},
	});
}

/* ---------------------------------------------------------------- Markers */

const SEVERITY = {
	error: monaco.MarkerSeverity.Error,
	warning: monaco.MarkerSeverity.Warning,
	info: monaco.MarkerSeverity.Info,
};

function lintModel(model: monaco.editor.ITextModel) {
	const language = languageOf(model);
	if (!language || model.isDisposed()) return;
	const text = model.getValue();
	const findings: Finding[] = language === 'rml' ? checkRml(text) : checkRcss(text);
	monaco.editor.setModelMarkers(model, 'folio-text', findings.map((finding) => {
		const range = toRange(model, finding.from, finding.to);
		return { ...range, severity: SEVERITY[finding.severity], message: finding.message, source: 'Folio' };
	}));
}

/** The renderer's diagnostics, on the lines it named, in whichever open file it named. */
function markEngineDiagnostics() {
	const byModel = new Map<monaco.editor.ITextModel, monaco.editor.IMarkerData[]>();
	for (const model of monaco.editor.getModels()) if (languageOf(model)) byModel.set(model, []);

	for (const entry of store.get().diagnostics) {
		if (!entry.file || entry.severity === 'info') continue;
		const path = resolveSourcePath(entry.file);
		const model = path ? peekModel(path) : null;
		const markers = model ? byModel.get(model) : undefined;
		if (!model || !markers) continue;
		const line = Math.min(Math.max(1, entry.line || 1), model.getLineCount());
		markers.push({
			startLineNumber: line,
			startColumn: model.getLineFirstNonWhitespaceColumn(line) || 1,
			endLineNumber: line,
			endColumn: model.getLineMaxColumn(line),
			severity: SEVERITY[entry.severity],
			message: entry.message,
			code: entry.code,
			source: 'Renderer',
		});
	}
	for (const [model, markers] of byModel) monaco.editor.setModelMarkers(model, 'folio-engine', markers);
}

/** The schema's complaints about the document on the canvas, on the first place each name is bound. */
function markSchemaIssues() {
	const { document } = store.get();
	for (const model of monaco.editor.getModels()) if (languageOf(model) === 'rml') monaco.editor.setModelMarkers(model, 'folio-schema', []);
	const model = document ? peekModel(document) : null;
	if (!model) return;

	const text = model.getValue();
	const markers: monaco.editor.IMarkerData[] = [];
	for (const state of dataStore.get().models) {
		for (const issue of state.issues) {
			// A declared field nothing binds has no place in the markup to point at.
			if (issue.severity !== 'error') continue;
			const name = issue.name.includes('.') ? issue.name.slice(issue.name.indexOf('.')) : issue.name;
			const match = new RegExp(`${name.startsWith('.') ? '\\' : '\\b'}${name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\b`).exec(text);
			if (!match) continue;
			const from = match.index + (name.startsWith('.') ? 1 : 0);
			markers.push({ ...toRange(model, from, from + name.replace(/^\./, '').length), severity: SEVERITY.error, message: `${issue.message} (${state.name})`, source: 'Schema' });
		}
	}
	monaco.editor.setModelMarkers(model, 'folio-schema', markers);
}

function watchModel(model: monaco.editor.ITextModel) {
	if (!languageOf(model)) return;
	let timer: number | null = null;
	lintModel(model);
	model.onDidChangeContent(() => {
		if (timer !== null) window.clearTimeout(timer);
		timer = window.setTimeout(() => {
			timer = null;
			lintModel(model);
		}, 250);
	});
}

/* ---------------------------------------------------------------- Commands */

/** Whether a file is one Folio ships, which a project other than the plugin shows but does not edit. */
export function isShipped(path: string): boolean {
	const { project } = store.get();
	return !!project && project.folioRoot !== project.root && isUnder(project.folioRoot, path);
}

/** Formats a model quietly when it is an .rml or .rcss this project edits. */
export function formatIfEditable(model: monaco.editor.ITextModel) {
	const path = pathOf(model);
	const kind = extension(path);
	if ((kind === '.rml' || kind === '.rcss') && !isShipped(path)) formatModel(model);
}

/** Formats the file in front, or reports why it was not. */
export function formatActiveFile() {
	const path = store.get().activeFile;
	const model = path ? peekModel(path) : null;
	if (!path || !model) return;
	const kind = extension(path);
	const status = (text: string) => store.set({ status: text });
	if (kind !== '.rml' && kind !== '.rcss') return status('Only .rml and .rcss files can be formatted');
	if (isShipped(path)) return status(basename(path) + ' ships with Folio and is not edited here');
	status(formatModel(model) ? 'Formatted ' + basename(path) : basename(path) + ' is already formatted');
}

let registered = false;

export function registerLanguageFeatures() {
	if (registered) return;
	registered = true;
	registerProviders('rml');
	registerProviders('rcss');
	registerSignatureHelp();

	for (const model of monaco.editor.getModels()) watchModel(model);
	monaco.editor.onDidCreateModel(watchModel);
	monaco.editor.onDidCreateModel(() => {
		markEngineDiagnostics();
		markSchemaIssues();
	});

	let diagnostics = store.get().diagnostics;
	store.subscribe(() => {
		if (store.get().diagnostics === diagnostics) return;
		diagnostics = store.get().diagnostics;
		markEngineDiagnostics();
	});
	dataStore.subscribe(markSchemaIssues);
}
