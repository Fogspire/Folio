import type { IDockviewPanelProps } from 'dockview-react';
import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { elementMenu, revealInCode } from '../commands';
import { MARKUP_MIME } from '../dnd';
import { FileTabs } from '../FileTabs';
import {
	clearPreview, defaultTarget, deleteElement, dpRatio, duplicate, elementText, insertInto, isEditable, LEAF_TAGS, moveInto, previewStyle,
	setElementText, setStyles, shiftLength, sourceElement, wrapElement,
} from '../edit/commands';
import { Inspection, Rect, TreeNode } from '../engine';
import { basename, extension, stem } from '../paths';
import {
	advanceTimeline, canvasScale, copyLibraryDocument, engine, findNode, isLibraryDocument, MIN_ZOOM, seekTimeline, select, setEditing, setHover,
	setRenderScale, setTimelinePlaying, setViewport, setZoom, store, toggleTimeline, updateSettings,
} from '../session';

const SIZES: [string, number, number][] = [
	['1920 × 1080', 1920, 1080],
	['1280 × 720', 1280, 720],
	['2560 × 1440', 2560, 1440],
	['3840 × 2160', 3840, 2160],
	['1280 × 800', 1280, 800],
	['2560 × 1080', 2560, 1080],
	['1024 × 640', 1024, 640],
];
const ZOOMS = [0.25, 0.5, 0.75, 1, 1.5, 2];
/** Room left around a document fitted to the panel, so its edge reads as an edge. */
const FIT_MARGIN = 24;
/** How far the pointer travels before a press becomes a drag, in screen pixels. */
const DRAG_THRESHOLD = 4;
/** How close to a container's edge a drop means "beside it" rather than "inside it", in document pixels. */
const EDGE_ZONE = 8;
/** How much one notch of the wheel zooms, as an exponent per pixel of scroll. */
const WHEEL_ZOOM_RATE = 0.0015;

type Handle = 'e' | 's' | 'se';

interface Drop {
	parentId: string;
	/** The child of the parent to insert before, by its place among the live children. */
	index: number;
	/** Where the insertion is drawn, in document pixels. */
	line: Rect;
}

interface Axis {
	/** The property the gesture writes, which is whichever side the element is already pinned from. */
	property: string;
	/** +1 when the property grows with the pointer, -1 for `right` and `bottom`, which shrink. */
	sign: number;
	written: string | undefined;
	/** What a percentage of this property is a percentage of. */
	basis: number;
	/** The property's present value in pixels, for when it is not written in a unit that can be shifted. */
	fallback: number;
}

type Gesture =
	/** A button is down and has not travelled far enough to mean anything yet. */
	| { kind: 'press'; id: string; x: number; y: number; through: string | null }
	| { kind: 'move'; id: string; x: number; y: number; horizontal: Axis; vertical: Axis; values: [string, string][] }
	| { kind: 'reorder'; id: string; drop: Drop | null }
	| { kind: 'resize'; id: string; x: number; y: number; handle: Handle; width: Axis; height: Axis; values: [string, string][] };

function boxStyle(rect: Rect, scale: number) {
	return { left: rect[0] * scale, top: rect[1] * scale, width: rect[2] * scale, height: rect[3] * scale };
}

/** The band between an outer box and the one inside it, which is all a margin or a padding is. */
function ringStyle(outer: Rect, inner: Rect, scale: number): React.CSSProperties {
	const edges = [
		inner[1] - outer[1],
		outer[0] + outer[2] - (inner[0] + inner[2]),
		outer[1] + outer[3] - (inner[1] + inner[3]),
		inner[0] - outer[0],
	].map((edge) => Math.max(0, edge * scale) + 'px');
	return { ...boxStyle(outer, scale), padding: edges.join(' ') };
}

function parentOf(id: string): string | null {
	if (id === '') return null;
	const slash = id.lastIndexOf('/');
	return slash === -1 ? '' : id.slice(0, slash);
}

function isWithin(id: string, ancestor: string): boolean {
	return id === ancestor || id.startsWith(ancestor + '/');
}

