import { describe, expect, it } from 'vitest';
import { appendRule, applyEdits, findDeclarations, findRule, findRules, removeDeclaration, setDeclaration, setInlineStyle } from '../app/src/edit/rcss';
import { attributeOf, duplicateElement, elementAt, insertMarkup, moveElement, ownStyleBlock, parseRml, removeElement, setAttribute, setText } from '../app/src/edit/rml';

// Every case asserts on the whole text. The promise is that nothing else moves, and only a full
// comparison can hold that.

const sheet = `/* A stylesheet with things worth preserving. */

.panel
{
	display: flex;      /* a trailing comment */
	left: 10dp;
}

/* A comment between rules. */
.btn { color: #fff; }

@media (theme: high-contrast)
{
	.btn { color: #000; }
}
`;

describe('rcss', () => {
	it('changes a value and touches nothing else', () => {
		const rule = findRule(sheet, '.panel')!;
		expect(applyEdits(sheet, [setDeclaration(sheet, rule, 'left', '42dp')])).toBe(sheet.replace('left: 10dp;', 'left: 42dp;'));
	});

	it('adds a property after the last declaration with the body indent', () => {
		const rule = findRule(sheet, '.panel')!;
		expect(applyEdits(sheet, [setDeclaration(sheet, rule, 'top', '8dp')])).toBe(sheet.replace('left: 10dp;\n', 'left: 10dp;\n\ttop: 8dp;\n'));
	});

	it('keeps a one-line rule on one line', () => {
		const rule = findRule(sheet, '.btn', 10)!;
		expect(applyEdits(sheet, [setDeclaration(sheet, rule, 'padding', '4dp')])).toBe(sheet.replace('.btn { color: #fff; }', '.btn { color: #fff; padding: 4dp; }'));
	});

	it('tells a themed rule from the plain one by line', () => {
		expect(findRule(sheet, '.btn', 10)!.line).toBe(10);
		expect(findRule(sheet, '.btn', 14)!.line).toBe(14);
		expect(findRule(sheet, '@media (theme: high-contrast) .btn')!.line).toBe(14);
		expect(findRule(sheet, '.missing')).toBeNull();
	});

	it('finds a rule by any one of its selector list', () => {
		const text = '.ring { top: 1dp; }\n\n.glyph, .shape,\n.pointer { width: 112dp; }';
		const rule = findRule(text, '.shape', 3)!;
		expect(applyEdits(text, [setDeclaration(text, rule, 'width', '120dp')])).toBe('.ring { top: 1dp; }\n\n.glyph, .shape,\n.pointer { width: 120dp; }');
		expect(findRule(text, '.pointer')!.line).toBe(3);
		expect(findRule(text, '.glyph, .shape, .pointer')!.line).toBe(3);
		expect(findRule(text, '.sha')).toBeNull();
	});

	it('does not read a comment as part of a declaration', () => {
		const rule = findRule(sheet, '.panel')!;
		expect(findDeclarations(sheet, rule.bodyStart, rule.bodyEnd).map((declaration) => declaration.name)).toEqual(['display', 'left']);
	});

	it('reads a last declaration with no semicolon, and adds after it', () => {
		const text = '.a { color: red }';
		const rule = findRule(text, '.a')!;
		expect(findDeclarations(text, rule.bodyStart, rule.bodyEnd)[0].value).toBe('red');
		expect(applyEdits(text, [setDeclaration(text, rule, 'top', '1dp')])).toBe('.a { color: red; top: 1dp; }');
	});

	it('removes a declaration with its line', () => {
		const rule = findRule(sheet, '.panel')!;
		expect(applyEdits(sheet, [removeDeclaration(sheet, rule, 'left')!])).toBe(sheet.replace('\tleft: 10dp;\n', ''));
	});

	it('finds rules only inside a given range', () => {
		const text = '<style>\n\t.a { top: 1dp; }\n</style>\n<body class=".b { }"/>';
		const from = text.indexOf('>') + 1;
		const rules = findRules(text, { from, to: text.indexOf('</style>') });
		expect(rules.map((rule) => rule.selector)).toEqual(['.a']);
		expect(rules[0].line).toBe(2);
	});

	it('appends a rule inside a style block at its indent', () => {
		const text = '<head>\n\t<style>\n\t\t.a { top: 1dp; }\n\t</style>\n</head>';
		const range = { from: text.indexOf('<style>') + 7, to: text.indexOf('</style>') };
		expect(applyEdits(text, [appendRule(text, range, '#x', [['left', '2dp']])])).toBe(
			'<head>\n\t<style>\n\t\t.a { top: 1dp; }\n\n\t\t#x { left: 2dp; }\n\t</style>\n</head>');
	});

	it('edits a style attribute in place', () => {
		expect(setInlineStyle('', 'left', '4dp')).toBe('left: 4dp;');
		expect(setInlineStyle('left: 1dp; top: 2dp;', 'left', '4dp')).toBe('left: 4dp; top: 2dp;');
		expect(setInlineStyle('left: 1dp', 'top', '2dp')).toBe('left: 1dp; top: 2dp;');
		expect(setInlineStyle('left: 1dp; top: 2dp;', 'left', null)).toBe('top: 2dp;');
	});
});

