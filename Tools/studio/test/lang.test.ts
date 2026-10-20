import { describe, expect, it } from 'vitest';
import { Schema } from '../app/src/data/schema';
import { checkRcss, checkRml } from '../app/src/lang/check';
import { complete, hover, NO_SOURCES, signature, Sources } from '../app/src/lang/complete';
import { classify } from '../app/src/lang/context';
import { propertyNames, vocabulary } from '../app/src/lang/vocabulary';

/** Splits the last `|` out of a text, giving the text and where the cursor was. */
function at(marked: string): [string, number] {
	const offset = marked.lastIndexOf('|');
	return [marked.slice(0, offset) + marked.slice(offset + 1), offset];
}

const labels = (marked: string, language: 'rml' | 'rcss' = 'rml', sources: Sources = NO_SOURCES) => {
	const [text, offset] = at(marked);
	return complete(text, offset, language, sources).map((item) => item.label);
};

const schema: Schema = {
	model: 'pause',
	fields: {
		Title: { type: 'text', doc: 'The heading.' },
		Rows: { type: 'array', of: 'FPauseRow' },
		Volume: { type: 'float', writable: true },
	},
	structs: { FPauseRow: { fields: { Label: { type: 'text' }, Cells: { type: 'array', of: 'FCell' } } }, FCell: { fields: { Value: { type: 'int' } } } },
	commands: { Resume: [], Adjust: ['int Index', 'int Delta'] },
};

const sources: Sources = { schema: (name) => (name === 'pause' ? schema : null), models: ['pause', 'hud'], stylesheets: [{ text: '.btn { } .btn-primary:hover { }' }] };

describe('vocabulary', () => {
	it('holds what RCSS has and none of what it lacks', () => {
		for (const name of ['decorator', 'nav', 'font-effect', 'tab-index', 'reveal', 'drag']) expect(propertyNames()).toContain(name);
		for (const name of ['grid', 'text-shadow', 'outline', 'background-image']) expect(propertyNames()).not.toContain(name);
		expect(vocabulary.decorators).toContain('dashed-border');
		expect(vocabulary.filters).toContain('outside-blur');
		expect(vocabulary.fontEffects).toContain('fill-image');
		expect(vocabulary.animationKeywords).toContain('cubic-out');
	});
});

describe('context', () => {
	it('tells tag, attribute, value and content apart', () => {
		expect(classify(...at('<di|'), 'rml').kind).toBe('tag');
		expect(classify(...at('<div cl|'), 'rml').kind).toBe('attribute');
		expect(classify(...at('<div class="a b|"'), 'rml').kind).toBe('value');
		expect(classify(...at('<div>text|</div>'), 'rml').kind).toBe('none');
		expect(classify(...at('<!-- <div | -->'), 'rml').kind).toBe('none');
	});

	it('reads a <style> block and a style attribute as RCSS', () => {
		const sheet = classify(...at('<style>.a { col| }</style>'), 'rml');
		expect(sheet.kind === 'rcss' && sheet.context.kind).toBe('property');
		const inline = classify(...at('<div style="display: fl|">'), 'rml');
		expect(inline.kind === 'rcss' && inline.context.kind === 'value' && inline.context.property).toBe('display');
	});

	it('follows loops back to the arrays they walk', () => {
		const context = classify(...at('<div data-model="pause"><div data-for="row : Rows"><div data-for="cell : row.Cells">{{ cell.| }}</div></div></div>'), 'rml');
		expect(context.kind).toBe('expression');
		if (context.kind !== 'expression') return;
		expect(context.scope.model).toBe('pause');
		expect(context.path).toEqual(['cell']);
		expect(context.scope.aliases.map((alias) => alias.name)).toEqual(['row', 'it_index', 'cell', 'it_index']);
	});

	it('reads a loop\'s array in the scope outside the loop', () => {
		const context = classify(...at('<div data-model="pause"><div data-for="row : Ro|">'), 'rml');
		expect(context.kind === 'expression' && context.scope.aliases.length).toBe(0);
	});
});

