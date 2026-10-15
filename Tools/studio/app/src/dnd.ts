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
	{ name: 'Rich text', hint: 'Prose with styled runs, as one translatable string', markup: '<text>Prose with <shout>emphasis</> in it.</text>' },
	{ name: 'Button', hint: 'A focusable button', markup: '<button class="btn focusable">Button</button>' },
	{ name: 'Primary button', hint: 'The one action a screen leads with', markup: '<button class="btn btn-primary focusable">Button</button>' },
	{ name: 'Panel', hint: 'A framed surface with a body', markup: '<div class="panel">\n\t<div class="panel-body"></div>\n</div>' },
	{ name: 'Divider', hint: 'A hairline between groups', markup: '<div class="divider"/>' },
	{ name: 'Image', hint: 'A sprite from a spritesheet', markup: '<img sprite=""/>' },
	{ name: 'Slider', hint: 'A range input', markup: '<input type="range" class="slider focusable" folio-label="Value" min="0" max="100" value="50"/>' },
	{
		name: 'Menu',
		hint: 'A vertical menu whose rows take focus',
		markup: '<div class="menu">\n\t<div class="menu-item focusable"><div class="menu-marker"/><span class="menu-item-label">First</span></div>\n\t<div class="menu-item focusable"><div class="menu-marker"/><span class="menu-item-label">Second</span></div>\n</div>',
	},
	{
		name: 'List',
		hint: 'A scrolling column of focusable rows',
		markup: '<div class="list">\n\t<div class="list-row focusable"><span class="list-cell list-cell-grow">Name</span><span class="list-cell list-cell-num">1</span></div>\n\t<div class="list-row focusable"><span class="list-cell list-cell-grow">Name</span><span class="list-cell list-cell-num">2</span></div>\n</div>',
	},
	{
		name: 'Toggle',
		hint: 'An on/off setting with its label',
		markup: '<div class="setting">\n\t<span class="setting-label">Setting</span>\n\t<input type="checkbox" class="toggle focusable"/>\n</div>',
	},
	{ name: 'Checkbox', hint: 'A square box with a tick, for lists of options', markup: '<input type="checkbox" class="check focusable" folio-label="Option"/>' },
	{
		name: 'Radio group',
		hint: 'One choice out of several',
		markup: '<div class="radio-group">\n\t<input type="radio" class="radio focusable" name="choice" value="a" checked/>\n\t<span class="radio-label">A</span>\n\t<input type="radio" class="radio focusable" name="choice" value="b"/>\n\t<span class="radio-label">B</span>\n</div>',
	},
	{
		name: 'Tabs',
		hint: 'A tab bar and its panels',
		markup: '<tabset class="tabs">\n\t<tabs>\n\t\t<tab class="tab focusable">First</tab>\n\t\t<tab class="tab focusable">Second</tab>\n\t</tabs>\n\t<panels>\n\t\t<panel class="tab-panel"></panel>\n\t\t<panel class="tab-panel"></panel>\n\t</panels>\n</tabset>',
	},
	{
		name: 'Modal',
		hint: 'A dialog over a scrim. Put .is-inert on what is behind it while it is up',
		markup: '<div class="modal-scrim">\n\t<div class="modal">\n\t\t<div class="modal-title t-title c-text">Title</div>\n\t\t<div class="modal-body"><p class="t-body c-text">Body</p></div>\n\t\t<div class="modal-actions">\n\t\t\t<button class="btn focusable">Cancel</button>\n\t\t\t<button class="btn btn-primary focusable">OK</button>\n\t\t</div>\n\t</div>\n</div>',
	},
	{ name: 'Key prompt', hint: 'A drawn keycap with what it does', markup: '<span class="prompt"><span class="pg pg-key">F</span><span class="pg-label">Interact</span></span>' },
	{
		name: 'Motes',
		hint: 'Drifting motes for a menu background. Place it first so it draws behind',
		markup: '<div class="motes">\n\t<div class="motes-layer motes-far"><div class="motes-scroll"><div class="motes-tile"/><div class="motes-tile"/></div></div>\n\t<div class="motes-layer motes-mid"><div class="motes-scroll"><div class="motes-tile"/><div class="motes-tile"/></div></div>\n\t<div class="motes-layer motes-near"><div class="motes-scroll"><div class="motes-tile"/><div class="motes-tile"/></div></div>\n</div>',
	},
];
