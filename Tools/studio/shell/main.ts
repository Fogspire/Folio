import { app, BrowserWindow, dialog, ipcMain, Menu, MenuItemConstructorOptions, nativeTheme, net, protocol, shell } from 'electron';
import * as fs from 'node:fs';
import * as path from 'node:path';
import { pathToFileURL } from 'node:url';
import { createShortcuts } from './shortcuts';
import { BUILD_SCRIPTS, checkBuilds } from './builds';
import { checkForUpdates } from './updates';
import type { IPty } from '@lydell/node-pty';

/** Tools/studio, whether run from a checkout or from a packaged app's resources. */
const studioDir = path.resolve(__dirname, '..', '..');
const appDir = path.join(studioDir, 'out', 'app');
const engineDir = path.join(studioDir, 'engine', 'dist');
/** The Folio that ships with this Studio, used when a project carries no plugin of its own. */
const bundledFolio = app.isPackaged ? path.join(process.resourcesPath, 'folio') : path.resolve(studioDir, '..', '..');

const IGNORED_DIRECTORIES = new Set([
	'.git', '.folio-cache', '.idea', '.vs', '.vscode', 'node_modules',
	'Binaries', 'Intermediate', 'Saved', 'DerivedDataCache', 'build', 'out',
]);
/** Larger than any UI asset, and small enough that a stray video does not stall a project load. */
const MAX_MIRRORED_FILE_BYTES = 64 * 1024 * 1024;

interface Project {
	root: string;
	folioRoot: string;
	name: string;
}

let window: BrowserWindow | null = null;
let project: Project | null = null;
const watchers = new Map<string, fs.FSWatcher>();
const pendingChanges = new Set<string>();
let changeTimer: NodeJS.Timeout | null = null;
/** What the page last said about the settings the menu shows a mark against. */
let menuState: { updateCheck: boolean; mode: string; keys: Record<string, string | undefined> } = { updateCheck: true, mode: 'edit', keys: {} };

const testing = process.argv.includes('--test');

function argument(name: string): string | undefined {
	const index = process.argv.indexOf('--' + name);
	return index >= 0 ? process.argv[index + 1] : undefined;
}

function forward(value: string): string {
	return value.split(path.sep).join('/');
}

function isInside(directory: string, target: string): boolean {
	const relative = path.relative(directory, target);
	return relative === '' || (!relative.startsWith('..') && !path.isAbsolute(relative));
}

/** Folders the person picked in the system dialog this session, which the page may then write into. */
const pickedFolders = new Set<string>();

/** Every file operation the page asks for is confined to the open project, the Folio it uses, and folders the person picked. */
function assertAllowed(target: string): string {
	const resolved = path.resolve(target);
	if (project && (isInside(project.root, resolved) || isInside(project.folioRoot, resolved))) return resolved;
	if (isInside(bundledFolio, resolved)) return resolved;
	for (const folder of pickedFolders) if (isInside(folder, resolved)) return resolved;
	throw new Error('path is outside the open project: ' + target);
}

function hasFolioContent(directory: string): boolean {
	return fs.existsSync(path.join(directory, 'rcss', 'folio.rcss'));
}

function resolveProject(root: string): Project {
	const resolved = path.resolve(root);
	const ownPlugin = path.join(resolved, 'Plugins', 'Folio');
	let folioRoot = bundledFolio;
	if (hasFolioContent(resolved)) folioRoot = resolved;
	else if (hasFolioContent(ownPlugin)) folioRoot = ownPlugin;
	return { root: resolved, folioRoot, name: path.basename(resolved) };
}

interface TreeFile {
	path: string;
	size: number;
}

function listTree(directory: string): TreeFile[] {
	const out: TreeFile[] = [];
	const walk = (current: string, prefix: string) => {
		let entries: fs.Dirent[];
		try {
			entries = fs.readdirSync(current, { withFileTypes: true });
		} catch {
			return;
		}
		for (const entry of entries) {
			const relative = prefix ? prefix + '/' + entry.name : entry.name;
			if (entry.isDirectory()) {
				if (!IGNORED_DIRECTORIES.has(entry.name)) walk(path.join(current, entry.name), relative);
			} else if (entry.isFile()) {
				out.push({ path: relative, size: fs.statSync(path.join(current, entry.name)).size });
			}
		}
	};
	walk(directory, '');
	return out;
}

