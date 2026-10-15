import { FitAddon } from '@xterm/addon-fit';
import { Terminal } from '@xterm/xterm';
import '@xterm/xterm/css/xterm.css';
import { useEffect, useRef, useState } from 'react';
import { host } from '../host';
import { checkBuilds, store } from '../session';

/** A shell in the project's folder, for git, an agent, or anything else run from a command line. */
export function TerminalPanel() {
	const project = store.use((state) => state.project);
	const container = useRef<HTMLDivElement>(null);
	const [error, setError] = useState<string | null>(null);
	/** Bumped to start a fresh shell after one exits. */
	const [session, setSession] = useState(0);
	const root = project?.root ?? null;
	const fontSize = store.use((state) => state.settings.terminalFontSize);
	const live = useRef<{ terminal: Terminal; fit: FitAddon } | null>(null);
	const build = store.use((state) => state.terminalBuild);
	/** Whether the running session is the requested build, so the request starts one session, not two. */
	const building = useRef(false);

	useEffect(() => {
		if (!live.current) return;
		live.current.terminal.options.fontSize = fontSize;
		live.current.fit.fit();
	}, [fontSize]);

	useEffect(() => {
		if (!root) return;
		const terminal = new Terminal({
			fontFamily: "'Roboto Mono', Consolas, Menlo, monospace",
			fontSize: store.get().settings.terminalFontSize,
			cursorBlink: true,
			theme: { background: '#1c1c1c', foreground: '#d8d8d8', cursor: '#d8d8d8', selectionBackground: '#4a5560' },
		});
		const fit = new FitAddon();
		terminal.loadAddon(fit);
		terminal.open(container.current!);
		fit.fit();
		live.current = { terminal, fit };

		let id: number | null = null;
		let closed = false;
		setError(null);

		// Ctrl+C stays an interrupt, so copy is Ctrl+Shift+C. Paste is left to the browser, which hands it to the terminal.
		terminal.attachCustomKeyEventHandler((event) => {
			const key = event.key.toLowerCase();
			if (event.type !== 'keydown' || !(event.ctrlKey || event.metaKey)) return true;
			if (key === 'c' && event.shiftKey) {
				navigator.clipboard.writeText(terminal.getSelection()).catch(() => undefined);
				event.preventDefault();
				return false;
			}
			return key !== 'v';
		});

		const offData = host.onTerminalData((from, data) => { if (from === id) terminal.write(data); });
		const offExit = host.onTerminalExit((from) => {
			if (from !== id) return;
			id = null;
			if (building.current) {
				building.current = false;
				store.set({ terminalBuild: null });
				checkBuilds();
				terminal.write('\r\n[the build finished; press any key for a shell]\r\n');
			} else {
				terminal.write('\r\n[the shell exited; press any key for a new one]\r\n');
			}
		});
		terminal.onData((data) => {
			if (id !== null) host.terminalInput(id, data);
			else if (!closed) setSession((value) => value + 1);
		});
		terminal.onResize(({ cols, rows }) => { if (id !== null) host.terminalResize(id, cols, rows); });

		const { terminalShell, terminalArguments } = store.get().settings;
		const requested = store.get().terminalBuild;
		building.current = requested !== null;
		const opening = requested
			? host.terminalBuild(requested, terminal.cols, terminal.rows)
			: host.terminalOpen(root, terminal.cols, terminal.rows, terminalShell, terminalArguments);
		opening.then((opened) => {
			if (typeof opened === 'string') {
				if (building.current) {
					building.current = false;
					store.set({ terminalBuild: null });
				}
				return setError(opened);
			}
			if (closed) return host.terminalClose(opened);
			id = opened;
		});

		const observer = new ResizeObserver(() => {
			// A hidden panel has no size, and fitting to nothing would shrink the shell to one column.
			if (container.current?.clientWidth) fit.fit();
		});
		observer.observe(container.current!);

		return () => {
			closed = true;
			live.current = null;
			observer.disconnect();
			offData();
			offExit();
			if (id !== null) host.terminalClose(id);
			terminal.dispose();
		};
	}, [root, session]);

	// A build asked for while a shell is already open replaces it.
	useEffect(() => {
		if (build && !building.current) setSession((value) => value + 1);
	}, [build]);

	if (!root) return <div className="panel empty">No project open.</div>;
	return (
		<div className="panel terminal-panel">
			{error && <div className="terminal-error">{error}</div>}
			<div className="terminal-surface" ref={container} />
		</div>
	);
}
