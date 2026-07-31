/*
 * Folio preview for VSCode.
 *
 * A webview and a spawn-on-save over uipreview, which is deliberately all it is. The renderer already
 * exists, already watches files and already screenshots; putting a second renderer in a webview would
 * be a second thing to keep honest, and the whole value of a preview is that it shows what the game
 * will show.
 *
 * What it can promise is what `scripts/compare_renderers.sh` proves: layout matches the engine
 * exactly - every text line, on every document it covers. What it cannot yet promise is the
 * appearance of layer effects, because the preview draws those in OpenGL and the engine replays them
 * through RHI, and nothing compares those pixels. Documents using them get a note saying so, rather
 * than a silent maybe.
 *
 * No build step and no dependencies: this file is the extension. Anything needing a bundler would
 * need installing and maintaining before anyone could look at a document.
 */

const vscode = require("vscode");
const cp = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");

/** Properties whose appearance the harness does not compare. Worth saying so on the document. */
const UNCOMPARED = ["box-shadow", "filter:", "backdrop-filter", "mask-image"];

let panel = null;
let currentDocument = null;
let renderIndex = 0;
let rendering = false;
let pending = false;

function config() {
	return vscode.workspace.getConfiguration("folio");
}

function workspaceRoot() {
	const folders = vscode.workspace.workspaceFolders;
	return folders && folders.length > 0 ? folders[0].uri.fsPath : null;
}

/**
 * Where uipreview is.
 *
 * Looked for rather than required, because the common case is a game with the plugin under Plugins/
 * and asking for a path there would be a setting nobody should have to find.
 */
function findUipreview() {
	const configured = config().get("uipreviewPath");
	if (configured) {
		return configured;
	}

	const root = workspaceRoot();
	if (!root) {
		return null;
	}

	const candidates = [
		path.join(root, "Plugins", "Folio", "bin", "win64", "uipreview.exe"),
		path.join(root, "bin", "win64", "uipreview.exe"),
		path.join(root, "Plugins", "Folio", "bin", "linux", "uipreview"),
		path.join(root, "bin", "linux", "uipreview"),
	];

	return candidates.find((candidate) => fs.existsSync(candidate)) || null;
}

function rootArgument() {
	return config().get("root") || workspaceRoot();
}

function buildArguments(document, output) {
	const settings = config();
	const args = [document, "--root", rootArgument(), "--screenshot", output];

	const size = settings.get("size");
	if (size) {
		args.push("--size", size);
	}

	// Pinned by default. The preview's ratio otherwise follows its viewport, and an unpinned ratio is
	// the difference that reads as a layout bug rather than a setting.
	const dp = settings.get("dpRatio");
	if (dp) {
		args.push("--dp-ratio", String(dp));
	}

	if (settings.get("strict")) {
		args.push("--strict");
	}

	const culture = settings.get("culture");
	if (culture) {
		args.push("--culture", culture);
	}

	for (const theme of settings.get("themes") || []) {
		args.push("--theme", theme);
	}

	return args;
}

function uncomparedFeatures(document) {
	try {
		const text = fs.readFileSync(document, "utf8");
		return UNCOMPARED.filter((feature) => text.includes(feature));
	} catch {
		return [];
	}
}

function render() {
	if (!panel || !currentDocument) {
		return;
	}

	// One at a time, with the last request kept. Saving repeatedly while a render is in flight would
	// otherwise queue a process per keystroke-and-save.
	if (rendering) {
		pending = true;
		return;
	}

	const uipreview = findUipreview();
	if (!uipreview) {
		show({ error: "Could not find uipreview. Build it with ./scripts/build.sh, or set folio.uipreviewPath." });
		return;
	}

	const output = path.join(os.tmpdir(), `folio-preview-${process.pid}-${++renderIndex}.png`);
	const args = buildArguments(currentDocument, output);

	rendering = true;
	cp.execFile(uipreview, args, { timeout: 30000 }, (error, stdout, stderr) => {
		rendering = false;

		const messages = [stdout, stderr].filter(Boolean).join("").trim();

		if (!fs.existsSync(output)) {
			show({
				error: messages || (error ? String(error) : "uipreview wrote no image"),
			});
		} else {
			show({
				image: output,
				messages,
				// A non-zero exit under --strict means a warning, which is the point of --strict. The
				// image is still worth showing; the warnings are shown beside it rather than instead.
				warned: Boolean(error),
				uncompared: uncomparedFeatures(currentDocument),
			});
		}

		if (pending) {
			pending = false;
			render();
		}
	});
}