const markup = `<rml>
<head>
	<title>Test</title>
	<style>
		.row { display: flex; }
	</style>
</head>

<body class="screen">
	<!-- a comment <div> that is not an element -->
	<div class="menu" id="menu">
		<button class="btn" id="one"
			data-loc="first">One</button>
		<button class="btn" id="two">Two</button>
		<text id="prose">You <angry>must</> go</text>
	</div>

	<div class="build"/>
</body>
</rml>
`;

function find(text: string, id: string) {
	const document = parseRml(text);
	const search = (elements: ReturnType<typeof parseRml>['roots']): (typeof elements)[number] | null => {
		for (const element of elements) {
			if (attributeOf(element, 'id')?.value === id) return element;
			const inner = search(element.children);
			if (inner) return inner;
		}
		return null;
	};
	return { document, element: search(document.roots)! };
}

describe('rml', () => {
	it('parses structure, skipping comments and raw content', () => {
		const document = parseRml(markup);
		expect(document.body!.children.map((child) => child.tag)).toEqual(['div', 'div']);
		const menu = document.body!.children[0];
		expect(menu.children.map((child) => child.tag)).toEqual(['button', 'button', 'text']);
		expect(menu.children[2].children).toEqual([]);
		expect(document.body!.children[1].selfClosing).toBe(true);
		expect(document.styles.length).toBe(1);
		expect(markup.slice(document.styles[0].from, document.styles[0].to)).toContain('.row');
		expect(menu.children[0].hasText).toBe(true);
		expect(menu.hasText).toBe(false);
	});

	it('changes one attribute value', () => {
		const { element } = find(markup, 'one');
		expect(applyEdits(markup, [setAttribute(markup, element, 'class', 'btn btn-primary')!])).toBe(markup.replace('class="btn" id="one"', 'class="btn btn-primary" id="one"'));
	});

	it('adds an attribute after the last one, across lines', () => {
		const { element } = find(markup, 'one');
		expect(applyEdits(markup, [setAttribute(markup, element, 'autofocus', '')!])).toBe(markup.replace('data-loc="first">', 'data-loc="first" autofocus="">'));
	});

	it('removes an attribute with the whitespace before it', () => {
		const { element } = find(markup, 'one');
		expect(applyEdits(markup, [setAttribute(markup, element, 'data-loc', null)!])).toBe(markup.replace('id="one"\n\t\t\tdata-loc="first">', 'id="one">'));
	});

	it('escapes what an attribute or text cannot hold', () => {
		const { element } = find(markup, 'two');
		expect(applyEdits(markup, [setAttribute(markup, element, 'data-if', 'a < 2 && b')!])).toContain('data-if="a &lt; 2 &amp;&amp; b"');
		expect(applyEdits(markup, [setText(markup, element, 'A & B')])).toContain('>A &amp; B</button>');
	});

	it('removes an element and its line', () => {
		const { element } = find(markup, 'two');
		expect(applyEdits(markup, [removeElement(markup, element)])).toBe(markup.replace('\t\t<button class="btn" id="two">Two</button>\n', ''));
	});

	it('inserts before a sibling at its indent', () => {
		const { element } = find(markup, 'menu');
		expect(applyEdits(markup, [insertMarkup(markup, element, 1, '<div class="gap"/>')])).toBe(
			markup.replace('\t\t<button class="btn" id="two">', '\t\t<div class="gap"/>\n\t\t<button class="btn" id="two">'));
	});

	it('appends a multi-line child above the closing tag', () => {
		const { element } = find(markup, 'menu');
		expect(applyEdits(markup, [insertMarkup(markup, element, 99, '<div>\n\t<p>x</p>\n</div>')])).toBe(
			markup.replace('</text>\n\t</div>', '</text>\n\t\t<div>\n\t\t\t<p>x</p>\n\t\t</div>\n\t</div>'));
	});

	it('opens a self-closing element to take a child', () => {
		const document = parseRml(markup);
		const build = document.body!.children[1];
		expect(applyEdits(markup, [insertMarkup(markup, build, 0, '<p>x</p>')])).toBe(
			markup.replace('<div class="build"/>', '<div class="build">\n\t\t<p>x</p>\n\t</div>'));
	});

	it('moves an element between parents, and refuses to move it into itself', () => {
		const { document, element } = find(markup, 'two');
		const build = document.body!.children[1];
		const moved = applyEdits(markup, moveElement(markup, element, build, 0)!);
		expect(moved).toContain('<div class="build">\n\t\t<button class="btn" id="two">Two</button>\n\t</div>');
		expect(moved).not.toContain('id="one">One</button>\n\t\t<button class="btn" id="two">');

		const menu = element.parent!;
		expect(moveElement(markup, menu, menu.children[0], 0)).toBeNull();
		expect(moveElement(markup, element, menu, 1)).toBeNull();
		expect(moveElement(markup, element, menu, 2)).toBeNull();
	});

	it('reorders siblings', () => {
		const { element } = find(markup, 'two');
		const moved = applyEdits(markup, moveElement(markup, element, element.parent!, 0)!);
		expect(moved.indexOf('id="two"')).toBeLessThan(moved.indexOf('id="one"'));
		expect(parseRml(moved).body!.children[0].children.map((child) => attributeOf(child, 'id')?.value)).toEqual(['two', 'one', 'prose']);
	});

	it('duplicates an element after itself', () => {
		const { element } = find(markup, 'two');
		const result = applyEdits(markup, [duplicateElement(markup, element)!]);
		expect(result).toContain('<button class="btn" id="two">Two</button>\n\t\t<button class="btn" id="two">Two</button>\n\t\t<text');
	});

	it('finds an element again by where it starts', () => {
		const { document, element } = find(markup, 'two');
		expect(elementAt(document, element.start)).toBe(element);
	});

	it('creates a style block when the head has none', () => {
		const text = '<rml>\n<head>\n\t<title>T</title>\n</head>\n<body/>\n</rml>\n';
		const block = ownStyleBlock(text, parseRml(text))!;
		expect('create' in block).toBe(true);
		if ('create' in block) {
			expect(applyEdits(text, [block.create('#a { top: 1dp; }')])).toBe(
				'<rml>\n<head>\n\t<title>T</title>\n\t<style>\n\t\t#a { top: 1dp; }\n\t</style>\n</head>\n<body/>\n</rml>\n');
		}
	});
});
