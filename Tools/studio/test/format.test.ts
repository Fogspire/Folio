import { readdirSync, readFileSync, statSync } from 'fs';
import { join } from 'path';
import { describe, expect, it } from 'vitest';
import { formatRcss, formatRml, minimalEdits } from '../app/src/edit/format';
import { applyEdits } from '../app/src/edit/rcss';

const folio = join(__dirname, '..', '..', '..');

function documents(): string[] {
	const out: string[] = [];
	const walk = (directory: string) => {
		for (const name of readdirSync(directory)) {
			const path = join(directory, name);
			if (statSync(path).isDirectory()) walk(path);
			else if (/\.(rml|rcss)$/.test(name)) out.push(path);
		}
	};
	for (const directory of ['rcss', 'examples', 'templates', 'UI']) {
		try {
			walk(join(folio, directory));
		} catch {
			// A checkout without that directory has nothing there to format.
		}
	}
	return out;
}

describe('format', () => {
	it('is a no-op the second time on everything Folio ships', () => {
		const files = documents();
		expect(files.length).toBeGreaterThan(10);
		for (const path of files) {
			const format = path.endsWith('.rml') ? formatRml : formatRcss;
			const once = format(readFileSync(path, 'utf8'));
			expect(format(once), path).toBe(once);
		}
	});

	it('reindents markup by nesting', () => {
		const messy = '<rml>\n<head>\n<title>T</title>\n</head>\n<body>\n<div class="a">\n        <span>x</span>\n<div>\n<p>y</p>\n   </div>\n</div>\n</body>\n</rml>\n';
		expect(formatRml(messy)).toBe('<rml>\n<head>\n\t<title>T</title>\n</head>\n<body>\n<div class="a">\n\t<span>x</span>\n\t<div>\n\t\t<p>y</p>\n\t</div>\n</div>\n</body>\n</rml>\n');
	});

	it('keeps whether <rml> and <body> indent their children', () => {
		const indented = '<rml>\n\t<head>\n\t\t<title>T</title>\n\t</head>\n\t<body>\n\t\t<div/>\n\t</body>\n</rml>\n';
		expect(formatRml(indented)).toBe(indented);
	});

	it('indents once for elements opened together on one line', () => {
		const text = '<body>\n\t<div class="wrap"><div class="inner">\n\t\t<span/>\n\t</div></div>\n</body>\n';
		expect(formatRml(text)).toBe(text);
	});

	it('indents a wrapped tag\'s attributes one past it', () => {
		expect(formatRml('<body>\n\t<button class="a"\n  id="b">Go</button>\n</body>\n')).toBe('<body>\n\t<button class="a"\n\t\tid="b">Go</button>\n</body>\n');
	});

	it('spaces attributes and leaves their values alone', () => {
		expect(formatRml('<div   class = "a  b"    id="c"  >x</div>\n')).toBe('<div class="a  b" id="c">x</div>\n');
		expect(formatRml('<img sprite="x"   />\n')).toBe('<img sprite="x" />\n');
		expect(formatRml('<img sprite="x"/>\n')).toBe('<img sprite="x"/>\n');
		expect(formatRml('<title >T</title >\n<p>a</ p>\n')).toBe('<title>T</title>\n<p>a</p>\n');
	});

	it('moves comments and <text> as blocks, keeping their own layout', () => {
		const text = '<body>\n<div>\n<!--\n  one\n    two\n-->\n<text>a <emph>b</>\n   c</text>\n</div>\n</body>\n';
		expect(formatRml(text)).toBe('<body>\n<div>\n\t<!--\n\t  one\n\t    two\n\t-->\n\t<text>a <emph>b</>\n\t   c</text>\n</div>\n</body>\n');
	});

	it('formats a <style> block as a stylesheet at its depth', () => {
		const text = '<rml>\n<head>\n<style>\n.a\n{\ncolor:#fff;\n}\n.b{ left:0 }\n</style>\n</head>\n</rml>\n';
		expect(formatRml(text)).toBe('<rml>\n<head>\n\t<style>\n\t\t.a\n\t\t{\n\t\t\tcolor: #fff;\n\t\t}\n\t\t.b { left: 0; }\n\t</style>\n</head>\n</rml>\n');
	});

	it('normalises declarations without reflowing them', () => {
		expect(formatRcss('.a{color :red;left:0}\n')).toBe('.a { color: red; left: 0; }\n');
		expect(formatRcss('.a {\n  left: 0; top: 0;\n}\n')).toBe('.a {\n\tleft: 0; top: 0;\n}\n');
		expect(formatRcss('.c-bg      { color: #000; }\n.c-surface { color: #111; }\n')).toBe('.c-bg      { color: #000; }\n.c-surface { color: #111; }\n');
	});

	it('indents inside @media and keeps aligned continuations', () => {
		expect(formatRcss('@media (theme: x)\n{\n.a { left: 0; }\n}\n')).toBe('@media (theme: x)\n{\n\t.a { left: 0; }\n}\n');
		const aligned = '.a\n{\n\ttransition: opacity 0.1s cubic-out,\n\t            transform 0.1s cubic-out;\n}\n';
		expect(formatRcss(aligned)).toBe(aligned);
		const hanging = '.a { left: 0;\n     top: 0; }\n';
		expect(formatRcss(hanging)).toBe(hanging);
	});

	it('collapses blank lines and trailing space, and keeps CRLF', () => {
		expect(formatRcss('.a { }   \n\n\n\n.b { }\n\n')).toBe('.a { }\n\n.b { }\n');
		expect(formatRcss('.a\r\n{\r\nleft: 0;\r\n}\r\n')).toBe('.a\r\n{\r\n\tleft: 0;\r\n}\r\n');
	});

	it('gives edits that reproduce the formatted text', () => {
		for (const [before, after] of [['a\nb\nc', 'a\nB\nc'], ['a\n\n\nb', 'a\n\nb'], ['x', 'x'], ['a\nb', 'a\nb\nc'], ['a\nb\nc', 'a'], ['a\n\n\nb', 'a\n\n\tb'], ['', 'a\nb']]) {
			expect(applyEdits(before, minimalEdits(before, after))).toBe(after);
		}
		const lines = ['<a>', '\t<b/>', '', '\t', 'text', '</a>', '<c>'];
		let seed = 7;
		const random = (n: number) => (seed = (seed * 1103515245 + 12345) % 2147483648) % n;
		for (let round = 0; round < 500; round++) {
			const make = () => Array.from({ length: random(8) }, () => lines[random(lines.length)]).join('\n');
			const before = make();
			const after = make();
			expect(applyEdits(before, minimalEdits(before, after))).toBe(after);
		}
	});

	it('keeps a reindent to its own line when blank lines elsewhere change', () => {
		for (const line of ['<title>', '<title>X</title >']) {
			const before = `<head>\n\t\t${line}\n\n\n</head>\n<body>\n</body>\n\n`;
			const edits = minimalEdits(before, formatRml(before));
			const start = before.indexOf(line);
			const onLine = edits.filter((edit) => edit.from <= start + line.length && edit.to >= start);
			expect(onLine.length > 0 && onLine.every((edit) => !edit.text.includes('\n'))).toBe(true);
			expect(applyEdits(before, edits)).toBe(formatRml(before));
		}
	});
});
