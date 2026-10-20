/*
 * What RML has: the tags with behaviour behind them, and the attributes RmlUi and Folio read.
 *
 * Any other tag is a generic element and is still valid, so completion offers these first and never
 * flags anything else.
 */

export interface AttributeInfo {
	name: string;
	doc: string;
	/** Values to offer. */
	values?: string[];
	/** Written bare, with no value. */
	bare?: boolean;
	/** What the value holds, which decides what is completed inside it. */
	value?: 'expression' | 'event' | 'model' | 'loop' | 'class' | 'style' | 'id' | 'path';
}

export interface ElementInfo {
	doc: string;
	attributes?: AttributeInfo[];
	/** Written self-closing. */
	empty?: boolean;
}

const attr = (name: string, doc: string, extra: Partial<AttributeInfo> = {}): AttributeInfo => ({ name, doc, ...extra });

const INPUT_TYPES = ['text', 'password', 'checkbox', 'radio', 'range', 'submit'];
const DIRECTIONS = ['top', 'right', 'bottom', 'left', 'clockwise', 'counter-clockwise'];

export const ELEMENTS: Record<string, ElementInfo> = {
	rml: { doc: 'The document root.' },
	head: { doc: 'Title, stylesheets, templates and metadata.' },
	body: {
		doc: 'The document element.',
		attributes: [
			attr('template', 'The template this document fills, by its name.'),
			attr('folio-bake-source', 'This document exists to be baked into textures, so the full-screen layer warning does not apply.', { bare: true }),
		],
	},
	title: { doc: 'The document\'s title.' },
	link: {
		doc: 'Links a stylesheet, or a template whose head merges into this one.',
		empty: true,
		attributes: [
			attr('type', '`text/rcss` for a stylesheet, `text/template` for a template.', { values: ['text/rcss', 'text/template'] }),
			attr('href', 'Root-relative path. `/folio/` is the plugin\'s own `rcss/` directory.', { value: 'path' }),
		],
	},
	style: { doc: 'A stylesheet inside the document.' },
	meta: {
		doc: 'Metadata. `<meta name="folio-size" content="1024x640"/>` says what size the document was designed for.',
		empty: true,
		attributes: [attr('name', 'What this names.', { values: ['folio-size'] }), attr('content', 'Its value, such as `1024x640`.')],
	},
	template: { doc: 'A template other documents fill.', attributes: [attr('name', 'What documents name it by.'), attr('content', 'The id of the element documents fill.')] },
	img: {
		doc: 'An image: a sprite, or a file with an optional rectangle.',
		empty: true,
		attributes: [
			attr('sprite', 'A sprite from a `@spritesheet`.'),
			attr('src', 'An image file, or `folio://<name>` for a texture the game registered.', { value: 'path' }),
			attr('rect', '`x y width height` within `src`.'),
			attr('width', 'Width, in pixels.'),
			attr('height', 'Height, in pixels.'),
		],
	},
	handle: {
		doc: 'A drag or resize handle. Its target must be `position: absolute`.',
		attributes: [
			attr('move_target', 'The id of the element this drags.', { value: 'id' }),
			attr('size_target', 'The id of the element this resizes.', { value: 'id' }),
			attr('edge_margin', 'Keeps the target inside its parent.'),
		],
	},
	form: { doc: 'A form. Submitting it raises `submit` with its controls\' values.' },
	input: {
		doc: 'A form control.',
		empty: true,
		attributes: [
			attr('type', 'What kind of control.', { values: INPUT_TYPES }),
			attr('name', 'The value\'s name when the form submits.'),
			attr('value', 'Its value.'),
			attr('checked', 'Checked, for a checkbox or radio.', { bare: true }),
			attr('min', 'The lowest value of a range.'),
			attr('max', 'The highest value of a range.'),
			attr('step', 'A range\'s step.'),
			attr('orientation', 'A range\'s direction.', { values: ['horizontal', 'vertical'] }),
			attr('size', 'A text field\'s width, in characters.'),
			attr('maxlength', 'The most characters a text field takes.'),
			attr('placeholder', 'Text shown while a text field is empty.'),
		],
	},
	select: { doc: 'A drop-down list of `<option>`s.', attributes: [attr('name', 'The value\'s name when the form submits.'), attr('value', 'The selected option\'s value.')] },
	option: { doc: 'An entry in a `<select>`.', attributes: [attr('value', 'What it submits.'), attr('selected', 'Selected to begin with.', { bare: true })] },
	textarea: { doc: 'A multi-line text field.', attributes: [attr('name', 'The value\'s name when the form submits.'), attr('rows', 'Height, in lines.'), attr('cols', 'Width, in characters.'), attr('maxlength', 'The most characters it takes.')] },
	label: { doc: 'Names a form control, and passes clicks on to it.', attributes: [attr('for', 'The id of the control it names.', { value: 'id' })] },
	progress: {
		doc: 'A bar or a dial. Drawn with the `fill-image` property.',
		attributes: [
			attr('value', 'How full, 0 to `max`.'),
			attr('max', 'The value that fills it. 1 by default.'),
			attr('direction', 'Which way it fills.', { values: DIRECTIONS }),
			attr('start-edge', 'Where a radial fill begins.', { values: ['top', 'right', 'bottom', 'left'] }),
		],
	},
	progressbar: { doc: 'The same element as `<progress>`.' },
	tabset: { doc: '`<tabset><tabs><tab>…</tab></tabs><panels><panel>…</panel></panels></tabset>`. Select `tabs:first-of-type` and `panels:first-of-type`: it grows a second, empty pair.' },
	tabs: { doc: 'The tabs of a `<tabset>`.' },
	tab: { doc: 'One tab. Gets `:selected` while active.' },
	panels: { doc: 'The panels of a `<tabset>`.' },
	panel: { doc: 'One panel. Gets `:selected` while its tab is active.' },
	text: { doc: 'Prose in Folio\'s markup rather than RML: `<style>x</>`, `{tag:}`, `{delay:}`. No `{{ }}` and no elements inside. Types out under `reveal: typewriter`.' },
	div: { doc: 'A generic element.' },
	span: { doc: 'A generic element, inline by default.' },
	p: { doc: 'A generic element.' },
	button: { doc: 'A generic element: no focus or click handling until it gets `tab-index: auto`.' },
	br: { doc: 'A line break.', empty: true },
};

