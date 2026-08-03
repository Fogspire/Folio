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

/**
 * The full-screen layer warning, pulled back out of uipreview's own output.
 *
 * Reported rather than re-derived: uipreview has the laid-out boxes and this does not, and a second
 * implementation of the same rule is a second thing to get wrong.
 */
const FULLSCREEN_LAYER = /\[W-PERF-FULLSCREENLAYER\]\s+(\S+?):\s+([^]*?)\s+on an element covering\s+(\d+)%/g;

function fullScreenLayers(messages) {
	const out = [];
	if (!messages) {
		return out;
	}

	FULLSCREEN_LAYER.lastIndex = 0;
	let match = FULLSCREEN_LAYER.exec(messages);
	while (match) {
		out.push({ path: match[1], properties: match[2], coverage: Number(match[3]) });
		match = FULLSCREEN_LAYER.exec(messages);
	}
	return out;
}

/** Properties whose appearance the harness does not compare. Worth saying so on the document. */
const UNCOMPARED = ["box-shadow", "filter:", "backdrop-filter", "mask-image"];

let panel = null;
/** Set by activate, so a reload can reopen on whatever was last previewed. */
let rememberDocument = () => {};
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

/*
 * The timeline.
 *
 * A render answers what a document settles into, which is the wrong question about motion: an entry
 * animation has finished before the first capture. So with the timeline open the same run also writes
 * a frame per step from the document's first, and the panel plays them.
 *
 * Off by default because it costs a frame's render each: a strip is seconds where a still is
 * fractions of one, and most of the time nobody is looking at an animation.
 */
let timeline = false;
/** Absolute paths, in order. Empty until a strip has been rendered. */
let frames = [];
let frameStep = 0;
let stripIndex = 0;
/** Kept until the panel is showing the new strip, so the frames on screen stay loadable. */
let previousStrip = null;

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