/** What a rule of the element's own, or its style attribute, sets a property to. */
function written(inspection: Inspection, property: string): string | undefined {
	if (property in inspection.inline) return inspection.inline[property];
	return inspection.rules.find((entry) => entry.name === property && !entry.inherited)?.value;
}

/** A computed length such as `20dp` or `24px`, in document pixels. */
function toPixels(value: string | undefined, fallback: number): number {
	const match = /^(-?\d*\.?\d+)(dp|px)?$/.exec((value ?? '').trim());
	if (!match) return fallback;
	return Number(match[1]) * (match[2] === 'dp' ? dpRatio() : 1);
}

/**
 * Works out where a drop at a point would put an element.
 *
 * The innermost editable element under the pointer takes it as a child, unless it is a leaf or the
 * pointer is at its very edge, in which case the drop lands beside it in its parent. Among the
 * children, the position follows the direction the container lays them out in.
 */
function findDrop(x: number, y: number, dragged: string | null): Drop | null {
	const tree = store.get().tree;
	if (!tree) return null;

	const chain = engine.hitTest(x, y).filter((id) => (dragged === null || !isWithin(id, dragged)) && isEditable(id));
	let containerId = chain[0];
	if (containerId === undefined) return null;

	let node = findNode(tree, containerId);
	if (!node) return null;

	const element = sourceElement(containerId);
	const leaf = LEAF_TAGS.has(node.tag) || (element?.hasText ?? false);
	const [left, top, width, height] = node.rect;
	const atEdge = x - left < EDGE_ZONE || left + width - x < EDGE_ZONE || y - top < EDGE_ZONE || top + height - y < EDGE_ZONE;

	if ((leaf || atEdge) && containerId !== '') {
		const parent = parentOf(containerId)!;
		if (isEditable(parent)) {
			containerId = parent;
			node = findNode(tree, containerId)!;
		} else if (leaf) {
			return null;
		}
	}

	const layout = engine.inspect(containerId, ['display', 'flex-direction'])?.computed ?? {};
	const row = (layout.display ?? '').includes('flex') && (layout['flex-direction'] ?? 'row').startsWith('row');
	const children = node.children
		.map((child, index) => ({ child, index }))
		.filter(({ child }) => child.visible && child.rect[2] > 0 && child.rect[3] > 0 && child.id !== dragged);

	const [cl, ct, cw, ch] = node.rect;
	if (!children.length) return { parentId: containerId, index: node.children.length, line: row ? [cl + 4, ct + 4, 2, Math.max(8, ch - 8)] : [cl + 4, ct + 4, Math.max(8, cw - 8), 2] };

	const pointer = row ? x : y;
	const start = (rect: Rect) => (row ? rect[0] : rect[1]);
	const end = (rect: Rect) => (row ? rect[0] + rect[2] : rect[1] + rect[3]);

	let before = children.findIndex(({ child }) => pointer < (start(child.rect) + end(child.rect)) / 2);
	if (before === -1) before = children.length;

	const previous = children[before - 1]?.child.rect;
	const next = children[before]?.child.rect;
	const at = previous && next ? (end(previous) + start(next)) / 2 : next ? start(next) - 3 : end(previous!) + 3;
	const reference = (next ?? previous)!;

	return {
		parentId: containerId,
		index: before < children.length ? children[before].index : node.children.length,
		line: row ? [at - 1, reference[1], 2, reference[3]] : [reference[0], at - 1, reference[2], 2],
	};
}

/** One side of a move: which property carries it, and what it is measured against. */
function moveAxis(inspection: Inspection, parent: Inspection | null, horizontal: boolean): Axis {
	const [near, far] = horizontal ? ['left', 'right'] : ['top', 'bottom'];
	const index = horizontal ? 0 : 1;
	const container = parent?.boxes.padding ?? [0, 0, store.get().width, store.get().height];
	const nearWritten = written(inspection, near);
	const farWritten = written(inspection, far);
	const box = inspection.boxes.margin;

	// Pinned from the far side and not the near one: keep it that way, or the element would be pinned from both.
	if (farWritten !== undefined && farWritten !== 'auto' && (nearWritten === undefined || nearWritten === 'auto')) {
		return { property: far, sign: -1, written: farWritten, basis: container[index + 2], fallback: container[index] + container[index + 2] - (box[index] + box[index + 2]) };
	}
	return { property: near, sign: 1, written: nearWritten, basis: container[index + 2], fallback: box[index] - container[index] };
}

