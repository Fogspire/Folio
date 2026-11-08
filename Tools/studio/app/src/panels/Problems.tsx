import { useState } from 'react';
import { popup } from '../commands';
import { showPanel } from '../dock';
import { Diagnostic } from '../engine';
import { checkFlashing, revealSource, setContrast, store } from '../session';

/** One problem as a line of text, in the same shape uipreview prints it. */
function describe(entry: Diagnostic): string {
	const where = entry.file ? `${entry.file}${entry.line ? ':' + entry.line : ''}: ` : '';
	return `${where}${entry.severity}: [${entry.code}] ${entry.message}`;
}

function reveal(entry: Diagnostic) {
	if (!entry.file) return;
	revealSource(entry.file, entry.line);
	showPanel('code');
}

function Toolbar({ all, setAll }: { all: boolean; setAll: (value: boolean) => void }) {
	const contrast = store.use((state) => state.contrast);
	const documentOpen = store.use((state) => state.documentOpen);
	return (
		<div className="toolbar">
			<div className="segmented" title="Notes are what the renderer reports that needs no change, such as a mock it mounted or a layer it measured">
				<button className={all ? '' : 'active'} onClick={() => setAll(false)}>Problems</button>
				<button className={all ? 'active' : ''} onClick={() => setAll(true)}>With Notes</button>
			</div>
			<label title="The contrast normal text is held to. 7:1 is what a high-contrast theme should meet.">
				Contrast
				<select value={contrast} onChange={(event) => setContrast(Number(event.target.value))}>
					<option value={4.5}>4.5:1</option>
					<option value={7}>7:1</option>
				</select>
			</label>
			<span className="spacer" />
			<button
				className="button"
				disabled={!documentOpen}
				title="Plays the document from its first frame and reports flashing over three a second, a seizure risk"
				onClick={checkFlashing}
			>
				Check Flashing
			</button>
		</div>
	);
}

export function ProblemsPanel() {
	const diagnostics = store.use((state) => state.diagnostics);
	const checks = store.use((state) => state.checks);
	const flash = store.use((state) => state.flash);
	const document = store.use((state) => state.document);
	const [all, setAll] = useState(false);

	const entries = [...diagnostics, ...checks, ...(flash && flash.document === document ? [flash.result] : [])];
	const shown = all ? entries : entries.filter((entry) => entry.severity !== 'info');

	return (
		<div className="panel problems-panel">
			<Toolbar all={all} setAll={setAll} />
			{!shown.length ? (
				<div className="panel empty">No problems.</div>
			) : (
				<div className="problems">
					{shown.map((entry, index) => (
						<div
							key={index}
							className={'problem ' + entry.severity}
							onClick={() => {
								// A click that ends a drag across the text is selecting it to copy, not asking to go there.
								if (!window.getSelection()?.toString()) reveal(entry);
							}}
							onContextMenu={async (event) => {
								event.preventDefault();
								const selected = window.getSelection()?.toString() ?? '';
								const chosen = await popup([
									{ id: 'selection', label: 'Copy Selection', enabled: !!selected },
									{ id: 'one', label: 'Copy Problem' },
									{ id: 'all', label: 'Copy All Problems' },
									{ separator: true },
									{ id: 'reveal', label: 'Go to Source', enabled: !!entry.file },
								]);
								if (chosen === 'selection') navigator.clipboard.writeText(selected);
								else if (chosen === 'one') navigator.clipboard.writeText(describe(entry));
								else if (chosen === 'all') navigator.clipboard.writeText(shown.map(describe).join('\n'));
								else if (chosen === 'reveal') reveal(entry);
							}}
						>
							<span className="severity">{entry.severity === 'info' ? 'note' : entry.severity}</span>
							<span className="message">{entry.message}</span>
							<span className="dim">
								{entry.code}
								{entry.file ? `  ${entry.file}${entry.line ? ':' + entry.line : ''}` : ''}
							</span>
						</div>
					))}
				</div>
			)}
		</div>
	);
}
