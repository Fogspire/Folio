/*
 * Mistakes visible in the text alone, as the editor underlines them while typing.
 *
 * Every one is CSS that RCSS parses and then ignores without a word, which is what makes it worth
 * flagging before the canvas quietly shows nothing.
 */

import { findDeclarations, findRules, Range } from '../edit/rcss';
import { parseRml, RmlElement } from '../edit/rml';
import { BANNED, functionsFor, isEffectProperty, isProperty, keywordsFor, vocabulary } from './vocabulary';

export interface Finding {
	from: number;
	to: number;
	severity: 'error' | 'warning' | 'info';
	message: string;
}

const CSS_EASINGS = /(?<![-\w])(ease-in-out|ease-in|ease-out|ease|cubic-bezier|steps)(?![-\w])/;

/** Checks one declaration's value, for the traps that look right. */
function checkValue(name: string, value: string, at: number, out: Finding[]) {
	const before = out.length;
	const flag = (pattern: RegExp, message: string, severity: Finding['severity'] = 'warning') => {
		const match = pattern.exec(value);
		if (match) out.push({ from: at + match.index, to: at + match.index + match[0].length, severity, message });
	};

	if (name === 'transition' || name === 'animation') {
		flag(/\b\d*\.?\d+ms\b/, 'Times are in seconds: `ms` does not exist. Write `0.12s`.');
		flag(CSS_EASINGS, 'CSS easings do not exist. Use RmlUi\'s, such as `cubic-out` or `quadratic-in-out`.');
	}
	flag(/rgba\(\s*[^,)]+,\s*[^,)]+,\s*[^,)]+,\s*(?:0?\.\d+|1\.0*)\s*\)/, '`rgba()` alpha is 0-255, so this is nearly transparent. Write `#rrggbbaa`.');
	flag(/\b(?:var|calc|clamp|min|max)\(/, 'RCSS has no CSS functions: write the value itself.');
	if (name === 'line-height') flag(/^\s*normal\s*$/, '`line-height` has no `normal`: give a number.');
	if (name === 'font-family') flag(/,/, 'One family only: there is no fallback stack.');
	if (name === 'z-index') flag(/^\s*none\s*$/, '`z-index` takes `auto` or a number.');
	if (name === 'text-align') flag(/\bjustify\b/, '`justify` parses and does nothing: justification is not implemented.', 'info');
	if (name === 'position') flag(/\bsticky\b/, 'There is no `sticky`: use `fixed` or `absolute`.');
	if (name === 'display') flag(/\b(?:grid|inline-grid|contents)\b/, 'Not a display RCSS has: use flexbox.');
	if (name === 'filter') flag(/\boutside-blur\(/, '`outside-blur` is a `backdrop-filter`.');
	if (/^(?:align|justify)-/.test(name)) flag(/(?<![-\w])(?:start|end|normal)(?![-\w])/, 'Use `flex-start`, `flex-end` or `stretch`.');

	if (isEffectProperty(name)) {
		const known = new Set(functionsFor(name === 'filter' ? 'backdrop-filter' : name));
		for (const match of value.matchAll(/([-\w]+)\s*\(/g)) {
			if (!known.has(match[1])) out.push({ from: at + match.index!, to: at + match.index! + match[1].length, severity: 'warning', message: `No ${name} called '${match[1]}'. It draws nothing.` });
		}
		return;
	}

	// A property read only through keywords takes nothing else, unless a trap above already said so.
	if (out.length > before) return;
	const info = vocabulary.properties[name];
	const word = /^\s*([a-z][-a-z]*)\s*$/.exec(value);
	if (info && !info.parsers.length && info.keywords.length && word && !['inherit', 'initial', 'unset'].includes(word[1]) && !keywordsFor(name).includes(word[1])) {
		const start = at + value.indexOf(word[1]);
		out.push({ from: start, to: start + word[1].length, severity: 'warning', message: `'${word[1]}' is not a value of ${name}. It takes ${keywordsFor(name).join(', ')}.` });
	}
}

/** Checks every rule in a stylesheet, or in one range of a document. */
export function checkRcss(text: string, range: Range = { from: 0, to: text.length }): Finding[] {
	const out: Finding[] = [];
	for (const rule of findRules(text, range)) checkBody(text, rule.bodyStart, rule.bodyEnd, out);
	for (const match of text.slice(range.from, range.to).matchAll(/::?(before|after)\b|:has\(/g)) {
		const from = range.from + match.index!;
		out.push({ from, to: from + match[0].length, severity: 'warning', message: match[0].startsWith(':has') ? 'There is no `:has()`.' : 'There are no pseudo-elements: use a real element.' });
	}
	return out;
}

function checkBody(text: string, bodyStart: number, bodyEnd: number, out: Finding[]) {
	for (const declaration of findDeclarations(text, bodyStart, bodyEnd)) {
		const name = declaration.name;
		const nameEnd = declaration.start + name.length;
		if (BANNED[name]) {
			out.push({ from: declaration.start, to: nameEnd, severity: 'warning', message: `'${name}' does not exist in RCSS and does nothing. ${BANNED[name]}` });
			continue;
		}
		if (name.startsWith('--')) {
			if (!isProperty(name)) out.push({ from: declaration.start, to: nameEnd, severity: 'warning', message: 'There are no custom properties: use classes in a token stylesheet.' });
			continue;
		}
		if (!isProperty(name)) {
			out.push({ from: declaration.start, to: nameEnd, severity: 'warning', message: `Unknown property '${name}'. It is ignored.` });
			continue;
		}
		const leading = /^\s*/.exec(text.slice(declaration.valueStart, declaration.valueEnd))![0].length;
		checkValue(name, declaration.value, declaration.valueStart + leading, out);
	}
}

/** Checks a document's own stylesheets, its style attributes, and what its markup gets wrong. */
export function checkRml(text: string): Finding[] {
	const document = parseRml(text);
	const out: Finding[] = [];
	for (const range of document.styles) out.push(...checkRcss(text, range));

	const walk = (element: RmlElement) => {
		const style = element.attributes.find((attribute) => attribute.name === 'style');
		if (style) checkBody(text, style.valueStart, style.valueEnd, out);
		if (element.tag === 'text') {
			const inner = text.slice(element.openEnd, element.closeStart);
			const braces = inner.indexOf('{{');
			if (braces !== -1) out.push({ from: element.openEnd + braces, to: element.openEnd + braces + 2, severity: 'warning', message: '`<text>` holds prose only: no `{{ }}`, because its braces are Folio\'s markup.' });
		}
		for (const child of element.children) walk(child);
	};
	for (const root of document.roots) walk(root);
	return out;
}
