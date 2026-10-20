/*
 * What RCSS has: the properties, keywords and effect names the renderer parses.
 *
 * vocabulary.json is generated from RmlUi's and FolioCore's sources by engine/vocabulary.py, so it
 * changes when the engine does. What it cannot read from code, such as which CSS habits silently do
 * nothing, is written here by hand from UI_CONVENTIONS.md.
 */

import data from './vocabulary.json';

export interface PropertyInfo {
	default: string;
	inherited: boolean;
	keywords: string[];
	parsers: string[];
}

interface Vocabulary {
	properties: Record<string, PropertyInfo>;
	shorthands: Record<string, string[]>;
	decorators: string[];
	filters: string[];
	fontEffects: string[];
	animationKeywords: string[];
	elements: string[];
}

export const vocabulary = data as Vocabulary;

/** CSS that parses as nothing in RCSS, with what to write instead. */
export const BANNED: Record<string, string> = {
	'grid': 'Use flexbox.',
	'grid-template-columns': 'Use flexbox.',
	'grid-template-rows': 'Use flexbox.',
	'grid-template-areas': 'Use flexbox.',
	'grid-area': 'Use flexbox.',
	'grid-column': 'Use flexbox.',
	'grid-row': 'Use flexbox.',
	'grid-gap': 'Use `gap`.',
	'background-image': 'Use `decorator: image(...)`.',
	'background-size': 'Use the image decorator\'s fit modes.',
	'background-position': 'Use `decorator: image(...)`.',
	'background-repeat': 'Use `decorator: tiled-box(...)` or the image decorator\'s `repeat`.',
	'border-style': '`border` is always solid. Use `decorator: dashed-border(...)` or `dotted-border(...)`.',
	'border-top-style': '`border` is always solid. Use `decorator: dashed-border(...)`.',
	'border-right-style': '`border` is always solid. Use `decorator: dashed-border(...)`.',
	'border-bottom-style': '`border` is always solid. Use `decorator: dashed-border(...)`.',
	'border-left-style': '`border` is always solid. Use `decorator: dashed-border(...)`.',
	'text-shadow': 'Use `font-effect: shadow(...)`.',
	'outline': 'Use `border` or a decorator.',
	'outline-color': 'Use `border` or a decorator.',
	'outline-width': 'Use `border` or a decorator.',
	'object-fit': 'Use the image decorator\'s fit modes: fill, contain, cover, scale-none, scale-down, repeat.',
	'content': 'There is no `::before` or `::after`. Use a real element.',
	'aspect-ratio': 'No equivalent: restructure.',
	'writing-mode': 'No equivalent: restructure.',
	'list-style': 'No equivalent: restructure.',
	'list-style-type': 'No equivalent: restructure.',
	'font-variant': 'No equivalent.',
	'font-stretch': 'No equivalent. Register a variable face with its axes pinned.',
	'text-indent': 'Use padding on the element.',
	'user-select': 'Not supported.',
	'resize': 'Use a `<handle size_target="...">`.',
};

/** Value-level traps worth repeating where the value is written. */
export const VALUE_NOTES: Record<string, string> = {
	'text-align': '`justify` parses but does nothing: justification is not implemented.',
	'line-height': 'There is no `normal`: give a number, length or percentage.',
	'font-family': 'One family only, no fallback stack. A family Folio registered, not a filename.',
	'font-weight': '`normal`, `bold`, or 1-1000. No `bolder`/`lighter`.',
	'position': '`fixed` behaves like `absolute` but ignores scrolling. No `sticky`.',
	'cursor': 'An application-defined string, not CSS keywords.',
	'color': '`rgba()` alpha is 0-255, not 0-1. Prefer `#rrggbbaa`.',
	'background-color': '`rgba()` alpha is 0-255, not 0-1. Prefer `#rrggbbaa`.',
	'z-index': '`auto` or a number: there is no `none`.',
	'clip': 'Not CSS `clip`: how many ancestor clip regions the element ignores.',
	'tab-index': 'A flag, not an order: `none` or `auto`. Tab order is document order.',
	'nav': 'Read from the focused element only, never inherited. Every focusable element needs its own.',
	'decorator': 'Several decorators paint in reverse order: the first named is on top.',
	'row-gap': 'A percentage resolves against the containing block\'s height.',
	'column-gap': 'A percentage resolves against the containing block\'s height.',
};

