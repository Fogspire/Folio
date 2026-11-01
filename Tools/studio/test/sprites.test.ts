import { execFileSync } from 'node:child_process';
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { afterAll, describe, expect, it } from 'vitest';
import {
	layoutFrames, packAtlas, packProblems, parsePackFile, relativePath, resolvePath, serialisePackFile, spriteName, spritesheet,
} from '../app/src/sprites/pack';
import { decodePng, encodePng, Pixels } from '../app/src/sprites/png';

const script = path.resolve(__dirname, '../../uiart/pack_sprites.py');
const forward = (target: string) => target.replace(/\\/g, '/');

function python(): string | null {
	for (const command of ['python', 'python3']) {
		try {
			execFileSync(command, ['-c', 'import PIL'], { stdio: 'ignore' });
			return command;
		} catch {
			// Try the next name.
		}
	}
	return null;
}

function solid(width: number, height: number, rgba: number[]): Pixels {
	const data = new Uint8Array(width * height * 4);
	for (let i = 0; i < data.length; i += 4) data.set(rgba, i);
	return { width, height, data };
}

describe('sprite packing', () => {
	it('lays cells out one source pixel apart and in from the edge', () => {
		const layout = layoutFrames([{ width: 16, height: 24 }, { width: 12, height: 20 }, { width: 16, height: 24 }], 8, 2);
		expect(layout).toMatchObject({ cellWidth: 16, cellHeight: 24, columns: 2, rows: 2, width: 2 * 17 * 8 + 8, height: 2 * 25 * 8 + 8 });
		expect(layout.cells[1]).toEqual({ x: 8 + 17 * 8, y: 8, width: 128, height: 192 });
		expect(layout.cells[2]).toEqual({ x: 8, y: 8 + 25 * 8, width: 128, height: 192 });
	});

	it('centres a smaller frame and stands it on the cell floor', () => {
		const { atlas, layout } = packAtlas([solid(4, 4, [255, 0, 0, 255]), solid(2, 1, [0, 255, 0, 128])], 2, 8);
		const at = (x: number, y: number) => [...atlas.data.subarray((y * atlas.width + x) * 4, (y * atlas.width + x) * 4 + 4)];
		const cell = layout.cells[1];
		expect(at(cell.x + 2, cell.y + 6)).toEqual([0, 255, 0, 128]);
		expect(at(cell.x + 2, cell.y + 5)).toEqual([0, 0, 0, 0]);
		expect(at(cell.x, cell.y + 6)).toEqual([0, 0, 0, 0]);
	});

	it('writes the @spritesheet', () => {
		const layout = layoutFrames([{ width: 2, height: 2 }], 4, 8);
		expect(spritesheet(['a'], layout, { sheet: 's', src: '/UI/art/s.png', scale: 4, columns: 8 }))
			.toBe('@spritesheet s\n{\n\tsrc: /UI/art/s.png;\n\tresolution: 4x;\n\n\ta: 4px 4px 8px 8px;\n}\n');
	});

	it('names sprites after their files and refuses bad names', () => {
		expect(spriteName('Hero_Walk 0')).toBe('hero_walk_0');
		expect(spriteName('00_Idle')).toBe('_00_idle');
		const settings = { sheet: 'hero', src: '/a.png', scale: 8, columns: 8 };
		expect(packProblems(['a', 'b'], settings)).toEqual([]);
		expect(packProblems(['a', 'a'], settings)).toHaveLength(1);
		expect(packProblems(['1a'], settings)).toHaveLength(1);
		expect(packProblems([], settings)).toHaveLength(1);
	});

	it('resolves pack paths both ways', () => {
		expect(relativePath('C:/p/UI/art', 'C:/p/Art/hero')).toBe('../../Art/hero');
		expect(relativePath('C:/p/UI/art', 'C:/p/UI/art/hero.png')).toBe('hero.png');
		expect(resolvePath('C:/p/UI/art', '../../Art/hero')).toBe('C:/p/Art/hero');
		const pack = { frames: 'f', atlas: 'a.png', stylesheet: 'a.rcss', sheet: 'a', src: '/a.png', scale: 2, columns: 3, sprites: [{ file: 'x', name: 'y' }] };
		expect(parsePackFile(serialisePackFile(pack))).toEqual(pack);
	});

	it('round-trips straight alpha exactly', async () => {
		const pixels: Pixels = { width: 3, height: 2, data: new Uint8Array([1, 2, 3, 4, 255, 0, 0, 128, 0, 0, 0, 0, 9, 8, 7, 255, 10, 200, 30, 1, 255, 255, 255, 255]) };
		expect(await decodePng(await encodePng(pixels))).toEqual(pixels);
	});
});

