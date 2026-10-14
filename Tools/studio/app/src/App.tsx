import { DockviewReact, DockviewReadyEvent } from 'dockview-react';
import 'dockview-react/dist/styles/dockview.css';
import { useEffect } from 'react';
import { handleKey, runCommand } from './commands';
import { Dialogs } from './Dialogs';
import { setDock } from './dock';
import { host } from './host';
import { CanvasPanel } from './panels/Canvas';
import { CodePanel } from './panels/Code';
import { DataPanel } from './panels/Data';
import { DocsPanel } from './panels/Docs';
import { FilesPanel } from './panels/Files';
import { InspectorPanel } from './panels/Inspector';
import { LibraryPanel } from './panels/Library';
import { OutlinePanel } from './panels/Outline';
import { ProblemsPanel } from './panels/Problems';
import { TerminalPanel } from './panels/Terminal';
import { basename } from './paths';
import { checkForUpdatesNow, openProject, pickProject, revertAll, saveAll, setMode, store, toggleUpdateCheck } from './session';

const components = {
	canvas: CanvasPanel,
	code: CodePanel,
	data: DataPanel,
	docs: DocsPanel,
	files: FilesPanel,
	inspector: InspectorPanel,
	library: LibraryPanel,
	outline: OutlinePanel,
	problems: ProblemsPanel,
	terminal: TerminalPanel,
};

function layout(event: DockviewReadyEvent) {
	setDock(event.api);
}

function TopBar() {
	const project = store.use((state) => state.project);
	const dirty = store.use((state) => state.dirty);
	const mode = store.use((state) => state.mode);
	const status = store.use((state) => state.status);
	const update = store.use((state) => state.update);
	const hostState = store.use((state) => state.hostState);
	const dirtyCount = Object.keys(dirty).length;

	return (
		<header className="topbar">
			<span className="brand">Folio Studio</span>
			<button className="button" onClick={pickProject} title="Open a project folder">
				{project ? project.name : 'Open project…'}
			</button>
			<button className="button" disabled={!dirtyCount} onClick={saveAll} title="Save all (Ctrl+S)">
				{dirtyCount ? `Save ${dirtyCount}` : 'Saved'}
			</button>
			{dirtyCount > 0 && (
				<button className="button" onClick={revertAll} title="Put every unsaved file back as it is on disk">
					Revert
				</button>
			)}
			<div className="segmented" role="group" aria-label="Canvas mode">
				<button className={mode === 'edit' ? 'active' : ''} onClick={() => setMode('edit')} title="Select and edit elements">Edit</button>
				<button className={mode === 'play' ? 'active' : ''} onClick={() => setMode('play')} title="Interact with the document as a player would">Play</button>
			</div>
			<span className="status">{status}</span>
			<span className="spacer" />
			{update?.available && (
				<span className="update" title={'Update with: git pull, then scripts/studio.sh run'}>
					Version {update.latest} is available (you have {update.current})
				</span>
			)}
			<button
				className="version"
				title="Click to check for updates. Alt-click to turn the check at startup on or off."
				onClick={(event) => (event.altKey ? toggleUpdateCheck() : checkForUpdatesNow())}
			>
				{hostState ? 'v' + hostState.version : ''}
			</button>
		</header>
	);
}

function Welcome() {
	const hostState = store.use((state) => state.hostState);
	return (
		<div className="welcome">
			<div className="welcome-card">
				<h1>Folio Studio</h1>
				<p>Open the folder that contains your project's <code>UI/</code> directory.</p>
				<button className="button primary" onClick={pickProject}>Open project…</button>
				{!!hostState?.recent.length && (
					<>
						<h2>Recent</h2>
						<ul className="recent">
							{hostState.recent.map((root) => (
								<li key={root}>
									<button className="link" onClick={() => openProject(root)} title={root}>
										{basename(root.replace(/\\/g, '/'))}
										<span className="dim"> {root}</span>
									</button>
								</li>
							))}
						</ul>
					</>
				)}
			</div>
		</div>
	);
}

export function App() {
	const project = store.use((state) => state.project);

	useEffect(() => {
		const onKey = (event: KeyboardEvent) => {
			if (!event.defaultPrevented && handleKey(event)) event.preventDefault();
		};
		// A panel's tab closes on a middle-click, as a file's does. View brings it back.
		const onMiddle = (event: MouseEvent) => {
			const tab = event.button === 1 && event.target instanceof HTMLElement ? event.target.closest('.dv-default-tab') : null;
			if (!tab) return;
			event.preventDefault();
			if (event.type === 'auxclick') tab.querySelector<HTMLElement>('.dv-default-tab-action')?.click();
		};
		window.addEventListener('keydown', onKey);
		window.addEventListener('mousedown', onMiddle);
		window.addEventListener('auxclick', onMiddle);
		host.onMenu(runCommand);
		return () => {
			window.removeEventListener('keydown', onKey);
			window.removeEventListener('mousedown', onMiddle);
			window.removeEventListener('auxclick', onMiddle);
		};
	}, []);

	return (
		<div className="studio">
			<TopBar />
			<main className="workspace">
				<DockviewReact className="dockview-theme-dark" components={components} onReady={layout} />
				{!project && <Welcome />}
				<Dialogs />
			</main>
		</div>
	);
}