function sizeAxis(inspection: Inspection, parent: Inspection | null, horizontal: boolean): Axis {
	const property = horizontal ? 'width' : 'height';
	const index = horizontal ? 2 : 3;
	const borderBox = inspection.computed['box-sizing'] === 'border-box';
	return {
		property,
		sign: 1,
		written: written(inspection, property),
		basis: parent?.boxes.content[index] ?? 0,
		fallback: (borderBox ? inspection.boxes.border : inspection.boxes.content)[index],
	};
}

function shift(axis: Axis, delta: number): [string, string] {
	return [axis.property, shiftLength(axis.written, delta * axis.sign, axis.basis, axis.fallback)];
}

function inspectParent(id: string): Inspection | null {
	const parent = parentOf(id);
	return parent !== null ? engine.inspect(parent) : null;
}

function Toolbar({ scale }: { scale: number }) {
	const width = store.use((state) => state.width);
	const height = store.use((state) => state.height);
	const ratio = store.use((state) => state.dpRatio);
	const zoom = store.use((state) => state.zoom);
	const timelineOpen = store.use((state) => state.timeline !== null);
	const sizeKey = `${width}x${height}`;
	const known = SIZES.some(([, w, h]) => w === width && h === height);

	return (
		<div className="toolbar">
			<label>
				Size
				<select
					value={sizeKey}
					onChange={(event) => {
						const [w, h] = event.target.value.split('x').map(Number);
						setViewport(w, h, ratio);
					}}
				>
					{!known && <option value={sizeKey}>{width} × {height}</option>}
					{SIZES.map(([label, w, h]) => <option key={label} value={`${w}x${h}`}>{label}</option>)}
				</select>
			</label>
			<label title="Density-independent pixel ratio. Auto follows the height against 1080, as the game does.">
				Scale
				<select
					value={ratio === null ? 'auto' : String(ratio)}
					onChange={(event) => setViewport(width, height, event.target.value === 'auto' ? null : Number(event.target.value))}
				>
					<option value="auto">Auto</option>
					{[0.5, 0.75, 1, 1.25, 1.5, 2].map((value) => <option key={value} value={value}>{value}×</option>)}
				</select>
			</label>
			<span className="spacer" />
			<button className={'button' + (timelineOpen ? ' active' : '')} title="Hold the document's clock, and play or scrub it from the first frame (Ctrl+Shift+T)" onClick={toggleTimeline}>
				Timeline
			</button>
			<label>
				Zoom
				<select value={String(zoom)} onChange={(event) => setZoom(event.target.value === 'fit' ? 'fit' : Number(event.target.value))}>
					<option value="fit">Fit ({Math.round(scale * 100)}%)</option>
					{zoom !== 'fit' && !ZOOMS.includes(zoom) && <option value={zoom}>{Math.round(zoom * 100)}%</option>}
					{ZOOMS.map((value) => <option key={value} value={value}>{value * 100}%</option>)}
				</select>
			</label>
		</div>
	);
}

