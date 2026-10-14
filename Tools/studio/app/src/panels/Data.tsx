import { useEffect, useState } from 'react';
import {
	addCommand, addField, createSchema, dataStore, ModelState, removeField, setCommand, setFieldType, setMockValue, setSample, updateSchema,
} from '../data/models';
import { Field, FIELD_TYPES, FieldType, Schema } from '../data/schema';
import { showPanel } from '../dock';
import { basename } from '../paths';
import { openFile, store } from '../session';

function Text({ value, onCommit, placeholder, className }: { value: string; onCommit: (value: string) => void; placeholder?: string; className?: string }) {
	const [draft, setDraft] = useState(value);
	useEffect(() => setDraft(value), [value]);
	return (
		<input
			className={className}
			value={draft}
			placeholder={placeholder}
			spellCheck={false}
			onChange={(event) => setDraft(event.target.value)}
			onBlur={() => {
				if (draft !== value) onCommit(draft);
			}}
			onKeyDown={(event) => {
				if (event.key === 'Enter') (event.target as HTMLInputElement).blur();
				event.stopPropagation();
			}}
		/>
	);
}

/** An editor for one scalar, shaped by its type, or by the value itself when there is no type to go by. */
function Scalar({ type, value, onChange }: { type: FieldType | null; value: unknown; onChange: (value: unknown) => void }) {
	const kind = type ?? (typeof value === 'boolean' ? 'bool' : typeof value === 'number' ? 'float' : 'string');
	if (kind === 'bool') return <input type="checkbox" checked={value === true} onChange={(event) => onChange(event.target.checked)} />;
	if (kind === 'int' || kind === 'float' || kind === 'enum') {
		return <Text value={String(value ?? 0)} onCommit={(next) => onChange(kind === 'float' ? Number(next) || 0 : Math.round(Number(next) || 0))} />;
	}
	return <Text value={typeof value === 'string' ? value : JSON.stringify(value ?? '')} onCommit={onChange} />;
}

/** The rows of an array, one line each, with a column per member when the rows are structs. */
function Rows({ field, schema, value, onChange }: { field: Field | null; schema: Schema | null; value: unknown[]; onChange: (value: unknown[]) => void }) {
	const struct = field?.of ? schema?.structs?.[field.of] : undefined;
	const sampleRow = value.find((row) => row && typeof row === 'object') as Record<string, unknown> | undefined;
	const columns = struct ? Object.keys(struct.fields) : sampleRow ? Object.keys(sampleRow) : null;
	const scalarType = !columns && field?.of && FIELD_TYPES.includes(field.of as FieldType) ? (field.of as FieldType) : null;

	const blank = (): unknown => {
		if (!columns) return scalarType === 'bool' ? false : scalarType === 'int' || scalarType === 'float' ? 0 : '';
		const row: Record<string, unknown> = {};
		for (const column of columns) {
			const type = struct?.fields[column]?.type;
			row[column] = type === 'bool' ? false : type === 'int' || type === 'float' ? 0 : typeof sampleRow?.[column] === 'boolean' ? false : typeof sampleRow?.[column] === 'number' ? 0 : '';
		}
		return row;
	};

	return (
		<div className="data-rows">
			{columns && (
				<div className="data-row head">
					{columns.map((column) => <span key={column} title={column}>{column}</span>)}
					<span className="gutter" />
				</div>
			)}
			{value.map((row, index) => (
				<div key={index} className="data-row">
					{columns
						? columns.map((column) => (
							<Scalar
								key={column}
								type={struct?.fields[column]?.type ?? null}
								value={(row as Record<string, unknown>)?.[column]}
								onChange={(next) => onChange(value.map((entry, at) => (at === index ? { ...(entry as object), [column]: next } : entry)))}
							/>
						))
						: <Scalar type={scalarType} value={row} onChange={(next) => onChange(value.map((entry, at) => (at === index ? next : entry)))} />}
					<button className="gutter" title="Remove row" onClick={() => onChange(value.filter((_, at) => at !== index))}>×</button>
				</div>
			))}
			<button className="link add" onClick={() => onChange([...value, blank()])}>+ row</button>
		</div>
	);
}

function FieldRow({ model, name, field, value }: { model: ModelState; name: string; field: Field | null; value: unknown }) {
	const reflected = model.schema?.source === 'reflected';
	const used = model.use.values.has(name);
	const write = (next: unknown) => (model.schema ? setSample(model.name, name, next) : setMockValue(model.name, name, next));
	const isArray = field ? field.type === 'array' : Array.isArray(value);

	return (
		<div className={'data-field' + (used ? '' : ' unused')}>
			<div className="data-field-head">
				<span className="data-name" title={used ? name : `${name}: nothing in this document binds it`}>{name}</span>
				{field && (
					<select value={field.type} disabled={reflected} title={reflected ? 'Typed by the C++ class this was exported from' : 'Type'} onChange={(event) => setFieldType(model.name, name, event.target.value as FieldType)}>
						{FIELD_TYPES.map((type) => <option key={type} value={type}>{type}</option>)}
					</select>
				)}
				{!isArray && <div className="data-value"><Scalar type={field?.type ?? null} value={value} onChange={write} /></div>}
				{isArray && <span className="spacer" />}
				{model.schema && !reflected && <button className="gutter" title="Remove field" onClick={() => removeField(model.name, name)}>×</button>}
			</div>
			{isArray && <Rows field={field} schema={model.schema} value={Array.isArray(value) ? value : []} onChange={write} />}
		</div>
	);
}

