import { FitAddon } from '@xterm/addon-fit';
import { Terminal } from '@xterm/xterm';
import '@xterm/xterm/css/xterm.css';
import { useEffect, useRef, useState } from 'react';
import { popup } from '../commands';
import { host } from '../host';
import { checkBuilds, store } from '../session';

/** Everything in the scrollback as plain text, with lines the terminal wrapped joined back up. */
function bufferText(terminal: Terminal): string {
	const buffer = terminal.buffer.active;
	const lines: string[] = [];
	for (let index = 0; index < buffer.length; index++) {
		const line = buffer.getLine(index);
		if (!line) continue;
		const text = line.translateToString(true);
		if (line.isWrapped && lines.length) lines[lines.length - 1] += text;
		else lines.push(text);
	}
	while (lines.length && !lines[lines.length - 1]) lines.pop();
	return lines.join('\n');
}

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

	/** The process the terminal is attached to, or null between one ending and Enter starting the next. */
	const attached = useRef<number | null>(null);

	useEffect(() => {
		if (!root) return;
		const terminal = new Terminal({
			fontFamily: "'Roboto Mono', Consolas, Menlo, monospace",
			fontSize: store.get().settings.terminalFontSize,
			cursorBlink: true,
			scrollback: 10000,
			theme: { background: '#1c1c1c', foreground: '#d8d8d8', cursor: '#d8d8d8', selectionBackground: '#4a5560' },
		});
		const fit = new FitAddon();
		terminal.loadAddon(fit);
		terminal.open(container.current!);
		fit.fit();
		live.current = { terminal, fit };

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

		const offData = host.onTerminalData((from, data) => { if (from === attached.current) terminal.write(data); });
		const offExit = host.onTerminalExit((from) => {
			if (from !== attached.current) return;
			attached.current = null;
			if (building.current) {
				building.current = false;
				store.set({ terminalBuild: null });
				checkBuilds();
				terminal.write('\r\n[the build finished; press Enter for a shell]\r\n');
			} else {
				terminal.write('\r\n[the shell exited; press Enter for a new one]\r\n');
			}
		});
		terminal.onData((data) => { if (attached.current !== null) host.terminalInput(attached.current, data); });
		// Only Enter starts the next shell: focus and mouse reports arrive as data too, and would replace the output before it could be read.
		terminal.onKey(({ domEvent }) => { if (attached.current === null && domEvent.key === 'Enter') setSession((value) => value + 1); });
		terminal.onResize(({ cols, rows }) => { if (attached.current !== null) host.terminalResize(attached.current, cols, rows); });

		const observer = new ResizeObserver(() => {
			// A hidden panel has no size, and fitting to nothing would shrink the shell to one column.
			if (container.current?.clientWidth) fit.fit();
		});
		observer.observe(container.current!);

		return () => {
			live.current = null;
			observer.disconnect();
			offData();
			offExit();
			terminal.dispose();
		};
	}, [root]);

	useEffect(() => {
		const terminal = live.current?.terminal;
		if (!root || !terminal) return;
		let closed = false;
		setError(null);
		// A new console clears the screen, so what the last one left on it is scrolled up out of the way first.
		if (terminal.buffer.active.baseY + terminal.buffer.active.cursorY > 0) terminal.write('\r\n'.repeat(terminal.rows));

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
			attached.current = opened;
		});

		return () => {
			closed = true;
			const running = attached.current;
			attached.current = null;
			if (running !== null) host.terminalClose(running);
		};
	}, [root, session]);

	// A build asked for while a shell is already open replaces it.
	useEffect(() => {
		if (build && !building.current) setSession((value) => value + 1);
	}, [build]);

	const onMenu = async (event: React.MouseEvent) => {
		event.preventDefault();
		const terminal = live.current?.terminal;
		if (!terminal) return;
		const selected = terminal.getSelection();
		const chosen = await popup([
			{ id: 'copy', label: 'Copy', accelerator: 'Ctrl+Shift+C', enabled: !!selected },
			{ id: 'all', label: 'Copy All Output' },
			{ id: 'paste', label: 'Paste' },
			{ separator: true },
			{ id: 'clear', label: 'Clear' },
		]);
		if (chosen === 'copy') navigator.clipboard.writeText(selected);
		else if (chosen === 'all') navigator.clipboard.writeText(bufferText(terminal));
		else if (chosen === 'paste') terminal.paste(await navigator.clipboard.readText());
		else if (chosen === 'clear') terminal.clear();
		terminal.focus();
	};

	if (!root) return <div className="panel empty">No project open.</div>;
	return (
		<div className="panel terminal-panel">
			{error && <div className="terminal-error">{error}</div>}
			<div className="terminal-surface" ref={container} onContextMenu={onMenu} />
		</div>
	);
}
