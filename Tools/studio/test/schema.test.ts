import { describe, expect, it } from 'vitest';
import { extractBindings, inferSchema, mockFromSchema, validate } from '../app/src/data/schema';

const document = `<rml>
<head><style>.a { top: 1dp; }</style></head>
<body data-model="pause" data-class-closing="!bOpen">
	<div class="screen" data-if="bAlive">
		<div class="title">{{ Title }}</div>
		<div class="crumb" data-if="Page != 0">{{ format('%s', Title) }}</div>
		<div class="row" data-for="r : Rows" data-attr-id="'row' + it_index" data-class-off="!r.bEnabled"
			data-event-click="Take(it_index)">
			<div class="name">{{ r.Label }}</div>
			<div class="arrow" data-event-click="Step(it_index, -1)"/>
			<div class="fill" data-style-width="(r.Fraction * 100) + '%'"/>
		</div>
		<div data-model="holo" data-style-opacity="Glow">{{ Caption }}</div>
		<div data-if="Count &gt; 0 &amp;&amp; bReady" data-loc="not_a_binding" data-event-click="Back"/>
	</div>
</body>
</rml>`;

describe('bindings', () => {
	const models = extractBindings(document);
	const pause = models.get('pause')!;

	it('finds every model a document names', () => {
		expect([...models.keys()]).toEqual(['pause', 'holo']);
	});

	it('reads root values, without literals, calls, reserved names or loop variables', () => {
		expect([...pause.values].sort()).toEqual(['Count', 'Page', 'Rows', 'Title', 'bAlive', 'bOpen', 'bReady']);
	});

	it('attributes a loop variable to the array it walks', () => {
		expect([...pause.members.get('Rows')!].sort()).toEqual(['Fraction', 'Label', 'bEnabled']);
	});

	it('reads commands with the arity at the call', () => {
		expect(Object.fromEntries(pause.commands)).toEqual({ Take: 1, Step: 2, Back: 0 });
	});

	it('keeps a nested model to itself', () => {
		expect([...models.get('holo')!.values].sort()).toEqual(['Caption', 'Glow']);
	});
});

describe('schema', () => {
	const mock = {
		bAlive: true,
		Page: 2,
		Title: 'AUDIO',
		Rows: [{ Label: 'VOLUME', bEnabled: true, Fraction: 0.8, Position: 8 }, { Label: 'MUSIC', bEnabled: true, Fraction: 1, Position: 5, bBar: true }],
	};
	const schema = inferSchema('pause', mock, ['UI/screens/menu/pause.rml']);

	it('infers types from a mock, and a row struct from every row', () => {
		expect(schema.fields.bAlive.type).toBe('bool');
		expect(schema.fields.Page.type).toBe('int');
		expect(schema.fields.Title.type).toBe('string');
		expect(schema.fields.Rows).toMatchObject({ type: 'array', of: 'FPauseRow' });
		expect(Object.keys(schema.structs!.FPauseRow.fields)).toEqual(['Label', 'bEnabled', 'Fraction', 'Position', 'bBar']);
		expect(schema.structs!.FPauseRow.fields.Fraction.type).toBe('float');
		expect(schema.structs!.FPauseRow.fields.Position.type).toBe('int');
	});

	it('gives back the mock it was inferred from', () => {
		expect(mockFromSchema(schema)).toEqual(mock);
	});

	it('reports what the markup binds and the model lacks, and the reverse', () => {
		const issues = validate(schema, extractBindings(document).get('pause')!);
		const errors = issues.filter((issue) => issue.severity === 'error').map((issue) => issue.name).sort();
		expect(errors).toEqual(['Back', 'Count', 'Step', 'Take', 'bOpen', 'bReady']);
		expect(issues.filter((issue) => issue.severity === 'warning')).toEqual([]);

		const declared = { ...schema, commands: { Take: ['int Index'], Step: ['int Index'], Back: [] } };
		const arity = validate(declared, extractBindings(document).get('pause')!).filter((issue) => issue.name === 'Step');
		expect(arity[0].message).toContain('2 argument(s) but takes 1');
	});
});
