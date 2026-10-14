import { useEffect, useMemo, useRef, useState } from 'react';
import { showPanel } from './dock';
import { fuzzyMatch } from './fuzzy';
import { host } from './host';
import { assignKey, COMMANDS, commandInfo, comboOf, displayKey, keysFor, resetKey } from './keys';
import { extension, relativeTo } from './paths';
import { answerDialog, bakeElement, openDocument, openFile, store, updateSettings } from './session';
import { DEFAULT_SETTINGS } from './settings';

export type Dialog =
	| { kind: 'preferences' }
	| { kind: 'keys' }
	| { kind: 'assign'; command: string }
	| { kind: 'bake'; id: string; name: string }
	| { kind: 'go-to-file' }
	| { kind: 'messages' }
	| { kind: 'notice'; title: string; detail: string; buttons: string[]; default: string };

function close() {
	answerDialog(null);
}

function Frame({ title, wide, children }: { title: string; wide?: boolean; children: React.ReactNode }) {
	return (
		<div className="dialog-scrim" onMouseDown={(event) => { if (event.target === event.currentTarget) close(); }}>
			<div className={'dialog' + (wide ? ' wide' : '')} role="dialog" aria-label={title}>
				<header>
					<h2>{title}</h2>
					<button className="link" onClick={close}>Close</button>
				</header>
				{children}
			</div>
		</div>
	);
}

function Preferences() {
	const settings = store.use((state) => state.settings);
	const [defaultShell, setDefaultShell] = useState('');
	useEffect(() => { host.defaultShell().then(setDefaultShell); }, []);

	const size = (name: 'terminalFontSize' | 'codeFontSize') => (
		<input
			type="number"
			min={8}
			max={32}
			value={settings[name]}
			onChange={(event) => {
				const value = Number(event.target.value);
				if (value >= 8 && value <= 32) updateSettings({ [name]: value });
			}}
		/>
	);

	return (
		<Frame title="Preferences">
			<div className="dialog-body">
				<h3>Terminal</h3>
				<label className="field-row">
					<span>Shell</span>
					<input value={settings.terminalShell} placeholder={defaultShell} spellCheck={false} onChange={(event) => updateSettings({ terminalShell: event.target.value })} />
				</label>
				<label className="field-row">
					<span>Arguments</span>
					<input value={settings.terminalArguments} placeholder={settings.terminalShell ? 'none' : 'the defaults for that shell'} spellCheck={false} onChange={(event) => updateSettings({ terminalArguments: event.target.value })} />
				</label>
				<p className="dim">The full path of the program to run, such as PowerShell or zsh. A change applies to the next terminal opened.</p>
				<label className="field-row"><span>Font size</span>{size('terminalFontSize')}</label>

				<h3>Code</h3>
				<label className="field-row"><span>Font size</span>{size('codeFontSize')}</label>

				<h3>Canvas</h3>
				<label className="check-row">
					<input type="checkbox" checked={settings.shiftResize} onChange={(event) => updateSettings({ shiftResize: event.target.checked })} />
					Shift-drag a resize handle to resize only that element
				</label>

				<h3>Startup</h3>
				<label className="check-row">
					<input type="checkbox" checked={settings.restoreLayout} onChange={(event) => updateSettings({ restoreLayout: event.target.checked })} />
					Reopen with the panels as they were left
				</label>
				<label className="check-row">
					<input type="checkbox" checked={settings.updateCheck} onChange={(event) => updateSettings({ updateCheck: event.target.checked })} />
					Check for updates
				</label>
			</div>
			<footer>
				<button className="button" onClick={() => updateSettings({ ...DEFAULT_SETTINGS, keys: settings.keys })}>Restore Defaults</button>
				<button className="button" onClick={() => store.set({ dialog: { kind: 'keys' } })}>Keyboard Shortcuts…</button>
			</footer>
		</Frame>
	);
}