function Overlay({ scale }: { scale: number }) {
	const tree = store.use((state) => state.tree);
	const hover = store.use((state) => state.hover);
	const selection = store.use((state) => state.selection);
	const inspection = store.use((state) => state.inspection);
	const mode = store.use((state) => state.mode);
	// Read so the overlay follows edits: what can be dragged depends on the parsed markup.
	store.use((state) => state.revision);
	/** The chain under the last click, so alt-click can step outward through it. */
	const chain = useRef<string[]>([]);
	const gesture = useRef<Gesture | null>(null);
	const root = useRef<HTMLDivElement>(null);
	const scaleRef = useRef(scale);
	scaleRef.current = scale;
	const [drop, setDrop] = useState<Drop | null>(null);
	/** How far the selection box has been dragged or stretched, before the document catches up. */
	const [offset, setOffset] = useState<[number, number, number, number]>([0, 0, 0, 0]);
	const editing = store.use((state) => state.editing);

	useEffect(() => setOffset([0, 0, 0, 0]), [selection]);

	// Held in a ref so the listeners added to the window for a drag are the same functions when removed.
	const handlers = useRef({
		move: (_event: MouseEvent) => {},
		up: () => {},
	});

	const point = (event: { clientX: number; clientY: number }) => {
		const bounds = root.current!.getBoundingClientRect();
		return [(event.clientX - bounds.left) / scaleRef.current, (event.clientY - bounds.top) / scaleRef.current] as const;
	};

	handlers.current.move = (event: MouseEvent) => {
		const active = gesture.current;
		if (!active) return;
		const [x, y] = point(event);

		if (active.kind === 'press') {
			if (Math.hypot(x - active.x, y - active.y) * scaleRef.current < DRAG_THRESHOLD) return;
			const current = store.get().inspection;
			if (!current || current.id !== active.id || !isEditable(active.id) || active.id === '') {
				gesture.current = { ...active, through: null };
				return;
			}
			const parent = inspectParent(active.id);
			// A positioned element moves freely. Anything in the flow sits where its siblings put it, so
			// dragging one changes its place among them instead.
			gesture.current = (current.computed.position ?? 'static') === 'static'
				? { kind: 'reorder', id: active.id, drop: null }
				: { kind: 'move', id: active.id, x: active.x, y: active.y, horizontal: moveAxis(current, parent, true), vertical: moveAxis(current, parent, false), values: [] };
			return;
		}

		if (active.kind === 'reorder') {
			active.drop = findDrop(x, y, active.id);
			setDrop(active.drop);
			return;
		}

		const dx = x - active.x;
		const dy = y - active.y;
		if (active.kind === 'move') {
			active.values = [shift(active.horizontal, dx), shift(active.vertical, dy)];
			setOffset([dx, dy, 0, 0]);
		} else {
			active.values = [];
			if (active.handle !== 's') active.values.push(shift(active.width, dx));
			if (active.handle !== 'e') active.values.push(shift(active.height, dy));
			setOffset([0, 0, active.handle !== 's' ? dx : 0, active.handle !== 'e' ? dy : 0]);
		}
		previewStyle(active.id, active.values);
	};

	handlers.current.up = () => {
		const active = gesture.current;
		gesture.current = null;
		setDrop(null);
		setOffset([0, 0, 0, 0]);
		window.removeEventListener('mousemove', onMove);
		window.removeEventListener('mouseup', onUp);
		if (!active) return;

		if (active.kind === 'press') {
			// Released without dragging on something inside the selection: that inner thing was what was meant.
			if (active.through !== null) select(active.through);
		} else if (active.kind === 'reorder') {
			if (active.drop) moveInto(active.id, active.drop.parentId, active.drop.index);
		} else {
			clearPreview(active.id, active.values.map(([name]) => name));
			const current = store.get().inspection;
			if (current && active.values.length) setStyles(active.id, active.values, defaultTarget(current, active.values[0][0]));
		}
	};

	const [onMove] = useState(() => (event: MouseEvent) => handlers.current.move(event));
	const [onUp] = useState(() => () => handlers.current.up());

	const begin = (next: Gesture) => {
		gesture.current = next;
		window.addEventListener('mousemove', onMove);
		window.addEventListener('mouseup', onUp);
	};

	if (mode !== 'edit') return null;

	const startResize = (event: React.MouseEvent, handle: Handle) => {
		event.stopPropagation();
		event.preventDefault();
		if (!inspection) return;
		const parent = inspectParent(inspection.id);
		const [x, y] = point(event);
		begin({ kind: 'resize', id: inspection.id, x, y, handle, width: sizeAxis(inspection, parent, true), height: sizeAxis(inspection, parent, false), values: [] });
	};

	const nudge = (dx: number, dy: number) => {
		if (!inspection || !isEditable(inspection.id) || (inspection.computed.position ?? 'static') === 'static') return;
		const parent = inspectParent(inspection.id);
		const values: [string, string][] = [];
		if (dx) values.push(shift(moveAxis(inspection, parent, true), dx));
		if (dy) values.push(shift(moveAxis(inspection, parent, false), dy));
		setStyles(inspection.id, values, defaultTarget(inspection, values[0][0]));
	};

	const hoverNode: TreeNode | null = hover !== null && hover !== selection && !gesture.current ? findNode(tree, hover) : null;
	const dropParent = drop ? findNode(tree, drop.parentId) : null;
	const editable = inspection ? isEditable(inspection.id) : false;
	const shifted = (rect: Rect): Rect => [rect[0] + offset[0], rect[1] + offset[1], rect[2] + offset[2], rect[3] + offset[3]];
	const editingText = editing !== null ? elementText(editing) : null;

	return (
		<div
			className="overlay"
			ref={root}
			tabIndex={0}
			data-element-keys
			onContextMenu={(event) => {
				event.preventDefault();
				if (!engine.isOpen) return;
				root.current!.focus();
				const [x, y] = point(event);
				const hit = engine.hitTest(x, y);
				// On the selection, the menu is for the selection, which may be an ancestor of what is under the pointer.
				const id = selection !== null && hit.includes(selection) ? selection : hit[0];
				if (id !== undefined) elementMenu(id);
			}}
			onMouseMove={(event) => {
				if (!engine.isOpen || gesture.current) return;
				const [x, y] = point(event);
				setHover(engine.hitTest(x, y)[0] ?? null);
			}}
			onMouseLeave={() => setHover(null)}
			onMouseDown={(event) => {
				if (event.button !== 0 || !engine.isOpen) return;
				root.current!.focus();
				const [x, y] = point(event);
				const hit = engine.hitTest(x, y);
				const innermost = hit[0] ?? null;
				// Alt steps outward from the current selection, for an element entirely covered by its child.
				const index = event.altKey && selection !== null ? chain.current.indexOf(selection) : -1;

				if (index >= 0 && chain.current.join() === hit.join()) {
					select(hit[Math.min(index + 1, hit.length - 1)]);
					return;
				}
				// A press inside the selection is the start of dragging it. Only if it turns out to be a
				// plain click does it select what was under the pointer instead.
				if (selection !== null && selection !== '' && hit.includes(selection)) {
					begin({ kind: 'press', id: selection, x, y, through: innermost !== selection ? innermost : null });
					return;
				}
				chain.current = hit;
				select(innermost);
				if (innermost !== null) begin({ kind: 'press', id: innermost, x, y, through: null });
			}}
			onDoubleClick={(event) => {
				const [x, y] = point(event);
				const id = engine.hitTest(x, y)[0];
				if (id === undefined) return;
				select(id);
				if (isEditable(id) && elementText(id) !== null) setEditing(id);
				else revealInCode(id);
			}}
			onKeyDown={(event) => {
				if (editing !== null) return;
				const key = event.key.toLowerCase();
				const step = event.shiftKey ? 10 : 1;

				if (!inspection || event.altKey) return;
				else if (key === 'arrowleft') nudge(-step, 0);
				else if (key === 'arrowright') nudge(step, 0);
				else if (key === 'arrowup') nudge(0, -step);
				else if (key === 'arrowdown') nudge(0, step);
				else return;

				event.preventDefault();
				event.stopPropagation();
			}}
			onDragOver={(event) => {
				if (!event.dataTransfer.types.includes(MARKUP_MIME)) return;
				event.preventDefault();
				event.dataTransfer.dropEffect = 'copy';
				const [x, y] = point(event);
				setDrop(findDrop(x, y, null));
			}}
			onDragLeave={() => setDrop(null)}
			onDrop={(event) => {
				const markup = event.dataTransfer.getData(MARKUP_MIME);
				const [x, y] = point(event);
				const target = findDrop(x, y, null);
				setDrop(null);
				if (!markup || !target) return;
				event.preventDefault();
				// The drag began in another panel, and the keyboard should follow what was dropped.
				root.current!.focus();
				insertInto(target.parentId, target.index, markup);
			}}
		>
			{hoverNode && <div className="box hover" style={boxStyle(hoverNode.rect, scale)} />}
			{dropParent && <div className="box drop-parent" style={boxStyle(dropParent.rect, scale)} />}
			{drop && <div className="box drop-line" style={boxStyle(drop.line, scale)} />}
			{inspection && (
				<>
					<div className="box ring margin" style={ringStyle(shifted(inspection.boxes.margin), shifted(inspection.boxes.border), scale)} />
					<div className="box ring padding" style={ringStyle(shifted(inspection.boxes.padding), shifted(inspection.boxes.content), scale)} />
					<div className="box selected" style={boxStyle(shifted(inspection.boxes.border), scale)}>
						<span className="tag" onMouseDown={(event) => event.stopPropagation()} onDoubleClick={(event) => event.stopPropagation()}>
							{inspection.id === '' ? 'body' : inspection.tag}
							{inspection.attributes.id ? '#' + inspection.attributes.id : ''}
							<span className="dim"> {Math.round(inspection.boxes.border[2] + offset[2])} × {Math.round(inspection.boxes.border[3] + offset[3])}</span>
							{inspection.id !== '' && <button title="Select parent" onClick={() => select(parentOf(inspection.id))}>↑</button>}
							{editable && inspection.id !== '' && (
								<>
									<button title="Show in Code" onClick={() => revealInCode(inspection.id)}>&lt;/&gt;</button>
									<button title="Duplicate (Ctrl+D)" onClick={() => duplicate(inspection.id)}>⧉</button>
									<button title="Wrap in a div" onClick={() => wrapElement(inspection.id)}>▢</button>
									<button title="Delete (Del)" onClick={() => deleteElement(inspection.id)}>×</button>
								</>
							)}
						</span>
						{editable && inspection.id !== '' && (['e', 's', 'se'] as Handle[]).map((handle) => (
							<span key={handle} className={'handle handle-' + handle} onMouseDown={(event) => startResize(event, handle)} />
						))}
					</div>
				</>
			)}
			{editing !== null && editingText !== null && inspection && inspection.id === editing && (
				<input
					className="inline-text"
					autoFocus
					defaultValue={editingText.trim()}
					style={{
						...boxStyle(inspection.boxes.border, scale),
						fontSize: Math.max(11, toPixels(inspection.computed['font-size'], 14) * scale),
						textAlign: (inspection.computed['text-align'] as 'left' | 'center' | 'right') ?? 'left',
					}}
					onMouseDown={(event) => event.stopPropagation()}
					onDoubleClick={(event) => event.stopPropagation()}
					onKeyDown={(event) => {
						event.stopPropagation();
						if (event.key === 'Enter') (event.target as HTMLInputElement).blur();
						if (event.key === 'Escape') {
							setEditing(null);
							root.current!.focus();
						}
					}}
					onBlur={(event) => {
						const value = event.target.value;
						const id = editing;
						// Escape has already ended the edit, and what was typed is then thrown away.
						if (store.get().editing !== id) return;
						setEditing(null);
						if (value !== editingText.trim()) setElementText(id, value);
					}}
				/>
			)}
		</div>
	);
}

