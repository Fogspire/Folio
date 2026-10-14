// Renders documents through the WebAssembly build of uipreview in a hidden window, so its
// screenshots can be diffed against the native tool's.
//
//   electron parity/main.js <folio root> <out dir> <dp ratio> <document>...
const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('path');

const [folioRoot, outDir, dpRatio, ...documents] = process.argv.slice(2);

app.commandLine.appendSwitch('ignore-gpu-blocklist');

app.whenReady().then(() => {
	const window = new BrowserWindow({
		show: false,
		webPreferences: { nodeIntegration: true, contextIsolation: false, backgroundThrottling: false },
	});

	ipcMain.handle('job', () => ({ folioRoot, outDir, dpRatio, documents }));
	ipcMain.on('log', (_event, line) => console.log(line));
	ipcMain.on('done', (_event, failures) => app.exit(failures ? 1 : 0));

	window.webContents.on('console-message', (_event, _level, message) => console.log('[page] ' + message));
	window.webContents.on('render-process-gone', (_event, details) => {
		console.log('renderer gone: ' + details.reason);
		app.exit(3);
	});
	window.loadFile(path.join(__dirname, 'index.html'));
});
