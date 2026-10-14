/** Markup being dragged out of the palette. */
export const MARKUP_MIME = 'application/x-folio-markup';
/** A live element being dragged within the outline, by id. */
export const ELEMENT_MIME = 'application/x-folio-element';

export interface Snippet {
	name: string;
	hint: string;
	markup: string;
}

/** What the palette offers. Each is built from the classes lib.rml gives every document. */
export const SNIPPETS: Snippet[] = [
	{ name: 'Box', hint: 'An empty container', markup: '<div></div>' },
	{ name: 'Row', hint: 'Children side by side', markup: '<div class="x-row s-gap-m"></div>' },
	{ name: 'Column', hint: 'Children stacked', markup: '<div class="x-col s-gap-m"></div>' },
	{ name: 'Text', hint: 'A line of body text', markup: '<div class="t-body c-text">Text</div>' },
	{ name: 'Heading', hint: 'A title', markup: '<div class="t-title c-text">Heading</div>' },
	{ name: 'Caption', hint: 'Small, dim text', markup: '<div class="t-caption c-dim">Caption</div>' },
	{ name: 'Button', hint: 'A focusable button', markup: '<button class="btn focusable">Button</button>' },
	{ name: 'Primary button', hint: 'The one action a screen leads with', markup: '<button class="btn btn-primary focusable">Button</button>' },
	{ name: 'Panel', hint: 'A framed surface with a body', markup: '<div class="panel">\n\t<div class="panel-body"></div>\n</div>' },
	{ name: 'Divider', hint: 'A hairline between groups', markup: '<div class="divider"/>' },
	{ name: 'Image', hint: 'A sprite from a spritesheet', markup: '<img sprite=""/>' },
	{ name: 'Slider', hint: 'A range input', markup: '<input type="range" class="slider focusable" min="0" max="100" value="50"/>' },
	{ name: 'Rich text', hint: 'Prose with styled runs, as one translatable string', markup: '<text>Prose with <shout>emphasis</> in it.</text>' },
];
