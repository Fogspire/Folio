/*
 * Applying one property to one rule, in place.
 *
 * The brief calls this a protocol rather than a file rewrite, and that distinction is the whole
 * design: the tool says "set this property on this selector to this value" and something that
 * understands RCSS applies it, preserving every comment, blank line and indent around it. A tool that
 * reformatted a stylesheet to move a button would be abandoned within a day, and rightly.
 *
 * So this never reformats. It finds the smallest span it can replace - a value, or an insertion point
 * inside a block - and touches nothing else. The output differs from the input by one line, or by one
 * run of characters inside one line.
 *
 * It is deliberately not a full RCSS parser. It understands blocks, strings, comments and at-rules
 * well enough to find a rule and its declarations, and it refuses rather than guesses when it cannot:
 * an edit that lands in the wrong place is far worse than one that does not happen.
 */

/** Everything the scanner needs to skip over without misreading a brace inside it. */
function scanSkippable(text, index) {
	if (text.startsWith("/*", index)) {
		const end = text.indexOf("*/", index + 2);
		return end === -1 ? text.length : end + 2;
	}

	const quote = text[index];
	if (quote === '"' || quote === "'") {
		let at = index + 1;
		while (at < text.length && text[at] !== quote) {
			at += text[at] === "\\" ? 2 : 1;
		}
		return at + 1;
	}

	return index;
}

/**
 * Every rule in the sheet, with the span of its selector and of its body.
 *
 * At-rules with blocks - @media and @keyframes - are descended into rather than skipped, so a rule
 * inside a theme is addressable. Its selector is reported with the at-rule prefixed, because
 * `.btn` inside `@media (theme: high-contrast)` is a different rule to `.btn` outside it and editing
 * the wrong one is exactly the mistake that would make a tool untrustworthy.
 */
function findRules(text, prefix = "", from = 0, to = text.length) {
	const rules = [];
	let index = from;
	let selectorStart = from;

	while (index < to) {
		const skipped = scanSkippable(text, index);
		if (skipped !== index) {
			// A comment before a rule belongs to the file, not to the selector. Without this the
			// selector of a rule following a comment is the comment plus the selector, and matches
			// nothing anyone would type.
			if (text.slice(selectorStart, index).trim() === "") {
				selectorStart = skipped;
			}

			index = skipped;
			continue;
		}

		const character = text[index];

		if (character === "{") {
			const selector = text.slice(selectorStart, index).trim();

			// Find the matching close, respecting nesting, strings and comments.
			let depth = 1;
			let at = index + 1;
			while (at < to && depth > 0) {
				const inner = scanSkippable(text, at);
				if (inner !== at) {
					at = inner;
					continue;
				}
				if (text[at] === "{") { depth += 1; }
				else if (text[at] === "}") { depth -= 1; }
				at += 1;
			}

			const bodyStart = index + 1;
			const bodyEnd = at - 1;

			if (selector.startsWith("@")) {
				// A block at-rule contains rules; a statement at-rule does not.
				if (/^@(media|keyframes|supports)/i.test(selector)) {
					rules.push(...findRules(text, `${prefix}${selector} `, bodyStart, bodyEnd));
				}
			} else if (selector) {
				rules.push({
					selector: prefix + selector,
					bareSelector: selector,
					// Where the selector's text begins rather than where the gap before it does, so a
					// reported line is the line someone would look at.
					selectorStart: selectorStart + (/^\s*/.exec(text.slice(selectorStart, index)) || [""])[0].length,
					bodyStart,
					bodyEnd,
				});
			}

			index = at;
			selectorStart = index;
			continue;
		}

		if (character === "}" || character === ";") {
			index += 1;
			selectorStart = index;
			continue;
		}

		index += 1;
	}

	return rules;
}

/** Selectors compare on their text with runs of whitespace flattened, so formatting does not matter. */
function normalise(selector) {
	return String(selector).replace(/\s+/g, " ").replace(/\s*,\s*/g, ", ").trim();
}

/**
 * The declarations in a body, as spans.
 *
 * Only top-level ones: a nested block belongs to something else and its declarations are not this
 * rule's. Comments are skipped rather than parsed, so a commented-out declaration stays commented out.
 */