export const EVENTS = [
	'click', 'dblclick', 'mousedown', 'mouseup', 'mouseover', 'mouseout', 'mousemove', 'mousescroll',
	'focus', 'blur', 'keydown', 'keyup', 'textinput', 'change', 'submit',
	'drag', 'dragstart', 'dragend', 'dragdrop', 'dragover', 'dragout', 'handledrag',
	'scroll', 'resize', 'animationend', 'transitionend', 'tabchange', 'load', 'unload', 'show', 'hide',
];

/** Attributes any element takes. A name ending in `-` takes a suffix: `data-class-active`. */
export const GLOBAL_ATTRIBUTES: AttributeInfo[] = [
	attr('id', 'The element\'s id.', { value: 'id' }),
	attr('class', 'Classes, separated by spaces.', { value: 'class' }),
	attr('style', 'Inline properties. A stylesheet-only reload does not touch them.', { value: 'style' }),
	attr('autofocus', 'Focused when the document loads, so a controller has somewhere to start.', { bare: true }),
	attr('disabled', 'Sets `:disabled`, and takes the element out of navigation and clicks. Bind it with `data-attrif-disabled`.', { bare: true }),
	attr('data-model', 'Binds a data model to this element and everything inside it.', { value: 'model' }),
	attr('data-for', 'Repeats the element over an array: `row : Rows`, `row, i : Rows`, or `Rows` with `it`.', { value: 'loop' }),
	attr('data-if', 'Adds and removes the element.', { value: 'expression' }),
	attr('data-visible', 'Toggles `visibility`, so the element keeps its box.', { value: 'expression' }),
	attr('data-text', 'Substitutes a value into text content, as `{{ }}` does.', { value: 'expression' }),
	attr('data-rml', 'Substitutes markup rather than text. Never on anything a player can influence.', { value: 'expression' }),
	attr('data-value', 'Two-way binding for a form control.', { value: 'expression' }),
	attr('data-checked', 'Two-way binding for a checkbox or radio.', { value: 'expression' }),
	attr('data-attr-', 'Sets an attribute.', { value: 'expression' }),
	attr('data-attrif-', 'Sets a valueless attribute while truthy.', { value: 'expression' }),
	attr('data-class-', 'Toggles a class.', { value: 'expression' }),
	attr('data-style-', 'Sets a property. Avoid: it writes an inline style.', { value: 'expression' }),
	attr('data-alias-', 'Renames a binding inside a `data-for`.', { value: 'expression' }),
	attr('data-event-', 'Runs a command or an assignment on an event: `Cmd(it_index)`, `bOpen = false`.', { value: 'event' }),
	attr('data-loc', 'A translation key. The element holds text and nothing else: translation replaces all of it.'),
	attr('data-noloc', 'Text that is not language, such as a number, and needs no key.', { bare: true }),
	attr('folio-label', 'Names a control with no text of its own, or whose text reads wrong out loud.'),
	attr('folio-role', 'What kind of control this is, when the tag does not say.'),
	attr('folio-description', 'A sentence read after the name, for a consequence the name cannot carry.'),
	attr('folio-checked', 'The state of a check box built out of `div`s.', { values: ['true', 'false'] }),
	attr('folio-hidden', 'Decoration. Skipped by the checks and by the screen-reader tree.', { bare: true }),
	attr('folio-announce', 'Read out whenever it changes. `assertive` cuts in; bare waits its turn.', { values: ['assertive'] }),
	attr('folio-screen-effect', 'A full-screen wash or flash, hidden when a player turns screen effects off.', { bare: true }),
	attr('folio-inert', 'Out of reach, as the background behind a modal is.', { bare: true }),
	attr('folio-drag', '`move`: the element follows the pointer while dragged.', { values: ['move'] }),
	attr('folio-drop', 'On a drop zone: what happens to what is dropped.', { values: ['hide', 'remove', 'accept'] }),
	attr('folio-close', 'On click, closes the element with this id, or the parent with none.', { value: 'id' }),
];

export function elementInfo(tag: string): ElementInfo | null {
	return ELEMENTS[tag.toLowerCase()] ?? null;
}

/** An attribute's description, matching a prefixed one such as `data-class-active` by its prefix. */
export function attributeInfo(tag: string, name: string): AttributeInfo | null {
	const own = elementInfo(tag)?.attributes?.find((entry) => entry.name === name);
	if (own) return own;
	const exact = GLOBAL_ATTRIBUTES.find((entry) => entry.name === name);
	if (exact) return exact;
	return GLOBAL_ATTRIBUTES.find((entry) => entry.name.endsWith('-') && name.startsWith(entry.name) && name.length > entry.name.length) ?? null;
}

export function attributesFor(tag: string): AttributeInfo[] {
	return [...(elementInfo(tag)?.attributes ?? []), ...GLOBAL_ATTRIBUTES];
}
