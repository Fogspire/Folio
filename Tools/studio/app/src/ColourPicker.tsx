import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import { host } from './host';
import { cssColour, formatHex, Hsva, hslToHsv, hsvToHsl, hsvToRgb, parseColour, rgbToHsv, shades } from './colour';

type Mode = 'hsv' | 'rgb' | 'hsl';

interface Props {
	value: string;
	/** Whether the colour carries alpha. A background the canvas is drawn over does not. */
	alpha?: boolean;
	disabled?: boolean;
	title?: string;
	/** Called as the colour moves, for a change shown without being written. */
	onPreview?: (value: string) => void;
	/** Called once when the picker closes on a colour other than the one it opened on. */
	onCommit: (value: string) => void;
	/** Called when the picker closes without a change, after a preview or not, so the preview can be taken back. */
	onCancel?: (original: string) => void;
	/** The colours the document already uses, read when the picker opens. */
	documentColours?: () => Promise<string[]> | string[];
}

const RECENT_KEY = 'folio.studio.recentColours';
const RECENT_COUNT = 16;
const MODE_KEY = 'folio.studio.colourMode';

/** A scripted run keeps its picks here and leaves the person's own history alone. */
let testRecent: string[] = [];

function loadRecent(): string[] {
	if (host.testing) return testRecent;
	try {
		const stored = JSON.parse(localStorage.getItem(RECENT_KEY) ?? '[]');
		return Array.isArray(stored) ? stored.filter((entry) => typeof entry === 'string') : [];
	} catch {
		return [];
	}
}

function remember(hex: string) {
	if (host.testing) {
		testRecent = [hex, ...testRecent.filter((entry) => entry !== hex)].slice(0, RECENT_COUNT);
		return;
	}
	try {
		localStorage.setItem(RECENT_KEY, JSON.stringify([hex, ...loadRecent().filter((entry) => entry !== hex)].slice(0, RECENT_COUNT)));
	} catch {
		// A full or blocked store only loses the history.
	}
}

function loadMode(): Mode {
	try {
		const stored = localStorage.getItem(MODE_KEY);
		return stored === 'rgb' || stored === 'hsl' ? stored : 'hsv';
	} catch {
		return 'hsv';
	}
}

const toHsva = (text: string, previous?: Hsva): Hsva => {
	const parsed = parseColour(text) ?? parseColour(text.trim().split(/\s+(?![^(]*\))/)[0] ?? '');
	return parsed ? rgbToHsv(parsed, previous) : { h: 0, s: 0, v: 0, a: 1 };
};

const hexOf = (colour: Hsva, alpha: boolean) => formatHex(hsvToRgb(colour), alpha);

/** A swatch that opens a picker: a square and hue strip, channel sliders, hex, eyedropper, and swatches to reuse. */
export function ColourPicker({ value, alpha = true, disabled, title, onPreview, onCommit, onCancel, documentColours }: Props) {
	const anchor = useRef<HTMLButtonElement>(null);
	const [open, setOpen] = useState(false);
	/** What it opened on: a preview can change `value` while it is open. */
	const opened = useRef(value);
	const shown = parseColour(value);

	return (
		<>
			<button
				ref={anchor}
				type="button"
				className={'well' + (open ? ' open' : '')}
				disabled={disabled}
				title={title ?? 'Pick a colour'}
				onClick={() => {
					opened.current = value;
					setOpen(!open);
				}}
			>
				<span style={{ background: shown ? cssColour(shown) : 'transparent' }} />
			</button>
			{open && anchor.current && (
				<Popover
					anchor={anchor.current}
					value={opened.current}
					alpha={alpha}
					onPreview={onPreview}
					documentColours={documentColours}
					onClose={(picked) => {
						setOpen(false);
						if (picked !== null && picked !== hexOf(toHsva(opened.current), alpha)) {
							remember(picked);
							onCommit(picked);
						}
						else onCancel?.(opened.current);
					}}
				/>
			)}
		</>
	);
}

interface PopoverProps {
	anchor: HTMLElement;
	value: string;
	alpha: boolean;
	onPreview?: (value: string) => void;
	/** With the colour to keep, or null to take it back. */
	onClose: (picked: string | null) => void;
	documentColours?: () => Promise<string[]> | string[];
}