function escape(text) {
	return String(text).replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" })[c]);
}

function show(state) {
	if (!panel) {
		return;
	}

	const name = currentDocument ? path.basename(currentDocument) : "";
	const source = state.image
		? panel.webview.asWebviewUri(vscode.Uri.file(state.image)).toString() + `?v=${renderIndex}`
		: null;

	const notes = [];
	if (state.uncompared && state.uncompared.length > 0) {
		notes.push(
			`<div class="note">This document uses ${escape(state.uncompared.join(", "))}. ` +
			`Layout here matches the engine exactly; the <em>appearance</em> of layer effects is not ` +
			`compared between the two renderers, so treat it as close rather than certain.</div>`);
	}

	panel.title = `Folio: ${name}`;
	panel.webview.html = `<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8"/>
<style>
	body {
		margin: 0;
		padding: 12px;
		font-family: var(--vscode-font-family);
		font-size: var(--vscode-font-size);
		color: var(--vscode-foreground);
		background: var(--vscode-editor-background);
	}
	img { max-width: 100%; display: block; image-rendering: auto; }
	.frame { background: repeating-conic-gradient(#00000022 0% 25%, transparent 0% 50%) 50% / 16px 16px; }
	.bar { display: flex; gap: 12px; align-items: baseline; margin-bottom: 8px; opacity: 0.8; }
	.note, .error, .warn {
		margin: 8px 0; padding: 8px 10px; border-left: 3px solid;
		white-space: pre-wrap; font-family: var(--vscode-editor-font-family);
	}
	.note  { border-color: var(--vscode-editorInfo-foreground);    background: #8884; }
	.warn  { border-color: var(--vscode-editorWarning-foreground); background: #fc04; }
	.error { border-color: var(--vscode-editorError-foreground);   background: #f004; }
</style>
</head>
<body>
	<div class="bar"><strong>${escape(name)}</strong><span>${escape(config().get("size") || "")}</span></div>
	${state.error ? `<div class="error">${escape(state.error)}</div>` : ""}
	${notes.join("")}
	${state.messages && !state.error
		? `<div class="${state.warned ? "warn" : "note"}">${escape(state.messages)}</div>`
		: ""}
	${source ? `<div class="frame"><img src="${source}"/></div>` : ""}
</body>
</html>`;
}

function openPreview() {
	const editor = vscode.window.activeTextEditor;
	if (editor && editor.document.fileName.endsWith(".rml")) {
		currentDocument = editor.document.fileName;
	}

	if (!currentDocument) {
		vscode.window.showErrorMessage("Folio: open an .rml document first.");
		return;
	}

	if (panel) {
		panel.reveal(vscode.ViewColumn.Beside);
	} else {
		panel = vscode.window.createWebviewPanel(
			"folioPreview", "Folio", vscode.ViewColumn.Beside,
			{ enableScripts: false, localResourceRoots: [vscode.Uri.file(os.tmpdir())] });

		panel.onDidDispose(() => { panel = null; });
	}

	render();
}

function activate(context) {
	context.subscriptions.push(
		vscode.commands.registerCommand("folio.openPreview", openPreview),
		vscode.commands.registerCommand("folio.refreshPreview", render));

	// A stylesheet change matters as much as a markup one, and a document links several. So any of
	// them re-renders whatever is open rather than only its own file.
	context.subscriptions.push(vscode.workspace.onDidSaveTextDocument((saved) => {
		if (!panel) {
			return;
		}

		if (/\.(rml|rcss)$/.test(saved.fileName)) {
			render();
		}
	}));

	// Following the active editor, so flicking between documents previews the one being looked at.
	context.subscriptions.push(vscode.window.onDidChangeActiveTextEditor((editor) => {
		if (panel && editor && editor.document.fileName.endsWith(".rml")
			&& editor.document.fileName !== currentDocument) {
			currentDocument = editor.document.fileName;
			render();
		}
	}));
}

function deactivate() {}

module.exports = { activate, deactivate };
