import { useEffect, useState } from 'react';
import { host } from './host';
import { assignKey, COMMANDS, commandInfo, comboOf, displayKey, keysFor, resetKey } from './keys';
import { store, updateSettings } from './session';
import { DEFAULT_SETTINGS } from './settings';

export type Dialog = { kind: 'preferences' } | { kind: 'keys' } | { kind: 'assign'; command: string };

function close() {
	store.set({ dialog: null });
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

export function Dialogs() {
	const dialog = store.use((state) => state.dialog);
	if (!dialog) return null;
	if (dialog.kind === 'preferences') return <Preferences />;
	if (dialog.kind === 'keys') return <Keys />;
	return <Assign command={dialog.command} />;
}
