import { showPanel } from '../dock';
import { host } from '../host';
import { basename, dirname, extension, isUnder, join, relativeTo, stem } from '../paths';
import { ask, store } from '../session';
import { createStore } from '../store';
import {
	PACK_SUFFIX, PackFile, packAtlas, packProblems, parsePackFile, relativePath, resolvePath, serialisePackFile, spriteName, spritesheet,
} from './pack';
import { decodePng, encodePng, Pixels } from './png';

export interface Frame {
	/** The file name without `.png`. */
	file: string;
	name: string;
	included: boolean;
	pixels: Pixels | null;
	/** Why the frame cannot be packed, such as its file having gone. */
	error: string | null;
	/** An object URL of the file, for the thumbnail. */
	url: string | null;
}

export interface SpriteState {
	folder: string | null;
	frames: Frame[];
	sheet: string;
	src: string;
	scale: number;
	columns: number;
	/** Absolute paths of what Pack writes. */
	atlas: string;
	stylesheet: string;
	/** The pack file this was opened from or last written to. */
	packFile: string | null;
	loading: boolean;
}

export const sprites = createStore<SpriteState>({
	folder: null,
	frames: [],
	sheet: '',
	src: '',
	scale: 8,
	columns: 8,
	atlas: '',
	stylesheet: '',
	packFile: null,
	loading: false,
});

const collator = new Intl.Collator(undefined, { numeric: true, sensitivity: 'base' });

function same(a: string, b: string): boolean {
	return a.toLowerCase() === b.toLowerCase();
}

/** The atlas path as a document names it: root-relative inside the project. */
export function srcFor(atlas: string): string {
	const { project } = store.get();
	return project && isUnder(project.root, atlas) ? '/' + relativeTo(project.root, atlas) : basename(atlas);
}

export function packFileFor(stylesheet: string): string {
	return join(dirname(stylesheet), stem(stylesheet) + PACK_SUFFIX);
}

export function isPackFile(path: string): boolean {
	return path.toLowerCase().endsWith(PACK_SUFFIX);
}

/** The PNGs directly in a folder, in natural order. */
export async function framesIn(folder: string): Promise<string[]> {
	const files: { path: string }[] = await host.listTree(folder);
	return files.map((file) => file.path).filter((path) => !path.includes('/') && extension(path) === '.png').map(stem).sort(collator.compare);
}

async function readFrame(folder: string, file: string): Promise<Pick<Frame, 'pixels' | 'error' | 'url'>> {
	const bytes: Uint8Array | null = await host.readFile(join(folder, file + '.png'));
	if (!bytes) return { pixels: null, error: 'missing', url: null };
	try {
		const pixels = await decodePng(new Uint8Array(bytes));
		return { pixels, error: null, url: URL.createObjectURL(new Blob([bytes as BlobPart], { type: 'image/png' })) };
	} catch (error) {
		return { pixels: null, error: error instanceof Error ? error.message : String(error), url: null };
	}
}

function releaseFrames() {
	for (const frame of sprites.get().frames) if (frame.url) URL.revokeObjectURL(frame.url);
}

function setStatus(status: string) {
	store.set({ status });
}

/**
 * Reads a folder's frames. With a pack, its sprites come first in its order and under its names, and
 * any other PNG in the folder follows, left out.
 */
async function loadFrames(folder: string, pack: PackFile | null): Promise<Frame[] | null> {
	let files: string[];
	try {
		files = await framesIn(folder);
	} catch {
		const choice = await ask('Studio cannot read ' + folder, 'It is outside the open project. Choose it again to let Studio read it this session.', ['Cancel', 'Choose Folder…']);
		if (choice !== 'Choose Folder…') return null;
		const picked = await host.pickFolder('Frames folder', folder);
		if (!picked) return null;
		return loadFrames(picked, pack);
	}

	const order: { file: string; name: string; included: boolean }[] = [];
	if (pack) {
		for (const sprite of pack.sprites) order.push({ ...sprite, included: true });
		for (const file of files) if (!pack.sprites.some((sprite) => same(sprite.file, file))) order.push({ file, name: spriteName(file), included: false });
	} else {
		for (const file of files) order.push({ file, name: spriteName(file), included: true });
	}
	return Promise.all(order.map(async (entry) => ({ ...entry, ...(await readFrame(folder, entry.file)) })));
}

/** Starts a new pack from the PNGs in a folder, with outputs named after the folder in UI/art/. */
export async function openFolder(folder: string) {
	const { project } = store.get();
	if (!project) return;
	showPanel('sprites');
	sprites.set({ loading: true });
	const frames = await loadFrames(folder, null);
	if (!frames) return void sprites.set({ loading: false });

	const sheet = spriteName(basename(folder)) || 'sprites';
	const atlas = join(project.root, 'UI', 'art', sheet + '.png');
	releaseFrames();
	sprites.set({
		folder, frames, sheet, src: srcFor(atlas), atlas, stylesheet: join(project.root, 'UI', 'art', sheet + '.rcss'), packFile: null, loading: false,
	});
	setStatus(frames.length ? `${frames.length} frames in ${basename(folder)}` : `No PNGs directly in ${basename(folder)}`);
}

export async function chooseFolder() {
	const { project } = store.get();
	if (!project) return;
	const picked = await host.pickFolder('Folder of frames to pack', sprites.get().folder ?? project.root);
	if (picked) await openFolder(picked);
}

