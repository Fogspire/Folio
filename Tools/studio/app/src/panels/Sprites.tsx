import { useEffect, useMemo, useRef, useState } from 'react';
import { popup } from '../commands';
import { join, relativeTo, isUnder } from '../paths';
import { store } from '../session';
import { Layout, packAtlas } from '../sprites/pack';
import {
	chooseFolder, included, isPackFile, moveFrame, openPack, pack, problems, reloadFrames, setAtlas, sprites, updateFrame,
} from '../sprites/session';

const FRAME_MIME = 'application/x-folio-sprite-frame';

/** A path shown relative to the project when it is inside it, and read back the same way. */
function PathField({ label, value, title, onChange }: { label: string; value: string; title: string; onChange: (path: string) => void }) {
	const root = store.use((state) => state.project?.root ?? '');
	const shown = root && isUnder(root, value) ? relativeTo(root, value) : value;
	const [text, setText] = useState(shown);
	useEffect(() => setText(shown), [shown]);
	const commit = () => {
		const trimmed = text.trim().replace(/\\/g, '/');
		if (!trimmed) return setText(shown);
		onChange(/^([A-Za-z]:)?\//.test(trimmed) ? trimmed : join(root, trimmed));
	};
	return (
		<label className="sprites-field" title={title}>
			<span className="dim">{label}</span>
			<input value={text} spellCheck={false} onChange={(event) => setText(event.target.value)} onBlur={commit} onKeyDown={(event) => event.key === 'Enter' && commit()} />
		</label>
	);
}

function NumberField({ label, value, title, onChange }: { label: string; value: number; title: string; onChange: (value: number) => void }) {
	return (
		<label className="sprites-field" title={title}>
			<span className="dim">{label}</span>
			<input type="number" min={1} step={1} value={value} onChange={(event) => onChange(Math.max(1, Math.round(Number(event.target.value) || 1)))} />
		</label>
	);
}

/** The atlas as Pack would write it, with the hovered frame's cell outlined. Click a cell to rename its frame. */
function Preview({ hovered, onPick }: { hovered: number | null; onPick: (index: number) => void }) {
	const frames = sprites.use((state) => state.frames);
	const scale = sprites.use((state) => state.scale);
	const columns = sprites.use((state) => state.columns);
	const canvas = useRef<HTMLCanvasElement>(null);

	const packed = useMemo<{ layout: Layout; image: ImageData } | null>(() => {
		const chosen = frames.filter((frame) => frame.included && frame.pixels);
		if (!chosen.length) return null;
		const { atlas, layout } = packAtlas(chosen.map((frame) => frame.pixels!), scale, columns);
		return { layout, image: new ImageData(new Uint8ClampedArray(atlas.data.buffer as ArrayBuffer), atlas.width, atlas.height) };
	}, [frames, scale, columns]);

	useEffect(() => {
		const element = canvas.current;
		if (!element || !packed) return;
		element.width = packed.image.width;
		element.height = packed.image.height;
		element.getContext('2d')!.putImageData(packed.image, 0, 0);
	}, [packed]);

	if (!packed) return <p className="dim sprites-note">Include a frame to see the atlas.</p>;
	const { layout } = packed;
	// Cells count among included frames only, so the hovered row maps through that list.
	const chosenIndices = frames.map((frame, index) => (frame.included && frame.pixels ? index : -1)).filter((index) => index >= 0);
	const cellIndex = hovered === null ? -1 : chosenIndices.indexOf(hovered);
	const cell = cellIndex >= 0 ? layout.cells[cellIndex] : null;
	const percent = (value: number, of: number) => (value / of) * 100 + '%';

	return (
		<>
			<div className="sprites-preview" style={{ aspectRatio: `${layout.width} / ${layout.height}` }}>
				<canvas
					ref={canvas}
					onClick={(event) => {
						const box = event.currentTarget.getBoundingClientRect();
						const x = ((event.clientX - box.left) / box.width) * layout.width;
						const y = ((event.clientY - box.top) / box.height) * layout.height;
						const index = layout.cells.findIndex((entry) => x >= entry.x && x < entry.x + entry.width && y >= entry.y && y < entry.y + entry.height);
						if (index >= 0) onPick(chosenIndices[index]);
					}}
				/>
				{cell && (
					<div
						className="sprites-cell"
						style={{ left: percent(cell.x, layout.width), top: percent(cell.y, layout.height), width: percent(cell.width, layout.width), height: percent(cell.height, layout.height) }}
					/>
				)}
			</div>
			<p className="dim sprites-note">
				{chosenIndices.length} frames, {layout.cellWidth}x{layout.cellHeight} cells at {scale}x, atlas {layout.width}x{layout.height}
			</p>
		</>
	);
}

function FrameRow({ index, hovered, setHovered }: { index: number; hovered: boolean; setHovered: (index: number | null) => void }) {
	const frame = sprites.use((state) => state.frames[index]);
	const count = sprites.use((state) => state.frames.length);
	const [over, setOver] = useState(false);

	return (
		<div
			className={'tree-row sprites-frame' + (hovered ? ' hovered' : '') + (frame.included ? '' : ' hidden') + (over ? ' drop' : '')}
			draggable
			title={frame.file + '.png' + (frame.error ? ': ' + frame.error : '') + '. Drag to reorder.'}
			onMouseEnter={() => setHovered(index)}
			onMouseLeave={() => setHovered(null)}
			onDragStart={(event) => {
				event.dataTransfer.setData(FRAME_MIME, String(index));
				event.dataTransfer.effectAllowed = 'move';
			}}
			onDragOver={(event) => {
				if (!event.dataTransfer.types.includes(FRAME_MIME)) return;
				event.preventDefault();
				setOver(true);
			}}
			onDragLeave={() => setOver(false)}
			onDrop={(event) => {
				setOver(false);
				const from = Number(event.dataTransfer.getData(FRAME_MIME));
				if (Number.isInteger(from)) moveFrame(from, index);
			}}
			onContextMenu={async (event) => {
				event.preventDefault();
				const chosen = await popup([
					{ id: 'up', label: 'Move Up', enabled: index > 0 },
					{ id: 'down', label: 'Move Down', enabled: index < count - 1 },
					{ id: 'first', label: 'Move to First', enabled: index > 0 },
					{ id: 'last', label: 'Move to Last', enabled: index < count - 1 },
				]);
				if (chosen === 'up') moveFrame(index, index - 1);
				else if (chosen === 'down') moveFrame(index, index + 1);
				else if (chosen === 'first') moveFrame(index, 0);
				else if (chosen === 'last') moveFrame(index, count - 1);
			}}
		>
			<input type="checkbox" checked={frame.included} title="Pack this frame" onChange={(event) => updateFrame(index, { included: event.target.checked })} />
			{frame.url ? <img className="sprites-thumb" src={frame.url} alt="" draggable={false} /> : <span className="sprites-thumb missing">!</span>}
			<input
				id={'sprite-name-' + index}
				className="sprites-name"
				value={frame.name}
				spellCheck={false}
				title="The sprite's name in the stylesheet"
				onChange={(event) => updateFrame(index, { name: event.target.value.trim() })}
			/>
			<span className="dim sprites-file">{frame.file}</span>
		</div>
	);
}

export function SpritesPanel() {
	const project = store.use((state) => state.project);
	const projectFiles = store.use((state) => state.projectFiles);
	const folder = sprites.use((state) => state.folder);
	const frames = sprites.use((state) => state.frames);
	const sheet = sprites.use((state) => state.sheet);
	const src = sprites.use((state) => state.src);
	const scale = sprites.use((state) => state.scale);
	const columns = sprites.use((state) => state.columns);
	const atlas = sprites.use((state) => state.atlas);
	const stylesheet = sprites.use((state) => state.stylesheet);
	const loading = sprites.use((state) => state.loading);
	const [hovered, setHovered] = useState<number | null>(null);

	if (!project) return <div className="panel empty">No project open.</div>;
	const packs = projectFiles.filter(isPackFile);
	const where = (path: string) => (isUnder(project.root, path) ? relativeTo(project.root, path) : path);
	const found = folder ? problems() : [];

	return (
		<div className="panel sprites">
			<div className="sprites-actions">
				<button className="button" onClick={chooseFolder} title="Start a sprite sheet from a folder of PNG frames">Frames Folder…</button>
				<button
					className="button"
					disabled={!packs.length}
					title={packs.length ? 'Open a sprite sheet packed before, to change it or pack it again' : 'No .spritepack.json in this project yet'}
					onClick={async () => {
						const chosen = await popup(packs.map((file) => ({ id: file, label: where(file) })));
						if (chosen) openPack(chosen);
					}}
				>
					Open Pack…
				</button>
			</div>

			{!folder ? (
				<p className="dim sprites-note">
					Packs loose pixel-art frames into one atlas PNG and the <code>@spritesheet</code> that names them, scaled up with sharp pixels. Choose a folder of frames, or right-click one in Files.
				</p>
			) : (
				<>
					<section>
						<div className="sprites-heading">
							<h3>Frames</h3>
							<span className="dim sprites-folder" title={folder}>{where(folder)}</span>
							<button className="link" disabled={loading} onClick={reloadFrames} title="Read the frames again from disk">Reload</button>
						</div>
						{loading && <p className="dim sprites-note">Reading frames…</p>}
						{frames.map((frame, index) => (
							<FrameRow key={frame.file} index={index} hovered={hovered === index} setHovered={setHovered} />
						))}
						{!loading && !frames.length && <p className="dim sprites-note">No PNGs directly in this folder.</p>}
					</section>

					<section className="sprites-settings">
						<h3>Sheet</h3>
						<label className="sprites-field" title="The @spritesheet's name">
							<span className="dim">Name</span>
							<input value={sheet} spellCheck={false} onChange={(event) => sprites.set({ sheet: event.target.value.trim() })} />
						</label>
						<NumberField label="Scale" value={scale} onChange={(value) => sprites.set({ scale: value })} title="How many times larger each pixel is drawn in the atlas. The stylesheet declares the same resolution, so a sprite's natural size stays its original pixel size in dp." />
						<NumberField label="Columns" value={columns} onChange={(value) => sprites.set({ columns: value })} title="Cells across the atlas before a new row starts" />
						<PathField label="Atlas" value={atlas} onChange={setAtlas} title="Where the atlas PNG is written, relative to the project" />
						<PathField label="Stylesheet" value={stylesheet} onChange={(path) => sprites.set({ stylesheet: path })} title="Where the @spritesheet is written. The pack is remembered beside it, as .spritepack.json." />
						<label className="sprites-field" title="The atlas path as a document names it, written as the stylesheet's src">
							<span className="dim">src</span>
							<input value={src} spellCheck={false} onChange={(event) => sprites.set({ src: event.target.value.trim() })} />
						</label>
					</section>

					<section>
						<h3>Atlas</h3>
						<Preview hovered={hovered} onPick={(index) => document.getElementById('sprite-name-' + index)?.focus()} />
					</section>

					{found.length > 0 && (
						<ul className="sprites-problems">
							{found.map((problem) => <li key={problem}>{problem}</li>)}
						</ul>
					)}
					<div className="sprites-actions">
						<button className="button primary" disabled={loading || found.length > 0 || !included().length} onClick={pack} title="Write the atlas, the stylesheet and the .spritepack.json">
							Pack
						</button>
					</div>
				</>
			)}
		</div>
	);
}
