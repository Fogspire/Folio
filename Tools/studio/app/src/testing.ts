import * as models from './data/models';
import { mockFromSchema } from './data/schema';
import * as commands from './commands';
import * as dock from './dock';
import * as edit from './edit/commands';
import { host } from './host';
import { monaco } from './monaco';
import * as session from './session';

/**
 * Runs a script handed over on the command line (`--test file.js`) against the live app.
 *
 * The script is the body of an async function given `studio`, and is how the app is checked
 * unattended: open a project, drive it, capture the window, exit with a code.
 */
export async function runTestScript() {
	const script: string | null = await host.testScript();
	if (!script) return;

	const studio = {
		session,
		edit,
		commands,
		dock,
		data: { ...models, mockFromSchema },
		monaco,
		shots: ((await host.state()) as { shots: string | null }).shots,
		store: session.store,
		engine: session.engine,
		host,
		log: (value: unknown) => host.testLog(typeof value === 'string' ? value : JSON.stringify(value)),
		wait: (milliseconds: number) => new Promise((resolve) => setTimeout(resolve, milliseconds)),
		capture: (target: string) => host.capture(target),
	};

	try {
		const run = new Function('studio', `return (async () => { ${script}\n })();`);
		await run(studio);
		host.quit(0);
	} catch (error) {
		host.testLog('TEST FAILED: ' + (error instanceof Error ? error.stack ?? error.message : String(error)));
		host.quit(1);
	}
}