function Popover({ anchor, value, alpha, onPreview, onClose, documentColours }: PopoverProps) {
	const root = useRef<HTMLDivElement>(null);
	const [original] = useState(() => toHsva(value));
	const [colour, setColour] = useState(original);
	const [mode, setModeState] = useState<Mode>(loadMode);
	const [position, setPosition] = useState<{ left: number; top: number } | null>(null);
	const [recent] = useState(loadRecent);
	const [used, setUsed] = useState<string[]>([]);
	const latest = useRef({ colour, onClose, alpha });
	latest.current = { colour, onClose, alpha };
	const hex = hexOf(colour, alpha);
	const rgb = hsvToRgb(colour);

	const setMode = (next: Mode) => {
		setModeState(next);
		try {
			localStorage.setItem(MODE_KEY, next);
		} catch {
			// Only the remembered mode is lost.
		}
	};

	useLayoutEffect(() => {
		const box = root.current!.getBoundingClientRect();
		const target = anchor.getBoundingClientRect();
		const margin = 8;
		let left = target.left;
		let top = target.bottom + 6;
		if (left + box.width > window.innerWidth - margin) left = target.right - box.width;
		if (top + box.height > window.innerHeight - margin) top = target.top - box.height - 6;
		const clampTo = (value: number, size: number, room: number) => Math.max(margin, Math.min(value, room - size - margin));
		setPosition({ left: clampTo(left, box.width, window.innerWidth), top: clampTo(top, box.height, window.innerHeight) });
	}, [anchor]);

	useEffect(() => {
		let live = true;
		Promise.resolve(documentColours?.() ?? []).then((found) => live && setUsed(found.slice(0, 24)));
		return () => {
			live = false;
		};
	}, []);

	// Coalesced to a frame, since a drag reports more often than the canvas can draw.
	const frame = useRef(0);
	const first = useRef(true);
	useEffect(() => {
		if (first.current) {
			first.current = false;
			return;
		}
		cancelAnimationFrame(frame.current);
		frame.current = requestAnimationFrame(() => onPreview?.(hex));
	}, [hex]);

	useEffect(() => {
		const finish = (picked: string | null) => {
			cancelAnimationFrame(frame.current);
			latest.current.onClose(picked);
		};
		const onDown = (event: MouseEvent) => {
			if (root.current?.contains(event.target as Node) || anchor.contains(event.target as Node)) return;
			finish(hexOf(latest.current.colour, latest.current.alpha));
		};
		const onKey = (event: KeyboardEvent) => {
			// A field with an unfinished edit takes Escape back itself.
			if (event.target instanceof HTMLElement && event.target.dataset.dirty) return;
			if (event.key === 'Escape') {
				event.stopPropagation();
				finish(null);
			}
			else if (event.key === 'Enter' && !(event.target instanceof HTMLInputElement)) finish(hexOf(latest.current.colour, latest.current.alpha));
		};
		const onAnchor = () => finish(hexOf(latest.current.colour, latest.current.alpha));
		window.addEventListener('mousedown', onDown, true);
		window.addEventListener('keydown', onKey, true);
		anchor.addEventListener('click', onAnchor);
		return () => {
			window.removeEventListener('mousedown', onDown, true);
			window.removeEventListener('keydown', onKey, true);
			anchor.removeEventListener('click', onAnchor);
		};
	}, [anchor]);

	const pickFromScreen = async () => {
		const EyeDropper = (window as unknown as { EyeDropper?: new () => { open(): Promise<{ sRGBHex: string }> } }).EyeDropper;
		if (!EyeDropper) return;
		try {
			const { sRGBHex } = await new EyeDropper().open();
			const picked = parseColour(sRGBHex);
			if (picked) setColour(rgbToHsv({ ...picked, a: colour.a }, colour));
		} catch {
			// Escape while sampling cancels it.
		}
	};

	const useText = (text: string) => {
		const parsed = parseColour(text);
		if (!parsed) return false;
		setColour(rgbToHsv(alpha ? parsed : { ...parsed, a: 1 }, colour));
		return true;
	};

	const { lightness, saturation } = shades(colour);
	const hasEyeDropper = 'EyeDropper' in window;

	return createPortal(
		<div
			ref={root}
			className="colour-picker"
			style={position ? { left: position.left, top: position.top } : { left: -9999, top: 0 }}
			onContextMenu={(event) => event.preventDefault()}
		>
			<div className="cp-columns">
				<div className="cp-controls">
					<div className="cp-compare">
						<span className="cp-checker">
							<span style={{ background: cssColour(rgb) }} title="New" />
							<button style={{ background: cssColour(hsvToRgb(original)) }} title={`Original, ${hexOf(original, alpha)}. Click to go back to it.`} onClick={() => setColour(original)} />
						</span>
						{hasEyeDropper && (
							<button className="cp-icon" title="Pick from the screen" onClick={pickFromScreen}>
								<EyeDropperIcon />
							</button>
						)}
					</div>
					<div className="cp-hex">
						<label htmlFor="cp-hex">Hex</label>
						<HexField id="cp-hex" value={hex} onCommit={useText} />
						<button className="cp-icon" title="Copy" onClick={() => navigator.clipboard.writeText(hex).catch(() => undefined)}>
							<CopyIcon />
						</button>
						<button className="cp-icon" title="Paste a hex, rgb() or hsl() colour" onClick={() => navigator.clipboard.readText().then((text) => useText(text)).catch(() => undefined)}>
							<PasteIcon />
						</button>
					</div>
					<div className="segmented cp-mode">
						{(['hsv', 'rgb', 'hsl'] as Mode[]).map((entry) => (
							<button key={entry} className={mode === entry ? 'active' : ''} onClick={() => setMode(entry)}>{entry.toUpperCase()}</button>
						))}
					</div>
					<Channels mode={mode} colour={colour} alpha={alpha} onChange={setColour} />
				</div>
				<Plane colour={colour} onChange={setColour} />
			</div>
			<div className="cp-columns cp-swatch-area">
				<div className="cp-controls">
					<Swatches title="Recent" colours={recent} empty="Colours you pick appear here." onPick={(entry) => useText(entry)} />
					{documentColours && <Swatches title="In this document" colours={used} empty="No colours written in this document yet." onPick={(entry) => useText(entry)} />}
				</div>
				<div className="cp-shades">
					<h4>Shades</h4>
					<div className="cp-swatches">
						{lightness.map((entry, index) => <Swatch key={'l' + index} colour={formatHex(entry, alpha)} onPick={(text) => useText(text)} />)}
					</div>
					<div className="cp-swatches">
						{saturation.map((entry, index) => <Swatch key={'s' + index} colour={formatHex(entry, alpha)} onPick={(text) => useText(text)} />)}
					</div>
				</div>
			</div>
		</div>,
		document.body,
	);
}

