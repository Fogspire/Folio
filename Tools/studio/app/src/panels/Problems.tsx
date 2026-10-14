import { showPanel } from '../dock';
import { revealSource, store } from '../session';

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
						if (!entry.file) return;
						revealSource(entry.file, entry.line);
						showPanel('code');
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
