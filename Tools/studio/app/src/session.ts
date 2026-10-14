import type { Dialog } from './Dialogs';
import type { Target } from './edit/target';
import { Diagnostic, EngineClient, Inspection, TreeNode } from './engine';
import { host, HostState, ProjectInfo, TreeFile, UpdateStatus } from './host';
import { COMMANDS, keysFor, toAccelerator } from './keys';
import { languageFor, monaco } from './monaco';
import { basename, dirname, extension, isUnder, join, relativeTo, stem } from './paths';
import { loadSettings, saveSettings, Settings } from './settings';
import { createStore } from './store';

export interface Reveal {
	path: string;
	line: number;
	column: number;
	/** Changes on every request, so revealing the same place twice still scrolls. */
	nonce: number;
}

export interface Timeline {
	/** Seconds since the document opened. */
	time: number;
	playing: boolean;
}

export interface State {
	hostState: HostState | null;
	project: ProjectInfo | null;
	projectFiles: string[];
	libraryFiles: string[];
	engineReady: boolean;
	/** The .rml on the canvas, as a host path. */
	document: string | null;
	documentOpen: boolean;
	openFiles: string[];
	activeFile: string | null;
	dirty: Record<string, true>;
	tree: TreeNode | null;
	selection: string | null;
	hover: string | null;
	inspection: Inspection | null;
	/** Where new properties on the selected element are written, once the person has said. */
	writeTarget: Target | null;
	diagnostics: Diagnostic[];
	width: number;
	height: number;
	/** Null derives the ratio from the height against 1080, as the game does. */
	dpRatio: number | null;
	zoom: number | 'fit';
	mode: 'edit' | 'play';
	themes: string[];
	culture: string;
	update: UpdateStatus | null;
	status: string;
	reveal: Reveal | null;
	/** Bumps whenever the engine's document was reloaded, for views that read it directly. */
	revision: number;
	/** The element whose text is being typed over on the canvas. */
	editing: string | null;
	/** Canvas pixels drawn per document pixel, which follows the zoom so a zoomed canvas is sharp. */
	renderScale: number;
	settings: Settings;
	/** The document's clock held still or played from its first frame, for looking at motion. */
	timeline: Timeline | null;
	/** The dialog open over the app, if one is. */
	dialog: Dialog | null;
}

export const store = createStore<State>({
	hostState: null,
	project: null,
	projectFiles: [],
	libraryFiles: [],
	engineReady: false,
	document: null,
	documentOpen: false,
	openFiles: [],
	activeFile: null,
	dirty: {},
	tree: null,
	selection: null,
	hover: null,
	inspection: null,
	writeTarget: null,
	diagnostics: [],
	width: 1920,
	height: 1080,
	dpRatio: null,
	zoom: 'fit',
	mode: 'edit',
	themes: [],
	culture: '',
	update: null,
	status: '',
	reveal: null,
	revision: 0,
	editing: null,
	renderScale: 1,
	settings: loadSettings(),
	timeline: null,
	dialog: null,
});

/** The scale the canvas is drawn at right now, which is what "fit" currently comes to. */
export const canvasScale = { value: 1 };
export const MIN_ZOOM = 0.05;
export const MAX_ZOOM = 8;

export const engine = new EngineClient();
const DEFAULT_WIDTH = 1920;
const DEFAULT_HEIGHT = 1080;

/** Properties the inspector resolves whether or not a rule set them. */
export const INSPECTED_PROPERTIES = [
	'display', 'position', 'left', 'top', 'right', 'bottom', 'z-index', 'width', 'height', 'min-width', 'min-height',
	'max-width', 'max-height', 'box-sizing', 'flex-direction', 'flex-wrap', 'justify-content', 'align-items', 'align-self',
	'flex-grow', 'flex-shrink', 'flex-basis', 'row-gap', 'column-gap',
	'margin-top', 'margin-right', 'margin-bottom', 'margin-left',
	'padding-top', 'padding-right', 'padding-bottom', 'padding-left',
	'font-family', 'font-size', 'font-weight', 'font-style', 'line-height', 'text-align', 'letter-spacing', 'color',
	'background-color', 'opacity',
	'border-top-width', 'border-right-width', 'border-bottom-width', 'border-left-width',
	'border-top-color', 'border-right-color', 'border-bottom-color', 'border-left-color',
	'border-top-left-radius', 'border-top-right-radius', 'border-bottom-right-radius', 'border-bottom-left-radius',
	'overflow-x', 'overflow-y', 'transform', 'filter', 'box-shadow', 'decorator', 'font-effect', 'transition',
];

