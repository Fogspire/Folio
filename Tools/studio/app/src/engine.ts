export type Rect = [number, number, number, number];

export interface TreeNode {
	/** Element-child indices from the document down, as "0/2/1". Empty for the document itself. */
	id: string;
	tag: string;
	attributes: Record<string, string>;
	rect: Rect;
	visible: boolean;
	text: string;
	children: TreeNode[];
}

export interface RuleProperty {
	name: string;
	value: string;
	rule: string;
	file: string;
	line: number;
	/** Set by a rule that matched an ancestor, and reached this element by inheritance. */
	inherited: boolean;
}

export interface Inspection {
	id: string;
	tag: string;
	attributes: Record<string, string>;
	boxes: { margin: Rect; border: Rect; padding: Rect; content: Rect };
	rules: RuleProperty[];
	inline: Record<string, string>;
	computed: Record<string, string>;
}

export interface Diagnostic {
	severity: 'info' | 'warning' | 'error';
	code: string;
	file: string;
	line: number;
	message: string;
}

export interface ActivityElement {
	id: string;
	path: string;
	/** Tag, id and classes, which is how a person names it. */
	label: string;
	visible: boolean;
	/** Animations that never end, so the document never idles while the element is shown. */
	forever: string[];
	finite: { name: string; times: number }[];
	/** Properties that transition when they change, or "all". */
	transitions: string[];
	/** The data-* attributes binding it to a model. */
	bindings: string[];
	/** The layer effects it draws, comma separated, or empty. */
	layer: string;
	/** Its share of the viewport, 0 to 1. */
	coverage: number;
}

export interface Activity {
	/** Seconds until RmlUi next wants an update: 0 for every frame, null for not until something changes. */
	next: number | null;
	elements: ActivityElement[];
}

export interface OpenSettings {
	root: string;
	document: string;
	width: number;
	height: number;
	dpRatio?: number;
	themes?: string[];
	culture?: string;
	textScale?: number;
	reducedMotion?: boolean;
	/** The clock's reading as the document loads, which is when its animations start. */
	time?: number;
}

interface NativeEngine {
	open(settings: string): boolean;
	close(): void;
	isOpen(): boolean;
	frame(seconds: number): void;
	resize(width: number, height: number, dpRatio: number): void;
	filesChanged(paths: string): void;
	hitTest(x: number, y: number): string;
	tree(): string;
	inspect(id: string, names: string): string;
	diagnostics(): string;
	activity(): string;
	setInlineProperty(id: string, name: string, value: string): boolean;
	removeInlineProperty(id: string, name: string): void;
	screenshot(path: string): boolean;
	step(seconds: number): void;
	bake(id: string, path: string, margin: number): boolean;
	setTransparent(value: boolean): void;
}

interface EngineModule {
	FS: {
		mkdirTree(path: string): void;
		writeFile(path: string, data: Uint8Array | string): void;
		readFile(path: string): Uint8Array;
		unlink(path: string): void;
		rmdir(path: string): void;
		readdir(path: string): string[];
		isDir(mode: number): boolean;
		analyzePath(path: string): { exists: boolean; object: { mode: number } | null };
	};
	Engine: new () => NativeEngine;
	version(): string;
}

declare function createFolioEngine(options: Record<string, unknown>): Promise<EngineModule>;

const KEY_EVENTS = new Set(['keydown', 'keypress', 'keyup']);

/**
 * The real Folio renderer, compiled to WebAssembly and drawing into one canvas.
 *
 * Everything here is synchronous once booted: a call is a function call, and a frame is drawn by the
 * time it returns.
 */
export class EngineClient {
	private module: EngineModule | null = null;
	private native: NativeEngine | null = null;
	private gated = new Map<EventListener, EventListener>();
	readonly canvas = document.createElement('canvas');
	readonly log: string[] = [];
	/** Whether the document, rather than the editor around it, should receive the keyboard. */
	acceptsKeys: () => boolean = () => false;
	/**
	 * Canvas pixels drawn per document pixel. The renderer is given a larger viewport and a dp ratio
	 * raised to match, which is how the game draws the same document on a larger screen, so a zoomed
	 * canvas stays sharp. Everything in and out of this class stays in document pixels.
	 */
	private scale = 1;
	private size = { width: 1920, height: 1080, dpRatio: 0 };