/** Keywords a property takes that RmlUi reads through a parser rather than a keyword list. */
const EXTRA_KEYWORDS: Record<string, string[]> = {
	'transition': vocabulary.animationKeywords,
	'animation': vocabulary.animationKeywords,
	'transform': ['none'],
	'decorator': ['none'],
	'filter': ['none'],
	'backdrop-filter': ['none'],
	'font-effect': ['none'],
	'box-shadow': ['none', 'inset'],
};

/** Functions a property's value is built from. */
const FUNCTIONS: Record<string, () => string[]> = {
	'decorator': () => vocabulary.decorators,
	'mask-image': () => vocabulary.decorators,
	'filter': () => vocabulary.filters.filter((name) => name !== 'outside-blur'),
	'backdrop-filter': () => vocabulary.filters,
	'font-effect': () => vocabulary.fontEffects,
	'transform': () => ['translate', 'translateX', 'translateY', 'translateZ', 'translate3d', 'scale', 'scaleX', 'scaleY', 'scaleZ', 'scale3d', 'rotate', 'rotateX', 'rotateY', 'rotateZ', 'rotate3d', 'skew', 'skewX', 'skewY', 'perspective', 'matrix', 'matrix3d'],
};

export function isProperty(name: string): boolean {
	return name in vocabulary.properties || name in vocabulary.shorthands;
}

/** Every property name a stylesheet can write, the per-edge ones and their shorthands both. */
export function propertyNames(): string[] {
	return [...Object.keys(vocabulary.properties), ...Object.keys(vocabulary.shorthands)].filter((name) => !name.startsWith('--')).sort();
}

export function keywordsFor(name: string): string[] {
	const own = vocabulary.properties[name]?.keywords ?? [];
	const through = (vocabulary.shorthands[name] ?? []).flatMap((part) => vocabulary.properties[part]?.keywords ?? []);
	return [...new Set([...own, ...through, ...(EXTRA_KEYWORDS[name] ?? [])])];
}

export function functionsFor(name: string): string[] {
	return FUNCTIONS[name]?.() ?? [];
}

export function isEffectProperty(name: string): boolean {
	return name === 'decorator' || name === 'mask-image' || name === 'filter' || name === 'backdrop-filter' || name === 'font-effect';
}

/** A property's documentation as markdown: what it defaults to, what it accepts, and its traps. */
export function describeProperty(name: string): string | null {
	const banned = BANNED[name];
	if (banned) return `**${name}** does not exist in RCSS and silently does nothing. ${banned}`;

	const info = vocabulary.properties[name];
	const parts = vocabulary.shorthands[name];
	if (!info && !parts) return null;

	const lines = [`**${name}**${parts ? ' (shorthand)' : ''}`];
	if (info) lines.push(`Default \`${info.default || '(none)'}\`${info.inherited ? ', inherited' : ''}.`);
	if (parts) lines.push(`Sets ${parts.map((part) => `\`${part}\``).join(', ')}.`);
	const keywords = keywordsFor(name);
	if (keywords.length) lines.push(`Keywords: ${keywords.map((word) => `\`${word}\``).join(' ')}`);
	const parsers = info?.parsers.filter((parser) => parser !== 'keyword') ?? [];
	if (parsers.length) lines.push(`Also takes: ${parsers.map((parser) => parser.replace(/_/g, ' ')).join(', ')}.`);
	const functions = functionsFor(name);
	if (functions.length && isEffectProperty(name)) lines.push(`Functions: ${functions.map((fn) => `\`${fn}()\``).join(' ')}`);
	const note = VALUE_NOTES[name];
	if (note) lines.push(note);
	return lines.join('\n\n');
}