function findDeclarations(text, bodyStart, bodyEnd) {
	const declarations = [];
	let index = bodyStart;
	let start = bodyStart;
	let depth = 0;

	while (index < bodyEnd) {
		const skipped = scanSkippable(text, index);
		if (skipped !== index) {
			// A comment before a declaration belongs to the file. Without this, the comment trailing
			// one declaration becomes part of the next one's name, the name fails to parse, and the
			// declaration is invisible - so setting it appends a second copy rather than changing it.
			if (text.slice(start, index).trim() === "") {
				start = skipped;
			}

			index = skipped;
			continue;
		}

		const character = text[index];
		if (character === "{") { depth += 1; }
		else if (character === "}") { depth -= 1; }
		else if (character === ";" && depth === 0) {
			const chunk = text.slice(start, index);
			const colon = chunk.indexOf(":");
			if (colon !== -1) {
				const name = chunk.slice(0, colon).trim();
				// A name with a brace or a comment in it is not a declaration this understands.
				if (/^[-a-zA-Z][-a-zA-Z0-9]*$/.test(name)) {
					declarations.push({
						name,
						valueStart: start + colon + 1,
						valueEnd: index,
						start,
						end: index + 1,
					});
				}
			}
			start = index + 1;
		}

		index += 1;
	}

	return declarations;
}

/** The indent used inside a body, so an inserted declaration matches what is already there. */
function bodyIndent(text, bodyStart, bodyEnd) {
	const match = /\n([ \t]+)\S/.exec(text.slice(bodyStart, bodyEnd));
	if (match) {
		return match[1];
	}

	// An empty or single-line body: take the rule's own indent and add one level of whatever the file
	// uses, which is a tab in every stylesheet Folio ships.
	const before = text.lastIndexOf("\n", bodyStart) + 1;
	const outer = /^[ \t]*/.exec(text.slice(before))[0];
	return outer + (outer.includes(" ") ? "    " : "\t");
}

/**
 * Sets a property on a rule, returning the new text and what changed.
 *
 * Returns `{ text, change }` on success, or `{ error }` when the rule is not there - never a guess.
 * `change` describes the edit for a caller that wants to show or log it.
 */
function setProperty(text, selector, property, value) {
	const wanted = normalise(selector);
	const rules = findRules(text).filter((rule) => normalise(rule.selector) === wanted);

	if (rules.length === 0) {
		return { error: `no rule '${selector}' in this stylesheet` };
	}

	// The last one wins in the cascade, so it is the one an edit has to land on: changing an earlier
	// duplicate would be overridden and read as the edit having done nothing.
	const rule = rules[rules.length - 1];
	const declarations = findDeclarations(text, rule.bodyStart, rule.bodyEnd);
	const existing = declarations.filter((declaration) => declaration.name === property).pop();

	if (existing) {
		// The value alone, so any comment after it and the whitespace around it survive.
		const before = text.slice(existing.valueStart, existing.valueEnd);
		const leading = /^\s*/.exec(before)[0];
		const trailing = /\s*$/.exec(before)[0];

		return {
			text: text.slice(0, existing.valueStart) + leading + value + trailing
				+ text.slice(existing.valueEnd),
			change: { kind: "changed", property, from: before.trim(), to: value, selector: rule.selector },
		};
	}

	const indent = bodyIndent(text, rule.bodyStart, rule.bodyEnd);
	const body = text.slice(rule.bodyStart, rule.bodyEnd);

	// Appended after the last declaration rather than at the end of the body, so a trailing comment
	// inside the rule stays at the bottom where its author put it.
	const lastEnd = declarations.length > 0
		? declarations[declarations.length - 1].end
		: rule.bodyStart;

	const newline = text.includes("\r\n") ? "\r\n" : "\n";
	const insertion = body.trim().length === 0
		? `${newline}${indent}${property}: ${value};${newline}`
		: `${newline}${indent}${property}: ${value};`;

	return {
		text: text.slice(0, lastEnd) + insertion + text.slice(lastEnd),
		change: { kind: "added", property, to: value, selector: rule.selector },
	};
}

/** Every rule in a sheet, for a caller offering a choice of what to edit. */
function listRules(text) {
	return findRules(text).map((rule) => ({
		selector: rule.selector,
		line: text.slice(0, rule.selectorStart).split("\n").length,
	}));
}

module.exports = { setProperty, listRules, findRules, findDeclarations, normalise };