function Commands({ model }: { model: ModelState }) {
	const declared = model.schema?.commands ?? {};
	const names = [...new Set([...Object.keys(declared), ...model.use.commands.keys()])].sort();
	if (!names.length) return null;

	return (
		<>
			<h4>Commands</h4>
			{names.map((name) => (
				<div key={name} className="data-field-head">
					<span className="data-name">{name}</span>
					{name in declared ? (
						<>
							<div className="data-value">
								<Text
									value={declared[name].join(', ')}
									placeholder="no parameters"
									onCommit={(next) => setCommand(model.name, name, next.split(',').map((part) => part.trim()).filter(Boolean))}
								/>
							</div>
							<button className="gutter" title="Remove command" onClick={() => setCommand(model.name, name, null)}>×</button>
						</>
					) : (
						<button className="link add" onClick={() => addCommand(model.name, name, model.use.commands.get(name) ?? 0)}>declare</button>
					)}
				</div>
			))}
		</>
	);
}

function Model({ model }: { model: ModelState }) {
	const [adding, setAdding] = useState('');
	const fields = model.schema ? model.schema.fields : null;
	const names = fields ? Object.keys(fields) : Object.keys(model.mock ?? {});
	const errors = model.issues.filter((issue) => issue.severity === 'error');
	const open = (path: string) => {
		openFile(path);
		showPanel('code');
	};

	return (
		<section className="data-model">
			<div className="data-model-head">
				<h3>{model.name}</h3>
				{model.mock && <button className="link" title={model.mockPath} onClick={() => open(model.mockPath)}>{basename(model.mockPath)}</button>}
				{model.schema && <button className="link" title={model.schemaPath} onClick={() => open(model.schemaPath)}>schema</button>}
			</div>

			{model.error && <p className="notice">{model.error}</p>}

			{!model.schema && (
				<p className="notice">
					{model.mock ? 'These values are a hand-kept mock, with no types behind them.' : 'Nothing supplies this model yet, so the document previews with nothing bound.'}{' '}
					<button className="link add" onClick={() => createSchema(model.name)}>Create a schema</button> to type it, check the markup against it, and generate the C++ class from it.
				</p>
			)}

			{model.schema?.source === 'reflected' && (
				<p className="dim small">Exported from {model.schema.class ?? 'a C++ class'}, which decides the fields and their types. Sample values are yours to set.</p>
			)}

			{names.map((name) => <FieldRow key={name} model={model} name={name} field={fields?.[name] ?? null} value={fields ? fields[name].sample : model.mock?.[name]} />)}

			{model.schema && model.schema.source !== 'reflected' && (
				<div className="data-field-head">
					<input
						className="data-add"
						placeholder="add field"
						value={adding}
						spellCheck={false}
						onChange={(event) => setAdding(event.target.value)}
						onKeyDown={(event) => {
							event.stopPropagation();
							if (event.key !== 'Enter' || !adding.trim()) return;
							addField(model.name, adding.trim());
							setAdding('');
						}}
					/>
				</div>
			)}

			{model.schema && <Commands model={model} />}

			{!!errors.length && (
				<>
					<h4>Not in the model</h4>
					{errors.map((issue) => (
						<div key={issue.name + issue.message} className="data-issue">
							<span>{issue.message}</span>
							{model.schema?.source !== 'reflected' && !issue.name.includes('.') && !model.use.commands.has(issue.name) && (
								<button className="link add" onClick={() => addField(model.name, issue.name)}>add</button>
							)}
							{model.schema?.source !== 'reflected' && issue.name.includes('.') && (
								<button
									className="link add"
									onClick={() => {
										const [array, member] = issue.name.split('.');
										updateSchema(model.name, (schema) => {
											const struct = schema.fields[array]?.of;
											if (struct && schema.structs?.[struct]) schema.structs[struct].fields[member] = { type: /^b[A-Z]/.test(member) ? 'bool' : 'string' };
										});
									}}
								>
									add
								</button>
							)}
						</div>
					))}
				</>
			)}
		</section>
	);
}

export function DataPanel() {
	const models = dataStore.use((state) => state.models);
	const document = store.use((state) => state.document);

	if (!document) return <div className="panel empty">No document.</div>;
	if (!models.length) return <div className="panel empty">This document binds no data. Give an element <code>data-model="name"</code> and it appears here.</div>;
	return (
		<div className="panel data">
			{models.map((model) => <Model key={model.name} model={model} />)}
		</div>
	);
}