const TEXT_EXTENSIONS = new Set(['.rml', '.rcss', '.json', '.terms', '.md', '.txt']);
/** Files a tab shows a picture of, or a specimen of, rather than their text. */
export const IMAGE_EXTENSIONS = new Set(['.png', '.jpg', '.svg']);
export const FONT_EXTENSIONS = new Set(['.ttf', '.otf']);
const LISTED_EXTENSIONS = new Set(['.rml', '.rcss', '.json', '.terms', '.png', '.jpg', '.tga', '.svg', '.ttf', '.otf']);
/** Folio directories the renderer reads from: stylesheets and recipes, fonts, examples and the plugin's mocks. */
const FOLIO_DIRECTORIES = ['rcss', 'fonts', 'examples', 'UI'];

interface Mount {
	host: string;
	wasm: string;
}

const mounts: Mount[] = [];
const models = new Map<string, monaco.editor.ITextModel>();
const savedVersions = new Map<string, number>();
const pendingPush = new Set<string>();
let pushTimer: number | null = null;
let relistTimer: number | null = null;
let engineBooted: () => void;
const engineBoot = new Promise<void>((resolve) => (engineBooted = resolve));
let revealNonce = 0;

function key(path: string): string {
	return path.toLowerCase();
}

function toWasm(hostPath: string): string[] {
	return mounts.filter((mount) => isUnder(mount.host, hostPath)).map((mount) => mount.wasm + hostPath.slice(mount.host.length));
}

/** The host directory the open document's root-relative paths resolve against. */
function documentRoot(): Mount | null {
	const { project, document } = store.get();
	if (!project || !document) return null;
	if (isUnder(project.root, document) && toWasm(document).some((path) => path.startsWith('/project/')))
		return { host: project.root, wasm: '/project' };
	return { host: project.folioRoot, wasm: '/folio' };
}

/**
 * Turns a path as the renderer reports it into a file on disk.
 *
 * The renderer names files the way documents do, root-relative, with /folio/ standing for the
 * plugin's own stylesheets. This is the same resolution its file interface performs.
 */
export function resolveSourcePath(reported: string): string | null {
	const { project } = store.get();
	const root = documentRoot();
	if (!project || !root || !reported) return null;

	const path = reported.replace(/\\/g, '/').replace(/^\/+/, '');
	if (path.startsWith('folio/examples/')) return join(project.folioRoot, 'examples', path.slice('folio/examples/'.length));
	if (path.startsWith('folio/')) return join(project.folioRoot, 'rcss', path.slice('folio/'.length));
	if (path.startsWith('project/')) return join(project.root, path.slice('project/'.length));
	return join(root.host, path);
}

async function mirror(hostDirectory: string, wasmDirectory: string): Promise<void> {
	if (mounts.some((mount) => key(mount.host) === key(hostDirectory) && mount.wasm === wasmDirectory)) return;
	if (!(await host.exists(hostDirectory))) return;

	await engineBoot;
	const files: { path: string; data: Uint8Array }[] = await host.readTree(hostDirectory);
	for (const file of files) engine.writeFile(wasmDirectory + '/' + file.path, file.data);
	mounts.push({ host: hostDirectory, wasm: wasmDirectory });
	host.watch(hostDirectory);
}

function setStatus(status: string) {
	store.set({ status });
}

export async function initialise() {
	const hostState: HostState = await host.state();
	store.set({ hostState });
	host.onFilesChanged(onFilesChanged);
	await bootEngine();
	pushMenuState();
	if (updateCheckEnabled()) host.checkUpdates().then((update: UpdateStatus) => store.set({ update }));
	if (hostState.initialProject) await openProject(hostState.initialProject, hostState.initialDocument);
}

/** Tells the menu bar what to mark and which key to show beside each command. */
function pushMenuState() {
	const { settings, mode } = store.get();
	const keys: Record<string, string | undefined> = {};
	for (const entry of COMMANDS) keys[entry.id] = toAccelerator(keysFor(entry.id, settings.keys)[0]);
	host.setMenuState({ updateCheck: settings.updateCheck, mode, keys });
}

export function updateSettings(changes: Partial<Settings>) {
	const settings = { ...store.get().settings, ...changes };
	saveSettings(settings);
	store.set({ settings });
	pushMenuState();
}

export function updateCheckEnabled(): boolean {
	return store.get().settings.updateCheck;
}

/** Asks the remote now, whatever the setting, and says what it found. */
export async function checkForUpdatesNow() {
	setStatus('Checking for updates…');
	const update: UpdateStatus = await host.checkUpdates();
	store.set({ update });
	setStatus(update.latest === null ? 'Could not read release tags from origin' : update.available ? '' : `Version ${update.current} is the latest`);
}

export function toggleUpdateCheck() {
	const enabled = !updateCheckEnabled();
	updateSettings({ updateCheck: enabled });
	setStatus(enabled ? 'Checking for updates at startup' : 'No longer checking for updates at startup');
}

async function bootEngine() {
	engine.acceptsKeys = () => store.get().mode === 'play' && document.activeElement === engine.canvas;
	await engine.boot();
	store.set({ engineReady: true });
	engineBooted();
}

