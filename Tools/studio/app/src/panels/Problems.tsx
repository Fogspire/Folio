import { popup } from '../commands';
import { showPanel } from '../dock';
import { Diagnostic } from '../engine';
import { revealSource, store } from '../session';

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

export function ProblemsPanel() {
	const diagnostics = store.use((state) => state.diagnostics);
	const problems = diagnostics.filter((entry) => entry.severity !== 'info');

	if (!problems.length) return <div className="panel empty">No problems.</div>;
	return (
		<div className="panel problems">
			{problems.map((entry, index) => (
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
						else if (chosen === 'all') navigator.clipboard.writeText(problems.map(describe).join('\n'));
						else if (chosen === 'reveal') reveal(entry);
					}}
				>
					<span className="severity">{entry.severity}</span>
					<span className="message">{entry.message}</span>
					<span className="dim">
						{entry.code}
						{entry.file ? `  ${entry.file}${entry.line ? ':' + entry.line : ''}` : ''}
					</span>
				</div>
			))}
		</div>
	);
}