function buildArguments(document, capture) {
	const settings = config();
	const args = [document, "--root", rootArgument()].concat(capture);

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

/**
 * Renders one element to a PNG with alpha, and offers to swap the rule over to it.
 *
 * The reason this is a button and not a note: the expensive thing and its replacement are the same
 * markup, so producing the texture needs nothing from the author but a filename. A path is prompted
 * for rather than derived, since where a game keeps its art is not something the extension knows.
 */
async function bakeElement(selector) {
	const document = currentDocument;
	if (!document) {
		vscode.window.showErrorMessage("Folio: open an .rml document first.");
		return;
	}

	const chosen = selector || await vscode.window.showInputBox({
		title: "Folio: bake an element",
		prompt: "A selector for the element to render, as RCSS would write it",
		placeHolder: "#scorch",
	});

	if (!chosen) {
		return;
	}

	const uipreview = findUipreview();
	if (!uipreview) {
		vscode.window.showErrorMessage(
			"Folio: could not find uipreview. Build it with ./scripts/build.sh, or set folio.uipreviewPath.");
		return;
	}

	// The id makes a better filename than the selector does, and a selector that is not an id has no
	// obvious name to suggest at all.
	const suggested = chosen.startsWith("#") ? chosen.slice(1).replace(/[^\w.-]/g, "_") : "baked";
	const target = await vscode.window.showSaveDialog({
		title: "Folio: bake to",
		defaultUri: vscode.Uri.file(path.join(rootArgument(), "UI", "art", suggested + ".png")),
		filters: { "PNG image": ["png"] },
	});

	if (!target) {
		return;
	}

	const settings = config();
	const args = [document, "--root", rootArgument(), "--bake", chosen + "=" + target.fsPath];

	// The document's own size, not the preview's zoomed one: a texture wants the resolution the
	// element is drawn at, and the zoom is for looking.
	const size = settings.get("size");
	if (size) {
		args.push("--size", size);
	}

	const dp = Number(settings.get("dpRatio"));
	if (Number.isFinite(dp) && dp > 0) {
		args.push("--dp-ratio", String(dp));
	}

	await new Promise((resolve) => {
		cp.execFile(uipreview, args, { timeout: 60000 }, (error, stdout, stderr) => {
			const messages = [stdout, stderr].filter(Boolean).join("").trim();

			if (!fs.existsSync(target.fsPath)) {
				vscode.window.showErrorMessage(
					"Folio: nothing was baked. " + (messages || String(error || "")));
			} else {
				vscode.window.showInformationMessage(
					"Folio: baked " + chosen + " to " + vscode.workspace.asRelativePath(target) + ". Draw it "
					+ "with decorator: image(<path> fill), and drop the layer property it replaces.");
			}
			resolve();
		});
	});
}

/**
 * Opens the document in the standalone renderer, in its own window.
 *
 * The preview here is the same binary rendering to a PNG, which is right for looking and wrong for
 * anything that needs to run: real keyboard and gamepad input, the RmlUi debugger on F8, an animation
 * at its own frame rate rather than the sampled strip the timeline plays. That is what this is for.
 */
function openInUipreview() {
	const editor = vscode.window.activeTextEditor;
	const document = editor && editor.document.fileName.endsWith(".rml")
		? editor.document.fileName
		: currentDocument;

	if (!document) {
		vscode.window.showErrorMessage("Folio: open an .rml document first.");
		return;
	}

	const uipreview = findUipreview();
	if (!uipreview) {
		vscode.window.showErrorMessage(
			"Folio: could not find uipreview. Build it with ./scripts/build.sh, or set folio.uipreviewPath.");
		return;
	}

	const settings = config();
	const args = [document, "--root", rootArgument()];

	const size = settings.get("size");
	if (size) {
		args.push("--size", size);
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

	// Detached, with no pipes held open: the window is the user's now, and it outlives the editor
	// closing. Its own file watcher reloads it on save, so nothing here has to follow the document.
	const child = cp.spawn(uipreview, args, { detached: true, stdio: "ignore" });
	child.on("error", (error) => {
		vscode.window.showErrorMessage(`Folio: could not start uipreview. ${error.message}`);
	});
	child.unref();
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
	const capture = ["--screenshot", output, "--provenance-report", provenance];

	// One run gives both. With a strip the screenshot is its last frame and the provenance describes
	// that frame, so the panel opens on a picture a click still means something on - two runs would
	// be two renders of two different instants, and the boxes would belong to neither.
	const shape = timelineShape();
	const strip = timeline
		? path.join(os.tmpdir(), `folio-filmstrip-${process.pid}-${++stripIndex}`)
		: null;

	if (strip) {
		capture.push("--filmstrip", strip,
			"--filmstrip-frames", String(shape.count), "--dt", String(shape.step));
	}

	const args = buildArguments(currentDocument, capture);

	rendering = true;
	// A strip is a render per frame, so it needs the time a still does not.
	const limit = strip ? 30000 + shape.count * 2000 : 30000;
	cp.execFile(uipreview, args, { timeout: limit }, (error, stdout, stderr) => {
		rendering = false;

		const messages = [stdout, stderr].filter(Boolean).join("").trim();

		if (!fs.existsSync(output)) {
			// Whatever the failed run managed to write is not a strip anyone can play, and a document
			// that fails to parse on every save would otherwise leave one behind each time.
			discardStrip(strip);
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

			frames = readStrip(strip);
			frameStep = shape.step;

			show({
				image: output,
				messages,
				// A non-zero exit under --strict means a warning, which is the point of --strict. The
				// image is still worth showing; the warnings are shown beside it rather than instead.
				warned: Boolean(error),
				uncompared: uncomparedFeatures(currentDocument),
				layers: fullScreenLayers(messages),
			});

			// Now that the panel is showing the new strip, and not before: the old frames are what was
			// on screen until this render landed.
			discardStrip(previousStrip);
			previousStrip = strip;
		}

		if (pending) {
			pending = false;
			render();
		}
	});
}

/** How long a strip runs for and how finely it samples, as a frame count and a step in milliseconds. */
function timelineShape() {
	const settings = config();
	const fps = Math.min(120, Math.max(1, Number(settings.get("timelineFps")) || 30));
	const seconds = Math.min(30, Math.max(0.1, Number(settings.get("timelineDuration")) || 2));

	return { count: Math.min(3600, Math.max(2, Math.round(seconds * fps))), step: 1000 / fps };
}

/** The frames a run wrote, read from the manifest rather than guessed, so a partial run shows none. */
function readStrip(directory) {
	if (!directory) {
		return [];
	}

	try {
		const manifest = JSON.parse(fs.readFileSync(path.join(directory, "filmstrip.json"), "utf8"));
		return (manifest.frames || []).map((name) => path.join(directory, name));
	} catch {
		return [];
	}
}

function discardStrip(directory) {
	if (directory) {
		fs.rmSync(directory, { recursive: true, force: true });
	}
}

/**
 * Opens and closes the timeline.
 *
 * Closing re-renders as well as opening: the panel would otherwise keep showing whichever frame the
 * scrubber was left on, which is not the still the rest of the tool talks about.
 */
function toggleTimeline() {
	timeline = !timeline;

	if (!timeline) {
		frames = [];
		discardStrip(previousStrip);
		previousStrip = null;
	}

	// Asking for the timeline with nothing open is asking for the preview as well.
	if (panel) {
		render();
	} else {
		openPreview();
	}
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

	const strip = frames.map((frame) => panel.webview.asWebviewUri(vscode.Uri.file(frame)).toString());
	const duration = ((strip.length * frameStep) / 1000).toFixed(2);

	// Collapsed when there is enough of it to bury what is under it, which is the common case on a
	// document that is mid-edit.
	const messageLines = state.messages ? state.messages.split(String.fromCharCode(10)).length : 0;

	const notes = [];
	for (const layer of state.layers || []) {
		notes.push(
			`<div class="note warn">` +
			`<strong>${escape(layer.path)}</strong> covers ${layer.coverage}% of the viewport and uses ` +
			`${escape(layer.properties)}, so it costs a full-screen render target every frame the ` +
			`document draws. Bake it and draw the texture instead. ` +
			`<button class="bake" data-path="${escape(layer.path)}">Bake to PNG</button></div>`);
	}

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
	#overlay .box { position: absolute; }

	/* Three states, three colours, none of them relying on color-mix: hover says what a click would
	   take, selected says what it took, and match says what a rule in the source reaches. Confusing
	   any two of them would make the other useless. */
	#overlay .box.hover    { outline: 1px dashed #5ac8fa; background: #5ac8fa22; }
	#overlay .box.selected { outline: 2px solid  #ffb454; background: #ffb45422; }
	#overlay .box.match    { outline: 2px solid  #a0f060; background: #a0f06022; }
	#overlay .box.outline-only { background: none; }

	/* Every box at once: an outline faint enough to read the render through, because the point is to
	   see where the boxes are, not to replace the document with a wireframe. No tint and no label -
	   a hundred labels overlapping tells you nothing. */
	#overlay .box.all { outline: 1px solid #ffffff2e; background: none; }
	#overlay .box.all .label { display: none; }

	#overlay .label {
		position: absolute; left: 0; top: -18px; white-space: nowrap;
		padding: 0 4px; font-size: 11px; line-height: 17px;
		font-family: var(--vscode-editor-font-family);
		color: #101014; background: #5ac8fa;
	}
	#overlay .box.selected .label { background: #ffb454; }
	#overlay .box.match .label { background: #a0f060; }
	.selection {
		display: flex; flex-wrap: wrap; gap: 6px; align-items: center;
		margin-bottom: 8px; min-height: 22px;
		font-family: var(--vscode-editor-font-family); font-size: 90%;
	}
	button.on { color: var(--vscode-button-foreground); background: var(--vscode-button-background); }
	#readout { font-weight: bold; }
	.rule {
		font: inherit; padding: 1px 6px; cursor: pointer;
		color: var(--vscode-textLink-foreground);
		background: var(--vscode-badge-background); border: none;
	}
	.rule:hover { text-decoration: underline; }
	.rule.empty { color: var(--vscode-descriptionForeground); cursor: default; background: none; }
	.rule.markup { color: var(--vscode-charts-orange, var(--vscode-textLink-foreground)); }
	button {
		font: inherit; color: var(--vscode-button-secondaryForeground);
		background: var(--vscode-button-secondaryBackground);
		border: none; padding: 2px 8px; cursor: pointer;
	}
	button:hover { background: var(--vscode-button-secondaryHoverBackground); }
	.spacer { flex: 1; }
	.frame { background: repeating-conic-gradient(#00000022 0% 25%, transparent 0% 50%) 50% / 16px 16px; }
	.transport {
		display: flex; gap: 10px; align-items: center; margin-top: 8px;
		font-family: var(--vscode-editor-font-family); font-size: 90%;
	}
	.transport #scrub { flex: 1; min-width: 120px; }
	.transport #clock { min-width: 96px; text-align: right; }
	#play { min-width: 32px; }
	.bar { display: flex; gap: 12px; align-items: baseline; margin-bottom: 8px; opacity: 0.8; }
	.note, .error, .warn {
		margin: 8px 0; padding: 8px 10px; border-left: 3px solid;
		white-space: pre-wrap; font-family: var(--vscode-editor-font-family);
	}
	/* The harness output folds from either end: a long run of warnings otherwise pushes the render off
	   the panel, and the way out was at the bottom of it. */
	details.note, details.warn { padding: 6px 10px; }
	details summary { cursor: pointer; user-select: none; opacity: 0.85; }
	details[open] summary { margin-bottom: 6px; }
	details pre { margin: 0; white-space: pre-wrap; font-family: var(--vscode-editor-font-family); }
	.fold {
		margin-top: 6px;
		background: none;
		border: 1px solid var(--vscode-foreground);
		color: var(--vscode-foreground);
		opacity: 0.7;
		cursor: pointer;
	}

	button.bake {
		margin-left: 6px;
		background: var(--vscode-button-background);
		color: var(--vscode-button-foreground);
		border: none;
		padding: 2px 10px;
		cursor: pointer;
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
		<button id="boxes" title="Outline every element, so a click on a busy document is aimed rather than hunted">boxes</button>
		<button id="timeline" class="${timeline ? "on" : ""}" title="Render a frame per step from the document's first, and play them. Costs a render per frame">timeline</button>
	</div>
	${state.error ? `<div class="error">${escape(state.error)}</div>` : ""}
	${notes.join("")}
	${state.messages && !state.error ? `
	<details id="harness" class="${state.warned ? "warn" : "note"}"${messageLines > 12 ? "" : " open"}>
		<summary>${messageLines} message${messageLines === 1 ? "" : "s"} from the harness</summary>
		<pre>${escape(state.messages)}</pre>
		<button class="fold" id="fold">collapse</button>
	</details>` : ""}
	<div class="selection"><span id="readout"></span><span id="rules"></span></div>
	${source ? `<div id="viewport" class="frame"><div class="stage"><img id="render" src="${source}" draggable="false"/><div id="overlay"></div></div></div>` : ""}
	${source && strip.length > 0 ? `
	<div class="transport">
		<button id="play" title="Play the animation from the start, looping">&#9654;</button>
		<input id="scrub" type="range" min="0" max="${strip.length - 1}" value="${strip.length - 1}"
			title="Simulated time. Elements can only be picked at the end of the strip, since that is the frame the rules were reported for"/>
		<span id="clock">${duration}s / ${duration}s</span>
	</div>` : ""}
	<script>
		const api = acquireVsCodeApi();
		const elements = ${JSON.stringify(elements)};
		const strip = ${JSON.stringify(strip)};
		const frameStep = ${frameStep};

		// The last frame is what the still is, so opening the timeline changes nothing until it is
		// played - and it is the frame the element boxes belong to.
		let frame = strip.length - 1;
		/** Whether the picture on screen is the one the provenance report describes. */
		function picking() { return strip.length === 0 || frame === strip.length - 1; }

		const image = document.getElementById("render");
		const viewport = document.getElementById("viewport");
		const harness = document.getElementById("harness");
		const fold = document.getElementById("fold");

		for (const button of document.querySelectorAll("button.bake")) {
			button.addEventListener("click", () => api.postMessage({ kind: "bake", path: button.dataset.path }));
		}

		if (fold && harness) {
			fold.addEventListener("click", () => {
				harness.open = false;
				harness.scrollIntoView({ block: "nearest" });
			});
		}

		// A drag on an image is a file drag in a webview, which is what makes a middle drag move the
		// picture instead of the view.
		if (image) {
			image.addEventListener("dragstart", (event) => event.preventDefault());
		}
		const overlay = document.getElementById("overlay");
		const rules = document.getElementById("rules");
		const readout = document.getElementById("readout");

		function ask(zoom) { api.postMessage({ kind: "zoom", zoom: zoom }); }

		// Kept in the webview's own state rather than the extension's: a zoom rebuilds this page, and
		// losing the outlines every time you zoomed would make the toggle useless on the documents it
		// exists for.
		let showAll = Boolean((api.getState() || {}).showAll);

		const boxesButton = document.getElementById("boxes");
		boxesButton.classList.toggle("on", showAll);
		boxesButton.onclick = () => {
			showAll = !showAll;
			boxesButton.classList.toggle("on", showAll);
			api.setState({ showAll: showAll });
			if (overlay) { redraw(); }
		};

		document.getElementById("timeline").onclick = () => api.postMessage({ kind: "timeline" });

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

		function describe(element) {
			const classes = String(element.classes || "").trim();
			return element.tag
				+ (element.id ? "#" + element.id : "")
				+ (classes ? "." + classes.split(/\s+/).join(".") : "");
		}

		/**
		 * The smallest element containing the point.
		 *
		 * Smallest rather than topmost: a document's order is not its depth, so picking the last match
		 * selects whichever full-width overlay happens to exist rather than the button under the cursor.
		 */
		function candidatesAt(x, y) {
			return elements
				.filter((element) => {
					const [left, top, width, height] = element.rect;
					return x >= left && y >= top && x <= left + width && y <= top + height;
				})
				.sort((a, b) => (a.rect[2] * a.rect[3]) - (b.rect[2] * b.rect[3]));
		}

		/**
		 * The smallest element containing the point, or the next one out when cycling.
		 *
		 * Smallest first, because a document's order is not its depth: taking the last match selects
		 * whichever full-width scrim happens to exist rather than the button under the cursor. Alt
		 * steps outwards through everything else under the same point, which is the only way to reach
		 * a container whose children cover it.
		 */
		function hitTest(x, y, depth) {
			const stack = candidatesAt(x, y);
			if (stack.length === 0) { return null; }
			return stack[Math.min(depth || 0, stack.length - 1)];
		}

		/*
		 * Panning. A render bigger than the panel is the normal case at any real zoom, and reaching for
		 * the scrollbars to look at the other corner is the thing that makes the preview tiring.
		 *
		 * Middle drag pans on its own. Space plus any drag pans as well, because that is the gesture
		 * every canvas tool uses and left drag is already picking elements.
		 */
		let spaceHeld = false;
		let panning = null;

		document.addEventListener("keydown", (event) => {
			if (event.code === "Space" && !spaceHeld) {
				spaceHeld = true;
				viewport.style.cursor = "grab";
				// Or the page scrolls a line down under the drag.
				event.preventDefault();
			}
		});

		document.addEventListener("keyup", (event) => {
			if (event.code === "Space") {
				spaceHeld = false;
				if (!panning) { viewport.style.cursor = ""; }
			}
		});

		viewport.addEventListener("mousedown", (event) => {
			const wants = event.button === 1 || (spaceHeld && event.button === 0);
			if (!wants) { return; }

			panning = {
				x: event.clientX,
				y: event.clientY,
				left: viewport.scrollLeft,
				top: viewport.scrollTop,
			};

			viewport.style.cursor = "grabbing";
			event.preventDefault();
			event.stopPropagation();
		});

		window.addEventListener("mousemove", (event) => {
			if (!panning) { return; }
			viewport.scrollLeft = panning.left - (event.clientX - panning.x);
			viewport.scrollTop = panning.top - (event.clientY - panning.y);
			event.preventDefault();
		});

		window.addEventListener("mouseup", () => {
			if (!panning) { return; }
			panning = null;
			viewport.style.cursor = spaceHeld ? "grab" : "";
		});

		// A middle click otherwise pastes on Linux and scroll-locks on Windows.
		viewport.addEventListener("auxclick", (event) => {
			if (event.button === 1) { event.preventDefault(); }
		});

		/**
		 * How many CSS pixels the image is drawn at per pixel it contains.
		 *
		 * One, until it is not: VSCode's own zoom scales CSS pixels, and a panel narrower than the
		 * render can shrink the image. Assuming one put every box a proportional distance away from the
		 * thing it was meant to be on, which reads as the hit test being wrong rather than the drawing.
		 */
		function displayScale() {
			const box = image.getBoundingClientRect();
			return image.naturalWidth > 0 ? box.width / image.naturalWidth : 1;
		}

		/** A point in the render's own pixels, which is the space every rect is in. */
		function documentPoint(event) {
			const box = image.getBoundingClientRect();
			const scale = displayScale() || 1;
			return [(event.clientX - box.left) / scale, (event.clientY - box.top) / scale];
		}

		function box(element, className) {
			const scale = displayScale();
			const [left, top, width, height] = element.rect;

			const node = document.createElement("div");
			node.className = "box " + className;
			node.style.left = (left * scale) + "px";
			node.style.top = (top * scale) + "px";
			node.style.width = (width * scale) + "px";
			node.style.height = (height * scale) + "px";

			// A tint over something that covers the document hides the document. Big things get an
			// outline only, which still says what is selected without making the render unreadable -
			// and a scrim is exactly the element you most often have to select through.
			const area = width * height;
			const whole = image.naturalWidth * image.naturalHeight;
			if (whole > 0 && area > whole * 0.4) {
				node.classList.add("outline-only");
			}

			const label = document.createElement("span");
			label.className = "label";
			label.textContent = describe(element) + "  " + Math.round(width) + "x" + Math.round(height);
			node.appendChild(label);
			return node;
		}

		let selection = null;
		let hovered = null;

		function redraw() {
			overlay.innerHTML = "";

			// A box drawn over a frame the report was not taken from is in the wrong place, and a
			// wrong box is worse than none: mid-animation the elements are where the animation has
			// them, not where they end up.
			if (!picking()) { return; }

			// Everything first, so hover and selection read over the top of it rather than under.
			if (showAll) {
				for (const element of elements) { overlay.appendChild(box(element, "all")); }
			}
			// Hover under selection, so the two never obscure each other when they are the same element.
			if (hovered && hovered !== selection) { overlay.appendChild(box(hovered, "hover")); }
			if (selection) { overlay.appendChild(box(selection, "selected")); }
		}

		function showRules(element) {
			rules.innerHTML = "";
			readout.textContent = element ? describe(element) : "";
			if (!element) { return; }

			// The markup first, because it is what an author clicking a label is usually asking about.
			// "where does DISK UTILITY come from" is answered by the document, and a list of the rules
			// that coloured it is not an answer to that question.
			if (element.text) {
				const markup = document.createElement("button");
				markup.className = "rule markup";
				markup.textContent = '"' + element.text + '"  in markup';
				markup.title = "Open where this text is written";
				markup.onclick = () => api.postMessage({ kind: "markup", path: element.path });
				rules.appendChild(markup);
			}

			const seen = new Set();
			for (const declaration of element.properties || []) {
				const key = declaration.rule + declaration.file + declaration.line;
				if (seen.has(key)) { continue; }
				seen.add(key);

				const item = document.createElement("button");
				item.className = "rule";
				item.textContent = declaration.rule + "  " + declaration.file + ":" + declaration.line;
				item.title = "Open this rule";
				item.onclick = () => api.postMessage({
					kind: "goto", file: declaration.file, line: declaration.line });
				rules.appendChild(item);
			}

			if (seen.size === 0) {
				const none = document.createElement("span");
				none.className = "rule empty";
				none.textContent = "no rule styles this element";
				rules.appendChild(none);
			}
		}

		window.addEventListener("message", (event) => {
			const message = event.data;
			if (!message || message.kind !== "highlight") { return; }

			overlay.innerHTML = "";
			for (const path of message.paths || []) {
				const element = elements.find((entry) => entry.path === path);
				if (element) { overlay.appendChild(box(element, "match")); }
			}
		});

		if (image) {
			const stage = image.parentElement;

			let depth = 0;
			let lastPoint = [0, 0];

			stage.addEventListener("mousemove", (event) => {
				if (!picking()) { return; }
				const [x, y] = documentPoint(event);

				// Moving away resets the cycle, so alt-clicking deep into one element does not leave
				// the next element you point at selected from the inside out.
				if (Math.abs(x - lastPoint[0]) > 3 || Math.abs(y - lastPoint[1]) > 3) {
					depth = 0;
					lastPoint = [x, y];
				}

				const hit = hitTest(x, y, event.altKey ? depth : 0);
				if (hit !== hovered) { hovered = hit; redraw(); }
			});

			stage.addEventListener("mouseleave", () => { hovered = null; redraw(); });

			document.addEventListener("keydown", (event) => {
				if (event.key !== "Escape" || !selection) { return; }

				selection = null;
				redraw();
				showRules(null);
				api.postMessage({ kind: "select", path: null });
			});

			stage.addEventListener("mousedown", (event) => {
				if (!picking()) { return; }
				const [x, y] = documentPoint(event);

				// Alt steps one further out each time, so a stack of overlapping elements is reachable
				// without hunting for a gap between them.
				if (event.altKey) {
					depth += 1;
				} else {
					depth = 0;
				}

				const stack = candidatesAt(x, y);
				const hit = stack.length > 0 ? stack[Math.min(depth, stack.length - 1)] : null;

				// Clicking past everything clears, the way clicking empty canvas does anywhere else.
				// Without it the last element stays selected for the life of the document.
				if (!hit) {
					selection = null;
					redraw();
					showRules(null);
					api.postMessage({ kind: "select", path: null });
					return;
				}

				selection = hit;
				redraw();
				showRules(hit);
				api.postMessage({ kind: "select", path: hit.path });

				// A drag has to leave the element before it counts, so a click to select never nudges.
				const startX = x, startY = y;
				let dragging = false;

				function move(moveEvent) {
					const [nx, ny] = documentPoint(moveEvent);
					if (!dragging && Math.abs(nx - startX) < 4 && Math.abs(ny - startY) < 4) { return; }

					dragging = true;
					const node = overlay.lastChild;
					if (node) {
						node.style.left = (hit.rect[0] + nx - startX) + "px";
						node.style.top = (hit.rect[1] + ny - startY) + "px";
					}
				}

				function up(upEvent) {
					document.removeEventListener("mousemove", move);
					document.removeEventListener("mouseup", up);
					if (!dragging) { redraw(); return; }

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

		/*
		 * The transport.
		 *
		 * Every frame is already a PNG, so scrubbing and playing are an image swap rather than a
		 * render - which is the whole reason the strip is rendered up front. Decoding one on the frame
		 * it is first shown would stutter exactly where the motion is, so they are all decoded now.
		 */
		if (strip.length > 0 && image) {
			for (const source of strip) { new Image().src = source; }

			const playButton = document.getElementById("play");
			const scrub = document.getElementById("scrub");
			const clock = document.getElementById("clock");

			const total = ((strip.length * frameStep) / 1000).toFixed(2);
			let playing = false;
			let startedAt = 0;
			let startedFrame = 0;

			function showFrame(next) {
				const wasPicking = picking();
				frame = Math.max(0, Math.min(strip.length - 1, next));

				image.src = strip[frame];
				scrub.value = String(frame);
				clock.textContent = (((frame + 1) * frameStep) / 1000).toFixed(2) + "s / " + total + "s";

				// Only when it changes: a redraw per frame during playback would rebuild the overlay
				// sixty times a second to draw nothing.
				if (picking() !== wasPicking || picking()) { redraw(); }
			}

			function pause() {
				playing = false;
				playButton.innerHTML = "&#9654;";
			}

			function tick(now) {
				if (!playing) { return; }

				const elapsed = now - startedAt;
				showFrame((startedFrame + Math.floor(elapsed / frameStep)) % strip.length);
				requestAnimationFrame(tick);
			}

			playButton.onclick = () => {
				if (playing) { pause(); return; }

				// From the start when it is sitting at the end, which is where it opens: the alternative
				// is a play button that appears to do nothing.
				if (frame >= strip.length - 1) { showFrame(0); }

				playing = true;
				playButton.innerHTML = "&#10074;&#10074;";
				startedAt = performance.now();
				startedFrame = frame;
				requestAnimationFrame(tick);
			};

			scrub.addEventListener("input", () => {
				pause();
				showFrame(Number(scrub.value));
			});

			// Arrows step a frame. Space is already the pan gesture, so play is the button.
			document.addEventListener("keydown", (event) => {
				if (event.target === scrub) { return; }
				if (event.key === "ArrowLeft") { pause(); showFrame(frame - 1); }
				else if (event.key === "ArrowRight") { pause(); showFrame(frame + 1); }
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

	// The plugin's own stylesheets are never a target. A drag in a game's preview that lands on
	// /folio/folio.rcss edits the shipped reset for every project that has ever linked it, and the
	// change shows up as every document being subtly wrong with nothing in the game to explain it.
	const editable = (declaration) => !/^\/?folio\//.test(String(declaration.file));

	for (const declaration of (element.properties || []).filter(editable)) {
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
	for (const declaration of (element.properties || []).filter(editable)) {
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

	if (message.kind === "timeline") {
		toggleTimeline();
		return;
	}

	if (message.kind === "bake") {
		await bakeElement(message.path);
		return;
	}

	// Before the element lookup: a goto names a file and a line rather than an element, so looking one
	// up first rejected it and this never ran.
	if (message.kind === "goto") {
		await openRule(message.file, message.line);
		return;
	}

	// Before the element lookup, for the same reason goto is: a clear names no element, so looking one
	// up first drops the message and the selection outlives every attempt to dismiss it.
	if (message.kind === "select" && !message.path) {
		selected = null;
		return;
	}

	const element = elements.find((entry) => entry.path === message.path);
	if (!element) {
		return;
	}

	if (message.kind === "select") {
		selected = element;
		return;
	}

	if (message.kind === "markup") {
		await openMarkup(element);
		return;
	}

	if (message.kind === "move") {
		await moveElement(element, message.dx, message.dy);
	}
}

/**
 * Where an element's text is written, in the document being previewed.
 *
 * The text is searched for with its whitespace loosened, because RmlUi hands back what it laid out and
 * markup wraps and indents freely - an exact match would miss a title split over two lines. Bindings
 * resolve before we see them, so a `{{name}}` never matches its own source; the id is the fallback,
 * and saying so is better than jumping somewhere plausible and wrong.
 */
function findInMarkup(source, element) {
	const escape = (value) => value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");

	if (element.text) {
		const pattern = element.text.trim().split(/\s+/).map(escape).join("\\s+");
		const match = new RegExp(pattern).exec(source);
		if (match) {
			return match.index;
		}
	}

	if (element.id) {
		const match = new RegExp(`id\\s*=\\s*["']${escape(element.id)}["']`).exec(source);
		if (match) {
			return match.index;
		}
	}

	return -1;
}

async function openMarkup(element) {
	if (!currentDocument || !fs.existsSync(currentDocument)) {
		return;
	}

	const document = await vscode.workspace.openTextDocument(vscode.Uri.file(currentDocument));
	const offset = findInMarkup(document.getText(), element);

	if (offset === -1) {
		vscode.window.showWarningMessage(
			`Folio: "${element.text}" is not in ${path.basename(currentDocument)} as written - it comes `
			+ "from a data binding, a template, or another document.");
		return;
	}

	const at = document.positionAt(offset);
	await vscode.window.showTextDocument(document, {
		viewColumn: sourceColumn(currentDocument),
		preserveFocus: true,
		selection: new vscode.Range(at, at.translate(0, (element.text || "").length)),
	});
}

/**
 * A column to show source in that is not the preview's.
 *
 * showTextDocument without one opens in the active column, and the active column is often the
 * preview's - so jumping to a rule replaces the preview with the stylesheet, which is the opposite of
 * what a jump is for. An editor already showing the file wins, then the editor showing the document
 * being previewed, then any editor that is not the preview.
 */
function sourceColumn(file) {
	const taken = panel ? panel.viewColumn : undefined;
	const editors = vscode.window.visibleTextEditors.filter((editor) => editor.viewColumn !== taken);

	const showing = editors.find((editor) => editor.document.uri.fsPath === file);
	if (showing) {
		return showing.viewColumn;
	}

	const previewed = editors.find((editor) => editor.document.fileName === currentDocument);
	if (previewed) {
		return previewed.viewColumn;
	}

	return editors.length > 0 ? editors[0].viewColumn : vscode.ViewColumn.One;
}

/**
 * Opens a rule, chosen by the author rather than guessed.
 *
 * Jumping on every click and picking the first rule was worse than not jumping: the first property is
 * whichever the property enumeration happened to reach first, so the destination looked arbitrary
 * because it was. Now the panel lists the rules that reach an element and this opens the one clicked.
 */
async function openRule(reportedFile, line) {
	const file = resolveStylesheet(reportedFile);
	if (!file) {
		vscode.window.showWarningMessage(`Folio: could not find ${reportedFile} under the workspace.`);
		return;
	}

	const document = await vscode.workspace.openTextDocument(vscode.Uri.file(file));
	const at = Math.max(0, (line || 1) - 1);
	await vscode.window.showTextDocument(document, {
		viewColumn: sourceColumn(file),
		preserveFocus: true,
		selection: new vscode.Range(at, 0, at, 0),
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
		rememberDocument(currentDocument);
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

		adoptPanel();
		lockPreviewGroup();
	}

	render();
}

/** Everything a panel needs whether it was just created or restored after a window reload. */
function adoptPanel() {
	panel.webview.onDidReceiveMessage(handleMessage);
	panel.onDidDispose(() => {
		panel = null;
		// A strip is a few megabytes of PNG, which is worth clearing up where a single screenshot is not.
		discardStrip(previousStrip);
		previousStrip = null;
		frames = [];
	});
}

/**
 * Locks the preview's editor group, so nothing else opens into it.
 *
 * preserveFocus stops the preview from *becoming* the active group, which is most of the problem, but
 * not all of it: VSCode remembers the last group a file was opened in, and a document previously
 * opened where the preview now sits keeps going back there - on top of the preview. Locking the group
 * is the mechanism VSCode itself provides for "this pane holds one thing", and it makes the preview
 * behave like the terminal panel rather than like another editor.
 *
 * The command is looked up rather than assumed: it is not part of the extension API contract, and an
 * unlocked group is a mild annoyance where a failed command would be an error on every open.
 *
 * There is no unlock on dispose. Closing the preview closes its group, taking the lock with it, and an
 * unlock aimed at whichever group happened to be active could undo a lock the author set themselves.
 */
async function lockPreviewGroup() {
	const command = "workbench.action.lockEditorGroup";

	if (!vscode.workspace.getConfiguration("folio").get("lockPreviewGroup", true)) {
		return;
	}

	if (!(await vscode.commands.getCommands(true)).includes(command)) {
		return;
	}

	// The command acts on the active group, so the preview has to be it for a moment. Whatever was
	// active is restored straight after, which is why this runs once at creation and not per render.
	const restore = vscode.window.activeTextEditor;

	if (panel) {
		panel.reveal(panel.viewColumn, /*preserveFocus*/ false);
	}

	await vscode.commands.executeCommand(command);

	if (restore) {
		await vscode.window.showTextDocument(restore.document, {
			viewColumn: restore.viewColumn, preview: false });
	}
}

function activate(context) {
	context.subscriptions.push(
		vscode.commands.registerCommand("folio.openPreview", openPreview),
		vscode.commands.registerCommand("folio.refreshPreview", render),
		vscode.commands.registerCommand("folio.toggleTimeline", toggleTimeline),
		vscode.commands.registerCommand("folio.openInUipreview", openInUipreview),
		vscode.commands.registerCommand("folio.bakeElement", () => bakeElement(null)));

	/*
	 * Reopening the preview after a window reload.
	 *
	 * VSCode restores the editor *layout* either way, so the preview's group comes back - locked,
	 * because that is how it was left. Without a serializer the webview inside it does not, and the
	 * result is an empty locked pane that has to be closed by hand before the preview can reopen
	 * beside its document. Registering one turns that into the preview simply still being there.
	 *
	 * The document comes from workspace state rather than the webview's: the panel's own state is a
	 * rendered image and a set of element boxes, all of which a render rebuilds anyway.
	 */
	context.subscriptions.push(vscode.window.registerWebviewPanelSerializer("folioPreview", {
		async deserializeWebviewPanel(restored) {
			panel = restored;
			adoptPanel();

			const remembered = context.workspaceState.get("folio.document");
			if (remembered && fs.existsSync(remembered)) {
				currentDocument = remembered;
				render();
				return;
			}

			// Nothing to show. Closing it is better than leaving a blank pane: the group goes with
			// it, which is the state someone who has not opened a preview expects.
			panel.dispose();
		},
	}));

	rememberDocument = (document) => context.workspaceState.update("folio.document", document);

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
	 * The same thing for changes VSCode did not make.
	 *
	 * A save event only fires for a file this editor saved, so a document edited by a script, an
	 * agent, another editor or a generator left the panel showing something stale with nothing to say
	 * it had. Watching the workspace covers all of them, and mock data and fonts as well.
	 */
	const watcher = vscode.workspace.createFileSystemWatcher("**/*.{rml,rcss,json,png,ttf,otf}");
	let coalesce = null;

	// Build output is not source. Rendering on a change under these re-runs uipreview while something
	// is writing it, which on Windows holds the binary open and fails the link that produced it.
	const IGNORED = /\/(bin|Binaries|Intermediate|Saved|DerivedDataCache|node_modules)\//i;

	const touched = (uri) => {
		if (!panel || (uri && IGNORED.test(uri.fsPath.split("\\").join("/")))) {
			return;
		}

		// A generator can write a dozen files in a row, and one render at the end of that is the
		// answer to all of them.
		clearTimeout(coalesce);
		coalesce = setTimeout(render, 120);
	};

	watcher.onDidChange(touched);
	watcher.onDidCreate(touched);
	watcher.onDidDelete(touched);
	context.subscriptions.push(watcher);

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
			rememberDocument(currentDocument);
			render();
		}
	}));
}

function deactivate() {}

module.exports = { activate, deactivate };