async function listProjectFiles(project: ProjectInfo): Promise<string[]> {
	const out: string[] = [];
	const add = async (directory: string) => {
		if (!(await host.exists(directory))) return;
		const files: TreeFile[] = await host.listTree(directory);
		for (const file of files) {
			if (LISTED_EXTENSIONS.has(extension(file.path))) out.push(join(directory, file.path));
		}
	};
	await add(join(project.root, 'UI'));
	// A project that is the plugin itself keeps its documents beside its stylesheets.
	if (key(project.root) === key(project.folioRoot)) {
		await add(join(project.root, 'rcss'));
		await add(join(project.root, 'examples'));
	}
	return out.sort();
}

async function listLibraryFiles(project: ProjectInfo): Promise<string[]> {
	const out: string[] = [];
	for (const directory of ['rcss/recipes', 'examples', 'rcss/lib']) {
		const base = join(project.folioRoot, directory);
		if (!(await host.exists(base))) continue;
		const files: TreeFile[] = await host.listTree(base);
		for (const file of files) {
			if (!file.path.includes('/') && ['.rml', '.rcss'].includes(extension(file.path))) out.push(join(base, file.path));
		}
	}
	return out.sort();
}

export async function pickProject() {
	const root: string | null = await host.pickProject();
	if (root) await openProject(root);
}

export async function openProject(root: string, initialDocument: string | null = null) {
	setStatus('Opening project…');
	const project: ProjectInfo = await host.openProject(root);

	for (const model of models.values()) model.dispose();
	models.clear();
	savedVersions.clear();
	mounts.length = 0;
	await engineBoot;
	engine.close();

	store.set({
		project, projectFiles: [], libraryFiles: [], document: null, documentOpen: false, openFiles: [], activeFile: null,
		dirty: {}, tree: null, selection: null, hover: null, inspection: null, diagnostics: [],
	});

	for (const directory of FOLIO_DIRECTORIES) await mirror(join(project.folioRoot, directory), '/folio/' + directory);
	await mirror(join(project.root, 'UI'), '/project/UI');

	const [projectFiles, libraryFiles] = await Promise.all([listProjectFiles(project), listLibraryFiles(project)]);
	store.set({ projectFiles, libraryFiles });
	setStatus('');

	const first = initialDocument
		? (initialDocument.includes(':') || initialDocument.startsWith('/') ? initialDocument.replace(/\\/g, '/') : join(project.root, initialDocument))
		: projectFiles.find((file) => extension(file) === '.rml');
	if (first) await openDocument(first);
}

let opening = 0;

/** Puts a document on the canvas and in the code editor. */
export async function openDocument(path: string) {
	const { project } = store.get();
	if (!project) return;
	const token = ++opening;

	// Documents can live outside UI/, and whatever sits beside one is what its relative paths reach.
	// Not for what Folio ships: that is mounted already, and its parent is every plugin the project has.
	const shipped = key(project.folioRoot) !== key(project.root) && isUnder(project.folioRoot, path);
	if (isUnder(project.root, path) && !shipped) {
		const top = relativeTo(project.root, path).split('/')[0];
		await mirror(join(project.root, top), '/project/' + top);
	}
	if (token !== opening) return;

	store.set({ document: path, selection: null, hover: null, inspection: null });
	await openFile(path);
	if (token !== opening) return;

	// A document that says what size it was designed for is shown at that size, as the engine's picker
	// fills it in for whoever assigns the document.
	// One that does not is shown as it last was, or at the default, never at the last document's size.
	const declared = /<meta\s+name="folio-size"\s+content="(\d+)\s*x\s*(\d+)"/i.exec(peekModel(path)?.getValue() ?? '');
	if (declared) store.set({ width: Number(declared[1]), height: Number(declared[2]), dpRatio: null });
	else store.set(documentSizes.get(key(path)) ?? { width: DEFAULT_WIDTH, height: DEFAULT_HEIGHT, dpRatio: null });
	reopen();
}

/** The time the renderer is told it is: the wall clock, or the timeline's moment when that is open. */
function engineTime(): number {
	const { timeline } = store.get();
	return timeline ? timelineBase + timeline.time : performance.now() / 1000;
}

/** What the timeline's zero is on the renderer's clock: the moment the document was last opened. */
let timelineBase = 0;
/** How finely the timeline steps through time to reach a moment. Animations sample at this rate. */
const TIMELINE_STEP = 1 / 60;

