const fs = require('fs');
const path = require('path');
const { ipcRenderer } = require('electron');

// What the tool reads at run time: stylesheets, recipes, fonts, mocks and example art.
const MIRRORED = ['rcss', 'fonts', 'UI', 'examples'];

function mirror(FS, hostDir, wasmDir) {
	FS.mkdirTree(wasmDir);
	for (const entry of fs.readdirSync(hostDir, { withFileTypes: true })) {
		const from = path.join(hostDir, entry.name);
		const to = wasmDir + '/' + entry.name;
		if (entry.isDirectory()) mirror(FS, from, to);
		else FS.writeFile(to, fs.readFileSync(from));
	}
}

async function render(job, document) {
	const name = path.basename(document, '.rml');
	const lines = [];
	const canvas = window.document.createElement('canvas');
	window.document.body.appendChild(canvas);

	const module = await createUipreview({
		canvas,
		locateFile: (file) => '../../../bin/wasm/' + file,
		print: (line) => lines.push(line),
		printErr: (line) => lines.push(line),
		preRun: [(m) => { m.ENV.FOLIO_ROOT = '/folio'; }],
	});

	for (const dir of MIRRORED) mirror(module.FS, path.join(job.folioRoot, dir), '/folio/' + dir);

	const relative = path.relative(job.folioRoot, document).split(path.sep).join('/');
	const started = performance.now();
	const exitCode = module.callMain([
		'/folio/' + relative, '--root', '/folio', '--dp-ratio', job.dpRatio, '--screenshot', '/out/shot.png',
	]);
	const elapsed = Math.round(performance.now() - started);

	let wrote = false;
	try {
		fs.writeFileSync(path.join(job.outDir, name + '.png'), module.FS.readFile('/out/shot.png'));
		wrote = true;
	} catch (error) {
		lines.push('no screenshot: ' + error.message);
	}

	canvas.remove();
	ipcRenderer.send('log', `${wrote && exitCode === 0 ? 'ok  ' : 'FAIL'} ${name} exit=${exitCode} ${elapsed}ms`);
	for (const line of lines) ipcRenderer.send('log', '    ' + line);
	return wrote && exitCode === 0;
}

(async () => {
	const job = await ipcRenderer.invoke('job');
	fs.mkdirSync(job.outDir, { recursive: true });

	let failures = 0;
	for (const document of job.documents) {
		try {
			if (!(await render(job, document))) failures++;
		} catch (error) {
			failures++;
			ipcRenderer.send('log', `FAIL ${document}: ${error && error.stack || error}`);
		}
	}
	ipcRenderer.send('done', failures);
})();
