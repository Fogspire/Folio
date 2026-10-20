import { useEffect, useState } from 'react';
import { dataStore, knownBindings } from '../data/models';
import { showPanel } from '../dock';
import {
	candidateTargets, defaultTarget, describeTarget, elementText, isEditable, isProjectFile, renameElementAttribute, setElementAttribute,
	setElementText, setStyle, setStyles, Target,
} from '../edit/commands';
import { Inspection, RuleProperty } from '../engine';
import { basename } from '../paths';
import { revealSource, setWriteTarget, store } from '../session';

interface Field {
	name: string;
	label?: string;
	/** The properties a shorthand stands for, in the order its value lists them. */
	longhands?: string[];
	/** Other shorthands a rule can write the same longhands with, removed with them when this one is set. */
	alternatives?: string[];
	/** The longhands are four horizontal values then four vertical ones, written with a slash between them. */
	slashed?: boolean;
	options?: string[];
	colour?: boolean;
}

const sides = (prefix: string, suffix = '') => ['top', 'right', 'bottom', 'left'].map((side) => `${prefix}-${side}${suffix}`);
const corners = (suffix = '') => ['top-left', 'top-right', 'bottom-right', 'bottom-left'].map((corner) => `border-${corner}-radius${suffix}`);
const LENGTH_HINT = ['auto', '0', '100%'];

const GROUPS: { title: string; fields: Field[] }[] = [
	{
		title: 'Layout',
		fields: [
			{ name: 'display', options: ['block', 'flex', 'inline', 'inline-block', 'inline-flex', 'none'] },
			{ name: 'position', options: ['static', 'relative', 'absolute', 'fixed'] },
			{ name: 'left', options: LENGTH_HINT },
			{ name: 'top', options: LENGTH_HINT },
			{ name: 'right', options: LENGTH_HINT },
			{ name: 'bottom', options: LENGTH_HINT },
			{ name: 'z-index' },
		],
	},
	{
		title: 'Flex',
		fields: [
			{ name: 'flex-direction', label: 'direction', options: ['row', 'column', 'row-reverse', 'column-reverse'] },
			{ name: 'justify-content', label: 'justify', options: ['flex-start', 'center', 'flex-end', 'space-between', 'space-around', 'space-evenly'] },
			{ name: 'align-items', label: 'align', options: ['stretch', 'flex-start', 'center', 'flex-end', 'baseline'] },
			{ name: 'flex-wrap', label: 'wrap', options: ['nowrap', 'wrap', 'wrap-reverse'] },
			{ name: 'row-gap' },
			{ name: 'column-gap' },
			{ name: 'flex-grow', label: 'grow' },
			{ name: 'flex-shrink', label: 'shrink' },
			{ name: 'flex-basis', label: 'basis', options: ['auto', '0'] },
			{ name: 'align-self', options: ['auto', 'stretch', 'flex-start', 'center', 'flex-end'] },
		],
	},
	{
		title: 'Size',
		fields: [
			{ name: 'width', options: LENGTH_HINT },
			{ name: 'height', options: LENGTH_HINT },
			{ name: 'min-width' },
			{ name: 'min-height' },
			{ name: 'max-width' },
			{ name: 'max-height' },
			{ name: 'box-sizing', options: ['border-box', 'content-box'] },
		],
	},
	{
		title: 'Spacing',
		fields: [
			{ name: 'margin', longhands: sides('margin') },
			{ name: 'padding', longhands: sides('padding') },
		],
	},
	{
		title: 'Text',
		fields: [
			{ name: 'font-family', label: 'family' },
			{ name: 'font-size', label: 'size' },
			{ name: 'font-weight', label: 'weight', options: ['normal', 'bold', '300', '400', '500', '600', '700', '800'] },
			{ name: 'font-style', label: 'style', options: ['normal', 'italic'] },
			{ name: 'line-height' },
			{ name: 'letter-spacing' },
			{ name: 'text-align', label: 'align', options: ['left', 'center', 'right', 'justify'] },
			{ name: 'color', colour: true },
		],
	},
	{
		title: 'Fill and border',
		fields: [
			{ name: 'background-color', label: 'background', colour: true },
			{ name: 'opacity' },
			{ name: 'border-width', longhands: sides('border', '-width') },
			{ name: 'border-color', longhands: sides('border', '-color'), colour: true },
			{ name: 'border-radius', label: 'radius', longhands: [...corners('-x'), ...corners('-y')], alternatives: corners(), slashed: true },
			{ name: 'overflow', longhands: ['overflow-x', 'overflow-y'], options: ['visible', 'hidden', 'auto', 'scroll'] },
		],
	},
	{
		title: 'Effects',
		fields: [
			{ name: 'decorator' },
			{ name: 'box-shadow' },
			{ name: 'filter' },
			{ name: 'font-effect' },
			{ name: 'transform' },
			{ name: 'transition' },
		],
	},
];