	/** A document length in canvas pixels. */
	scaled(length: number): number {
		return Math.max(1, Math.round(length * this.scale));
	}

	private get scaleX(): number {
		return this.scaled(this.size.width) / this.size.width;
	}

	private get scaleY(): number {
		return this.scaled(this.size.height) / this.size.height;
	}

	private toDocument(rect: Rect): Rect {
		return [rect[0] / this.scaleX, rect[1] / this.scaleY, rect[2] / this.scaleX, rect[3] / this.scaleY];
	}

	/** Returns whether the renderer was resized, which leaves its layout to be read again. */
	setRenderScale(scale: number): boolean {
		if (scale === this.scale) return false;
		this.scale = scale;
		if (!this.isOpen) return false;
		this.native!.resize(this.scaled(this.size.width), this.scaled(this.size.height), this.size.dpRatio * scale);
		return true;
	}

	async boot(): Promise<void> {
		this.canvas.tabIndex = 0;
		// Loaded as a plain script: it is the compiler's output, served beside the page rather than bundled into it.
		await new Promise<void>((resolve, reject) => {
			const script = document.createElement('script');
			script.src = 'engine/folio_engine.js';
			script.onload = () => resolve();
			script.onerror = () => reject(new Error('the renderer script did not load'));
			document.head.appendChild(script);
		});
		this.module = await createFolioEngine({
			canvas: this.canvas,
			locateFile: (file: string) => 'engine/' + file,
			print: (line: string) => this.record(line),
			printErr: (line: string) => this.record(line),
			preRun: [(module: { ENV: Record<string, string> }) => { module.ENV.FOLIO_ROOT = '/folio'; }],
		});
		this.native = new this.module.Engine();
		// The canvas element's own background is what shows behind the document.
		this.native.setTransparent(true);
	}

	private record(line: string) {
		this.log.push(line);
		if (this.log.length > 500) this.log.shift();
	}

	/**
	 * Runs a call that creates or destroys the renderer's window, gating the key listeners it installs.
	 *
	 * The windowing layer inside the module listens for keys on the page's window and swallows Tab and
	 * Backspace, which would take them from every text field in the editor. Each listener it registers
	 * is swapped for one that only passes keys on while the document is being played.
	 */
	private withGatedKeys<T>(run: () => T): T {
		const add = window.addEventListener;
		const remove = window.removeEventListener;
		const gate = (listener: EventListener) => {
			let wrapped = this.gated.get(listener);
			if (!wrapped) {
				wrapped = (event) => { if (this.acceptsKeys()) listener.call(window, event); };
				this.gated.set(listener, wrapped);
			}
			return wrapped;
		};
		window.addEventListener = ((type: string, listener: EventListener, options?: boolean | AddEventListenerOptions) =>
			add.call(window, type, KEY_EVENTS.has(type) && typeof listener === 'function' ? gate(listener) : listener, options)) as typeof add;
		window.removeEventListener = ((type: string, listener: EventListener, options?: boolean | EventListenerOptions) =>
			remove.call(window, type, KEY_EVENTS.has(type) && typeof listener === 'function' ? gate(listener) : listener, options)) as typeof remove;
		try {
			return run();
		} finally {
			window.addEventListener = add;
			window.removeEventListener = remove;
		}
	}

	get ready(): boolean {
		return this.native !== null;
	}

	get isOpen(): boolean {
		return !!this.native && this.native.isOpen();
	}

	version(): string {
		return this.module ? this.module.version() : '';
	}

	writeFile(path: string, data: Uint8Array | string) {
		const fs = this.module!.FS;
		fs.mkdirTree(path.slice(0, path.lastIndexOf('/')) || '/');
		fs.writeFile(path, data);
	}