/** Rebuilds the renderer for the current document, which is what a change of size policy or theme needs. */
export function reopen() {
	const state = store.get();
	// A document opened again starts its animations again, so the timeline starts again with it.
	if (state.timeline) {
		timelineBase = performance.now() / 1000;
		store.set({ timeline: { ...state.timeline, time: 0 } });
	}
	const root = documentRoot();
	if (!state.document || !root || !engine.ready) return;

	const wasmDocument = toWasm(state.document).find((path) => path.startsWith(root.wasm + '/'));
	if (!wasmDocument) return;

	const opened = engine.open({
		root: root.wasm,
		document: wasmDocument,
		width: state.width,
		height: state.height,
		dpRatio: state.dpRatio ?? undefined,
		themes: state.themes,
		culture: state.culture || undefined,
		// Loaded a moment before its first frame: an animation updated with no time passed shows its last keyframe.
		time: engineTime() - 0.001,
	});
	store.set({ documentOpen: opened });
	refresh();
}

/** Re-reads everything the page shows about the live document. */
export function refresh() {
	const state = store.get();
	const diagnostics = engine.diagnostics();
	if (!engine.isOpen) {
		store.set({ tree: null, inspection: null, diagnostics, revision: state.revision + 1 });
		return;
	}

	// One frame first: layout is what the boxes are read from, and a reload leaves it stale until drawn.
	engine.frame(engineTime());
	const tree = engine.tree();
	const inspection = state.selection !== null ? engine.inspect(state.selection, INSPECTED_PROPERTIES) : null;
	store.set({
		tree,
		diagnostics,
		inspection,
		selection: inspection ? state.selection : null,
		revision: state.revision + 1,
	});
}

export function select(id: string | null) {
	const inspection = id !== null && engine.isOpen ? engine.inspect(id, INSPECTED_PROPERTIES) : null;
	const selection = inspection ? id : null;
	if (selection !== store.get().selection) setEditing(null);
	// A chosen target belongs to the element it was chosen for.
	const writeTarget = selection === store.get().selection ? store.get().writeTarget : null;
	store.set({ selection, inspection, writeTarget });
}

export function setWriteTarget(writeTarget: Target | null) {
	store.set({ writeTarget });
}

export function setHover(id: string | null) {
	if (store.get().hover !== id) store.set({ hover: id });
}

/** The size each document was last shown at, for one that does not declare its own. */
const documentSizes = new Map<string, { width: number; height: number; dpRatio: number | null }>();

export function setViewport(width: number, height: number, dpRatio: number | null) {
	store.set({ width, height, dpRatio });
	const { document } = store.get();
	if (document) documentSizes.set(key(document), { width, height, dpRatio });
	if (!engine.isOpen) return;
	engine.resize(width, height, dpRatio);
	refresh();
}

export function setZoom(zoom: number | 'fit') {
	store.set({ zoom: zoom === 'fit' ? zoom : Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, zoom)) });
}

/** The most canvas pixels the renderer is asked for, since every effect layer is a target that size. */
const MAX_RENDER_PIXELS = 16_000_000;

/** Draws the document with enough pixels for the zoom it is shown at, and never fewer than its own. */
export function setRenderScale(wanted: number) {
	const { width, height, renderScale } = store.get();
	const limit = Math.sqrt(MAX_RENDER_PIXELS / (width * height));
	const next = Math.max(1, Math.round(Math.min(wanted, limit) * 20) / 20);
	if (next === renderScale) return;
	store.set({ renderScale: next });
	if (engine.ready && engine.setRenderScale(next)) refresh();
}

export function toggleTimeline() {
	if (store.get().timeline) {
		store.set({ timeline: null });
		return;
	}
	store.set({ timeline: { time: 0, playing: true } });
	reopen();
}

export function setTimelinePlaying(playing: boolean) {
	const { timeline, settings } = store.get();
	if (!timeline) return;
	// Play at the end means play again from the start.
	if (playing && timeline.time >= settings.timelineDuration) {
		seekTimeline(0);
		store.set({ timeline: { time: 0, playing: true } });
		return;
	}
	store.set({ timeline: { ...timeline, playing } });
}

/**
 * Shows the document as it is a number of seconds after opening. Animations only run forwards, so
 * going back opens the document again and steps from its start.
 */
export function seekTimeline(seconds: number) {
	const { timeline, settings } = store.get();
	if (!timeline || !engine.isOpen) return;
	const target = Math.max(0, Math.min(settings.timelineDuration, seconds));
	let from = timeline.time;
	if (target < from) {
		reopen();
		from = 0;
	}
	for (let time = from + TIMELINE_STEP; time < target; time += TIMELINE_STEP) engine.step(timelineBase + time);
	store.set({ timeline: { ...store.get().timeline!, time: target } });
	refresh();
}

/** Moves a playing timeline on by the time since the last frame, and draws it. */
export function advanceTimeline(seconds: number) {
	const { timeline, settings } = store.get();
	if (!timeline?.playing || !engine.isOpen) return;
	const time = Math.min(settings.timelineDuration, timeline.time + seconds);
	const finished = time >= settings.timelineDuration;
	store.set({ timeline: { time, playing: !finished } });
	if (finished) refresh();
	else engine.frame(timelineBase + time);
}

