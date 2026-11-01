import type { Pixels } from './png';

/**
 * Packs pixel-art frames into one atlas and the @spritesheet that names them, as
 * `Tools/uiart/pack_sprites.py` does, to the pixel and the character.
 *
 * Every frame sits in a cell the size of the largest, centred horizontally and standing on the cell's
 * bottom edge, so feet stay put between frames. Cells sit one source pixel apart and in from the edge,
 * or bilinear filtering reads a neighbour's pixels into a frame's border.
 */

export interface PackSettings {
	sheet: string;
	/** The atlas path as the document sees it, such as /UI/art/hero.png. */
	src: string;
	scale: number;
	columns: number;
}

export interface Cell {
	x: number;
	y: number;
	width: number;
	height: number;
}

export interface Layout {
	cellWidth: number;
	cellHeight: number;
	columns: number;
	rows: number;
	width: number;
	height: number;
	/** Each frame's cell in the atlas, in atlas pixels. */
	cells: Cell[];
}

/** The file that remembers a pack, so it can be packed again: `<stylesheet>.spritepack.json`. */
export interface PackFile {
	/** Paths relative to the pack file's folder. */
	frames: string;
	atlas: string;
	stylesheet: string;
	sheet: string;
	src: string;
	scale: number;
	columns: number;
	/** In atlas order. `file` is the frame's file name without `.png`. */
	sprites: { file: string; name: string }[];
}

export const PACK_SUFFIX = '.spritepack.json';
export const NAME_PATTERN = /^[A-Za-z_][A-Za-z0-9_-]*$/;

/** A sprite name from a frame's file name: lower case, anything else an underscore. */
export function spriteName(file: string): string {
	const name = file.toLowerCase().replace(/[^a-z0-9_-]+/g, '_').replace(/^_+|_+$/g, '');
	return /^[a-z_]/.test(name) ? name : '_' + name;
}

export function layoutFrames(sizes: { width: number; height: number }[], scale: number, wantedColumns: number): Layout {
	const cellWidth = Math.max(...sizes.map((size) => size.width));
	const cellHeight = Math.max(...sizes.map((size) => size.height));
	const columns = Math.min(wantedColumns, sizes.length);
	const rows = Math.ceil(sizes.length / columns);
	const pitchX = (cellWidth + 1) * scale;
	const pitchY = (cellHeight + 1) * scale;
	return {
		cellWidth,
		cellHeight,
		columns,
		rows,
		width: columns * pitchX + scale,
		height: rows * pitchY + scale,
		cells: sizes.map((_, index) => ({
			x: scale + (index % columns) * pitchX,
			y: scale + Math.floor(index / columns) * pitchY,
			width: cellWidth * scale,
			height: cellHeight * scale,
		})),
	};
}

export function packAtlas(frames: Pixels[], scale: number, columns: number): { atlas: Pixels; layout: Layout } {
	const layout = layoutFrames(frames, scale, columns);
	const atlas: Pixels = { width: layout.width, height: layout.height, data: new Uint8Array(layout.width * layout.height * 4) };
	frames.forEach((frame, index) => {
		const cell = layout.cells[index];
		const left = cell.x + Math.floor((layout.cellWidth - frame.width) / 2) * scale;
		const top = cell.y + (layout.cellHeight - frame.height) * scale;
		for (let y = 0; y < frame.height * scale; y++) {
			const from = Math.floor(y / scale) * frame.width;
			const to = ((top + y) * atlas.width + left) * 4;
			for (let x = 0; x < frame.width * scale; x++) {
				const source = (from + Math.floor(x / scale)) * 4;
				atlas.data.set(frame.data.subarray(source, source + 4), to + x * 4);
			}
		}
	});
	return { atlas, layout };
}

export function spritesheet(names: string[], layout: Layout, settings: PackSettings): string {
	const lines = names.map((name, index) => {
		const cell = layout.cells[index];
		return `\t${name}: ${cell.x}px ${cell.y}px ${cell.width}px ${cell.height}px;`;
	});
	return `@spritesheet ${settings.sheet}\n{\n\tsrc: ${settings.src};\n\tresolution: ${settings.scale}x;\n\n` + lines.join('\n') + '\n}\n';
}

/** What is wrong with a pack before it is written, or an empty list. */
export function packProblems(names: string[], settings: PackSettings): string[] {
	const problems: string[] = [];
	if (!names.length) problems.push('No frames are included.');
	const seen = new Set<string>();
	for (const name of names) {
		if (!NAME_PATTERN.test(name)) problems.push(`"${name}" is not a sprite name: use letters, digits, _ and -, not starting with a digit.`);
		else if (seen.has(name)) problems.push(`"${name}" names two frames.`);
		seen.add(name);
	}
	if (!NAME_PATTERN.test(settings.sheet)) problems.push(`"${settings.sheet}" is not a sheet name.`);
	if (!settings.src.trim()) problems.push('The atlas path the document uses is empty.');
	if (!Number.isInteger(settings.scale) || settings.scale < 1) problems.push('Scale must be a whole number of 1 or more.');
	if (!Number.isInteger(settings.columns) || settings.columns < 1) problems.push('Columns must be a whole number of 1 or more.');
	return problems;
}

export function parsePackFile(text: string): PackFile {
	const value = JSON.parse(text);
	const string = (field: string) => {
		if (typeof value[field] !== 'string') throw new Error(`"${field}" is missing`);
		return value[field] as string;
	};
	if (!Array.isArray(value.sprites)) throw new Error('"sprites" is missing');
	return {
		frames: string('frames'),
		atlas: string('atlas'),
		stylesheet: string('stylesheet'),
		sheet: string('sheet'),
		src: string('src'),
		scale: Number(value.scale ?? 8),
		columns: Number(value.columns ?? 8),
		sprites: value.sprites.map((entry: { file?: unknown; name?: unknown }) => ({ file: String(entry.file ?? ''), name: String(entry.name ?? '') })),
	};
}

export function serialisePackFile(pack: PackFile): string {
	return JSON.stringify(pack, null, '\t') + '\n';
}

/** `to` relative to the folder `from`, with forward slashes, climbing out with `..` where it must. */
export function relativePath(from: string, to: string): string {
	const a = from.replace(/\/+$/, '').split('/');
	const b = to.split('/');
	let common = 0;
	while (common < a.length && common < b.length && a[common].toLowerCase() === b[common].toLowerCase()) common++;
	if (common === 0) return to;
	return [...a.slice(common).map(() => '..'), ...b.slice(common)].join('/') || '.';
}

/** Resolves a path written relative to `folder`, folding away `.` and `..`. */
export function resolvePath(folder: string, relative: string): string {
	if (/^([A-Za-z]:)?\//.test(relative)) return relative;
	const parts = folder.replace(/\/+$/, '').split('/');
	for (const part of relative.split('/')) {
		if (part === '..') parts.pop();
		else if (part !== '.' && part !== '') parts.push(part);
	}
	return parts.join('/');
}
