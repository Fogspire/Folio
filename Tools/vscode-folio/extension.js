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
const rcss = require("./rcss");

/** Properties whose appearance the harness does not compare. Worth saying so on the document. */
const UNCOMPARED = ["box-shadow", "filter:", "backdrop-filter", "mask-image"];

let panel = null;
let currentDocument = null;

/**
 * Zoom is a re-render, not a magnification.
 *
 * Scaling the PNG would throw away the one thing Folio exists for: glyphs and vector art are distance
 * fields, so they are sharp at any size, and an upscaled screenshot shows none of that. Zooming in on
 * a blurry enlargement tells you nothing about whether the real thing is crisp.
 *
 * So a zoom scales the dp ratio and leaves the render size alone, which is what a browser's zoom does:
 * the viewport stays as configured and the content within it grows, so you see less of the document
 * at more detail. Every step is a fresh render, so text sharpens as it grows rather than blurring.
 *
 * Scaling the render size instead is the other reading of "zoom", and either would work now that the
 * preview renders offscreen and honours any size. dp was chosen because it is what a browser's zoom
 * does, and because a render size the panel cannot show is a scrollbar rather than a zoom.
 *
 * Held here rather than in the page because the HTML is rebuilt on every render.
 */
let zoom = 1;

/** Steps, so repeated presses land on the same values and 1:1 is always reachable exactly. */
const ZOOM_STEPS = [0.5, 0.67, 0.75, 1, 1.5, 2, 3, 4];
let renderIndex = 0;
let rendering = false;
let pending = false;

