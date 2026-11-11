/** Colour maths for the picker. Channels are 0-1 throughout; hue is in degrees. RCSS colours are sRGB. */

export interface Rgba {
	r: number;
	g: number;
	b: number;
	a: number;
}

export interface Hsva {
	h: number;
	s: number;
	v: number;
	a: number;
}

const clamp = (value: number, low = 0, high = 1) => Math.min(high, Math.max(low, value));

const NAMED: Record<string, string> = {
	transparent: '#00000000', black: '#000000', white: '#ffffff', red: '#ff0000', green: '#008000', blue: '#0000ff',
	yellow: '#ffff00', cyan: '#00ffff', aqua: '#00ffff', magenta: '#ff00ff', fuchsia: '#ff00ff', gray: '#808080',
	grey: '#808080', silver: '#c0c0c0', maroon: '#800000', olive: '#808000', lime: '#00ff00', teal: '#008080',
	navy: '#000080', purple: '#800080', orange: '#ffa500',
};

/** An RCSS colour: hex, a few names, rgb()/rgba() and hsl()/hsla(). Alpha in a function is 0-255, as RCSS reads it. */
export function parseColour(text: string): Rgba | null {
	const value = text.trim().toLowerCase();
	if (value in NAMED) return parseColour(NAMED[value]);

	const hex = /^#([0-9a-f]{3,8})$/.exec(value);
	if (hex) {
		let digits = hex[1];
		if (digits.length === 3 || digits.length === 4) digits = digits.split('').map((digit) => digit + digit).join('');
		if (digits.length !== 6 && digits.length !== 8) return null;
		const channel = (index: number) => parseInt(digits.slice(index * 2, index * 2 + 2), 16) / 255;
		return { r: channel(0), g: channel(1), b: channel(2), a: digits.length === 8 ? channel(3) : 1 };
	}

	const call = /^(rgba?|hsla?)\(([^)]*)\)$/.exec(value);
	if (!call) return null;
	const parts = call[2].split(/[\s,/]+/).filter(Boolean);
	if (parts.length < 3) return null;
	const number = (part: string, scale: number) => (part.endsWith('%') ? parseFloat(part) / 100 : parseFloat(part) / scale);
	const alpha = parts.length > 3 ? clamp(number(parts[3], 255)) : 1;
	if (parts.slice(0, 3).some((part) => Number.isNaN(parseFloat(part))) || Number.isNaN(alpha)) return null;

	if (call[1].startsWith('rgb')) return { r: clamp(number(parts[0], 255)), g: clamp(number(parts[1], 255)), b: clamp(number(parts[2], 255)), a: alpha };
	return { ...hslToRgb(parseFloat(parts[0]), clamp(number(parts[1], 100)), clamp(number(parts[2], 100))), a: alpha };
}

const byte = (channel: number) => Math.round(clamp(channel) * 255);
const pair = (channel: number) => byte(channel).toString(16).padStart(2, '0');

/** `#rrggbb`, or `#rrggbbaa` when it is not opaque. */
export function formatHex(colour: Rgba, withAlpha = true): string {
	const rgb = '#' + pair(colour.r) + pair(colour.g) + pair(colour.b);
	return withAlpha && byte(colour.a) < 255 ? rgb + pair(colour.a) : rgb;
}

/** For a CSS background in the picker itself, which is a browser and reads alpha as 0-1. */
export function cssColour(colour: Rgba): string {
	return `rgba(${byte(colour.r)}, ${byte(colour.g)}, ${byte(colour.b)}, ${clamp(colour.a).toFixed(3)})`;
}

export function hsvToRgb({ h, s, v, a }: Hsva): Rgba {
	const f = (n: number) => {
		const k = (n + h / 60) % 6;
		return v - v * s * Math.max(0, Math.min(k, 4 - k, 1));
	};
	return { r: f(5), g: f(3), b: f(1), a };
}

/** Hue and saturation are taken from `previous` where the colour has none of its own (a grey, or black). */
export function rgbToHsv({ r, g, b, a }: Rgba, previous?: Hsva): Hsva {
	const max = Math.max(r, g, b);
	const delta = max - Math.min(r, g, b);
	let h = previous?.h ?? 0;
	if (delta > 0) {
		if (max === r) h = 60 * (((g - b) / delta) % 6);
		else if (max === g) h = 60 * ((b - r) / delta + 2);
		else h = 60 * ((r - g) / delta + 4);
		if (h < 0) h += 360;
	}
	const s = max > 0 ? delta / max : (previous?.s ?? 0);
	return { h, s, v: max, a };
}

export function hslToRgb(h: number, s: number, l: number): Omit<Rgba, 'a'> {
	const k = (n: number) => (n + h / 30) % 12;
	const amount = s * Math.min(l, 1 - l);
	const f = (n: number) => l - amount * Math.max(-1, Math.min(k(n) - 3, 9 - k(n), 1));
	return { r: f(0), g: f(8), b: f(4) };
}

export function hsvToHsl({ h, s, v }: Hsva): { h: number; s: number; l: number } {
	const l = v * (1 - s / 2);
	return { h, s: l === 0 || l === 1 ? 0 : (v - l) / Math.min(l, 1 - l), l };
}

export function hslToHsv(h: number, s: number, l: number, a: number): Hsva {
	const v = l + s * Math.min(l, 1 - l);
	return { h, s: v === 0 ? 0 : 2 * (1 - l / v), v, a };
}

/** Every distinct colour written in some RCSS or RML, most used first. */
export function coloursIn(texts: string[]): string[] {
	const counts = new Map<string, number>();
	for (const text of texts) {
		for (const match of text.matchAll(/#[0-9a-f]{3,8}\b|\b(?:rgba?|hsla?)\([^)]*\)/gi)) {
			const colour = parseColour(match[0]);
			if (!colour) continue;
			const hex = formatHex(colour);
			counts.set(hex, (counts.get(hex) ?? 0) + 1);
		}
	}
	return [...counts.entries()].sort((a, b) => b[1] - a[1]).map(([hex]) => hex);
}

/** Lighter to darker versions of a colour at its own hue and saturation, then greyer to purer ones. */
export function shades(colour: Hsva): { lightness: Rgba[]; saturation: Rgba[] } {
	const { h, s } = hsvToHsl(colour);
	const steps = [0.92, 0.82, 0.7, 0.58, 0.46, 0.34, 0.22, 0.1];
	const { l } = hsvToHsl(colour);
	return {
		lightness: steps.map((step) => ({ ...hslToRgb(h, s, step), a: colour.a })),
		saturation: steps.map((_, index) => ({ ...hslToRgb(h, (index + 1) / steps.length, l), a: colour.a })),
	};
}