const GROUPED = new Set(GROUPS.flatMap((group) => group.fields.flatMap((field) => [field.name, ...(field.longhands ?? [])])));

/** Four sides written the way a shorthand writes them, as briefly as the values allow. */
function collapse(values: string[]): string {
	if (values.every((value) => value === values[0])) return values[0];
	if (values.length === 4 && values[0] === values[2] && values[1] === values[3]) return `${values[0]} ${values[1]}`;
	return values.join(' ');
}

function format(field: Field, values: string[]): string {
	if (!field.slashed) return collapse(values);
	const horizontal = collapse(values.slice(0, 4));
	const vertical = collapse(values.slice(4));
	return horizontal === vertical ? horizontal : `${horizontal} / ${vertical}`;
}

interface Resolved {
	/** What a rule or the style attribute sets, or empty when nothing does. */
	value: string;
	/** What the property resolves to regardless, shown in place of an unset value. */
	computed: string;
	origin: RuleProperty | 'inline' | null;
}

function resolve(inspection: Inspection, field: Field): Resolved {
	const names = field.longhands ?? [field.name];
	const own = (name: string) => inspection.rules.find((entry) => entry.name === name && !entry.inherited) ?? null;
	const inlineNames = names.filter((name) => name in inspection.inline);
	const computed = format(field, names.map((name) => inspection.computed[name] ?? ''));

	if (inlineNames.length) return { value: format(field, names.map((name) => inspection.inline[name] ?? inspection.computed[name] ?? '')), computed, origin: 'inline' };

	const sources = names.map(own);
	const first = sources.find(Boolean) ?? null;
	if (!first) return { value: '', computed, origin: null };
	return { value: format(field, names.map((name, index) => sources[index]?.value ?? inspection.computed[name] ?? '')), computed, origin: first };
}

/** `#rrggbb` out of an RCSS colour, for the colour well, which knows nothing of alpha. */
function toWell(value: string): string {
	const match = /^#([0-9a-f]{3,8})$/i.exec(value.trim());
	if (!match) return '#000000';
	const hex = match[1];
	if (hex.length === 3 || hex.length === 4) return '#' + hex.slice(0, 3).split('').map((digit) => digit + digit).join('');
	return '#' + hex.slice(0, 6);
}

function alphaOf(value: string): string {
	const match = /^#([0-9a-f]{8})$/i.exec(value.trim());
	return match ? match[1].slice(6) : '';
}

/** A text field that commits on Enter or on losing focus, and reverts on Escape. */
function CommitInput({ value, placeholder, list, onCommit, className }: { value: string; placeholder?: string; list?: string; onCommit: (value: string) => void; className?: string }) {
	const [draft, setDraft] = useState(value);
	useEffect(() => setDraft(value), [value]);

	return (
		<input
			className={className}
			value={draft}
			placeholder={placeholder}
			list={list}
			spellCheck={false}
			onChange={(event) => setDraft(event.target.value)}
			onBlur={() => {
				if (draft !== value) onCommit(draft.trim());
			}}
			onKeyDown={(event) => {
				if (event.key === 'Enter') (event.target as HTMLInputElement).blur();
				if (event.key === 'Escape') {
					setDraft(value);
					event.stopPropagation();
				}
			}}
		/>
	);
}