/** Shown over a document Folio ships, which is a preview until it is copied into the project. */
function LibraryBanner({ path }: { path: string }) {
	const [name, setName] = useState('');
	useEffect(() => setName(''), [path]);

	return (
		<form
			className="library-banner"
			onSubmit={(event) => {
				event.preventDefault();
				copyLibraryDocument(path, name.trim() || stem(path));
			}}
		>
			<span><b>{basename(path)}</b> ships with Folio. To build on it, copy it to UI/screens/ as</span>
			<input value={name} placeholder={stem(path)} onChange={(event) => setName(event.target.value)} spellCheck={false} />
			<button className="button primary" type="submit">Use this</button>
		</form>
	);
}

function TimelineBar() {
	const timeline = store.use((state) => state.timeline);
	const duration = store.use((state) => state.settings.timelineDuration);
	if (!timeline) return null;

	return (
		<div className="toolbar timeline">
			<button className="button" title="Back to the first frame" onClick={() => { seekTimeline(0); setTimelinePlaying(true); }}>Restart</button>
			<button className="button" onClick={() => setTimelinePlaying(!timeline.playing)}>{timeline.playing ? 'Pause' : 'Play'}</button>
			<input
				className="timeline-scrub"
				type="range"
				min={0}
				max={duration}
				step={1 / 60}
				value={timeline.time}
				onChange={(event) => {
					setTimelinePlaying(false);
					seekTimeline(Number(event.target.value));
				}}
			/>
			<span className="timeline-time">{timeline.time.toFixed(2)} s</span>
			<label>
				Length
				<input
					type="number"
					min={0.1}
					max={30}
					step={0.5}
					value={duration}
					onChange={(event) => {
						const value = Number(event.target.value);
						if (value >= 0.1 && value <= 30) updateSettings({ timelineDuration: value });
					}}
				/>
			</label>
		</div>
	);
}