	/** Removes a file, or a directory and everything in it. */
	removeFile(path: string) {
		const fs = this.module!.FS;
		const found = fs.analyzePath(path);
		if (!found.exists) return;
		if (!found.object || !fs.isDir(found.object.mode)) return fs.unlink(path);
		for (const entry of fs.readdir(path)) if (entry !== '.' && entry !== '..') this.removeFile(path + '/' + entry);
		fs.rmdir(path);
	}

	readFile(path: string): Uint8Array {
		return this.module!.FS.readFile(path);
	}

	open(settings: OpenSettings): boolean {
		this.size = { width: settings.width, height: settings.height, dpRatio: settings.dpRatio ?? 0 };
		const scaled: OpenSettings = {
			...settings,
			width: this.scaled(settings.width),
			height: this.scaled(settings.height),
			dpRatio: settings.dpRatio ? settings.dpRatio * this.scale : undefined,
		};
		return this.withGatedKeys(() => this.native!.open(JSON.stringify(scaled)));
	}

	close() {
		this.withGatedKeys(() => this.native?.close());
	}

	frame(seconds: number) {
		this.native!.frame(seconds);
	}

	resize(width: number, height: number, dpRatio: number | null) {
		this.size = { width, height, dpRatio: dpRatio ?? 0 };
		this.native!.resize(this.scaled(width), this.scaled(height), this.size.dpRatio * this.scale);
	}

	filesChanged(paths: string[]) {
		this.native!.filesChanged(JSON.stringify(paths));
	}

	hitTest(x: number, y: number): string[] {
		return JSON.parse(this.native!.hitTest(x * this.scaleX, y * this.scaleY));
	}

	tree(): TreeNode | null {
		const tree: TreeNode | null = JSON.parse(this.native!.tree());
		if (!tree || this.scale === 1) return tree;
		const convert = (node: TreeNode) => {
			node.rect = this.toDocument(node.rect);
			node.children.forEach(convert);
		};
		convert(tree);
		return tree;
	}

	inspect(id: string, computed: string[] = []): Inspection | null {
		const inspection: Inspection | null = JSON.parse(this.native!.inspect(id, JSON.stringify(computed)));
		if (inspection && this.scale !== 1) {
			const { boxes } = inspection;
			inspection.boxes = {
				margin: this.toDocument(boxes.margin), border: this.toDocument(boxes.border),
				padding: this.toDocument(boxes.padding), content: this.toDocument(boxes.content),
			};
		}
		return inspection;
	}

	diagnostics(): Diagnostic[] {
		return JSON.parse(this.native!.diagnostics());
	}

	/** When the document would next update in the game, and what about each element can make it. */
	activity(): Activity | null {
		return JSON.parse(this.native!.activity());
	}

	setInlineProperty(id: string, name: string, value: string): boolean {
		return this.native!.setInlineProperty(id, name, value);
	}

	removeInlineProperty(id: string, name: string) {
		this.native!.removeInlineProperty(id, name);
	}

	/** Updates the document at a time without drawing it. */
	step(seconds: number) {
		this.native!.step(seconds);
	}

	/**
	 * One element alone, as a PNG with alpha, drawn at the document's own resolution times the samples
	 * whatever the zoom.
	 * Null when it could not be baked, and the diagnostics say why.
	 */
	bake(id: string, margin = 0, samples = 1): Uint8Array | null {
		const scale = this.scale;
		this.setRenderScale(samples);
		const target = '/tmp/studio-bake.png';
		this.removeFile(target);
		const ok = this.native!.bake(id, target, margin);
		this.setRenderScale(scale);
		if (!ok || !this.module!.FS.analyzePath(target).exists) return null;
		const data = this.readFile(target);
		this.removeFile(target);
		return data;
	}

	screenshot(): Uint8Array | null {
		if (!this.native!.screenshot('/tmp/studio-shot.png')) return null;
		return this.readFile('/tmp/studio-shot.png');
	}
}