function PropertyRow({ inspection, field, editable }: { inspection: Inspection; field: Field; editable: boolean }) {
	const resolved = resolve(inspection, field);
	const { origin } = resolved;
	const shipped = origin !== null && origin !== 'inline' && !isProjectFile(origin.file);
	const listId = field.options ? `options-${field.name}` : undefined;

	const commit = (value: string) => {
		if (!editable) return;
		// A shorthand replaces whatever longhands the rule wrote, or they would go on overriding it.
		const longhands: [string, null][] = [...(field.longhands ?? []), ...(field.alternatives ?? [])].map((name) => [name, null]);

		if (value === '') {
			// Clearing removes the declaration where it is written. One that Folio ships is not ours to remove.
			if (origin === 'inline') setStyles(inspection.id, [...longhands, [field.name, null]], { kind: 'inline' });
			else if (origin && !shipped) setStyles(inspection.id, [...longhands, [field.name, null]], { kind: 'rule', rule: origin.rule, file: origin.file, line: origin.line });
			return;
		}
		const target: Target = origin === 'inline' ? { kind: 'inline' } : defaultTarget(inspection, field.longhands ? field.longhands[0] : field.name);
		setStyles(inspection.id, [...longhands, [field.name, value]], target);
	};

	return (
		<div className={'prop' + (origin ? ' set' : '')}>
			<label title={field.name}>{field.label ?? field.name}</label>
			<div className="prop-value">
				{field.colour && (
					<input
						type="color"
						className="well"
						disabled={!editable}
						value={toWell(resolved.value || resolved.computed)}
						onChange={(event) => commit(event.target.value + alphaOf(resolved.value || resolved.computed))}
					/>
				)}
				<CommitInput value={resolved.value} placeholder={resolved.computed} list={listId} onCommit={commit} />
				{listId && <datalist id={listId}>{field.options!.map((option) => <option key={option} value={option} />)}</datalist>}
			</div>
			{origin && (
				<button
					className={'origin' + (shipped ? ' shipped' : '')}
					title={origin === 'inline' ? 'Set on the element itself, in its style attribute' : `${origin.rule}\n${origin.file}:${origin.line}${shipped ? '\nShipped with Folio: a change here is written to your own rule instead' : ''}`}
					onClick={() => {
						if (origin === 'inline') return;
						revealSource(origin.file, origin.line);
						showPanel('code');
					}}
				>
					{origin === 'inline' ? 'inline' : origin.rule}
				</button>
			)}
		</div>
	);
}

function targetKey(target: Target): string {
	return target.kind === 'rule' ? `rule:${target.file}:${target.line}:${target.rule}` : target.kind === 'inline' ? 'inline' : `new:${target.selector}`;
}

function TargetPicker({ inspection }: { inspection: Inspection }) {
	const chosen = store.use((state) => state.writeTarget);
	const candidates = candidateTargets(inspection);
	const current = chosen ?? defaultTarget(inspection, '');

	return (
		<div className="prop target">
			<label title="A property that no rule of this element sets yet is added here. One that is already set is changed where it is written.">New properties go to</label>
			<select
				value={targetKey(current)}
				onChange={(event) => setWriteTarget(candidates.find((candidate) => targetKey(candidate) === event.target.value) ?? null)}
			>
				{candidates.map((candidate) => (
					<option key={targetKey(candidate)} value={targetKey(candidate)}>
						{describeTarget(candidate)}{candidate.kind === 'rule' ? `  (${basename(candidate.file)}:${candidate.line})` : ''}
					</option>
				))}
			</select>
		</div>
	);
}

function Classes({ inspection, editable }: { inspection: Inspection; editable: boolean }) {
	const classes = (inspection.attributes.class ?? '').split(/\s+/).filter(Boolean);
	const write = (next: string[]) => setElementAttribute(inspection.id, 'class', next.length ? next.join(' ') : null);

	return (
		<div className="chips">
			{classes.map((name) => (
				<span key={name} className="chip">
					{name}
					{editable && <button title="Remove class" onClick={() => write(classes.filter((entry) => entry !== name))}>×</button>}
				</span>
			))}
			{editable && (
				<CommitInput
					className="chip-add"
					value=""
					placeholder="add class"
					onCommit={(value) => {
						const added = value.split(/\s+/).map((name) => name.replace(/^\./, '')).filter((name) => name && !classes.includes(name));
						if (added.length) write([...classes, ...added]);
					}}
				/>
			)}
		</div>
	);
}