/** Waits for a key and hands it over. Escape gives up. */
function useCapture(active: boolean, onKey: (combo: string) => void, onCancel: () => void) {
	useEffect(() => {
		if (!active) return;
		const listener = (event: KeyboardEvent) => {
			event.preventDefault();
			event.stopPropagation();
			const combo = comboOf(event);
			if (!combo) return;
			if (combo === 'Escape') onCancel();
			else onKey(combo);
		};
		window.addEventListener('keydown', listener, true);
		return () => window.removeEventListener('keydown', listener, true);
	});
}

function assign(command: string, combo: string | null) {
	const { overrides, taken } = assignKey(store.get().settings.keys, command, combo);
	updateSettings({ keys: overrides });
	const lost = taken ? commandInfo(taken)?.label : null;
	store.set({ status: lost ? `${displayKey(combo!)} was taken from ${lost}` : '' });
}

function Keys() {
	const overrides = store.use((state) => state.settings.keys);
	const [recording, setRecording] = useState<string | null>(null);
	const [filter, setFilter] = useState('');
	useCapture(recording !== null, (combo) => {
		assign(recording!, combo);
		setRecording(null);
	}, () => setRecording(null));

	const wanted = filter.trim().toLowerCase();
	const shown = COMMANDS.filter((entry) => !wanted || (entry.group + ' ' + entry.label + ' ' + keysFor(entry.id, overrides).join(' ')).toLowerCase().includes(wanted));

	return (
		<Frame title="Keyboard Shortcuts" wide>
			<div className="dialog-body">
				<input className="key-filter" value={filter} placeholder="Filter by command or key" spellCheck={false} onChange={(event) => setFilter(event.target.value)} />
				<p className="dim">Change sets a new key and frees it from whatever had it. Ctrl+Shift+click an item in the menu bar does the same from there.</p>
				<table className="key-table">
					<tbody>
						{shown.map((entry) => {
							const keys = keysFor(entry.id, overrides);
							return (
								<tr key={entry.id} data-command={entry.id}>
									<td className="dim">{entry.group}</td>
									<td>{entry.label}</td>
									<td className="key-cell">
										{recording === entry.id ? <span className="key recording">Press a key, or Escape</span>
											: keys.length ? keys.map((key) => <span key={key} className="key">{displayKey(key)}</span>)
											: <span className="dim">none</span>}
									</td>
									<td className="key-actions">
										<button className="link" onClick={() => setRecording(entry.id)}>Change</button>
										<button className="link" disabled={!keys.length} onClick={() => assign(entry.id, null)}>Remove</button>
										<button className="link" disabled={!(entry.id in overrides)} onClick={() => updateSettings({ keys: resetKey(overrides, entry.id) })}>Default</button>
									</td>
								</tr>
							);
						})}
					</tbody>
				</table>
			</div>
			<footer>
				<button className="button" disabled={!Object.keys(overrides).length} onClick={() => updateSettings({ keys: {} })}>Restore All Defaults</button>
			</footer>
		</Frame>
	);
}

function Messages() {
	const messages = store.use((state) => state.messages);
	const time = (value: number) => new Date(value).toLocaleTimeString();
	const all = () => messages.map((entry) => `${time(entry.time)}  ${entry.text}`).join('\n');
	return (
		<Frame title="Messages" wide>
			<div className="dialog-body messages">
				{messages.length ? (
					<ul>
						{[...messages].reverse().map((entry, index) => (
							<li key={messages.length - index}>
								<span className="dim">{time(entry.time)}</span>
								<span>{entry.text}</span>
							</li>
						))}
					</ul>
				) : <p className="dim">Nothing yet. Messages from the bar at the top collect here as they appear.</p>}
			</div>
			<footer>
				<button className="button" disabled={!messages.length} onClick={() => navigator.clipboard.writeText(all())}>Copy All</button>
				<button className="button" disabled={!messages.length} onClick={() => store.set({ messages: [] })}>Clear</button>
			</footer>
		</Frame>
	);
}

