// The Sprites panel: a folder of frames in, an atlas, a @spritesheet and a pack file out, and back in again.
const { store, commands, host, log, wait, capture, sprites } = studio;
const fail = (message) => { throw new Error(message); };
const expect = (condition, message) => { if (!condition) fail(message); log('ok   ' + message); };
const text = async (path) => new TextDecoder().decode(await host.readFile(path));

await wait(2000);
const root = store.get().project.root;
const folder = root + '/UI/source/hero';
const frame = (width, height, rgba) => {
	const data = new Uint8Array(width * height * 4);
	for (let i = 0; i < data.length; i += 4) data.set(i % 12 ? rgba : [0, 0, 0, 0], i);
	return sprites.encodePng({ width, height, data });
};
await host.writeFile(folder + '/Hero_Idle.png', await frame(16, 24, [200, 60, 60, 255]));
await host.writeFile(folder + '/Hero_Walk_1.png', await frame(14, 22, [60, 200, 60, 255]));
await host.writeFile(folder + '/Hero_Walk_10.png', await frame(16, 20, [60, 60, 200, 128]));
await host.writeFile(folder + '/Hero_Walk_2.png', await frame(12, 24, [200, 200, 60, 255]));
await wait(800);

commands.runCommand('panel:sprites');
await sprites.openFolder(folder);
await wait(300);
let state = sprites.sprites.get();
expect(state.frames.map((entry) => entry.file).join() === 'Hero_Idle,Hero_Walk_1,Hero_Walk_2,Hero_Walk_10', 'frames are listed in natural order');
expect(state.frames.every((entry) => entry.pixels && entry.included), 'and every one is read and included');
expect(state.sheet === 'hero' && state.src === '/UI/art/hero.png' && state.stylesheet === root + '/UI/art/hero.rcss', 'outputs default to UI/art/ named after the folder');
expect(document.querySelectorAll('.sprites-frame').length === 4 && document.querySelector('.sprites-preview canvas'), 'the panel shows the frames and the atlas');

sprites.updateFrame(0, { name: 'hero_idle_0' });
sprites.updateFrame(3, { included: false });
sprites.moveFrame(2, 1);
sprites.setAtlas(root + '/UI/art/sheets/hero.png');
expect(sprites.sprites.get().src === '/UI/art/sheets/hero.png', 'moving the atlas moves the src that follows it');

sprites.updateFrame(1, { name: 'hero_idle_0' });
expect(sprites.problems().some((problem) => problem.includes('names two frames')), 'a name used twice is a problem');
await wait(100);
expect(document.querySelector('.sprites .button.primary').disabled, 'and Pack is disabled while there is one');
sprites.updateFrame(1, { name: 'hero_walk_2' });
await wait(100);

await sprites.pack();
await wait(500);
const rcss = await text(root + '/UI/art/hero.rcss');
expect(rcss.startsWith('@spritesheet hero\n{\n\tsrc: /UI/art/sheets/hero.png;\n\tresolution: 8x;'), 'the stylesheet names the sheet and its atlas');
expect(/hero_idle_0: 8px 8px 128px 192px;\n\thero_walk_2: 144px 8px 128px 192px;\n\thero_walk_1: 280px 8px/.test(rcss), 'in the panel order, with the renamed frame');
expect(!rcss.includes('hero_walk_10'), 'and without the frame left out');
const atlas = await sprites.decodePng(new Uint8Array(await host.readFile(root + '/UI/art/sheets/hero.png')));
expect(atlas.width === 3 * 17 * 8 + 8 && atlas.height === 25 * 8 + 8, 'the atlas is three cells wide and one tall');
const pack = sprites.parsePackFile(await text(root + '/UI/art/hero.spritepack.json'));
expect(pack.frames === '../source/hero' && pack.atlas === 'sheets/hero.png' && pack.sprites.length === 3, 'the pack file remembers it relative to itself');
await capture(studio.shots + '/studio-sprites.png');

await sprites.openFolder(root + '/UI');
await sprites.openPack(root + '/UI/art/hero.spritepack.json');
await wait(300);
state = sprites.sprites.get();
expect(state.frames.map((entry) => entry.name + (entry.included ? '' : '-')).join() === 'hero_idle_0,hero_walk_2,hero_walk_1,hero_walk_10-', 'opening the pack restores names, order and what was left out');
expect(state.atlas === root + '/UI/art/sheets/hero.png', 'and where it writes');

// Packing again from the pack it was opened from replaces the files without asking.
await host.writeFile(folder + '/Hero_Walk_2.png', await frame(12, 24, [10, 20, 30, 255]));
await sprites.reloadFrames();
await sprites.pack();
const repacked = await sprites.decodePng(new Uint8Array(await host.readFile(root + '/UI/art/sheets/hero.png')));
const cell = (repacked.width * (8 + 191) + 144 + 8 * 2 + 8) * 4;
expect(repacked.data[cell] === 10 && repacked.data[cell + 1] === 20, 'a changed frame is in the atlas packed again');