/** Saturation and value as a square, with hue as a strip beside it. */
function Plane({ colour, onChange }: { colour: Hsva; onChange: (colour: Hsva) => void }) {
	const hueColour = cssColour(hsvToRgb({ h: colour.h, s: 1, v: 1, a: 1 }));
	return (
		<div className="cp-plane">
			<Drag
				className="cp-square"
				style={{ background: `linear-gradient(to top, #000, transparent), linear-gradient(to right, #fff, ${hueColour})` }}
				label="Saturation and value"
				onMove={(x, y) => onChange({ ...colour, s: x, v: 1 - y })}
				onKey={(dx, dy) => onChange({ ...colour, s: clamp01(colour.s + dx / 100), v: clamp01(colour.v - dy / 100) })}
			>
				<span className="cp-thumb" style={{ left: `${colour.s * 100}%`, top: `${(1 - colour.v) * 100}%`, background: cssColour({ ...hsvToRgb(colour), a: 1 }) }} />
			</Drag>
			<Drag
				className="cp-hue"
				label="Hue"
				onMove={(_, y) => onChange({ ...colour, h: y * 360 })}
				onKey={(_, dy) => onChange({ ...colour, h: (colour.h + dy * 3.6 + 360) % 360 })}
			>
				<span className="cp-bar" style={{ top: `${(colour.h / 360) * 100}%` }} />
			</Drag>
		</div>
	);
}

const clamp01 = (value: number) => Math.min(1, Math.max(0, value));

/** A surface dragged across with the pointer, reporting where as 0-1 each way, and nudged with the arrow keys. */
function Drag({ className, style, label, onMove, onKey, children }: {
	className: string;
	style?: React.CSSProperties;
	label: string;
	onMove: (x: number, y: number) => void;
	onKey: (dx: number, dy: number) => void;
	children: React.ReactNode;
}) {
	const report = (event: React.PointerEvent<HTMLDivElement>) => {
		const box = event.currentTarget.getBoundingClientRect();
		onMove(clamp01((event.clientX - box.left) / box.width), clamp01((event.clientY - box.top) / box.height));
	};
	return (
		<div
			className={className}
			style={style}
			role="slider"
			aria-label={label}
			tabIndex={0}
			onPointerDown={(event) => {
				event.currentTarget.setPointerCapture(event.pointerId);
				event.currentTarget.focus();
				report(event);
			}}
			onPointerMove={(event) => {
				if (event.currentTarget.hasPointerCapture(event.pointerId)) report(event);
			}}
			onKeyDown={(event) => {
				const step = event.shiftKey ? 10 : 1;
				const moves: Record<string, [number, number]> = { ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, -step], ArrowDown: [0, step] };
				const move = moves[event.key];
				if (!move) return;
				event.preventDefault();
				onKey(...move);
			}}
		>
			{children}
		</div>
	);
}