/** One command waiting for its key, which is what a Ctrl+Shift+click in the menu bar opens. */
function Assign({ command }: { command: string }) {
	const overrides = store.use((state) => state.settings.keys);
	const info = commandInfo(command);
	const keys = keysFor(command, overrides);
	useCapture(!!info, (combo) => {
		assign(command, combo);
		close();
	}, close);

	return (
		<Frame title={info ? 'Shortcut for ' + info.label : 'Shortcut'}>
			<div className="dialog-body">
				{!info ? <p className="dim">This item cannot be given a key.</p> : (
					<>
						<p>Press the key to use, or Escape to leave it as it is.</p>
						<p className="key-cell">
							<span className="dim">Now: </span>
							{keys.length ? keys.map((key) => <span key={key} className="key">{displayKey(key)}</span>) : <span className="dim">none</span>}
						</p>
					</>
				)}
			</div>
			{info && (
				<footer>
					<button className="button" disabled={!keys.length} onClick={() => { assign(command, null); close(); }}>Remove Shortcut</button>
					<button className="button" disabled={!(command in overrides)} onClick={() => { updateSettings({ keys: resetKey(overrides, command) }); close(); }}>Use Default</button>
				</footer>
			)}
		</Frame>
	);
}

/** A message with a choice of buttons, which is what the app asks instead of a system message box. */
function Notice({ title, detail, buttons, preferred }: { title: string; detail: string; buttons: string[]; preferred: string }) {
	return (
		<Frame title={title}>
			<div className="dialog-body message">{detail}</div>
			<footer>
				<span />
				<span className="row">
					{buttons.map((label) => (
						<button key={label} className={'button' + (label === preferred ? ' primary' : '')} autoFocus={label === preferred} onClick={() => answerDialog(label)}>
							{label}
						</button>
					))}
				</span>
			</footer>
		</Frame>
	);
}

const SAMPLES = [1, 2, 4];
const BACKGROUNDS: [string, string][] = [['none', 'None (transparent)'], ['#ffffff', 'White'], ['#000000', 'Black'], ['#808080', '50% grey'], ['custom', 'Custom']];

/** Choices for a bake, before asking where to save it. */
function Bake({ id, name }: { id: string; name: string }) {
	const samples = store.use((state) => state.settings.bakeSamples);
	const background = store.use((state) => state.settings.bakeBackground);
	const custom = store.use((state) => state.settings.bakeCustomColour);
	const isCustom = background !== 'none' && !BACKGROUNDS.some(([value]) => value === background);
	const colour = background === 'none' ? null : background;
	return (
		<Frame title={'Bake ' + name + ' to PNG'}>
			<div className="dialog-body">
				<label className="field-row">
					<span>Samples</span>
					<select value={samples} onChange={(event) => updateSettings({ bakeSamples: Number(event.target.value) })}>
						{SAMPLES.map((count) => <option key={count} value={count}>{count === 1 ? '1 (as drawn)' : count + ' each way'}</option>)}
					</select>
				</label>
				<label className="field-row">
					<span>Background</span>
					<select
						value={isCustom ? 'custom' : background}
						onChange={(event) => updateSettings({ bakeBackground: event.target.value === 'custom' ? custom : event.target.value })}
					>
						{BACKGROUNDS.map(([value, label]) => <option key={value} value={value}>{label}</option>)}
					</select>
					{isCustom && (
						<input
							type="color"
							value={background}
							onChange={(event) => updateSettings({ bakeBackground: event.target.value, bakeCustomColour: event.target.value })}
						/>
					)}
				</label>
				<p className="dim">More samples draw the element at that many times the pixels and average them back down, so a slanted or curved edge comes out smooth at the same size. Lengths in px do not grow while it draws, so check a layout that leans on them.</p>
			</div>
			<footer>
				<span />
				<span className="row">
					<button className="button" onClick={close}>Cancel</button>
					<button className="button primary" autoFocus onClick={() => { answerDialog(null); bakeElement(id, undefined, samples, colour); }}>Bake…</button>
				</span>
			</footer>
		</Frame>
	);
}