function flushChanges() {
	changeTimer = null;
	const changed = [...pendingChanges];
	pendingChanges.clear();
	if (changed.length && window && !window.isDestroyed()) window.webContents.send('files-changed', changed);
}

function watchDirectory(directory: string) {
	if (watchers.has(directory) || !fs.existsSync(directory)) return;
	try {
		const watcher = fs.watch(directory, { recursive: true }, (_event, filename) => {
			if (!filename) return;
			const parts = filename.toString().split(/[\\/]/);
			if (parts.some((part) => IGNORED_DIRECTORIES.has(part))) return;
			pendingChanges.add(forward(path.join(directory, filename.toString())));
			// Editors save by writing a temporary file and renaming it, which is a burst for one save.
			if (changeTimer) clearTimeout(changeTimer);
			changeTimer = setTimeout(flushChanges, 120);
		});
		watcher.on('error', () => undefined);
		watchers.set(directory, watcher);
	} catch {
		// A directory that cannot be watched still opens; it just does not hot reload.
	}
}

function closeWatchers() {
	for (const watcher of watchers.values()) watcher.close();
	watchers.clear();
}

const layoutsDir = () => path.join(app.getPath('userData'), 'layouts');

/** Saved layouts, by file, newest first. */
function savedLayouts(): string[] {
	try {
		return fs.readdirSync(layoutsDir())
			.filter((entry) => entry.endsWith('.json'))
			.map((entry) => path.join(layoutsDir(), entry))
			.sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs);
	} catch {
		return [];
	}
}

const terminals = new Map<number, IPty>();
/** Files written where the person chose in a save dialog, which may be outside the project. */
const savedFiles = new Set<string>();
let nextTerminal = 1;

/** The shell a terminal runs: FOLIO_STUDIO_SHELL, else Git Bash on Windows when it is installed, else the system's own. */
function terminalShell(preferred = '', preferredArguments = ''): { file: string; args: string[] } {
	const chosen = preferred.trim() || process.env.FOLIO_STUDIO_SHELL;
	if (chosen) return { file: chosen, args: preferredArguments.split(/\s+/).filter(Boolean) };
	if (process.platform !== 'win32') return { file: process.env.SHELL || '/bin/bash', args: ['-l'] };

	const bash = gitBash();
	if (bash) return { file: bash, args: ['--login', '-i'] };
	return { file: 'powershell.exe', args: ['-NoLogo'] };
}

/** Git for Windows' bash, which Folio's scripts are written for. Null when it is not installed. */
function gitBash(): string | null {
	const roots = [process.env.ProgramFiles, process.env['ProgramFiles(x86)'], process.env.LOCALAPPDATA && path.join(process.env.LOCALAPPDATA, 'Programs')];
	for (const base of roots) {
		const bash = base ? path.join(base, 'Git', 'bin', 'bash.exe') : '';
		if (bash && fs.existsSync(bash)) return bash;
	}
	return null;
}

/** Starts a process in a pseudo-terminal whose output and exit the page receives by id, or says why it could not. */
function spawnTerminal(file: string, args: string[], directory: string, cols: number, rows: number): number | string {
	try {
		const pty: typeof import('@lydell/node-pty') = require('@lydell/node-pty');
		const id = nextTerminal++;
		const terminal = pty.spawn(file, args, {
			name: 'xterm-256color',
			cols: Math.max(2, cols),
			rows: Math.max(2, rows),
			cwd: fs.existsSync(directory) ? directory : app.getPath('home'),
			env: process.env as Record<string, string>,
		});
		terminals.set(id, terminal);
		terminal.onData((data) => {
			if (window && !window.isDestroyed()) window.webContents.send('terminal-data', id, data);
		});
		terminal.onExit(() => {
			terminals.delete(id);
			if (window && !window.isDestroyed()) window.webContents.send('terminal-exit', id);
		});
		return id;
	} catch (error) {
		return 'The terminal could not start: ' + (error instanceof Error ? error.message : String(error));
	}
}

