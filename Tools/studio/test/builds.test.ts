import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { checkBuilds, patchStamp, stalePreview, staleLibraries } from '../shell/builds';

let root: string;

function write(relative: string, content: string, modified?: number) {
	const target = path.join(root, relative);
	fs.mkdirSync(path.dirname(target), { recursive: true });
	fs.writeFileSync(target, content);
	if (modified !== undefined) fs.utimesSync(target, modified, modified);
}

beforeEach(() => {
	root = fs.mkdtempSync(path.join(os.tmpdir(), 'folio-builds-'));
	write('scripts/build_ue_libs.sh', '');
	write('patches/RmlUi/0001-a.patch', 'one');
	write('patches/RmlUi/0002-b.patch', 'two');
});

afterEach(() => fs.rmSync(root, { recursive: true, force: true }));

const libraries = 'Source/ThirdParty/RmlUiLibrary/lib/Win64/';

describe('staleLibraries', () => {
	it('says when they were never built', () => {
		expect(staleLibraries(root, 'win32')).toBe('they have not been built');
	});

	it('is satisfied when the stamp matches the patches', () => {
		write(libraries + 'rmlui.lib', '');
		write(libraries + 'RMLUI_PATCHES', patchStamp(root).join('\n') + '\n');
		expect(staleLibraries(root, 'win32')).toBeNull();
	});

	it('names a patch added since the build', () => {
		write(libraries + 'rmlui.lib', '');
		write(libraries + 'RMLUI_PATCHES', patchStamp(root).join('\n') + '\n');
		write('patches/RmlUi/0003-c.patch', 'three');
		expect(staleLibraries(root, 'win32')).toBe('they were built without 0003-c.patch');
	});

	it('names a patch changed since the build', () => {
		write(libraries + 'rmlui.lib', '');
		write(libraries + 'RMLUI_PATCHES', patchStamp(root).join('\n') + '\n');
		write('patches/RmlUi/0001-a.patch', 'one, revised');
		expect(staleLibraries(root, 'win32')).toBe('they were built without 0001-a.patch');
	});

	it('names a patch removed since the build', () => {
		write(libraries + 'rmlui.lib', '');
		write(libraries + 'RMLUI_PATCHES', patchStamp(root).join('\n') + '\n');
		fs.rmSync(path.join(root, 'patches/RmlUi/0002-b.patch'));
		expect(staleLibraries(root, 'win32')).toBe('they were built with 0002-b.patch, which is no longer in the tree');
	});

	it('treats libraries without a stamp as built without the patches', () => {
		write(libraries + 'rmlui.lib', '');
		expect(staleLibraries(root, 'win32')).toBe('they were built without 0001-a.patch');
	});
});

describe('stalePreview', () => {
	it('says nothing when uipreview was never built', () => {
		expect(stalePreview(root, 'win32')).toBeNull();
	});

	it('flags a binary older than a source', () => {
		write('bin/win64/uipreview.exe', '', 1000);
		write('Tools/uipreview/src/App.cpp', '', 2000);
		expect(stalePreview(root, 'win32')).toBe('it is older than its sources');
	});

	it('ignores the Unreal libraries\' own build files', () => {
		write('bin/win64/uipreview.exe', '', 2000);
		write('Tools/uipreview/src/App.cpp', '', 1000);
		write('patches/RmlUi/0001-a.patch', 'one', 1000);
		write('patches/RmlUi/0002-b.patch', 'two', 1000);
		write('cmake/ue/CMakeLists.txt', '', 3000);
		expect(stalePreview(root, 'win32')).toBeNull();
	});
});

describe('checkBuilds', () => {
	it('has nothing to say about a plugin without its build scripts', () => {
		fs.rmSync(path.join(root, 'scripts'), { recursive: true });
		expect(checkBuilds(root, 'win32')).toEqual({ libraries: null, preview: null });
	});
});