interface Channel {
	letter: string;
	name: string;
	/** The value shown, in the field's own units. */
	shown: number;
	max: number;
	unit: string;
	decimals: number;
	set: (shown: number) => Hsva;
	/** The colour at a point along the track, 0-1. */
	at: (t: number) => Hsva;
}

function channels(mode: Mode, colour: Hsva, alpha: boolean): Channel[] {
	const list: Channel[] = [];
	if (mode === 'hsv') {
		list.push(
			{ letter: 'H', name: 'Hue', shown: colour.h, max: 360, unit: '°', decimals: 0, set: (h) => ({ ...colour, h }), at: (t) => ({ ...colour, h: t * 360, a: 1 }) },
			{ letter: 'S', name: 'Saturation', shown: colour.s * 100, max: 100, unit: '%', decimals: 0, set: (s) => ({ ...colour, s: s / 100 }), at: (t) => ({ ...colour, s: t, a: 1 }) },
			{ letter: 'V', name: 'Value', shown: colour.v * 100, max: 100, unit: '%', decimals: 0, set: (v) => ({ ...colour, v: v / 100 }), at: (t) => ({ ...colour, v: t, a: 1 }) },
		);
	}
	else if (mode === 'rgb') {
		const rgb = hsvToRgb(colour);
		const channel = (key: 'r' | 'g' | 'b', letter: string, name: string): Channel => ({
			letter, name, shown: rgb[key] * 255, max: 255, unit: '', decimals: 0,
			set: (shown) => rgbToHsv({ ...rgb, [key]: shown / 255 }, colour),
			at: (t) => rgbToHsv({ ...rgb, [key]: t, a: 1 }, colour),
		});
		list.push(channel('r', 'R', 'Red'), channel('g', 'G', 'Green'), channel('b', 'B', 'Blue'));
	}
	else {
		const hsl = hsvToHsl(colour);
		const from = (h: number, s: number, l: number, a = colour.a) => hslToHsv(h, s, l, a);
		list.push(
			{ letter: 'H', name: 'Hue', shown: hsl.h, max: 360, unit: '°', decimals: 0, set: (h) => from(h, hsl.s, hsl.l), at: (t) => from(t * 360, hsl.s, hsl.l, 1) },
			{ letter: 'S', name: 'Saturation', shown: hsl.s * 100, max: 100, unit: '%', decimals: 0, set: (s) => from(hsl.h, s / 100, hsl.l), at: (t) => from(hsl.h, t, hsl.l, 1) },
			{ letter: 'L', name: 'Lightness', shown: hsl.l * 100, max: 100, unit: '%', decimals: 0, set: (l) => from(hsl.h, hsl.s, l / 100), at: (t) => from(hsl.h, hsl.s, t, 1) },
		);
	}
	if (alpha) {
		list.push({ letter: 'A', name: 'Alpha', shown: colour.a * 100, max: 100, unit: '%', decimals: 0, set: (a) => ({ ...colour, a: a / 100 }), at: (t) => ({ ...colour, a: t }) });
	}
	return list;
}

function Channels({ mode, colour, alpha, onChange }: { mode: Mode; colour: Hsva; alpha: boolean; onChange: (colour: Hsva) => void }) {
	return (
		<div className="cp-channels">
			{channels(mode, colour, alpha).map((channel) => {
				const stops = Array.from({ length: 7 }, (_, index) => cssColour(hsvToRgb(channel.at(index / 6))));
				const fraction = channel.shown / channel.max;
				return (
					<div className="cp-channel" key={channel.letter}>
						<span className="cp-letter" title={channel.name}>{channel.letter}</span>
						<Drag
							className={'cp-track' + (channel.letter === 'A' ? ' cp-checker' : '')}
							label={channel.name}
							onMove={(x) => onChange(channel.set(x * channel.max))}
							onKey={(dx, dy) => onChange(channel.set(Math.min(channel.max, Math.max(0, channel.shown + (dx || -dy) * (channel.max / 100)))))}
						>
							<span className="cp-gradient" style={{ background: `linear-gradient(to right, ${stops.join(', ')})` }} />
							<span className="cp-bar" style={{ left: `${fraction * 100}%` }} />
						</Drag>
						<NumberField channel={channel} onChange={(shown) => onChange(channel.set(shown))} />
					</div>
				);
			})}
		</div>
	);
}

