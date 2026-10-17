import { DockviewReact, DockviewReadyEvent } from 'dockview-react';
import 'dockview-react/dist/styles/dockview.css';
import { useEffect } from 'react';
import { handleKey, runCommand } from './commands';
import { Dialogs } from './Dialogs';
import { setDock, showPanel } from './dock';
import { host } from './host';
import { ActivityPanel } from './panels/Activity';
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
	activity: ActivityPanel,
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

/** A build of the Folio checkout that is behind its sources, with a button to bring it up to date in the Terminal panel. */
function BuildNotice({ which, running, script, children }: { which: 'libraries' | 'preview'; running: string | null; script: string; children: React.ReactNode }) {
	return (
		<span className="update build-notice" title={'Runs ' + script + ' in the Terminal panel'}>
			{children}
			<button
				className="button"
				disabled={running !== null}
				onClick={() => {
					showPanel('terminal');
					store.set({ terminalBuild: which });
				}}
			>
				{running === which ? 'Rebuilding…' : 'Rebuild'}
			</button>
		</span>
	);
}

function TopBar() {
	const project = store.use((state) => state.project);
	const dirty = store.use((state) => state.dirty);
	const mode = store.use((state) => state.mode);
	const status = store.use((state) => state.status);
	const update = store.use((state) => state.update);
	const builds = store.use((state) => state.builds);
	const running = store.use((state) => state.terminalBuild);
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
			<span className="status" title="Click for every message this session (View > Messages)" onClick={() => store.set({ dialog: { kind: 'messages' } })}>{status}</span>
			<span className="spacer" />
			{builds.libraries && (
				<BuildNotice which="libraries" running={running} script="scripts/build_ue_libs.sh">
					Folio's Unreal libraries need rebuilding: {builds.libraries}
				</BuildNotice>
			)}
			{builds.preview && (
				<BuildNotice which="preview" running={running} script="scripts/build.sh">
					uipreview needs rebuilding: {builds.preview}
				</BuildNotice>
			)}
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

function HintBar() {
	const hint = store.use((state) => state.hint);
	const count = store.use((state) => state.messages.length);
	return (
		<footer className="hintbar">
			<span className="hint">{hint}</span>
			<button className="link" title="Every status message this session" onClick={() => store.set({ dialog: { kind: 'messages' } })}>
				Messages{count ? ` (${count})` : ''}
			</button>
		</footer>
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
		// The bar along the bottom shows the title of whatever is under the pointer, so the explanation
		// is there to read without waiting for a tooltip.
		const onOver = (event: MouseEvent) => {
			const titled = event.target instanceof Element ? event.target.closest('[title]') : null;
			const hint = titled?.getAttribute('title') ?? '';
			if (hint !== store.get().hint) store.set({ hint });
		};
		// The next thing the person does clears the last message, so it is never read as that action's result.
		const onAction = (event: Event) => {
			if (event instanceof KeyboardEvent && ['Control', 'Shift', 'Alt', 'Meta'].includes(event.key)) return;
			const { status } = store.get();
			if (status && !status.endsWith('…')) store.set({ status: '' });
		};
		window.addEventListener('mousedown', onAction, true);
		window.addEventListener('keydown', onAction, true);
		window.addEventListener('mouseover', onOver);
		window.addEventListener('keydown', onKey);
		window.addEventListener('mousedown', onMiddle);
		window.addEventListener('auxclick', onMiddle);
		host.onMenu(runCommand);
		return () => {
			window.removeEventListener('mousedown', onAction, true);
			window.removeEventListener('keydown', onAction, true);
			window.removeEventListener('mouseover', onOver);
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
			<HintBar />
		</div>
	);
}
