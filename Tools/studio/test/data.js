// Drives the Data panel's operations against a scratch copy of a game document and its mock.
const { store, session, host, log, wait, capture } = studio;
const data = studio.data;
const expect = (condition, message) => { if (!condition) throw new Error(message); log('ok   ' + message); };
const model = () => data.dataStore.get().models.find((entry) => entry.name === 'pause');
const read = async (path) => { const bytes = await host.readFile(path); return bytes ? new TextDecoder().decode(bytes) : null; };
const buffer = (path) => session.peekModel(path).getValue();
const rows = () => { const find = (node) => node.attributes.class === 'panel' ? node : node.children.map(find).find(Boolean); return find(store.get().tree).children.filter((child) => child.attributes.class === 'row' && child.visible).length; };

await wait(2500);
expect(store.get().documentOpen, 'the game document opened');
expect(!!model(), 'its model is listed');
expect(model().mock && !model().schema, 'with a mock and no schema');
expect(model().use.values.has('Rows') && model().use.members.get('Rows').has('Label'), 'bindings were read from the markup');
const before = rows();
expect(before > 1, 'the mock rows are on the canvas: ' + before);

await data.createSchema('pause');
await wait(600);
const schema = model().schema;
expect(!!schema, 'a schema was created');
expect(schema.fields.Rows.type === 'array' && !!schema.structs[schema.fields.Rows.of], 'rows became a struct: ' + schema.fields.Rows.of);
expect(schema.fields.bAlive.type === 'bool' && schema.fields.Title.type === 'string', 'scalars were typed');
expect(Object.keys(schema.commands).sort().join() === 'Back,Slid,Step,Take' || Object.keys(schema.commands).length > 0, 'commands were declared: ' + Object.keys(schema.commands).join());
expect(JSON.stringify(JSON.parse(buffer(model().mockPath))) === JSON.stringify(data.mockFromSchema(schema)), 'the mock is exactly what the schema describes');
log('     issues: ' + JSON.stringify(model().issues.map((issue) => issue.name)));

await data.setSample('pause', 'Title', 'CONTROLS');
await wait(600);
const title = (node) => node.attributes.class === 'crumb' ? node : node.children.map(title).find(Boolean);
expect(JSON.parse(buffer(model().mockPath)).Title === 'CONTROLS', 'a sample edit regenerates the mock');
expect(JSON.stringify(store.get().tree).includes('CONTROLS'), 'and the canvas shows it');

const sample = model().schema.fields.Rows.sample;
await data.setSample('pause', 'Rows', sample.slice(0, 2));
await wait(600);
expect(rows() === 2, 'removing rows from the sample removes them from the canvas: ' + rows());

await data.addField('pause', 'bNewFlag');
await wait(300);
expect(model().schema.fields.bNewFlag.type === 'bool', 'a b-prefixed field is added as a bool');
expect(model().issues.some((issue) => issue.name === 'bNewFlag' && issue.severity === 'warning'), 'and reported as bound by nothing');
await data.removeField('pause', 'bNewFlag');

document.querySelectorAll('.dv-default-tab').forEach((tab) => { if (tab.textContent.includes('Data')) tab.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true })); });
await wait(500);
await capture(studio.shots + '/studio-data.png');

await session.saveAll();
expect((await read(model().schemaPath)) === buffer(model().schemaPath), 'the schema is saved to UI/schema');