function closeTerminals() {
	for (const terminal of terminals.values()) {
		try {
			terminal.kill();
		} catch {
			// Already gone.
		}
	}
	terminals.clear();
}

const recentFile = () => path.join(app.getPath('userData'), 'recent.json');

function readRecent(): string[] {
	try {
		const parsed = JSON.parse(fs.readFileSync(recentFile(), 'utf8'));
		return Array.isArray(parsed) ? parsed.filter((entry) => typeof entry === 'string' && fs.existsSync(entry)) : [];
	} catch {
		return [];
	}
}

function rememberRecent(root: string) {
	if (testing) return;
	const recent = [root, ...readRecent().filter((entry) => entry !== root)].slice(0, 10);
	try {
		fs.mkdirSync(path.dirname(recentFile()), { recursive: true });
		fs.writeFileSync(recentFile(), JSON.stringify(recent, null, '\t'));
	} catch {
		// Recents are a convenience.
	}
}

/** The Unreal project this checkout is a plugin of, which is what running Studio from inside one means. */
function enclosingProject(): string | null {
	if (app.isPackaged) return null;
	const plugins = path.dirname(bundledFolio);
	const candidate = path.dirname(plugins);
	if (path.basename(plugins).toLowerCase() !== 'plugins') return null;
	try {
		return fs.readdirSync(candidate).some((entry) => entry.endsWith('.uproject')) ? candidate : null;
	} catch {
		return null;
	}
}

function send(command: string, argument?: string) {
	if (window && !window.isDestroyed()) window.webContents.send('menu', command, argument);
}

/**
 * The application menu. Every item is a command the page runs, and none registers its accelerator:
 * the page handles the keys itself, because what Ctrl+Z means depends on where the focus is.
 */
function buildMenu() {
	const item = (label: string, command: string): MenuItemConstructorOptions => ({
		label,
		accelerator: menuState.keys[command],
		registerAccelerator: false,
		click: (_item, _window, event) => {
			if (!(event.ctrlKey && event.shiftKey)) return send(command);
			// A Ctrl+Shift+click asks for the item's key instead of running it. Rebuilt, since the click
			// has already flipped any mark the item carries.
			send('assign-key', command);
			buildMenu();
		},
	});
	const separator: MenuItemConstructorOptions = { type: 'separator' };
	const recent = readRecent();
	const layouts = savedLayouts();

	const template: MenuItemConstructorOptions[] = [
		...(process.platform === 'darwin' ? [{ role: 'appMenu' } as MenuItemConstructorOptions] : []),
		{
			label: 'File',
			submenu: [
				item('Open Project…', 'open-project'),
				item('Go to File…', 'go-to-file'),
				{
					label: 'Open Recent',
					enabled: recent.length > 0,
					submenu: recent.map((root) => ({ label: root, click: () => send('open-recent', root) })),
				},
				separator,
				item('New Document from Recipe…', 'new-from-recipe'),
				separator,
				item('Save All', 'save-all'),
				item('Revert Unsaved Changes', 'revert-all'),
				separator,
				item('Create Shortcuts', 'create-shortcuts'),
				item('Preferences…', 'preferences'),
				item('Keyboard Shortcuts…', 'keybindings'),
				separator,
				process.platform === 'darwin' ? { role: 'close' } : { role: 'quit' },
			],
		},
		{
			label: 'Edit',
			submenu: [
				item('Undo', 'undo'),
				item('Redo', 'redo'),
				separator,
				item('Cut', 'cut'),
				item('Copy', 'copy'),
				item('Paste', 'paste'),
				item('Duplicate', 'duplicate'),
				item('Delete', 'delete'),
				separator,
				item('Wrap in a Div', 'wrap'),
				item('Edit Text', 'edit-text'),
				item('Show in Code', 'show-in-code'),
				item('Bake to PNG…', 'bake'),
				separator,
				item('Select Parent', 'select-parent'),
				item('Select First Child', 'select-first-child'),
				item('Select Previous Sibling', 'select-previous'),
				item('Select Next Sibling', 'select-next'),
				item('Deselect', 'deselect'),
			],
		},
		{
			label: 'View',
			submenu: [
				...['Canvas', 'Code', 'Files', 'Library', 'Outline', 'Properties', 'Data', 'Problems', 'Docs', 'Terminal']
					.map((label) => item(label, 'panel:' + (label === 'Properties' ? 'inspector' : label.toLowerCase()))),
				separator,
				item('Messages', 'show-messages'),
				separator,
				{
					label: 'Layout',
					submenu: [
						item('Save Layout As…', 'save-layout'),
						item('Load Layout…', 'load-layout'),
						...(layouts.length ? [separator] : []),
						...layouts.map((file): MenuItemConstructorOptions => ({ label: path.basename(file, '.json'), click: () => send('load-layout', file) })),
						separator,
						item('Reset Layout', 'reset-layout'),
					],
				},
				separator,
				item('Zoom In', 'zoom-in'),
				item('Zoom Out', 'zoom-out'),
				item('Zoom to Fit', 'zoom-fit'),
				item('Actual Size', 'zoom-100'),
				separator,
				item('Timeline', 'toggle-timeline'),
				separator,
				{ ...item('Edit Mode', 'mode-edit'), type: 'radio', checked: menuState.mode === 'edit' },
				{ ...item('Play Mode', 'mode-play'), type: 'radio', checked: menuState.mode === 'play' },
				separator,
				{ role: 'toggleDevTools' },
				{ role: 'togglefullscreen' },
			],
		},
		{
			label: 'Help',
			submenu: [
				item('Documentation', 'panel:docs'),
				item('Folio on GitHub', 'open-repo'),
				separator,
				item('Check for Updates', 'check-updates'),
					{ ...item('Check for Updates at Startup', 'toggle-update-check'), type: 'checkbox', checked: menuState.updateCheck },
				...(process.platform === 'darwin' ? [] : [separator, { role: 'about', label: 'About Folio Studio' } as MenuItemConstructorOptions]),
			],
		},
	];
	Menu.setApplicationMenu(Menu.buildFromTemplate(template));
}