const command = python();

describe.skipIf(!command)('matches Tools/uiart/pack_sprites.py', () => {
	const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'folio-sprites-'));
	afterAll(() => fs.rmSync(scratch, { recursive: true, force: true }));

	it('to the pixel, the character and the pack file', async () => {
		const frames = path.join(scratch, 'frames');
		fs.mkdirSync(frames);
		// Every colour type Pillow writes, odd sizes, and partial alpha, which a canvas would not keep.
		execFileSync(command!, ['-c', `
import random, sys
from PIL import Image
random.seed(3)
out = sys.argv[1]
def noise(mode, size):
    image = Image.new('RGBA', size)
    image.putdata([(random.randrange(256), random.randrange(256), random.randrange(256), random.choice([0, 255, random.randrange(256)])) for _ in range(size[0] * size[1])])
    return image if mode == 'RGBA' else image.convert(mode)
noise('RGBA', (16, 24)).save(out + '/A_Idle.png')
noise('RGB', (13, 20)).save(out + '/B_Walk.png')
noise('LA', (9, 7)).save(out + '/C_Duck.png')
noise('L', (5, 5)).save(out + '/D_Dot.png')
noise('RGBA', (16, 11)).convert('P').save(out + '/E_Pal.png', transparency=0)
noise('1', (7, 9)).save(out + '/F_Bits.png')
`, forward(frames)]);

		const map: [string, string][] = [
			['A_Idle', 'idle_0'], ['B_Walk', 'walk_0'], ['C_Duck', 'duck_0'], ['D_Dot', 'dot_0'], ['E_Pal', 'pal_0'], ['F_Bits', 'bits_0'],
		];
		const art = path.join(scratch, 'UI', 'art');
		execFileSync(command!, [script, frames, path.join(art, 'hero.png'), path.join(art, 'hero.rcss'),
			'--sheet', 'hero', '--src', '/UI/art/hero.png', '--scale', '3', '--columns', '3',
			...map.flatMap(([file, name]) => ['--map', `${file}=${name}`])]);

		const decoded = await Promise.all(map.map(([file]) => decodePng(new Uint8Array(fs.readFileSync(path.join(frames, file + '.png'))))));
		const settings = { sheet: 'hero', src: '/UI/art/hero.png', scale: 3, columns: 3 };
		const { atlas, layout } = packAtlas(decoded, settings.scale, settings.columns);

		expect(spritesheet(map.map(([, name]) => name), layout, settings)).toBe(fs.readFileSync(path.join(art, 'hero.rcss'), 'utf8'));
		const theirs = await decodePng(new Uint8Array(fs.readFileSync(path.join(art, 'hero.png'))));
		expect(theirs.width).toBe(atlas.width);
		expect(theirs.height).toBe(atlas.height);
		expect(Buffer.compare(Buffer.from(theirs.data), Buffer.from(atlas.data))).toBe(0);

		const base = forward(art);
		const ours = serialisePackFile({
			frames: relativePath(base, forward(frames)), atlas: 'hero.png', stylesheet: 'hero.rcss', ...settings, sprites: map.map(([file, name]) => ({ file, name })),
		});
		expect(ours).toBe(fs.readFileSync(path.join(art, 'hero.spritepack.json'), 'utf8'));

		// And the script packs again from the pack file to the same atlas.
		fs.rmSync(path.join(art, 'hero.png'));
		execFileSync(command!, [script, '--pack', path.join(art, 'hero.spritepack.json')]);
		const again = await decodePng(new Uint8Array(fs.readFileSync(path.join(art, 'hero.png'))));
		expect(Buffer.compare(Buffer.from(again.data), Buffer.from(atlas.data))).toBe(0);
	});
});
