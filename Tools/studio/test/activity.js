// The Activity panel: what keeps a document from idling, what can wake it, and what draws a layer.
const { store, session, commands, engine, host, log, wait, capture } = studio;
const fail = (message) => { throw new Error(message); };
const expect = (condition, message) => { if (!condition) fail(message); log('ok   ' + message); };

await wait(2000);
const root = store.get().project.root;
const write = async (name, body, style) => {
	const path = root + '/UI/screens/' + name + '.rml';
	await host.writeFile(path, `<rml><head><style>body { width: 100%; height: 100%; } ${style}</style></head><body>${body}</body></rml>`);
	return path;
};

const still = await write('still',
	'<div id="card">Card</div><div id="shade"></div>',
	'#card { width: 200dp; height: 100dp; transition: opacity 0.2s linear-in-out; } #shade { position: absolute; left: 0; top: 0; right: 0; bottom: 0; filter: blur(4dp); }');
const forever = await write('forever',
	'<div id="spin">Spin</div>',
	'@keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } } #spin { width: 50dp; height: 50dp; animation: 1s linear-in-out infinite spin; }');
await wait(1000);

commands.runCommand('panel:activity');
await session.openDocument(still);
await wait(800);

let activity = engine.activity();
const card = activity.elements.find((entry) => entry.path.includes('card'));
const shade = activity.elements.find((entry) => entry.path.includes('shade'));
expect(activity.next === null, 'a settled document with nothing running is idle');
expect(card && card.transitions.includes('opacity'), 'a transition is listed as what can wake it');
expect(shade && shade.layer === 'filter' && shade.coverage > 0.9, 'a full-screen filter is listed as a layer with its coverage');
expect(document.querySelector('.activity-verdict.idle'), 'and the panel says Idle');

await session.openDocument(forever);
await wait(800);
activity = engine.activity();
const spin = activity.elements.find((entry) => entry.path.includes('spin'));
expect(spin && spin.forever.includes('spin'), 'an infinite animation is named');
expect(activity.next === 0, 'and the document updates every frame');
expect(document.querySelector('.activity-verdict.never'), 'so the panel says it never idles');
await capture(studio.shots + '/studio-activity.png');

// An endless animation something switches off costs only while it is on, so it does not make the document never idle.
const hovered = await write('hovered',
	'<div id="pulse">Pulse</div>',
	'@keyframes pulse { from { opacity: 0.5; } to { opacity: 1; } } #pulse { width: 50dp; height: 50dp; } #pulse:hover { animation: 1s linear-in-out infinite pulse; }');
await wait(1000);
await session.openDocument(hovered);
await wait(800);
activity = engine.activity();
expect(activity.elements.every((entry) => !entry.forever.length), 'an animation waiting on hover is not running yet');
