/*
 * Tests for the edit protocol.
 *
 * Every case asserts on the whole file rather than the changed line: the promise is that nothing else
 * moves, and only a full comparison can hold that. Run with `node rcss.test.js`.
 */

const assert = require("assert");
const { setProperty, listRules, normalise } = require("./rcss");

let passed = 0;
function check(name, fn) {
	try {
		fn();
		passed += 1;
	} catch (error) {
		console.error(`FAIL ${name}\n  ${error.message}`);
		process.exitCode = 1;
	}
}

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

check("changes a value and touches nothing else", () => {
	const result = setProperty(sheet, ".panel", "left", "42dp");
	assert.ok(!result.error, result.error);
	assert.strictEqual(result.text, sheet.replace("left: 10dp;", "left: 42dp;"));
	assert.strictEqual(result.change.kind, "changed");
	assert.strictEqual(result.change.from, "10dp");
});

check("keeps a trailing comment on the line it changes", () => {
	const result = setProperty(sheet, ".panel", "display", "block");
	assert.ok(result.text.includes("display: block;      /* a trailing comment */"));
});

check("adds a property after the last declaration", () => {
	const result = setProperty(sheet, ".panel", "top", "8dp");
	assert.strictEqual(result.change.kind, "added");
	assert.ok(result.text.includes("\tleft: 10dp;\n\ttop: 8dp;"));
	// The comment between the rules is untouched.
	assert.ok(result.text.includes("/* A comment between rules. */"));
});

check("addresses a rule inside a media block separately", () => {
	const themed = setProperty(sheet, "@media (theme: high-contrast) .btn", "color", "#111");
	assert.ok(!themed.error, themed.error);
	assert.ok(themed.text.includes(".btn { color: #111; }"));
	// The unthemed rule is left alone.
	assert.ok(themed.text.includes(".btn { color: #fff; }"));
});

check("edits the outer rule when the plain selector is asked for", () => {
	const plain = setProperty(sheet, ".btn", "color", "#eee");
	assert.ok(plain.text.includes(".btn { color: #eee; }"));
	assert.ok(plain.text.includes(".btn { color: #000; }"));
});

check("refuses a rule that is not there", () => {
	const result = setProperty(sheet, ".nope", "left", "0dp");
	assert.ok(result.error);
	assert.ok(!result.text);
});

check("takes the last of duplicate rules, which is the one that wins", () => {
	const duplicated = ".a { left: 1dp; }\n.a { left: 2dp; }\n";
	const result = setProperty(duplicated, ".a", "left", "9dp");
	assert.strictEqual(result.text, ".a { left: 1dp; }\n.a { left: 9dp; }\n");
});

check("is not fooled by a brace in a string or a comment", () => {
	const tricky = `.a { content: "}"; }\n/* } */\n.b { left: 1dp; }\n`;
	const result = setProperty(tricky, ".b", "left", "5dp");
	assert.ok(!result.error, result.error);
	assert.strictEqual(result.text, `.a { content: "}"; }\n/* } */\n.b { left: 5dp; }\n`);
});

check("normalises whitespace in a selector", () => {
	assert.strictEqual(normalise(".a   >  .b ,.c"), ".a > .b, .c");
	const spaced = ".a  >  .b { left: 1dp; }\n";
	assert.ok(!setProperty(spaced, ".a > .b", "left", "3dp").error);
});

check("lists rules with their lines", () => {
	const rules = listRules(sheet);
	assert.deepStrictEqual(rules.map((r) => r.selector),
		[".panel", ".btn", "@media (theme: high-contrast) .btn"]);
	assert.strictEqual(rules[0].line, 3);
});

console.log(`${passed} test(s) passed`);
