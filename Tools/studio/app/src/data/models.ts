import { join, relativeTo } from '../paths';
import { documentRootPath, getModel, peekModel, store, writeText } from '../session';
import { createStore } from '../store';
import { extractBindings, Field, FieldType, formatJson, inferSchema, Issue, mockFromSchema, ModelUse, Schema, validate } from './schema';

export interface ModelState {
	name: string;
	use: ModelUse;
	mockPath: string;
	schemaPath: string;
	mock: Record<string, unknown> | null;
	schema: Schema | null;
	/** Why a file that exists could not be read as JSON. */
	error: string | null;
	issues: Issue[];
}

/** The data models the open document binds, with what the project holds for each. */
export const dataStore = createStore<{ models: ModelState[] }>({ models: [] });

let lastRevision = -1;
let lastDocument: string | null = null;
let running = false;
let again = false;

async function readJson(path: string): Promise<{ value: Record<string, unknown> | null; error: string | null }> {
	const model = await getModel(path);
	if (!model) return { value: null, error: null };
	try {
		const value = JSON.parse(model.getValue());
		return value && typeof value === 'object' && !Array.isArray(value) ? { value, error: null } : { value: null, error: 'not a JSON object' };
	} catch (error) {
		return { value: null, error: error instanceof Error ? error.message : String(error) };
	}
}

async function refresh() {
	if (running) {
		again = true;
		return;
	}
	running = true;
	try {
		const { document } = store.get();
		const root = documentRootPath();
		const text = document ? peekModel(document)?.getValue() : undefined;
		if (!document || !root || text === undefined) {
			dataStore.set({ models: [] });
			return;
		}

		const models: ModelState[] = [];
		for (const [name, use] of extractBindings(text)) {
			// The reserved model Folio mounts itself, reporting the input device.
			if (name === 'folio') continue;
			const mockPath = join(root, 'UI', 'mock', name + '.json');
			const schemaPath = join(root, 'UI', 'schema', name + '.json');
			const mock = await readJson(mockPath);
			const schema = await readJson(schemaPath);
			const parsed = schema.value && typeof schema.value.fields === 'object' ? (schema.value as unknown as Schema) : null;
			models.push({
				name,
				use,
				mockPath,
				schemaPath,
				mock: mock.value,
				schema: parsed,
				error: mock.error ? `${name}.json: ${mock.error}` : schema.error ? `schema ${name}.json: ${schema.error}` : null,
				issues: parsed ? validate(parsed, use) : [],
			});
		}
		dataStore.set({ models });
	} finally {
		running = false;
		if (again) {
			again = false;
			refresh();
		}
	}
}

store.subscribe(() => {
	const { revision, document } = store.get();
	if (revision === lastRevision && document === lastDocument) return;
	lastRevision = revision;
	lastDocument = document;
	refresh();
});

function find(name: string): ModelState | null {
	return dataStore.get().models.find((model) => model.name === name) ?? null;
}

/** Writes a schema and the mock it describes, which are one change and never out of step. */
async function writeSchema(state: ModelState, schema: Schema) {
	await writeText(state.schemaPath, formatJson(schema));
	await writeText(state.mockPath, formatJson(mockFromSchema(schema)));
	refresh();
}

/** Starts a schema from the mock a model already has, typed by what its values are. */
export async function createSchema(name: string) {
	const state = find(name);
	const root = documentRootPath();
	const { document } = store.get();
	if (!state || !root || !document) return;

	const schema = inferSchema(name, state.mock ?? {}, [relativeTo(root, document)]);
	// Whatever the markup binds that the mock never had, so the first schema already matches the document.
	for (const value of state.use.values) {
		if (!(value in schema.fields)) schema.fields[value] = guessField(value, state.use);
	}
	if (state.use.commands.size) {
		schema.commands = {};
		for (const [command, count] of state.use.commands) schema.commands[command] = Array.from({ length: count }, (_, index) => `int Arg${index}`);
	}
	await writeSchema(state, schema);
}

/** A field for a name the markup uses, from the little a name and its use give away. */
function guessField(name: string, use: ModelUse): Field {
	if (use.members.has(name)) return { type: 'array', of: 'string', sample: [] };
	if (/^b[A-Z]/.test(name)) return { type: 'bool', sample: false };
	return { type: 'string', sample: '' };
}

export async function updateSchema(name: string, change: (schema: Schema) => void) {
	const state = find(name);
	if (!state?.schema) return;
	const schema: Schema = JSON.parse(JSON.stringify(state.schema));
	change(schema);
	await writeSchema(state, schema);
}

export function addField(name: string, field: string) {
	const state = find(name);
	if (!state) return;
	return updateSchema(name, (schema) => {
		if (!(field in schema.fields)) schema.fields[field] = guessField(field, state.use);
	});
}

export function removeField(name: string, field: string) {
	return updateSchema(name, (schema) => {
		delete schema.fields[field];
	});
}

function coerce(type: FieldType, value: unknown): unknown {
	switch (type) {
		case 'bool': return value === true || value === 'true' || value === 1;
		case 'int': case 'enum': return Math.round(Number(value) || 0);
		case 'float': return Number(value) || 0;
		case 'array': return Array.isArray(value) ? value : [];
		default: return value === undefined || value === null || typeof value === 'object' ? '' : String(value);
	}
}

export function setFieldType(name: string, field: string, type: FieldType) {
	return updateSchema(name, (schema) => {
		const entry = schema.fields[field];
		if (!entry) return;
		entry.type = type;
		entry.sample = coerce(type, entry.sample);
		if (type === 'array') entry.of = entry.of ?? 'string';
		else delete entry.of;
		if (type !== 'enum') delete entry.enum;
	});
}

export function setSample(name: string, field: string, sample: unknown) {
	return updateSchema(name, (schema) => {
		if (schema.fields[field]) schema.fields[field].sample = sample;
	});
}

export function addCommand(name: string, command: string, count: number) {
	return updateSchema(name, (schema) => {
		schema.commands = schema.commands ?? {};
		schema.commands[command] = Array.from({ length: count }, (_, index) => `int Arg${index}`);
	});
}

export function setCommand(name: string, command: string, parameters: string[] | null) {
	return updateSchema(name, (schema) => {
		schema.commands = schema.commands ?? {};
		if (parameters === null) delete schema.commands[command];
		else schema.commands[command] = parameters;
	});
}

/** Sets one value in a mock that has no schema behind it. */
export async function setMockValue(name: string, field: string, value: unknown) {
	const state = find(name);
	if (!state || state.schema) return;
	await writeText(state.mockPath, formatJson({ ...(state.mock ?? {}), [field]: value }));
	refresh();
}

/** Every name the open document could bind, for completing a binding as it is typed. */
export function knownBindings(): string[] {
	const names = new Set<string>();
	for (const model of dataStore.get().models) {
		const fields = model.schema ? model.schema.fields : null;
		for (const name of Object.keys(fields ?? model.mock ?? {})) names.add(name);
		for (const struct of Object.values(model.schema?.structs ?? {})) for (const member of Object.keys(struct.fields)) names.add(member);
		for (const command of Object.keys(model.schema?.commands ?? {})) names.add(command);
	}
	return [...names].sort();
}
