/**
 * A PNG reader and writer for straight-alpha RGBA pixels.
 *
 * A canvas stores premultiplied colour, so a half-transparent pixel read back through one comes out
 * changed. Packing has to copy pixels exactly, so it decodes and encodes them itself.
 */

export interface Pixels {
	width: number;
	height: number;
	/** RGBA, 8 bits a channel, rows top to bottom. */
	data: Uint8Array;
}

const SIGNATURE = [137, 80, 78, 71, 13, 10, 26, 10];
const CHANNELS: Record<number, number> = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 };
/** Adam7: the first column and row of each pass, and the step between them. */
const PASSES = [
	[0, 0, 8, 8], [4, 0, 8, 8], [0, 4, 4, 8], [2, 0, 4, 4], [0, 2, 2, 4], [1, 0, 2, 2], [0, 1, 1, 2],
];

async function through(data: Uint8Array, stream: CompressionStream | DecompressionStream): Promise<Uint8Array> {
	const piped = new Blob([data as BlobPart]).stream().pipeThrough(stream);
	return new Uint8Array(await new Response(piped).arrayBuffer());
}

let crcTable: Uint32Array | null = null;

function crc32(bytes: Uint8Array, start: number, end: number): number {
	if (!crcTable) {
		crcTable = new Uint32Array(256);
		for (let n = 0; n < 256; n++) {
			let c = n;
			for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
			crcTable[n] = c >>> 0;
		}
	}
	let crc = 0xffffffff;
	for (let i = start; i < end; i++) crc = crcTable[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
	return (crc ^ 0xffffffff) >>> 0;
}

function paeth(a: number, b: number, c: number): number {
	const p = a + b - c;
	const pa = Math.abs(p - a);
	const pb = Math.abs(p - b);
	const pc = Math.abs(p - c);
	return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/** Reverses the per-row filters in place and returns each row's unfiltered bytes. */
function unfilter(data: Uint8Array, offset: number, stride: number, rows: number, bytesPerPixel: number): Uint8Array[] {
	const out: Uint8Array[] = [];
	let previous: Uint8Array = new Uint8Array(stride);
	for (let y = 0; y < rows; y++) {
		const type = data[offset];
		const row = data.subarray(offset + 1, offset + 1 + stride);
		if (row.length < stride) throw new Error('image data ends early');
		for (let x = 0; x < stride; x++) {
			const left = x >= bytesPerPixel ? row[x - bytesPerPixel] : 0;
			const up = previous[x];
			const upLeft = x >= bytesPerPixel ? previous[x - bytesPerPixel] : 0;
			if (type === 1) row[x] = (row[x] + left) & 0xff;
			else if (type === 2) row[x] = (row[x] + up) & 0xff;
			else if (type === 3) row[x] = (row[x] + ((left + up) >> 1)) & 0xff;
			else if (type === 4) row[x] = (row[x] + paeth(left, up, upLeft)) & 0xff;
			else if (type !== 0) throw new Error('unknown row filter ' + type);
		}
		out.push(row);
		previous = row;
		offset += stride + 1;
	}
	return out;
}

export async function decodePng(bytes: Uint8Array): Promise<Pixels> {
	if (bytes.length < 8 || SIGNATURE.some((value, index) => bytes[index] !== value)) throw new Error('not a PNG');
	const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

	let width = 0, height = 0, depth = 0, colour = -1, interlace = 0;
	let palette: Uint8Array | null = null;
	let transparency: Uint8Array | null = null;
	const parts: Uint8Array[] = [];
	for (let at = 8; at + 8 <= bytes.length;) {
		const length = view.getUint32(at);
		const type = String.fromCharCode(...bytes.subarray(at + 4, at + 8));
		const body = bytes.subarray(at + 8, at + 8 + length);
		if (type === 'IHDR') {
			width = view.getUint32(at + 8);
			height = view.getUint32(at + 12);
			depth = body[8];
			colour = body[9];
			interlace = body[12];
		} else if (type === 'PLTE') palette = body;
		else if (type === 'tRNS') transparency = body;
		else if (type === 'IDAT') parts.push(body);
		else if (type === 'IEND') break;
		at += 12 + length;
	}
	const channels = CHANNELS[colour];
	if (!channels || !width || !height) throw new Error('unsupported PNG header');
	if (colour === 3 && !palette) throw new Error('indexed PNG without a palette');

	const packed = new Uint8Array(parts.reduce((sum, part) => sum + part.length, 0));
	parts.reduce((at, part) => (packed.set(part, at), at + part.length), 0);
	const raw = await through(packed, new DecompressionStream('deflate'));

	const bitsPerPixel = channels * depth;
	const bytesPerPixel = Math.max(1, bitsPerPixel >> 3);
	const max = (1 << depth) - 1;
	const out = new Uint8Array(width * height * 4);

	// A sample as stored, at the image's own depth, so a tRNS key can be matched against it exactly.
	const sample = (row: Uint8Array, x: number, channel: number): number => {
		if (depth === 16) return (row[(x * channels + channel) * 2] << 8) | row[(x * channels + channel) * 2 + 1];
		if (depth === 8) return row[x * channels + channel];
		const bit = (x * channels + channel) * depth;
		return (row[bit >> 3] >> (8 - depth - (bit & 7))) & max;
	};
	const to8 = (value: number) => (depth === 16 ? value >> 8 : depth === 8 ? value : Math.round((value * 255) / max));
	const key = (index: number) => (transparency && transparency.length >= index * 2 + 2 ? (transparency[index * 2] << 8) | transparency[index * 2 + 1] : -1);

	const writeRow = (row: Uint8Array, y: number, columns: number, x0: number, dx: number) => {
		for (let i = 0; i < columns; i++) {
			const o = (y * width + x0 + i * dx) * 4;
			if (colour === 3) {
				const index = sample(row, i, 0);
				out[o] = palette![index * 3];
				out[o + 1] = palette![index * 3 + 1];
				out[o + 2] = palette![index * 3 + 2];
				out[o + 3] = transparency && index < transparency.length ? transparency[index] : 255;
			} else if (colour === 0 || colour === 4) {
				const grey = sample(row, i, 0);
				out[o] = out[o + 1] = out[o + 2] = to8(grey);
				out[o + 3] = colour === 4 ? to8(sample(row, i, 1)) : grey === key(0) ? 0 : 255;
			} else {
				const r = sample(row, i, 0), g = sample(row, i, 1), b = sample(row, i, 2);
				out[o] = to8(r);
				out[o + 1] = to8(g);
				out[o + 2] = to8(b);
				out[o + 3] = colour === 6 ? to8(sample(row, i, 3)) : r === key(0) && g === key(1) && b === key(2) ? 0 : 255;
			}
		}
	};

	if (interlace === 0) {
		const stride = Math.ceil((width * bitsPerPixel) / 8);
		unfilter(raw, 0, stride, height, bytesPerPixel).forEach((row, y) => writeRow(row, y, width, 0, 1));
	} else {
		let offset = 0;
		for (const [x0, y0, dx, dy] of PASSES) {
			const columns = Math.ceil((width - x0) / dx);
			const rows = Math.ceil((height - y0) / dy);
			if (columns <= 0 || rows <= 0) continue;
			const stride = Math.ceil((columns * bitsPerPixel) / 8);
			unfilter(raw, offset, stride, rows, bytesPerPixel).forEach((row, i) => writeRow(row, y0 + i * dy, columns, x0, dx));
			offset += rows * (stride + 1);
		}
	}
	return { width, height, data: out };
}

function chunk(type: string, body: Uint8Array): Uint8Array {
	const out = new Uint8Array(body.length + 12);
	const view = new DataView(out.buffer);
	view.setUint32(0, body.length);
	for (let i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i);
	out.set(body, 8);
	view.setUint32(8 + body.length, crc32(out, 4, 8 + body.length));
	return out;
}

/** Writes 8-bit RGBA, every row filtered by its left neighbour, which suits large flat runs. */
export async function encodePng(pixels: Pixels): Promise<Uint8Array> {
	const { width, height, data } = pixels;
	const stride = width * 4;
	const filtered = new Uint8Array((stride + 1) * height);
	for (let y = 0; y < height; y++) {
		const row = y * (stride + 1);
		filtered[row] = 1;
		for (let x = 0; x < stride; x++) {
			const value = data[y * stride + x];
			filtered[row + 1 + x] = x >= 4 ? (value - data[y * stride + x - 4]) & 0xff : value;
		}
	}
	const header = new Uint8Array(13);
	const view = new DataView(header.buffer);
	view.setUint32(0, width);
	view.setUint32(4, height);
	header[8] = 8;
	header[9] = 6;

	const parts = [
		new Uint8Array(SIGNATURE),
		chunk('IHDR', header),
		chunk('IDAT', await through(filtered, new CompressionStream('deflate'))),
		chunk('IEND', new Uint8Array(0)),
	];
	const out = new Uint8Array(parts.reduce((sum, part) => sum + part.length, 0));
	parts.reduce((at, part) => (out.set(part, at), at + part.length), 0);
	return out;
}