/** Text with the characters at the given indices marked. */
function Highlighted({ text, indices, offset }: { text: string; indices: number[]; offset: number }) {
	const marked = new Set(indices.map((index) => index - offset));
	return <>{[...text].map((character, index) => (marked.has(index) ? <b key={index}>{character}</b> : character))}</>;
}

const SHOWN = 60;

/** Type part of a file's name, pick it from the list, and Enter opens it. */
function GoToFile() {
	const root = store.use((state) => state.project?.root ?? '');
	const files = store.use((state) => state.projectFiles);
	const [query, setQuery] = useState('');
	const [chosen, setChosen] = useState(0);
	const list = useRef<HTMLDivElement>(null);

	const matches = useMemo(() => files
		.map((file) => {
			const path = relativeTo(root, file);
			return { file, path, match: fuzzyMatch(query, path) };
		})
		.filter((entry) => entry.match !== null)
		.sort((a, b) => b.match!.score - a.match!.score || a.path.localeCompare(b.path))
		.slice(0, SHOWN), [files, root, query]);

	useEffect(() => setChosen(0), [query]);
	useEffect(() => { list.current?.children[chosen]?.scrollIntoView({ block: 'nearest' }); }, [chosen]);

	const open = (file: string) => {
		answerDialog(null);
		if (extension(file) === '.rml') {
			openDocument(file);
			showPanel('canvas');
		} else {
			openFile(file);
			showPanel('code');
		}
	};

	return (
		<div className="dialog-scrim top" onMouseDown={(event) => { if (event.target === event.currentTarget) close(); }}>
			<div className="dialog go-to-file" role="dialog" aria-label="Go to File">
				<input
					autoFocus
					value={query}
					placeholder="Go to a file by name"
					spellCheck={false}
					onChange={(event) => setQuery(event.target.value)}
					onKeyDown={(event) => {
						const step = event.key === 'ArrowDown' ? 1 : event.key === 'ArrowUp' ? -1 : 0;
						if (step && matches.length) {
							event.preventDefault();
							setChosen((chosen + step + matches.length) % matches.length);
						} else if (event.key === 'Enter' && matches[chosen]) {
							event.preventDefault();
							open(matches[chosen].file);
						} else if (event.key === 'Tab' && matches[chosen]) {
							// Completes to the chosen file's name, which narrows the list to it.
							event.preventDefault();
							setQuery(matches[chosen].path.split('/').pop()!);
						}
					}}
				/>
				<div className="go-to-file-list" ref={list}>
					{matches.map(({ file, path, match }, index) => {
						const slash = path.lastIndexOf('/') + 1;
						return (
							<div
								key={file}
								className={'go-to-file-row' + (index === chosen ? ' selected' : '')}
								onMouseMove={() => setChosen(index)}
								onClick={() => open(file)}
							>
								<span className="name"><Highlighted text={path.slice(slash)} indices={match!.indices} offset={slash} /></span>
								<span className="dim"><Highlighted text={path.slice(0, slash)} indices={match!.indices} offset={0} /></span>
							</div>
						);
					})}
					{!matches.length && <div className="go-to-file-row dim">No file matches.</div>}
				</div>
			</div>
		</div>
	);
}

export function Dialogs() {
	const dialog = store.use((state) => state.dialog);
	if (!dialog) return null;
	if (dialog.kind === 'preferences') return <Preferences />;
	if (dialog.kind === 'keys') return <Keys />;
	if (dialog.kind === 'go-to-file') return <GoToFile />;
	if (dialog.kind === 'messages') return <Messages />;
	if (dialog.kind === 'bake') return <Bake id={dialog.id} name={dialog.name} />;
	if (dialog.kind === 'notice') return <Notice title={dialog.title} detail={dialog.detail} buttons={dialog.buttons} preferred={dialog.default} />;
	return <Assign command={dialog.command} />;
}