interface ContextItem {
	id?: string;
	label?: string;
	accelerator?: string;
	enabled?: boolean;
	separator?: boolean;
	submenu?: ContextItem[];
}

function registerIpc() {
	ipcMain.handle('state', () => ({
		version: app.getVersion(),
		recent: readRecent(),
		initialProject: argument('project') ?? enclosingProject() ?? readRecent()[0] ?? null,
		initialDocument: argument('open') ?? null,
		platform: process.platform,
		shots: argument('shots') ?? null,
	}));

	ipcMain.handle('pick-project', async () => {
		const result = await dialog.showOpenDialog(window!, {
			title: 'Open project',
			message: 'Choose the folder that contains UI/',
			properties: ['openDirectory'],
		});
		return result.canceled ? null : result.filePaths[0];
	});

	ipcMain.handle('pick-folder', async (_event, title: string, start: string) => {
		const result = await dialog.showOpenDialog(window!, { title, defaultPath: path.normalize(start), properties: ['openDirectory', 'createDirectory'] });
		if (result.canceled) return null;
		pickedFolders.add(path.resolve(result.filePaths[0]));
		return forward(result.filePaths[0]);
	});

	ipcMain.handle('open-project', (_event, root: string) => {
		closeWatchers();
		project = resolveProject(root);
		rememberRecent(project.root);
		buildMenu();
		return { root: forward(project.root), folioRoot: forward(project.folioRoot), name: project.name };
	});

	ipcMain.handle('list-tree', (_event, directory: string) => listTree(assertAllowed(directory)));

	ipcMain.handle('read-tree', (_event, directory: string) => {
		const base = assertAllowed(directory);
		return listTree(base)
			.filter((file) => file.size <= MAX_MIRRORED_FILE_BYTES)
			.map((file) => ({ path: file.path, data: fs.readFileSync(path.join(base, file.path)) }));
	});

	ipcMain.handle('read-file', (_event, target: string) => {
		try {
			return fs.readFileSync(assertAllowed(target));
		} catch {
			return null;
		}
	});

	ipcMain.handle('write-file', (_event, target: string, data: string | Uint8Array) => {
		const resolved = assertAllowed(target);
		fs.mkdirSync(path.dirname(resolved), { recursive: true });
		fs.writeFileSync(resolved, data);
	});

	ipcMain.handle('exists', (_event, target: string) => fs.existsSync(assertAllowed(target)));
	ipcMain.handle('watch', (_event, directory: string) => watchDirectory(assertAllowed(directory)));
	ipcMain.handle('reveal', (_event, target: string) => shell.showItemInFolder(assertAllowed(target)));
	ipcMain.handle('check-updates', () => checkForUpdates(studioDir, app.getVersion()));

	// A layout is saved wherever the person says, and the ones kept in the default place are listed in the menu.
	ipcMain.handle('save-layout', async (_event, layout: string, target?: string) => {
		fs.mkdirSync(layoutsDir(), { recursive: true });
		let chosen = testing ? target : undefined;
		if (!chosen) {
			const result = await dialog.showSaveDialog(window!, {
				title: 'Save layout',
				defaultPath: path.join(layoutsDir(), 'layout.json'),
				filters: [{ name: 'Layout', extensions: ['json'] }],
			});
			if (result.canceled || !result.filePath) return null;
			chosen = result.filePath;
		}
		fs.writeFileSync(chosen, layout);
		buildMenu();
		return path.basename(chosen, '.json');
	});

	ipcMain.handle('load-layout', async (_event, target?: string) => {
		let chosen = target;
		if (!chosen) {
			const result = await dialog.showOpenDialog(window!, {
				title: 'Load layout',
				defaultPath: layoutsDir(),
				filters: [{ name: 'Layout', extensions: ['json'] }],
				properties: ['openFile'],
			});
			if (result.canceled) return null;
			chosen = result.filePaths[0];
		}
		try {
			return fs.readFileSync(chosen, 'utf8');
		} catch {
			return null;
		}
	});

	// Resolves with the terminal's id, or with the reason there is not one.
	// On the desktop and in the start menu, or wherever the platform keeps programs.
	ipcMain.handle('create-shortcuts', (_event, into?: string) => {
		try {
			return createShortcuts({
				program: process.execPath,
				argument: app.isPackaged ? null : studioDir,
				icon: path.join(bundledFolio, 'Resources', 'Icon256.png'),
				scratch: path.join(app.getPath('userData'), 'shortcut'),
				into: testing ? into : undefined,
			});
		} catch (error) {
			return 'Could not create shortcuts: ' + (error instanceof Error ? error.message : String(error));
		}
	});

	// Asks where, then writes there. Anywhere the person picks, since they picked it: a texture can
	// belong to another project or an art folder outside this one.
	ipcMain.handle('save-file', async (_event, title: string, defaultPath: string, extension: string, data: Uint8Array) => {
		fs.mkdirSync(path.dirname(defaultPath), { recursive: true });
		const result = await dialog.showSaveDialog(window!, {
			title,
			defaultPath,
			filters: [{ name: extension.toUpperCase(), extensions: [extension] }],
		});
		if (result.canceled || !result.filePath) return null;
		const target = path.extname(result.filePath) ? result.filePath : result.filePath + '.' + extension;
		try {
			fs.writeFileSync(target, data);
			savedFiles.add(path.resolve(target));
			return { path: forward(target) };
		} catch (error) {
			return { error: error instanceof Error ? error.message : String(error) };
		}
	});

	// Opens a file in whatever the system opens that kind with, or shows it in its folder. Only files in
	// the project or ones this session saved: a saved one can be outside the project, and the page
	// should open nothing else.
	ipcMain.handle('open-saved', async (_event, target: string, how: 'open' | 'reveal') => {
		const resolved = path.resolve(target);
		if (!savedFiles.has(resolved) && !(project && isInside(project.root, resolved))) return 'not a file Studio saved';
		if (how === 'reveal') {
			shell.showItemInFolder(resolved);
			return '';
		}
		return shell.openPath(resolved);
	});

	ipcMain.handle('default-shell', () => {
		const { file, args } = terminalShell();
		return [file, ...args].join(' ');
	});

	// To the system's trash, so a file deleted by mistake can be put back.
	ipcMain.handle('trash', async (_event, target: string) => {
		const resolved = assertAllowed(target);
		try {
			await shell.trashItem(resolved);
			return true;
		} catch {
			return false;
		}
	});

	ipcMain.handle('terminal-open', (_event, directory: string, cols: number, rows: number, preferred?: string, preferredArguments?: string) => {
		const { file, args } = terminalShell(preferred, preferredArguments);
		return spawnTerminal(file, args, directory, cols, rows);
	});
	// Only Folio's own build scripts, named rather than passed, so the page cannot run anything else this way.
	ipcMain.handle('terminal-build', (_event, which: keyof typeof BUILD_SCRIPTS, cols: number, rows: number) => {
		const script = project && BUILD_SCRIPTS[which];
		if (!project || !script) return 'Nothing to build.';
		const bash = process.platform === 'win32' ? gitBash() : '/bin/bash';
		if (!bash) return `Git Bash was not found. Run ${script} from a bash shell in ${project.folioRoot}.`;
		return spawnTerminal(bash, ['-c', `cd "${forward(project.folioRoot)}" && ./${script}`], project.folioRoot, cols, rows);
	});
	ipcMain.handle('check-builds', () => (project ? checkBuilds(project.folioRoot) : { libraries: null, preview: null }));
	ipcMain.on('terminal-input', (_event, id: number, data: string) => terminals.get(id)?.write(data));
	ipcMain.on('terminal-resize', (_event, id: number, cols: number, rows: number) => {
		try {
			terminals.get(id)?.resize(Math.max(2, cols), Math.max(2, rows));
		} catch {
			// The shell exited between the resize and its arrival.
		}
	});
	ipcMain.on('terminal-close', (_event, id: number) => {
		try {
			terminals.get(id)?.kill();
		} catch {
			// Already gone.
		}
		terminals.delete(id);
	});

	ipcMain.handle('menu-state', (_event, state: typeof menuState) => {
		menuState = state;
		buildMenu();
	});

	// Resolves with the id of the item chosen, or null when the menu was dismissed.
	ipcMain.handle('context-menu', (_event, items: ContextItem[]) => new Promise<string | null>((resolve) => {
		let settled = false;
		const settle = (id: string | null) => {
			if (!settled) resolve(id);
			settled = true;
		};
		const build = (entries: ContextItem[]): MenuItemConstructorOptions[] => entries.map((entry) => {
			if (entry.separator) return { type: 'separator' };
			if (entry.submenu) return { label: entry.label, enabled: entry.enabled !== false, submenu: build(entry.submenu) };
			return { label: entry.label, accelerator: entry.accelerator, registerAccelerator: false, enabled: entry.enabled !== false, click: () => settle(entry.id ?? null) };
		});
		const menu = Menu.buildFromTemplate(build(items));
		// Closing is reported before the click on some platforms, so a dismissal waits for one.
		menu.popup({ window: window!, callback: () => setTimeout(() => settle(null), 100) });
	}));

	// The clipboard and undo commands as the browser performs them, on whatever text has the caret.
	ipcMain.handle('native', (_event, action: string) => {
		const contents = window!.webContents;
		const actions: Record<string, () => void> = {
			undo: () => contents.undo(), redo: () => contents.redo(), cut: () => contents.cut(),
			copy: () => contents.copy(), paste: () => contents.paste(), delete: () => contents.delete(),
		};
		actions[action]?.();
	});

	ipcMain.handle('open-external', (_event, url: string) => {
		if (/^https?:\/\//.test(url)) return shell.openExternal(url);
	});

	// Test hooks: a scripted run captures the window and exits, so the app can be checked unattended.
	ipcMain.handle('capture', async (_event, target: string) => {
		const image = await window!.webContents.capturePage();
		fs.mkdirSync(path.dirname(target), { recursive: true });
		fs.writeFileSync(target, image.toPNG());
	});
	ipcMain.handle('test-script', () => {
		const script = argument('test');
		return script ? fs.readFileSync(script, 'utf8') : null;
	});
	// Clicks an item of the menu bar by its labels, as a person would.
	ipcMain.handle('test-menu', (_event, labels: string[], modifiers: Electron.KeyboardEvent = {}) => {
		let items = Menu.getApplicationMenu()?.items ?? [];
		let found: Electron.MenuItem | undefined;
		for (const label of labels) {
			found = items.find((entry) => entry.label === label);
			items = found?.submenu?.items ?? [];
		}
		found?.click(modifiers);
		return !!found;
	});
	ipcMain.handle('test-menu-checked', (_event, labels: string[]) => {
		let items = Menu.getApplicationMenu()?.items ?? [];
		let found: Electron.MenuItem | undefined;
		for (const label of labels) {
			found = items.find((entry) => entry.label === label);
			items = found?.submenu?.items ?? [];
		}
		return found ? found.checked : null;
	});
	ipcMain.handle('test-menu-key', (_event, labels: string[]) => {
		let items = Menu.getApplicationMenu()?.items ?? [];
		let found: Electron.MenuItem | undefined;
		for (const label of labels) {
			found = items.find((entry) => entry.label === label);
			items = found?.submenu?.items ?? [];
		}
		return found?.accelerator ?? null;
	});
	ipcMain.on('test-log', (_event, line: string) => console.log(line));
	ipcMain.on('quit', (_event, code: number) => {
		closeTerminals();
		app.exit(code);
	});
}

function resolveAsset(url: URL): string | null {
	const pathname = decodeURIComponent(url.pathname);
	const serve = (base: string, relative: string) => {
		const target = path.resolve(base, '.' + path.posix.normalize('/' + relative));
		return isInside(base, target) ? target : null;
	};
	if (pathname.startsWith('/engine/')) return serve(engineDir, pathname.slice('/engine/'.length));
	if (pathname.startsWith('/folio/')) return serve(project?.folioRoot ?? bundledFolio, pathname.slice('/folio/'.length));
	return serve(appDir, pathname === '/' ? 'index.html' : pathname.slice(1));
}

protocol.registerSchemesAsPrivileged([
	{ scheme: 'studio', privileges: { standard: true, secure: true, supportFetchAPI: true, stream: true } },
]);

app.whenReady().then(() => {
	protocol.handle('studio', (request) => {
		const target = resolveAsset(new URL(request.url));
		if (!target || !fs.existsSync(target)) return new Response('not found', { status: 404 });
		return net.fetch(pathToFileURL(target).toString());
	});

	registerIpc();
	app.setAboutPanelOptions({
		applicationName: 'Folio Studio',
		applicationVersion: app.getVersion(),
		copyright: 'Copyright (c) 2026 Jared Taylor',
		website: 'https://github.com/Fogspire/Folio',
		iconPath: path.join(bundledFolio, 'Resources', 'Icon128.png'),
	});
	// Dark and neutral, so the menu bar does not sit over the chrome as a bright strip.
	nativeTheme.themeSource = 'dark';
	buildMenu();

	window = new BrowserWindow({
		width: Number(argument('width') ?? 1600),
		height: Number(argument('height') ?? 1000),
		show: !testing,
		// A scripted run still has to paint to be captured, so its window is shown without focus and
		// far off any display, where it interrupts nobody.
		x: testing ? -32000 : undefined,
		y: testing ? -32000 : undefined,
		backgroundColor: '#1c1c1c',
		title: 'Folio Studio',
		icon: path.join(bundledFolio, 'Resources', 'Icon128.png'),
		webPreferences: {
			preload: path.join(__dirname, 'preload.js'),
			contextIsolation: true,
			nodeIntegration: false,
			backgroundThrottling: !testing,
			additionalArguments: testing ? ['--studio-test'] : [],
		},
	});
	window.webContents.on('console-message', (event) => {
		if (testing) console.log('[page] ' + event.message);
	});
	window.webContents.setWindowOpenHandler(({ url }) => {
		if (/^https?:\/\//.test(url)) shell.openExternal(url);
		return { action: 'deny' };
	});
	if (testing) window.showInactive();
	window.loadURL('studio://app/index.html');
});

app.on('window-all-closed', () => {
	closeWatchers();
	closeTerminals();
	app.quit();
});
