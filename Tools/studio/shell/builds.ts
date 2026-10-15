import * as crypto from 'node:crypto';
import * as fs from 'node:fs';
import * as path from 'node:path';

/** What of a Folio checkout's own builds is behind its sources: a reason for each, or null when it is current. */
export interface BuildStatus {
	libraries: string | null;
	preview: string | null;
}

/** The script that brings each build up to date, run from the Folio root. */
export const BUILD_SCRIPTS = { libraries: 'scripts/build_ue_libs.sh', preview: 'scripts/build.sh' } as const;

const LIBRARY_PLATFORM: Record<string, string> = { win32: 'Win64', darwin: 'Mac', linux: 'Linux' };
const PREVIEW_PLATFORM: Record<string, string> = { win32: 'win64', darwin: 'mac', linux: 'linux' };

/** One line per patch, as build_ue_libs.sh records them beside the libraries. */
export function patchStamp(folioRoot: string): string[] {
	const directory = path.join(folioRoot, 'patches', 'RmlUi');
	let names: string[];
	try {
		names = fs.readdirSync(directory).filter((name) => name.endsWith('.patch')).sort();
	} catch {
		return [];
	}
	return names.map((name) => name + ' ' + crypto.createHash('sha256').update(fs.readFileSync(path.join(directory, name))).digest('hex'));
}

/** Why the Unreal RmlUi libraries need rebuilding, matching what RmlUiLibrary.Build.cs refuses to build with. */
export function staleLibraries(folioRoot: string, platform = process.platform): string | null {
	const directory = path.join(folioRoot, 'Source', 'ThirdParty', 'RmlUiLibrary', 'lib', LIBRARY_PLATFORM[platform] ?? 'Win64');
	const library = platform === 'win32' ? 'rmlui.lib' : 'librmlui.a';
	if (!fs.existsSync(path.join(directory, library))) return 'they have not been built';

	let built: string[] = [];
	try {
		built = fs.readFileSync(path.join(directory, 'RMLUI_PATCHES'), 'utf8').split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
	} catch {
		// Built before the stamp existed, which is as good as built without the patches.
	}
	const expected = patchStamp(folioRoot);
	const missing = expected.find((line) => !built.includes(line));
	if (missing) return 'they were built without ' + missing.split(' ')[0];
	const removed = built.find((line) => !expected.includes(line));
	if (removed) return 'they were built with ' + removed.split(' ')[0] + ', which is no longer in the tree';
	return null;
}

function newestModified(target: string): number {
	let stat: fs.Stats;
	try {
		stat = fs.statSync(target);
	} catch {
		return 0;
	}
	if (!stat.isDirectory()) return stat.mtimeMs;
	let newest = 0;
	for (const entry of fs.readdirSync(target)) newest = Math.max(newest, newestModified(path.join(target, entry)));
	return newest;
}

/** Why uipreview needs rebuilding: it is older than something it is built from. Null when it was never built, since it is optional. */
export function stalePreview(folioRoot: string, platform = process.platform): string | null {
	const binary = path.join(folioRoot, 'bin', PREVIEW_PLATFORM[platform] ?? 'win64', platform === 'win32' ? 'uipreview.exe' : 'uipreview');
	let built: number;
	try {
		built = fs.statSync(binary).mtimeMs;
	} catch {
		return null;
	}
	// cmake/ue is the Unreal libraries' build and not this one's, so only the top of cmake/ counts.
	const cmake = path.join(folioRoot, 'cmake');
	let shared: string[] = [];
	try {
		shared = fs.readdirSync(cmake).filter((name) => name.endsWith('.cmake')).map((name) => path.join('cmake', name));
	} catch {
		// No cmake directory, nothing shared.
	}
	const sources = ['Tools/uipreview/src', 'Tools/uipreview/CMakeLists.txt', 'Source/FolioCore', 'patches', 'CMakeLists.txt', ...shared];
	return sources.some((source) => newestModified(path.join(folioRoot, source)) > built) ? 'it is older than its sources' : null;
}

/** Checks a Folio checkout's builds. A plugin without its build scripts is a distribution, and has nothing to rebuild. */
export function checkBuilds(folioRoot: string, platform = process.platform): BuildStatus {
	if (!fs.existsSync(path.join(folioRoot, BUILD_SCRIPTS.libraries))) return { libraries: null, preview: null };
	return { libraries: staleLibraries(folioRoot, platform), preview: stalePreview(folioRoot, platform) };
}