/** The last provenance report, which is what makes a click mean an element rather than a pixel. */
let elements = [];
let selected = null;
let lastImage = null;
let lastMessages = "";
let lastUncompared = [];

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

	// Pinned by default, and the zoom rides on it. The preview's ratio otherwise follows its viewport,
	// and an unpinned ratio is the difference that reads as a layout bug rather than a setting.
	const dp = Number(settings.get("dpRatio"));
	if (Number.isFinite(dp) && dp > 0) {
		args.push("--dp-ratio", String(dp * zoom));
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
	const provenance = `${output}.json`;
	const args = buildArguments(currentDocument, output).concat(["--provenance-report", provenance]);

	rendering = true;
	cp.execFile(uipreview, args, { timeout: 30000 }, (error, stdout, stderr) => {
		rendering = false;

		const messages = [stdout, stderr].filter(Boolean).join("").trim();

		if (!fs.existsSync(output)) {
			show({
				error: messages || (error ? String(error) : "uipreview wrote no image"),
			});
		} else {
			// Geometry and rules together, so a click can name an element and say which rule to edit.
			try {
				const report = JSON.parse(fs.readFileSync(provenance, "utf8"));
				elements = report.elements || [];
			} catch {
				elements = [];
			}

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

	if (state.image) {
		lastImage = state.image;
		lastMessages = state.messages || "";
		lastUncompared = state.uncompared || [];
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
	/* Shown at its natural size: the image is already the resolution the zoom asked for, so scaling it
	   here would undo the re-render it came from. */
	img { display: block; }
	#viewport { overflow: auto; }
	.stage { position: relative; display: inline-block; cursor: crosshair; }
	#overlay { position: absolute; left: 0; top: 0; right: 0; bottom: 0; pointer-events: none; }
	/* A match from the source side is deliberately a different colour to a selection: one says "this is
	   what you picked", the other says "this is what that rule reaches", and confusing them would make
	   the second useless. */
	#overlay .box.match {
		outline-color: var(--vscode-editorWarning-foreground);
		background: color-mix(in srgb, var(--vscode-editorWarning-foreground) 12%, transparent);
	}
	#overlay .box {
		position: absolute;
		outline: 1px solid var(--vscode-focusBorder);
		background: color-mix(in srgb, var(--vscode-focusBorder) 12%, transparent);
	}
	.selection { font-family: var(--vscode-editor-font-family); opacity: 0.9; }
	button {
		font: inherit; color: var(--vscode-button-secondaryForeground);
		background: var(--vscode-button-secondaryBackground);
		border: none; padding: 2px 8px; cursor: pointer;
	}
	button:hover { background: var(--vscode-button-secondaryHoverBackground); }
	.spacer { flex: 1; }
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
	<div class="bar">
		<strong>${escape(name)}</strong>
		<span>${escape(config().get("size") || "")}</span>
		<span class="spacer"></span>
		<button id="out" title="Zoom out (Ctrl+-)">&minus;</button>
		<span id="level">${Math.round(zoom * 100)}%</span>
		<button id="in" title="Zoom in: re-renders at a higher resolution (Ctrl++)">+</button>
		<button id="reset" title="Back to 100% (Ctrl+0)">1:1</button>
	</div>
	${state.error ? `<div class="error">${escape(state.error)}</div>` : ""}
	${notes.join("")}
	${selected ? `<div class="selection">${escape(selected.tag)}${selected.id ? "#" + escape(selected.id) : ""}`
		+ `${selected.classes ? "." + escape(String(selected.classes).trim().split(/\s+/).join(".")) : ""}`
		+ ` &mdash; ${(selected.properties || []).length} property/ies from `
		+ `${escape([...new Set((selected.properties || []).map((p) => p.rule))].join(", ")) || "no rule"}</div>`
		: ""}
	${state.messages && !state.error
		? `<div class="${state.warned ? "warn" : "note"}">${escape(state.messages)}</div>`
		: ""}
	${source ? `<div id="viewport" class="frame"><div class="stage"><img id="render" src="${source}"/><div id="overlay"></div></div></div>` : ""}
	<script>
		const api = acquireVsCodeApi();
		const elements = ${JSON.stringify(elements)};
		const selectedPath = ${JSON.stringify(selected && selected.path)};

		const image = document.getElementById("render");
		const overlay = document.getElementById("overlay");

		function ask(zoom) { api.postMessage({ kind: "zoom", zoom: zoom }); }

		document.getElementById("in").onclick = () => ask("in");
		document.getElementById("out").onclick = () => ask("out");
		document.getElementById("reset").onclick = () => ask("reset");

		document.addEventListener("wheel", (event) => {
			if (!event.ctrlKey) { return; }
			event.preventDefault();
			ask(event.deltaY < 0 ? "in" : "out");
		}, { passive: false });

		document.addEventListener("keydown", (event) => {
			if (!event.ctrlKey) { return; }
			if (event.key === "=" || event.key === "+") { ask("in"); }
			else if (event.key === "-") { ask("out"); }
			else if (event.key === "0") { ask("reset"); }
		});

		/**
		 * The smallest element containing the point.
		 *
		 * Smallest rather than last: a document's order is not its depth, and picking the last match
		 * selects a full-width overlay whenever one exists rather than the button under the cursor.
		 */
		function hitTest(x, y) {
			let best = null;
			for (const element of elements) {
				const [left, top, width, height] = element.rect;
				if (x < left || y < top || x > left + width || y > top + height) { continue; }
				if (!best || width * height < best.rect[2] * best.rect[3]) { best = element; }
			}
			return best;
		}

		function documentPoint(event) {
			const box = image.getBoundingClientRect();
			return [event.clientX - box.left, event.clientY - box.top];
		}

		function drawOverlay(element) {
			overlay.innerHTML = "";
			if (!element) { return; }

			const [left, top, width, height] = element.rect;
			const box = document.createElement("div");
			box.className = "box";
			box.style.left = left + "px";
			box.style.top = top + "px";
			box.style.width = width + "px";
			box.style.height = height + "px";
			overlay.appendChild(box);
		}

		window.addEventListener("message", (event) => {
			const message = event.data;
			if (!message || message.kind !== "highlight") { return; }

			overlay.innerHTML = "";
			for (const path of message.paths || []) {
				const element = elements.find((entry) => entry.path === path);
				if (!element) { continue; }

				const [left, top, width, height] = element.rect;
				const box = document.createElement("div");
				box.className = "box match";
				box.style.left = left + "px";
				box.style.top = top + "px";
				box.style.width = width + "px";
				box.style.height = height + "px";
				overlay.appendChild(box);
			}
		});

		if (image) {
			let current = elements.find((element) => element.path === selectedPath) || null;
			drawOverlay(current);

			image.parentElement.addEventListener("mousedown", (event) => {
				const [x, y] = documentPoint(event);
				const hit = hitTest(x, y);
				if (!hit) { return; }

				current = hit;
				drawOverlay(current);
				api.postMessage({ kind: "select", path: hit.path });

				// A drag from inside the selection moves it; the mouse has to leave the element before
				// it counts, so a click to select never nudges anything.
				const startX = x, startY = y;
				let dragging = false;

				function move(moveEvent) {
					const [nx, ny] = documentPoint(moveEvent);
					if (!dragging && Math.abs(nx - startX) < 4 && Math.abs(ny - startY) < 4) { return; }

					dragging = true;
					const [left, top, width, height] = hit.rect;
					const box = overlay.firstChild;
					if (box) {
						box.style.left = (left + nx - startX) + "px";
						box.style.top = (top + ny - startY) + "px";
					}
				}

				function up(upEvent) {
					document.removeEventListener("mousemove", move);
					document.removeEventListener("mouseup", up);
					if (!dragging) { return; }

					const [nx, ny] = documentPoint(upEvent);
					api.postMessage({
						kind: "move", path: hit.path,
						dx: Math.round(nx - startX), dy: Math.round(ny - startY),
					});
				}

				document.addEventListener("mousemove", move);
				document.addEventListener("mouseup", up);
			});
		}
	</script>
</body>
</html>`;
}

/**
 * Which rule an edit should land on, and the alternatives.
 *
 * A rule is usually the right target rather than an element: moving one button should be able to move
 * every button. But not always, and the brief is firm that the tool must not silently choose - so the
 * rule that already sets the property is offered first, the element's own id rule next, and the
 * author picks. Silently choosing either is wrong half the time.
 */
function targetsFor(element, property) {
	const targets = [];

	for (const declaration of element.properties || []) {
		if (declaration.name === property) {
			targets.push({
				label: `Rule ${declaration.rule}`,
				detail: `${declaration.file}:${declaration.line} - already sets ${property}: ${declaration.value}`,
				rule: declaration.rule, file: declaration.file, line: declaration.line,
			});
		}
	}

	// Rules that style this element but do not set this property yet: adding it there is the next most
	// likely intent, and the most specific one is offered first.
	for (const declaration of element.properties || []) {
		if (declaration.name !== property && !targets.some((t) => t.rule === declaration.rule)) {
			targets.push({
				label: `Rule ${declaration.rule}`,
				detail: `${declaration.file}:${declaration.line} - would gain ${property}`,
				rule: declaration.rule, file: declaration.file, line: declaration.line,
			});
		}
	}

	return targets;
}

function resolveStylesheet(file) {
	const root = rootArgument();
	if (!root) {
		return null;
	}

	// The report's paths are as RmlUi resolved them, so /folio/ is the plugin's rcss directory.
	const relative = String(file).replace(/^\/?folio\//, "Plugins/Folio/rcss/").replace(/^\//, "");
	const candidate = path.isAbsolute(relative) ? relative : path.join(root, relative);
	return fs.existsSync(candidate) ? candidate : null;
}

/** Applies one property to one rule, saves, and lets the save trigger the re-render. */
async function applyEdit(target, property, value) {
	const file = resolveStylesheet(target.file);
	if (!file) {
		vscode.window.showErrorMessage(`Folio: could not find ${target.file} under the workspace.`);
		return;
	}

	const before = fs.readFileSync(file, "utf8");
	const result = rcss.setProperty(before, target.rule, property, value);
	if (result.error) {
		vscode.window.showErrorMessage(`Folio: ${result.error}`);
		return;
	}

	// Through the workspace edit API rather than writing the file, so it lands in the editor's undo
	// stack. An edit a person cannot undo with ctrl+z is not one they will trust.
	const uri = vscode.Uri.file(file);
	const document = await vscode.workspace.openTextDocument(uri);
	const edit = new vscode.WorkspaceEdit();
	edit.replace(uri, new vscode.Range(
		document.positionAt(0), document.positionAt(before.length)), result.text);

	await vscode.workspace.applyEdit(edit);
	await document.save();

	vscode.window.setStatusBarMessage(
		`Folio: ${result.change.kind} ${property} on ${result.change.selector}`, 4000);
}

async function handleMessage(message) {
	if (!message) {
		return;
	}

	if (message.kind === "zoom") {
		const index = ZOOM_STEPS.indexOf(zoom);
		if (message.zoom === "in") {
			zoom = ZOOM_STEPS[Math.min(ZOOM_STEPS.length - 1, (index === -1 ? 3 : index) + 1)];
		} else if (message.zoom === "out") {
			zoom = ZOOM_STEPS[Math.max(0, (index === -1 ? 3 : index) - 1)];
		} else {
			zoom = 1;
		}

		render();
		return;
	}

	const element = elements.find((entry) => entry.path === message.path);
	if (!element) {
		return;
	}

	if (message.kind === "select") {
		selected = element;
		revealSource(element);
		show({ image: lastImage, messages: lastMessages, uncompared: lastUncompared });
		return;
	}

	if (message.kind === "move") {
		await moveElement(element, message.dx, message.dy);
	}
}

/** Click in the render, land on the rule in the source. Half of what makes this an editor. */
async function revealSource(element) {
	const first = (element.properties || [])[0];
	if (!first) {
		return;
	}

	const file = resolveStylesheet(first.file);
	if (!file) {
		return;
	}

	const document = await vscode.workspace.openTextDocument(vscode.Uri.file(file));
	const line = Math.max(0, (first.line || 1) - 1);
	await vscode.window.showTextDocument(document, {
		preserveFocus: true,
		selection: new vscode.Range(line, 0, line, 0),
	});
}

async function moveElement(element, dx, dy) {
	if (dx === 0 && dy === 0) {
		return;
	}

	const targets = targetsFor(element, "left");
	if (targets.length === 0) {
		vscode.window.showWarningMessage(
			"Folio: nothing styles this element, so there is no rule to move it on. Give it a rule first.");
		return;
	}

	const picked = targets.length === 1 ? targets[0] : await vscode.window.showQuickPick(
		targets.map((target) => ({ label: target.label, detail: target.detail, target })),
		{ title: `Move ${element.tag}${element.id ? "#" + element.id : ""} by editing which rule?` })
		.then((choice) => choice && choice.target);

	if (!picked) {
		return;
	}

	// The element's laid-out position plus the drag, which is what the author saw happen. Read from the
	// box rather than from the rule, so a rule with no left yet still lands somewhere sensible.
	const left = Math.round(element.rect[0] + dx);
	const top = Math.round(element.rect[1] + dy);

	await applyEdit(picked, "left", `${left}px`);
	await applyEdit(picked, "top", `${top}px`);
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
		panel.reveal(vscode.ViewColumn.Beside, /*preserveFocus*/ true);
	} else {
		// preserveFocus matters more than it looks: without it the preview's column becomes the active
		// one, and VSCode then opens every file you click into it, on top of the preview.
		panel = vscode.window.createWebviewPanel(
			"folioPreview", "Folio",
			{ viewColumn: vscode.ViewColumn.Beside, preserveFocus: true },
			{ enableScripts: true, retainContextWhenHidden: true,
				localResourceRoots: [vscode.Uri.file(os.tmpdir())] });

		panel.webview.onDidReceiveMessage(handleMessage);

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

	/*
	 * The other direction: put the caret in a rule and the elements it styles light up.
	 *
	 * Falls out of provenance - every element already carries the rules that styled it - and it is
	 * most of what makes the tool feel like an editor rather than a viewer, because it answers "what
	 * does this rule actually reach" without reading the markup.
	 */
	context.subscriptions.push(vscode.window.onDidChangeTextEditorSelection((event) => {
		if (!panel || !event.textEditor.document.fileName.endsWith(".rcss")) {
			return;
		}

		const line = event.selections[0].active.line + 1;
		const rules = rcss.listRules(event.textEditor.document.getText());

		// The last rule beginning at or before the caret is the one it is inside.
		let inside = null;
		for (const rule of rules) {
			if (rule.line <= line && (!inside || rule.line > inside.line)) {
				inside = rule;
			}
		}

		if (!inside) {
			return;
		}

		const matched = elements.filter((element) =>
			(element.properties || []).some((declaration) => declaration.rule === inside.selector));

		panel.webview.postMessage({ kind: "highlight", paths: matched.map((element) => element.path) });
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