/** Why a bake was refused, in terms of the element the person picked rather than the name it was baked under. */
function bakeRefusal(code: string, message: string, id: string | null): string {
	if (code === 'E-BAKE-NEEDSENGINE') {
		const { project, document } = store.get();
		const where = project && document && isUnder(project.root, document) ? relativeTo(project.root, document) : basename(document ?? '');
		return 'It draws a layer effect (box-shadow, filter, backdrop-filter or mask-image), which Studio and the engine draw differently, so it has to be baked in the Unreal editor: '
			+ (id ? `folio.Bake ${where} #${id} UI/art/${id}.png` : 'Give the element an id, then: folio.Bake ' + where + ' #<id> UI/art/<id>.png');
	}
	return message ? message.replace(/#folio-studio-bake/g, 'The element') : 'The renderer did not say why. Problems may have more.';
}

let answerPending: ((choice: string | null) => void) | null = null;

/**
 * Asks something in the app's own dialog and resolves with the button pressed, or null when it was
 * dismissed. A scripted run has nobody to ask, so it takes the default.
 */
export function ask(title: string, detail: string, buttons: string[], preferred = buttons[buttons.length - 1]): Promise<string | null> {
	if (host.testing) return Promise.resolve(preferred);
	answerDialog(null);
	return new Promise((resolve) => {
		answerPending = resolve;
		store.set({ dialog: { kind: 'notice', title, detail, buttons, default: preferred } });
	});
}

/** Closes whatever dialog is open, answering it if it asked something. */
export function answerDialog(choice: string | null) {
	const pending = answerPending;
	answerPending = null;
	store.set({ dialog: null });
	pending?.(choice);
}

function bakeName(id: string): string {
	return findNode(store.get().tree, id)?.attributes.id || (id === '' ? stem(store.get().document ?? 'baked') : 'baked');
}

/** An element as a person would point at it: its id, or its tag and first class. */
function describeElement(id: string): string {
	const node = findNode(store.get().tree, id);
	if (!node || id === '') return basename(store.get().document ?? 'the document');
	if (node.attributes.id) return '#' + node.attributes.id;
	const firstClass = (node.attributes.class ?? '').split(/\s+/).filter(Boolean)[0];
	return node.tag + (firstClass ? '.' + firstClass : '');
}

/** Asks how to bake an element, then bakes it. */
export function askBake(id: string) {
	if (engine.isOpen) store.set({ dialog: { kind: 'bake', id, name: describeElement(id) } });
}

/**
 * Brings a bake to its size and puts it over its background.
 *
 * Halves the image until it reaches the size asked for: each halving averages four pixels into one,
 * so every sample drawn counts, which one large step down does not. The background goes under last,
 * so the averaging is of the element's own edge and not of the edge against a colour.
 */
async function finishBake(png: Uint8Array, factor: number, background: string | null): Promise<Uint8Array> {
	if (factor <= 1 && !background) return png;
	const image = await createImageBitmap(new Blob([png.slice()], { type: 'image/png' }), { colorSpaceConversion: 'none', premultiplyAlpha: 'premultiply' });
	const width = Math.max(1, Math.round(image.width / factor));
	const height = Math.max(1, Math.round(image.height / factor));
	let source: CanvasImageSource = image;
	let current = { width: image.width, height: image.height };
	while (current.width > width || current.height > height) {
		const next = { width: Math.max(width, Math.round(current.width / 2)), height: Math.max(height, Math.round(current.height / 2)) };
		const canvas = new OffscreenCanvas(next.width, next.height);
		const context = canvas.getContext('2d')!;
		context.imageSmoothingEnabled = true;
		context.imageSmoothingQuality = 'high';
		context.drawImage(source, 0, 0, next.width, next.height);
		source = canvas;
		current = next;
	}

	const result = new OffscreenCanvas(width, height);
	const context = result.getContext('2d')!;
	if (background) {
		context.fillStyle = background;
		context.fillRect(0, 0, width, height);
	}
	context.drawImage(source, 0, 0);
	const blob = await result.convertToBlob({ type: 'image/png' });
	return new Uint8Array(await blob.arrayBuffer());
}

/** Renders an element to a PNG the game can draw instead, which is how a costly effect becomes a texture. */
export async function bakeElement(id: string, chosen?: string, samples = 1, background: string | null = null) {
	const { project, tree } = store.get();
	if (!project || !engine.isOpen) return;
	const node = findNode(tree, id);
	const name = bakeName(id);

	// Baked before asking where, so an element that cannot be baked says why without a wasted dialog.
	const drawn = engine.bake(id, 0, samples);
	refresh();
	if (!drawn) {
		const reason = engine.diagnostics().filter((entry) => entry.code.startsWith('E-BAKE')).pop();
		await ask('Nothing was baked', bakeRefusal(reason?.code ?? '', reason?.message ?? '', node?.attributes.id ?? null), ['OK']);
		return;
	}
	const data = await finishBake(drawn, samples, background);

	let target = chosen ?? null;
	if (target) {
		await host.writeFile(target, data);
	} else {
		const saved = await host.saveFile('Bake to PNG', join(project.root, 'UI', 'art', name + '.png'), 'png', data);
		if (!saved) return;
		if (saved.error || !saved.path) {
			await ask('The PNG could not be written', saved.error ?? '', ['OK']);
			return;
		}
		target = saved.path;
	}
	const relative = isUnder(project.root, target) ? relativeTo(project.root, target) : target;
	const use = `Draw it with decorator: image(/${relative} fill), and drop what it replaces.`;
	setStatus(`Baked to ${relative}. ${use}`);

	const choice = await ask('Baked ' + basename(target), relative + '. ' + use, ['Open Image', 'Show in Folder', 'OK'], 'OK');
	if (choice === 'Open Image' || choice === 'Show in Folder') {
		const problem = await host.openSaved(target, choice === 'Open Image' ? 'open' : 'reveal');
		if (problem) setStatus('Could not open ' + basename(target) + ': ' + problem);
	}
}

export function setEditing(editing: string | null) {
	if (store.get().editing !== editing) store.set({ editing });
}

/** Whether a document is one Folio ships, shown as a preview rather than the project's own. */
export function isLibraryDocument(path: string | null): boolean {
	const { project, libraryFiles } = store.get();
	return !!project && !!path && libraryFiles.includes(path) && !isUnder(project.root + '/UI', path);
}

export function setMode(mode: 'edit' | 'play') {
	const played = store.get().mode === 'play' && mode === 'edit';
	store.set({ mode, hover: null });
	// Playing moves things the source knows nothing about, so editing starts again from the source.
	if (played) reopen();
	pushMenuState();
}

export function setThemes(themes: string[]) {
	store.set({ themes });
	reopen();
}

export function setCulture(culture: string) {
	store.set({ culture });
	reopen();
}

export function findNode(tree: TreeNode | null, id: string | null): TreeNode | null {
	if (!tree || id === null) return null;
	let node: TreeNode | undefined = tree;
	if (id === '') return node;
	for (const segment of id.split('/')) {
		node = node.children[Number(segment)];
		if (!node) return null;
	}
	return node;
}

// Buffers. A file open in the editor is a Monaco model, and the model, not the disk, is what the
// renderer draws: typing previews live, and saving is what the game's hot reload sees.

function recomputeDirty(path: string, model: monaco.editor.ITextModel) {
	const isDirty = model.getAlternativeVersionId() !== savedVersions.get(key(path));
	const { dirty } = store.get();
	if (isDirty === !!dirty[path]) return;
	const next = { ...dirty };
	if (isDirty) next[path] = true;
	else delete next[path];
	store.set({ dirty: next });
}

function schedulePush(path: string) {
	pendingPush.add(path);
	if (pushTimer !== null) window.clearTimeout(pushTimer);
	pushTimer = window.setTimeout(flushPush, 120);
}

/** Sends edited buffers to the renderer and reloads as little as they require. */
export function flushPush() {
	if (pushTimer !== null) window.clearTimeout(pushTimer);
	pushTimer = null;
	if (!pendingPush.size || !engine.ready) return;

	const changed: string[] = [];
	for (const path of pendingPush) {
		const model = models.get(key(path));
		if (!model) continue;
		for (const wasmPath of toWasm(path)) {
			engine.writeFile(wasmPath, model.getValue());
			changed.push(wasmPath);
		}
	}
	pendingPush.clear();

	if (!changed.length) return;
	if (engine.isOpen && changed.some((path) => /\/mock\/[^/]+\.json$/i.test(path))) {
		// Mock data is read when a document opens, and only the first model's file is watched for after.
		reopen();
	} else if (engine.isOpen) {
		engine.filesChanged(changed);
		refresh();
	} else {
		// A document that failed to load has nothing to reload into, so an edit is a fresh attempt.
		reopen();
	}
}

export async function getModel(path: string): Promise<monaco.editor.ITextModel | null> {
	const existing = models.get(key(path));
	if (existing) return existing;

	const data: Uint8Array | null = await host.readFile(path);
	if (!data) return null;
	const raced = models.get(key(path));
	if (raced) return raced;

	const model = monaco.editor.createModel(new TextDecoder().decode(data), languageFor(path), monaco.Uri.file(path));
	models.set(key(path), model);
	savedVersions.set(key(path), model.getAlternativeVersionId());
	model.onDidChangeContent(() => {
		recomputeDirty(path, model);
		schedulePush(path);
	});
	return model;
}

/** The host directory the open document's root-relative paths, and so its UI/mock and UI/schema, sit under. */
export function documentRootPath(): string | null {
	return documentRoot()?.host ?? null;
}

/**
 * Replaces a text file's contents, creating it if need be.
 *
 * Through the buffer when there is one, so the change can be undone and is saved with everything else.
 * A file that does not exist yet is written straight away, since there is nothing to be unsaved against.
 */
export async function writeText(path: string, text: string) {
	let model = await getModel(path);
	if (!model) {
		await host.writeFile(path, text);
		model = await getModel(path);
		if (model) schedulePush(path);
		return;
	}
	if (model.getValue() === text) return;
	model.pushStackElement();
	model.pushEditOperations(null, [{ range: model.getFullModelRange(), text }], () => null);
	model.pushStackElement();
}

export function peekModel(path: string): monaco.editor.ITextModel | null {
	return models.get(key(path)) ?? null;
}

export async function openFile(path: string, line = 0, column = 1) {
	const kind = extension(path);
	if (TEXT_EXTENSIONS.has(kind)) {
		if (!(await getModel(path))) return;
	} else if (!IMAGE_EXTENSIONS.has(kind) && !FONT_EXTENSIONS.has(kind)) {
		setStatus('No preview for ' + kind + ' files');
		return;
	}

	const { openFiles } = store.get();
	store.set({
		openFiles: openFiles.includes(path) ? openFiles : [...openFiles, path],
		activeFile: path,
		reveal: line > 0 ? { path, line, column, nonce: ++revealNonce } : store.get().reveal,
	});
}

/** Brings an open file to the front. A document comes to the front of the canvas as well. */
export function activateFile(path: string) {
	if (extension(path) === '.rml' && path !== store.get().document) openDocument(path);
	else store.set({ activeFile: path });
}

/**
 * Closes tabs. Unsaved edits stay in their buffers, so Save All still writes them.
 *
 * Closing the document on the canvas puts the nearest document still open there instead.
 */
export function closeFiles(paths: string[]) {
	const { openFiles, activeFile, document } = store.get();
	const closing = new Set(paths);
	const next = openFiles.filter((file) => !closing.has(file));
	if (next.length === openFiles.length) return;

	/** The tab that takes over from a closed one: the nearest survivor before it, or else after it. */
	const neighbour = (path: string, wanted: (file: string) => boolean) => {
		const index = openFiles.indexOf(path);
		return [...openFiles.slice(0, index).reverse(), ...openFiles.slice(index + 1)].find((file) => !closing.has(file) && wanted(file)) ?? null;
	};

	store.set({ openFiles: next, activeFile: activeFile && closing.has(activeFile) ? neighbour(activeFile, () => true) : activeFile });
	if (!document || !closing.has(document)) return;

	const successor = neighbour(document, (file) => extension(file) === '.rml');
	if (successor) {
		openDocument(successor);
		return;
	}
	++opening;
	engine.close();
	store.set({ document: null, documentOpen: false, tree: null, selection: null, hover: null, inspection: null, diagnostics: [] });
}

/** Moves a file to the trash, and closes whatever was showing it. */
export async function deleteFile(path: string): Promise<boolean> {
	const { project } = store.get();
	if (!project) return false;
	if (await ask('Delete ' + basename(path) + '?', 'It is moved to the trash.', ['Cancel', 'Delete'], 'Delete') !== 'Delete') return false;
	if (!(await host.trash(path))) return false;

	closeFiles([path]);
	models.get(key(path))?.dispose();
	models.delete(key(path));
	savedVersions.delete(key(path));
	pendingPush.delete(path);
	const dirty = { ...store.get().dirty };
	delete dirty[path];
	for (const wasmPath of toWasm(path)) engine.removeFile(wasmPath);
	store.set({ dirty, projectFiles: await listProjectFiles(project) });
	setStatus('Moved ' + basename(path) + ' to the trash');
	return true;
}

export async function save(path: string) {
	const model = models.get(key(path));
	if (!model) return;
	await host.writeFile(path, model.getValue());
	savedVersions.set(key(path), model.getAlternativeVersionId());
	recomputeDirty(path, model);
}

/**
 * Puts files back as they are on disk, after asking. Done as an edit, so Undo brings the changes back.
 */
export async function revertFiles(paths: string[]) {
	const unsaved = paths.filter((path) => store.get().dirty[path]);
	if (!unsaved.length) return;
	const names = unsaved.length === 1 ? basename(unsaved[0]) : unsaved.length + ' files';
	if (await ask('Revert ' + names + '?', 'Unsaved changes are replaced with what is on disk. Undo brings them back.', ['Cancel', 'Revert'], 'Revert') !== 'Revert') return;

	for (const path of unsaved) {
		const model = models.get(key(path));
		const data: Uint8Array | null = await host.readFile(path);
		if (!model || !data) continue;
		model.pushStackElement();
		model.pushEditOperations(null, [{ range: model.getFullModelRange(), text: new TextDecoder().decode(data) }], () => null);
		model.pushStackElement();
		savedVersions.set(key(path), model.getAlternativeVersionId());
		recomputeDirty(path, model);
	}
	setStatus('Reverted ' + names);
}

export function revertAll() {
	return revertFiles(Object.keys(store.get().dirty));
}

export async function saveAll() {
	for (const path of Object.keys(store.get().dirty)) await save(path);
}

/** Opens a file the renderer named, at a line, such as the rule a property came from. */
export function revealSource(reported: string, line: number) {
	const path = resolveSourcePath(reported);
	if (path) openFile(path, Math.max(1, line));
}

function scheduleRelist() {
	if (relistTimer !== null) window.clearTimeout(relistTimer);
	relistTimer = window.setTimeout(async () => {
		relistTimer = null;
		const { project } = store.get();
		if (project) store.set({ projectFiles: await listProjectFiles(project) });
	}, 400);
}

async function onFilesChanged(paths: string[]) {
	const changedWasm: string[] = [];
	let relist = false;

	for (const path of paths) {
		const wasmPaths = toWasm(path);
		if (!wasmPaths.length) continue;

		const data: Uint8Array | null = await host.readFile(path);
		const model = models.get(key(path));

		if (!data) {
			// Deleted, or a directory. Either way the listing is what changed.
			for (const wasmPath of wasmPaths) engine.removeFile(wasmPath);
			relist = true;
			continue;
		}

		if (!store.get().projectFiles.some((file) => key(file) === key(path))) relist = true;

		if (model) {
			const text = new TextDecoder().decode(data);
			if (text === model.getValue()) continue;
			if (store.get().dirty[path]) {
				setStatus(basename(path) + ' changed on disk; keeping your unsaved edits');
				continue;
			}
			// The model's own listener pushes this to the renderer.
			model.setValue(text);
			savedVersions.set(key(path), model.getAlternativeVersionId());
			recomputeDirty(path, model);
			continue;
		}

		for (const wasmPath of wasmPaths) {
			engine.writeFile(wasmPath, data);
			changedWasm.push(wasmPath);
		}
	}

	if (relist) scheduleRelist();
	if (changedWasm.length && engine.isOpen) {
		engine.filesChanged(changedWasm);
		refresh();
	}
}

const BLANK_DOCUMENT = `<rml>
<head>
	<title>TITLE</title>
	<link type="text/template" href="/folio/lib/lib.rml"/>
	<style>
		body { width: 100%; height: 100%; nav: auto; }
	</style>
</head>

<body>
</body>
</rml>
`;

/** Creates an empty document or stylesheet in a directory of the project, and opens it. */
export async function createFile(directory: string, name: string, kind: '.rml' | '.rcss'): Promise<string | null> {
	const { project } = store.get();
	const bare = name.trim().replace(/\.(rml|rcss)$/i, '');
	if (!project || !bare) return null;

	const target = join(directory, bare + kind);
	if (await host.exists(target)) {
		setStatus(basename(target) + ' already exists');
		return null;
	}
	await host.writeFile(target, kind === '.rml' ? BLANK_DOCUMENT.replace('TITLE', basename(bare)) : '');

	// The watcher would deliver this too, but not before the file is opened below.
	await onFilesChanged([target]);
	store.set({ projectFiles: await listProjectFiles(project) });
	if (kind === '.rml') await openDocument(target);
	else await openFile(target);
	return target;
}

/**
 * Copies a library document into the project, with any mock data it previews against.
 * It goes to UI/screens unless a directory is given.
 */
export async function copyLibraryDocument(source: string, name: string, directory: string | null = null): Promise<string | null> {
	const { project } = store.get();
	if (!project) return null;

	const target = join(directory ?? join(project.root, 'UI', 'screens'), name.trim().replace(/\.rml$/i, '') + '.rml');
	if (await host.exists(target)) {
		setStatus(basename(target) + ' already exists');
		return null;
	}

	const data: Uint8Array | null = await host.readFile(source);
	if (!data) return null;
	const text = new TextDecoder().decode(data);
	await host.writeFile(target, text);

	for (const match of text.matchAll(/data-model\s*=\s*"([^"]+)"/g)) {
		const mock = join(project.folioRoot, 'UI', 'mock', match[1] + '.json');
		const mockTarget = join(project.root, 'UI', 'mock', match[1] + '.json');
		const mockData: Uint8Array | null = await host.readFile(mock);
		if (mockData && !(await host.exists(mockTarget))) await host.writeFile(mockTarget, mockData);
	}

	// The watcher would deliver these too, but not before the document is opened below.
	await onFilesChanged([target, ...[...text.matchAll(/data-model\s*=\s*"([^"]+)"/g)].map((match) => join(project.root, 'UI', 'mock', match[1] + '.json'))]);
	store.set({ projectFiles: await listProjectFiles(project) });
	await openDocument(target);
	return target;
}

export { dirname };
