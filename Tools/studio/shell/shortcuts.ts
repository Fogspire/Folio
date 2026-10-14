// Puts Folio Studio where the desktop starts programs from, so it opens without a terminal.
import { execFileSync } from 'node:child_process';
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';

export interface Launch {
	/** The program to run: Electron for a checkout, the app itself when packaged. */
	program: string;
	/** What to pass it: the checkout's Tools/studio, or nothing for a packaged app. */
	argument: string | null;
	/** A PNG to show as its icon. */
	icon: string;
	/** Somewhere to keep files the shortcut refers to, such as a Windows icon. */
	scratch: string;
	/** Where to put the shortcuts instead of the usual places, for trying this out. */
	into?: string;
}

function windows(launch: Launch): string[] {
	// An .ico may hold a PNG as it is, behind a directory that says where.
	const png = fs.readFileSync(launch.icon);
	const header = Buffer.alloc(22);
	header.writeUInt16LE(1, 2);
	header.writeUInt16LE(1, 4);
	header.writeUInt16LE(1, 10);
	header.writeUInt16LE(32, 12);
	header.writeUInt32LE(png.length, 14);
	header.writeUInt32LE(22, 18);
	const ico = path.join(launch.scratch, 'folio-studio.ico');
	fs.mkdirSync(launch.scratch, { recursive: true });
	fs.writeFileSync(ico, Buffer.concat([header, png]));

	const places = launch.into ? [launch.into] : [
		path.join(os.homedir(), 'Desktop'),
		path.join(process.env.APPDATA ?? '', 'Microsoft', 'Windows', 'Start Menu', 'Programs'),
	];
	const quote = (value: string) => "'" + value.replace(/'/g, "''") + "'";
	const made: string[] = [];
	for (const place of places) {
		if (!fs.existsSync(place)) continue;
		const link = path.join(place, 'Folio Studio.lnk');
		execFileSync('powershell', ['-NoProfile', '-Command', [
			'$s = (New-Object -ComObject WScript.Shell).CreateShortcut(' + quote(link) + ')',
			'$s.TargetPath = ' + quote(launch.program),
			'$s.Arguments = ' + quote(launch.argument ? '"' + launch.argument + '"' : ''),
			'$s.WorkingDirectory = ' + quote(launch.argument ?? path.dirname(launch.program)),
			'$s.IconLocation = ' + quote(ico),
			'$s.Description = ' + quote('Folio Studio'),
			'$s.Save()',
		].join('; ')], { windowsHide: true });
		made.push(link);
	}
	return made;
}

function linux(launch: Launch): string[] {
	const place = launch.into ?? path.join(os.homedir(), '.local', 'share', 'applications');
	fs.mkdirSync(place, { recursive: true });
	const entry = path.join(place, 'folio-studio.desktop');
	fs.writeFileSync(entry, [
		'[Desktop Entry]',
		'Type=Application',
		'Name=Folio Studio',
		`Exec="${launch.program}"` + (launch.argument ? ` "${launch.argument}"` : ''),
		`Icon=${launch.icon}`,
		'Categories=Development;',
		'',
	].join('\n'));
	return [entry];
}

function mac(launch: Launch): string[] {
	const bundle = path.join(launch.into ?? path.join(os.homedir(), 'Applications'), 'Folio Studio.app');
	const binaries = path.join(bundle, 'Contents', 'MacOS');
	fs.mkdirSync(binaries, { recursive: true });
	const run = `exec "${launch.program}"` + (launch.argument ? ` "${launch.argument}"` : '');
	fs.writeFileSync(path.join(binaries, 'folio-studio'), `#!/bin/sh\n${run}\n`, { mode: 0o755 });
	fs.writeFileSync(path.join(bundle, 'Contents', 'Info.plist'), [
		'<?xml version="1.0" encoding="UTF-8"?>',
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">',
		'<plist version="1.0"><dict>',
		'<key>CFBundleName</key><string>Folio Studio</string>',
		'<key>CFBundleIdentifier</key><string>com.fogspire.folio.studio.launcher</string>',
		'<key>CFBundleExecutable</key><string>folio-studio</string>',
		'<key>CFBundlePackageType</key><string>APPL</string>',
		'</dict></plist>',
		'',
	].join('\n'));
	return [bundle];
}

/** Creates the shortcuts for this platform and returns where they went. */
export function createShortcuts(launch: Launch): string[] {
	if (process.platform === 'win32') return windows(launch);
	if (process.platform === 'darwin') return mac(launch);
	return linux(launch);
}

// Run as a script by studio.sh: a checkout, launched through its own Electron.
if (process.argv[1]?.endsWith('shortcuts.js')) {
	const studio = path.resolve(__dirname, '..', '..');
	const made = createShortcuts({
		// Outside Electron, requiring it gives the path of its executable.
		program: require('electron') as unknown as string,
		argument: studio,
		icon: path.resolve(studio, '..', '..', 'Resources', 'Icon256.png'),
		scratch: path.join(studio, 'out'),
		into: process.env.FOLIO_SHORTCUT_DIR,
	});
	for (const entry of made) console.log('Created ' + entry);
	if (!made.length) {
		console.error('Nowhere to put a shortcut.');
		process.exit(1);
	}
}