export function CanvasPanel(props: IDockviewPanelProps) {
	const width = store.use((state) => state.width);
	const height = store.use((state) => state.height);
	const zoom = store.use((state) => state.zoom);
	const mode = store.use((state) => state.mode);
	const documentPath = store.use((state) => state.document);
	const documentOpen = store.use((state) => state.documentOpen);
	const engineReady = store.use((state) => state.engineReady);
	const renderScale = store.use((state) => state.renderScale);
	const openFiles = store.use((state) => state.openFiles);
	const documents = openFiles.filter((file) => extension(file) === '.rml');
	const viewport = useRef<HTMLDivElement>(null);
	const surface = useRef<HTMLDivElement>(null);
	const [available, setAvailable] = useState<[number, number]>([0, 0]);
	const [visible, setVisible] = useState(props.api.isVisible);
	const pan = store.use((state) => state.pan);
	const [panning, setPanning] = useState(false);

	// The panel is kept rendered so its canvas survives a tab switch, which means hiding it is on us.
	useEffect(() => {
		const subscription = props.api.onDidVisibilityChange((event) => setVisible(event.isVisible));
		return () => subscription.dispose();
	}, [props.api]);

	useLayoutEffect(() => {
		surface.current!.appendChild(engine.canvas);
		return () => engine.canvas.remove();
	}, []);

	useEffect(() => {
		const observer = new ResizeObserver(([entry]) => setAvailable([entry.contentRect.width, entry.contentRect.height]));
		observer.observe(viewport.current!);
		return () => observer.disconnect();
	}, []);

	useEffect(() => {
		let handle = 0;
		let last = 0;
		const tick = (time: number) => {
			handle = requestAnimationFrame(tick);
			const elapsed = last ? (time - last) / 1000 : 0;
			last = time;
			if (!engine.isOpen || document.visibilityState !== 'visible' || !props.api.isVisible) return;
			// The timeline owns the clock while it is open, and draws only when it moves.
			if (store.get().timeline) advanceTimeline(elapsed);
			else engine.frame(time / 1000);
		};
		handle = requestAnimationFrame(tick);
		return () => cancelAnimationFrame(handle);
	}, [props.api]);

	const fit = Math.min(1, Math.max(MIN_ZOOM, Math.min((available[0] - FIT_MARGIN * 2) / width, (available[1] - FIT_MARGIN * 2) / height)));
	const scale = zoom === 'fit' ? fit : zoom;
	canvasScale.value = scale;

	// Once the zoom settles, since each change of resolution lays the document out again.
	useEffect(() => {
		const timer = window.setTimeout(() => setRenderScale(scale * window.devicePixelRatio), 150);
		return () => window.clearTimeout(timer);
	}, [scale, width, height]);

	const renderWidth = Math.max(1, Math.round(width * renderScale));
	const renderHeight = Math.max(1, Math.round(height * renderScale));

	/** The document point a wheel zoom is holding still, and where on screen it has to stay. */
	const anchor = useRef<{ x: number; y: number; clientX: number; clientY: number } | null>(null);
	const stage = useRef<HTMLDivElement>(null);

	// Registered by hand: React's wheel listener is passive, and this one has to stop the panel scrolling.
	useEffect(() => {
		const element = viewport.current!;
		const onWheel = (event: WheelEvent) => {
			if (!event.ctrlKey && !event.metaKey) return;
			event.preventDefault();
			const bounds = stage.current!.getBoundingClientRect();
			// Several notches can arrive before one is drawn, so each builds on the zoom asked for, not the one on screen.
			const drawn = canvasScale.value;
			const asked = store.get().zoom;
			anchor.current = { x: (event.clientX - bounds.left) / drawn, y: (event.clientY - bounds.top) / drawn, clientX: event.clientX, clientY: event.clientY };
			setZoom((asked === 'fit' ? drawn : asked) * Math.exp(-event.deltaY * WHEEL_ZOOM_RATE));
		};
		element.addEventListener('wheel', onWheel, { passive: false });
		return () => element.removeEventListener('wheel', onWheel);
	}, []);

	useLayoutEffect(() => {
		const held = anchor.current;
		anchor.current = null;
		if (!held) return;
		const bounds = stage.current!.getBoundingClientRect();
		viewport.current!.scrollLeft += bounds.left + held.x * scale - held.clientX;
		viewport.current!.scrollTop += bounds.top + held.y * scale - held.clientY;
	}, [scale]);

	useEffect(() => store.set({ pan: { x: 0, y: 0 } }), [documentPath]);

	/** Middle drag moves the document: by scrolling while there is room to, and by offsetting it past that. */
	const startPan = (event: React.MouseEvent) => {
		if (event.button !== 1) return;
		// Otherwise the middle button starts the browser's own autoscroll.
		event.preventDefault();
		let last = { x: event.clientX, y: event.clientY };
		setPanning(true);
		const move = (moved: MouseEvent) => {
			const element = viewport.current!;
			const dx = moved.clientX - last.x;
			const dy = moved.clientY - last.y;
			last = { x: moved.clientX, y: moved.clientY };
			const before = { x: element.scrollLeft, y: element.scrollTop };
			element.scrollLeft -= dx;
			element.scrollTop -= dy;
			const { pan: current } = store.get();
			store.set({ pan: { x: current.x + dx - (before.x - element.scrollLeft), y: current.y + dy - (before.y - element.scrollTop) } });
		};
		const up = (released: MouseEvent) => {
			if (released.button !== 1) return;
			window.removeEventListener('mousemove', move);
			window.removeEventListener('mouseup', up);
			setPanning(false);
		};
		window.addEventListener('mousemove', move);
		window.addEventListener('mouseup', up);
	};

	return (
		<div className="panel canvas-panel" style={{ display: visible ? undefined : 'none' }}>
			{documents.length > 0 && <FileTabs files={documents} active={documentPath} />}
			<Toolbar scale={scale} />
			<TimelineBar />
			{documentPath && isLibraryDocument(documentPath) && <LibraryBanner path={documentPath} />}
			<div className={'canvas-viewport' + (panning ? ' panning' : '')} ref={viewport} onMouseDownCapture={startPan} onAuxClick={(event) => event.preventDefault()}>
				<div className="canvas-stage" ref={stage} style={{ width: width * scale, height: height * scale, transform: pan.x || pan.y ? `translate(${pan.x}px, ${pan.y}px)` : undefined, visibility: documentOpen ? 'visible' : 'hidden' }}>
					<div
						className={'canvas-surface' + (mode === 'play' ? ' playing' : '')}
						ref={surface}
						style={{ width: renderWidth, height: renderHeight, transform: `scale(${(width * scale) / renderWidth}, ${(height * scale) / renderHeight})` }}
						onMouseDown={() => engine.canvas.focus()}
					/>
					<Overlay scale={scale} />
				</div>
				{!documentOpen && (
					<div className="canvas-empty">
						{!engineReady ? 'Starting the renderer…'
							: !documentPath ? 'Choose a document from Files, or start from one in Library.'
							: 'This document did not load. Problems has the reason.'}
					</div>
				)}
			</div>
		</div>
	);
}