export async function openPack(file: string) {
	const bytes: Uint8Array | null = await host.readFile(file);
	let pack: PackFile;
	try {
		if (!bytes) throw new Error('it could not be read');
		pack = parsePackFile(new TextDecoder().decode(bytes));
	} catch (error) {
		await ask('Not a sprite pack', basename(file) + ': ' + (error instanceof Error ? error.message : String(error)), ['OK']);
		return;
	}
	showPanel('sprites');
	sprites.set({ loading: true });
	const folder = dirname(file);
	const frames = await loadFrames(resolvePath(folder, pack.frames), pack);
	if (!frames) return void sprites.set({ loading: false });

	releaseFrames();
	sprites.set({
		folder: resolvePath(folder, pack.frames),
		frames,
		sheet: pack.sheet,
		src: pack.src,
		scale: pack.scale,
		columns: pack.columns,
		atlas: resolvePath(folder, pack.atlas),
		stylesheet: resolvePath(folder, pack.stylesheet),
		packFile: file,
		loading: false,
	});
	const missing = frames.filter((frame) => frame.included && frame.error).length;
	if (missing) setStatus(`${missing} of the pack's frames cannot be read`);
}

/** Reads the frames again from disk, keeping names, order and what is left out. */
export async function reloadFrames() {
	const { folder, frames } = sprites.get();
	if (!folder) return;
	const pack: PackFile = { ...packSettings(), sprites: frames.filter((frame) => frame.included).map(({ file, name }) => ({ file, name })) };
	const left = new Set(frames.filter((frame) => !frame.included).map((frame) => frame.file.toLowerCase()));
	sprites.set({ loading: true });
	const reloaded = await loadFrames(folder, pack);
	if (!reloaded) return void sprites.set({ loading: false });
	releaseFrames();
	// A file new since the last read is included, as it would be in a fresh folder.
	sprites.set({ frames: reloaded.map((frame) => (frame.included || left.has(frame.file.toLowerCase()) ? frame : { ...frame, included: true })), loading: false });
}

function packSettings(): Omit<PackFile, 'sprites'> {
	const { folder, atlas, stylesheet, sheet, src, scale, columns } = sprites.get();
	const base = dirname(stylesheet);
	return { frames: relativePath(base, folder ?? ''), atlas: relativePath(base, atlas), stylesheet: relativePath(base, stylesheet), sheet, src, scale, columns };
}

export function updateFrame(index: number, patch: Partial<Frame>) {
	sprites.set({ frames: sprites.get().frames.map((frame, at) => (at === index ? { ...frame, ...patch } : frame)) });
}

export function moveFrame(from: number, to: number) {
	const frames = [...sprites.get().frames];
	if (from === to || to < 0 || to >= frames.length) return;
	const [frame] = frames.splice(from, 1);
	frames.splice(to, 0, frame);
	sprites.set({ frames });
}

/** Moves the atlas, and the path documents name it by along with it unless that was set by hand. */
export function setAtlas(atlas: string) {
	const state = sprites.get();
	sprites.set({ atlas, src: state.src === srcFor(state.atlas) ? srcFor(atlas) : state.src });
}

export function included(): Frame[] {
	return sprites.get().frames.filter((frame) => frame.included);
}

/** Problems that stop a pack, or an empty list. */
export function problems(): string[] {
	const state = sprites.get();
	const chosen = included();
	const list = packProblems(chosen.map((frame) => frame.name), state);
	for (const frame of chosen) if (frame.error) list.push(`${frame.file}.png: ${frame.error}.`);
	if (!state.atlas || extension(state.atlas) !== '.png') list.push('The atlas must be a .png.');
	if (!state.stylesheet || extension(state.stylesheet) !== '.rcss') list.push('The stylesheet must be a .rcss.');
	return list;
}

export async function pack() {
	const state = sprites.get();
	const { project } = store.get();
	if (!project || !state.folder) return;
	const found = problems();
	if (found.length) {
		await ask('Nothing was packed', found.join('\n'), ['OK']);
		return;
	}

	const packFile = packFileFor(state.stylesheet);
	// Overwriting is expected when packing again from the same pack file; anything else asks first.
	if (!state.packFile || !same(state.packFile, packFile)) {
		const existing = [];
		for (const target of [state.atlas, state.stylesheet, packFile]) if (await host.exists(target)) existing.push(basename(target));
		if (existing.length && (await ask(existing.join(', ') + (existing.length === 1 ? ' exists' : ' exist') + ' already', 'Packing replaces ' + (existing.length === 1 ? 'it' : 'them') + '.', ['Cancel', 'Replace'])) !== 'Replace')
			return;
	}

	const chosen = included();
	const { atlas, layout } = packAtlas(chosen.map((frame) => frame.pixels!), state.scale, state.columns);
	await host.writeFile(state.atlas, await encodePng(atlas));
	await host.writeFile(state.stylesheet, spritesheet(chosen.map((frame) => frame.name), layout, state));
	await host.writeFile(packFile, serialisePackFile({ ...packSettings(), sprites: chosen.map(({ file, name }) => ({ file, name })) }));
	sprites.set({ packFile });

	const where = (path: string) => (isUnder(project.root, path) ? relativeTo(project.root, path) : path);
	setStatus(`Packed ${chosen.length} frames, ${layout.cellWidth}x${layout.cellHeight} cells at ${state.scale}x, into ${where(state.atlas)} and ${where(state.stylesheet)}`);
}