describe('completion', () => {
	it('offers properties, keywords and functions', () => {
		expect(labels('.a { dec| }', 'rcss')).toContain('decorator');
		expect(labels('.a { display: | }', 'rcss')).toEqual(expect.arrayContaining(['flex', 'block', 'none']));
		expect(labels('.a { decorator: | }', 'rcss')).toEqual(expect.arrayContaining(['image', 'dashed-border', 'none']));
		expect(labels('.a { transition: opacity 0.2s | }', 'rcss')).toContain('cubic-out');
		expect(labels('.a { filter: | }', 'rcss')).not.toContain('outside-blur');
		expect(labels('.a { backdrop-filter: | }', 'rcss')).toContain('outside-blur');
	});

	it('offers only @spritesheet\'s own declarations inside one', () => {
		expect(labels('@spritesheet icons { s| }', 'rcss')).toEqual(['src', 'resolution']);
	});

	it('offers classes from the document and its linked sheets', () => {
		const offered = labels('<style>.menu { }</style><div class="m|"', 'rml', sources);
		expect(offered).toEqual(expect.arrayContaining(['menu', 'btn', 'btn-primary']));
		expect(labels('.btn, .|', 'rcss', sources)).toEqual(expect.arrayContaining(['btn']));
		expect(labels('.btn:|', 'rcss')).toContain('focus-visible');
	});

	it('offers tags, attributes and attribute values', () => {
		expect(labels('<prog|')).toContain('progress');
		expect(labels('<img |')).toEqual(expect.arrayContaining(['sprite', 'src', 'data-if', 'folio-label']));
		expect(labels('<img sprite="x" |')).not.toContain('sprite');
		expect(labels('<input type="|"')).toEqual(['text', 'password', 'checkbox', 'radio', 'range', 'submit']);
		expect(labels('<div data-model="|"', 'rml', sources)).toEqual(['pause', 'hud']);
		expect(labels('<div |')).toContain('data-event-click');
	});

	it('closes the innermost open tag', () => {
		const [text, offset] = at('<div><span></|');
		expect(complete(text, offset, 'rml')[0]).toMatchObject({ label: 'span', insert: 'span>' });
	});

	it('offers the fields of a model, and of a row', () => {
		expect(labels('<p data-model="pause">{{ | }}</p>', 'rml', sources)).toEqual(expect.arrayContaining(['Title', 'Rows', 'Volume']));
		expect(labels('<div data-model="pause"><p data-for="row : Rows">{{ row.| }}</p></div>', 'rml', sources)).toEqual(['Label', 'Cells']);
		expect(labels('<div data-model="pause"><p data-for="Rows">{{ it.| }}</p></div>', 'rml', sources)).toEqual(['Label', 'Cells']);
		expect(labels('<div data-model="pause"><p data-for="r : Rows"><b data-for="c : r.Cells">{{ c.| }}</b></p></div>', 'rml', sources)).toEqual(['Value']);
		expect(labels('<p data-model="pause" data-if="Rows.|">', 'rml', sources)).toEqual(['size']);
		expect(labels('<p data-model="pause">{{ Title | |}}</p>', 'rml', sources)).toContain('to_upper');
	});

	it('offers commands where an event begins a statement', () => {
		const [text, offset] = at('<p data-model="pause" data-event-click="|">');
		const items = complete(text, offset, 'rml', sources);
		expect(items.find((item) => item.label === 'Adjust')).toMatchObject({ insert: 'Adjust(${1:Index}, ${2:Delta})', snippet: true });
		expect(labels('<p data-model="pause" data-event-click="Adjust(|">', 'rml', sources)).not.toContain('Resume');
	});
});

describe('hover and signatures', () => {
	it('describes properties and bindings', () => {
		const [sheet, inSheet] = at('.a { na|v: auto; }');
		expect(hover(sheet, inSheet, 'rcss')?.markdown).toContain('nav-up');
		const [banned, inBanned] = at('.a { text-sh|adow: none; }');
		expect(hover(banned, inBanned, 'rcss')?.markdown).toContain('font-effect: shadow');
		const [markup, inMarkup] = at('<p data-model="pause">{{ Ti|tle }}</p>');
		expect(hover(markup, inMarkup, 'rml', sources)?.markdown).toContain('The heading.');
	});

	it('follows the argument being typed', () => {
		const [text, offset] = at('<p data-model="pause" data-event-click="Adjust(it_index, |)">');
		expect(signature(text, offset, sources)).toMatchObject({ label: 'Adjust(int Index, int Delta)', active: 1 });
	});
});

describe('checks', () => {
	const messages = (text: string) => checkRcss(text).map((finding) => text.slice(finding.from, finding.to));

	it('flags what RCSS silently ignores', () => {
		expect(messages('.a { text-shadow: none; colr: red; display: grid; }')).toEqual(['text-shadow', 'colr', 'grid']);
		expect(messages('.a { transition: opacity 120ms ease-out; }')).toEqual(['120ms', 'ease-out']);
		expect(messages('.a { color: rgba(0, 0, 0, 0.5); }')).toEqual(['rgba(0, 0, 0, 0.5)']);
		expect(messages('.a { decorator: imag(x.png); }')).toEqual(['imag']);
		expect(messages('.a { display: flexx; }')).toEqual(['flexx']);
		expect(messages('.a::before { }')).toEqual(['::before']);
	});

	it('passes what is right', () => {
		expect(checkRcss('.a { display: flex; align-items: flex-start; transition: opacity 0.12s cubic-out; decorator: dashed-border(#fff 1dp); border-radius: 50%; }')).toEqual([]);
	});

	it('checks a document\'s style blocks, style attributes and <text>', () => {
		const text = '<rml><head><style>.a { outline: none; }</style></head><body><div style="grid-area: x"/><text>{{ Name }}</text></body></rml>';
		expect(checkRml(text).map((finding) => text.slice(finding.from, finding.to))).toEqual(['outline', 'grid-area', '{{']);
	});
});