function Attributes({ inspection, editable }: { inspection: Inspection; editable: boolean }) {
	const entries = Object.entries(inspection.attributes).filter(([name]) => name !== 'id' && name !== 'class' && name !== 'style');
	const [name, setName] = useState('');
	// Read so the suggestions follow the models as they load and change.
	dataStore.use((state) => state.models);
	const bindings = knownBindings();

	return (
		<section>
			<h3>Attributes</h3>
			<datalist id="known-bindings">{bindings.map((binding) => <option key={binding} value={binding} />)}</datalist>
			{entries.map(([attribute, value]) => (
				<div key={attribute} className="prop set">
					<CommitInput
						className="attribute-name"
						value={attribute}
						onCommit={(next) => (next ? renameElementAttribute(inspection.id, attribute, next) : setElementAttribute(inspection.id, attribute, null))}
					/>
					<div className="prop-value">
						<CommitInput
							value={value}
							list={attribute.startsWith('data-') && bindings.length ? 'known-bindings' : undefined}
							onCommit={(next) => editable && setElementAttribute(inspection.id, attribute, next)}
						/>
					</div>
					{editable && <button className="origin" title="Remove attribute" onClick={() => setElementAttribute(inspection.id, attribute, null)}>×</button>}
				</div>
			))}
			{editable && (
				<div className="prop">
					<input
						className="attribute-name"
						placeholder="add attribute"
						value={name}
						spellCheck={false}
						onChange={(event) => setName(event.target.value)}
						onKeyDown={(event) => {
							if (event.key !== 'Enter' || !name.trim()) return;
							setElementAttribute(inspection.id, name.trim(), '');
							setName('');
						}}
					/>
				</div>
			)}
		</section>
	);
}

export function InspectorPanel() {
	const inspection = store.use((state) => state.inspection);
	// Read so the panel follows edits: whether an element is editable depends on the parsed markup.
	store.use((state) => state.revision);
	const [extra, setExtra] = useState('');

	if (!inspection) return <div className="panel empty">Select an element on the canvas or in Outline.</div>;

	const editable = isEditable(inspection.id);
	const text = elementText(inspection.id);
	const { boxes } = inspection;
	const others = inspection.rules.filter((entry) => !entry.inherited && !GROUPED.has(entry.name));
	const otherInline = Object.keys(inspection.inline).filter((name) => !GROUPED.has(name));

	return (
		<div className="panel inspector">
			<div className="inspector-title">
				<span className="tag-name">{inspection.id === '' ? 'body' : inspection.tag}</span>
				<span className="dim"> {Math.round(boxes.border[2])} × {Math.round(boxes.border[3])} at {Math.round(boxes.border[0])}, {Math.round(boxes.border[1])}</span>
			</div>

			{!editable && (
				<p className="notice">
					This element is not written in the document as it appears here, so it can be inspected but not edited on the canvas.
				</p>
			)}

			<section>
				<div className="prop set">
					<label>id</label>
					<div className="prop-value">
						<CommitInput value={inspection.attributes.id ?? ''} onCommit={(value) => editable && setElementAttribute(inspection.id, 'id', value || null)} />
					</div>
				</div>
				<div className="prop set">
					<label>class</label>
					<Classes inspection={inspection} editable={editable} />
				</div>
				{text !== null && (
					<div className="prop set">
						<label>text</label>
						<div className="prop-value">
							<CommitInput value={text.trim()} onCommit={(value) => editable && setElementText(inspection.id, value)} />
						</div>
					</div>
				)}
			</section>

			{editable && <TargetPicker inspection={inspection} />}

			{GROUPS.map((group) => (
				<section key={group.title}>
					<h3>{group.title}</h3>
					{group.fields.map((field) => <PropertyRow key={field.name} inspection={inspection} field={field} editable={editable} />)}
				</section>
			))}

			<section>
				<h3>Other</h3>
				{[...new Set([...others.map((entry) => entry.name), ...otherInline])].map((name) => (
					<PropertyRow key={name} inspection={inspection} field={{ name }} editable={editable} />
				))}
				{editable && (
					<div className="prop">
						<input
							className="attribute-name"
							placeholder="add property"
							value={extra}
							spellCheck={false}
							onChange={(event) => setExtra(event.target.value)}
						/>
						<div className="prop-value">
							<CommitInput
								value=""
								placeholder="value"
								onCommit={(value) => {
									if (!extra.trim() || !value) return;
									setStyle(inspection.id, extra.trim(), value, defaultTarget(inspection, extra.trim()));
									setExtra('');
								}}
							/>
						</div>
					</div>
				)}
			</section>

			<Attributes inspection={inspection} editable={editable} />
		</div>
	);
}
