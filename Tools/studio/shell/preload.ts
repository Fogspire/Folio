import { contextBridge, ipcRenderer } from 'electron';

interface ContextItem {
	id?: string;
	label?: string;
	accelerator?: string;
	enabled?: boolean;
	separator?: boolean;
	submenu?: ContextItem[];
}

const host = {
	state: () => ipcRenderer.invoke('state'),
	pickProject: () => ipcRenderer.invoke('pick-project'),
	openProject: (root: string) => ipcRenderer.invoke('open-project', root),
	listTree: (directory: string) => ipcRenderer.invoke('list-tree', directory),
	readTree: (directory: string) => ipcRenderer.invoke('read-tree', directory),
	readFile: (target: string) => ipcRenderer.invoke('read-file', target),
	writeFile: (target: string, data: string | Uint8Array) => ipcRenderer.invoke('write-file', target, data),
	exists: (target: string) => ipcRenderer.invoke('exists', target),
	watch: (directory: string) => ipcRenderer.invoke('watch', directory),
	reveal: (target: string) => ipcRenderer.invoke('reveal', target),
	openExternal: (url: string) => ipcRenderer.invoke('open-external', url),
	checkUpdates: () => ipcRenderer.invoke('check-updates'),
	onFilesChanged: (callback: (paths: string[]) => void) => {
		ipcRenderer.on('files-changed', (_event, paths: string[]) => callback(paths));
	},
	native: (action: 'undo' | 'redo' | 'cut' | 'copy' | 'paste' | 'delete') => ipcRenderer.invoke('native', action),
	contextMenu: (items: ContextItem[]): Promise<string | null> =>
		ipcRenderer.invoke('context-menu', items),
	/** A scripted run starts from the default layout, not the one last left on this machine. */
	testing: process.argv.includes('--studio-test'),
	saveLayout: (layout: string, target?: string): Promise<string | null> => ipcRenderer.invoke('save-layout', layout, target),
	loadLayout: (target?: string): Promise<string | null> => ipcRenderer.invoke('load-layout', target),
	terminalOpen: (directory: string, cols: number, rows: number, shell = '', shellArguments = ''): Promise<number | string> =>
		ipcRenderer.invoke('terminal-open', directory, cols, rows, shell, shellArguments),
	createShortcuts: (into?: string): Promise<string[] | string> => ipcRenderer.invoke('create-shortcuts', into),
	saveFile: (title: string, defaultPath: string, extension: string, data: Uint8Array): Promise<{ path?: string; error?: string } | null> =>
		ipcRenderer.invoke('save-file', title, defaultPath, extension, data),
	/** Resolves with an error message, or an empty string when it opened. */
	openSaved: (target: string, how: 'open' | 'reveal'): Promise<string> => ipcRenderer.invoke('open-saved', target, how),
	defaultShell: (): Promise<string> => ipcRenderer.invoke('default-shell'),
	trash: (target: string): Promise<boolean> => ipcRenderer.invoke('trash', target),
	terminalInput: (id: number, data: string) => ipcRenderer.send('terminal-input', id, data),
	terminalResize: (id: number, cols: number, rows: number) => ipcRenderer.send('terminal-resize', id, cols, rows),
	terminalClose: (id: number) => ipcRenderer.send('terminal-close', id),
	/** Both return a function that stops listening. */
	onTerminalData: (callback: (id: number, data: string) => void) => {
		const listener = (_event: unknown, id: number, data: string) => callback(id, data);
		ipcRenderer.on('terminal-data', listener);
		return () => { ipcRenderer.removeListener('terminal-data', listener); };
	},
	onTerminalExit: (callback: (id: number) => void) => {
		const listener = (_event: unknown, id: number) => callback(id);
		ipcRenderer.on('terminal-exit', listener);
		return () => { ipcRenderer.removeListener('terminal-exit', listener); };
	},
	setMenuState: (state: { updateCheck: boolean; mode: string; keys: Record<string, string | undefined> }) => ipcRenderer.invoke('menu-state', state),
	onMenu: (callback: (command: string, argument?: string) => void) => {
		ipcRenderer.on('menu', (_event, command: string, argument?: string) => callback(command, argument));
	},
	capture: (target: string) => ipcRenderer.invoke('capture', target),
	testScript: () => ipcRenderer.invoke('test-script'),
	testMenuChecked: (labels: string[]): Promise<boolean | null> => ipcRenderer.invoke('test-menu-checked', labels),
	testMenu: (labels: string[], modifiers?: { ctrlKey?: boolean; shiftKey?: boolean }): Promise<boolean> => ipcRenderer.invoke('test-menu', labels, modifiers),
	testMenuKey: (labels: string[]): Promise<string | null> => ipcRenderer.invoke('test-menu-key', labels),
	testLog: (line: string) => ipcRenderer.send('test-log', line),
	quit: (code: number) => ipcRenderer.send('quit', code),
};

contextBridge.exposeInMainWorld('host', host);

export type Host = typeof host;
