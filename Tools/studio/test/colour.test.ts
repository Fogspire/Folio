import { describe, expect, it } from 'vitest';
import { coloursIn, formatHex, hslToHsv, hsvToHsl, hsvToRgb, parseColour, rgbToHsv } from '../app/src/colour';

describe('colour', () => {
	it('reads RCSS colours, with function alpha as 0-255', () => {
		expect(formatHex(parseColour('#abc')!)).toBe('#aabbcc');
		expect(formatHex(parseColour('#11223380')!)).toBe('#11223380');
		expect(formatHex(parseColour('rgba(255, 0, 0, 128)')!)).toBe('#ff000080');
		expect(formatHex(parseColour('hsl(120, 100%, 50%)')!)).toBe('#00ff00');
		expect(formatHex(parseColour('transparent')!)).toBe('#00000000');
		expect(parseColour('auto')).toBeNull();
	});

	it('round-trips through HSV and HSL', () => {
		for (const hex of ['#3c8dbc', '#000000', '#ffffff', '#7f1a99', '#12345678']) {
			const hsv = rgbToHsv(parseColour(hex)!);
			expect(formatHex(hsvToRgb(hsv))).toBe(hex);
			const { h, s, l } = hsvToHsl(hsv);
			expect(formatHex(hsvToRgb(hslToHsv(h, s, l, hsv.a)))).toBe(hex);
		}
	});

	it('keeps the hue of a colour that turns grey', () => {
		expect(rgbToHsv(parseColour('#808080')!, { h: 200, s: 0.5, v: 0.5, a: 1 }).h).toBe(200);
	});

	it('lists a document\'s colours, most used first', () => {
		expect(coloursIn(['a { color: #fff; } b { color: #ffffff; background: #000; }'])).toEqual(['#ffffff', '#000000']);
	});
});