/** A number that commits on Enter or losing focus, steps with the arrow keys, and shows its unit. */
function NumberField({ channel, onChange }: { channel: Channel; onChange: (value: number) => void }) {
	const text = channel.shown.toFixed(channel.decimals);
	const [draft, setDraft] = useState<string | null>(null);
	const apply = () => {
		if (draft === null) return;
		const parsed = parseFloat(draft);
		setDraft(null);
		if (!Number.isNaN(parsed)) onChange(Math.min(channel.max, Math.max(0, parsed)));
	};
	return (
		<label className="cp-number" title={channel.name}>
			<input
				value={draft ?? text}
				data-dirty={draft !== null ? 'true' : undefined}
				spellCheck={false}
				onChange={(event) => setDraft(event.target.value)}
				onFocus={(event) => event.target.select()}
				onBlur={apply}
				onKeyDown={(event) => {
					if (event.key === 'Enter') apply();
					else if (event.key === 'Escape' && draft !== null) {
						setDraft(null);
						event.stopPropagation();
					}
					else if (event.key === 'ArrowUp' || event.key === 'ArrowDown') {
						event.preventDefault();
						const step = (event.shiftKey ? 10 : 1) * (event.key === 'ArrowUp' ? 1 : -1);
						setDraft(null);
						onChange(Math.min(channel.max, Math.max(0, Math.round(channel.shown) + step)));
					}
				}}
			/>
			{channel.unit && <span>{channel.unit}</span>}
		</label>
	);
}

function HexField({ id, value, onCommit }: { id: string; value: string; onCommit: (text: string) => boolean }) {
	const [draft, setDraft] = useState<string | null>(null);
	const apply = () => {
		if (draft === null) return;
		const text = draft.trim();
		setDraft(null);
		onCommit(/^[0-9a-f]{3,8}$/i.test(text) ? '#' + text : text);
	};
	return (
		<input
			id={id}
			className="cp-hex-field"
			value={draft ?? value}
			data-dirty={draft !== null ? 'true' : undefined}
			spellCheck={false}
			onChange={(event) => setDraft(event.target.value)}
			onFocus={(event) => event.target.select()}
			onBlur={apply}
			onKeyDown={(event) => {
				if (event.key === 'Enter') apply();
				else if (event.key === 'Escape' && draft !== null) {
					setDraft(null);
					event.stopPropagation();
				}
			}}
		/>
	);
}

function Swatches({ title, colours, empty, onPick }: { title: string; colours: string[]; empty: string; onPick: (colour: string) => void }) {
	return (
		<section className="cp-section">
			<h4>{title}</h4>
			{colours.length ? (
				<div className="cp-swatches">{colours.map((entry) => <Swatch key={entry} colour={entry} onPick={onPick} />)}</div>
			) : (
				<p className="cp-empty">{empty}</p>
			)}
		</section>
	);
}

function Swatch({ colour, onPick }: { colour: string; onPick: (colour: string) => void }) {
	const parsed = parseColour(colour);
	return (
		<button className="cp-swatch cp-checker" title={colour} onClick={() => onPick(colour)}>
			<span style={{ background: parsed ? cssColour(parsed) : 'transparent' }} />
		</button>
	);
}

const iconProps = { width: 14, height: 14, viewBox: '0 0 16 16', fill: 'none', stroke: 'currentColor', strokeWidth: 1.3, strokeLinecap: 'round' as const, strokeLinejoin: 'round' as const };

function EyeDropperIcon() {
	return (
		<svg {...iconProps} aria-hidden>
			<path d="M10.5 2.5a1.8 1.8 0 0 1 2.5 0l.5.5a1.8 1.8 0 0 1 0 2.5L12 7 9 4z" />
			<path d="M9 4l3 3-6.5 6.5H2.5v-3z" />
		</svg>
	);
}

function CopyIcon() {
	return (
		<svg {...iconProps} aria-hidden>
			<rect x="5.5" y="5.5" width="8" height="8" rx="1.2" />
			<path d="M10.5 3.5v-.8a1.2 1.2 0 0 0-1.2-1.2H3.7a1.2 1.2 0 0 0-1.2 1.2v5.6a1.2 1.2 0 0 0 1.2 1.2h.8" />
		</svg>
	);
}

function PasteIcon() {
	return (
		<svg {...iconProps} aria-hidden>
			<path d="M5.5 3H4a1 1 0 0 0-1 1v9.5a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1V4a1 1 0 0 0-1-1h-1.5" />
			<rect x="5.5" y="1.75" width="5" height="2.5" rx="0.8" />
		</svg>
	);
}
