import { useEffect, useState } from 'react';
import { Activity, ActivityElement } from '../engine';
import { engine, select, setHover, store } from '../session';

/** How often the panel rereads the document, which is often enough to watch a settle without costing a frame. */
const POLL_MS = 250;

type Verdict = { kind: 'never' | 'conditional' | 'busy' | 'timer' | 'idle'; title: string; detail: string };

/**
 * What the game will do with this document in play, not only with the data it has now.
 *
 * Folio skips a document's frame until RmlUi asks for one, so the difference between these is the
 * difference between a screen that costs nothing while it sits there and one that costs every frame.
 * An endless animation that something switches off - a data-if, a bound class, hover - only costs
 * while it is on, so it is reported with what turns it on rather than as never idling.
 */
function verdict(activity: Activity): Verdict {
	const always = activity.elements.filter((entry) => entry.visible && entry.forever.length && !entry.conditions.length);
	if (always.length) {
		return {
			kind: 'never',
			title: 'Never idles',
			detail: `${always.length === 1 ? 'An animation runs' : always.length + ' animations run'} forever and nothing switches ${always.length === 1 ? 'it' : 'them'} off, so the game draws this document every frame.`,
		};
	}

	const switched = activity.elements.filter((entry) => entry.forever.length && entry.conditions.length);
	if (switched.length) {
		const conditions = [...new Set(switched.flatMap((entry) => entry.conditions))];
		const onNow = switched.some((entry) => entry.visible);
		return {
			kind: 'conditional',
			title: 'Idles, except while ' + conditions.join(' or '),
			detail: onNow
				? 'With the current data that holds, so right now the game draws it every frame.'
				: 'With the current data it does not, so right now it idles.',
		};
	}
	if (activity.next === 0) {
		return { kind: 'busy', title: 'Updating now', detail: 'Something is animating or transitioning. It idles once that settles.' };
	}
	if (activity.next !== null) {
		return { kind: 'timer', title: `Next update in ${activity.next.toFixed(2)}s`, detail: 'A timer, such as a caret blink, wakes it. Between, it costs nothing.' };
	}
	return { kind: 'idle', title: 'Idle', detail: 'Nothing will update it until input, a data change or a reload.' };
}

function describeTicks(entry: ActivityElement): string[] {
	const reasons: string[] = [];
	for (const animation of entry.finite) {
		reasons.push(`animation ${animation.name} plays ${animation.times === 1 ? 'once' : animation.times + ' times'} when it starts`);
	}
	if (entry.transitions.length) reasons.push(`transitions ${entry.transitions.join(', ')} when they change`);
	if (entry.bindings.length) reasons.push(`updates when its data changes (${entry.bindings.join(', ')})`);
	return reasons;
}

function Row({ entry, children }: { entry: ActivityElement; children: React.ReactNode }) {
	return (
		<div
			className={'activity-row' + (entry.visible ? '' : ' hidden')}
			onClick={() => select(entry.id)}
			onMouseEnter={() => setHover(entry.id)}
			onMouseLeave={() => setHover(null)}
			title={entry.path + (entry.visible ? '' : '\nHidden, so it costs nothing until shown')}
		>
			<span className="path">{entry.label}</span>
			<span className="why">{children}</span>
		</div>
	);
}

export function ActivityPanel() {
	const open = store.use((state) => state.documentOpen);
	const revision = store.use((state) => state.revision);
	const [activity, setActivity] = useState<Activity | null>(null);

	useEffect(() => {
		if (!open) {
			setActivity(null);
			return;
		}
		const read = () => setActivity(engine.isOpen ? engine.activity() : null);
		read();
		const timer = window.setInterval(read, POLL_MS);
		return () => window.clearInterval(timer);
	}, [open, revision]);

	if (!activity) return <div className="panel empty">No document open.</div>;

	const state = verdict(activity);
	const forever = activity.elements.filter((entry) => entry.forever.length);
	const ticks = activity.elements.filter((entry) => describeTicks(entry).length);
	const layers = activity.elements.filter((entry) => entry.layer);

	return (
		<div className="panel activity">
			<div className={'activity-verdict ' + state.kind}>
				<div className="title">{state.title}</div>
				<div className="detail">{state.detail}</div>
			</div>

			{forever.length > 0 && (
				<section>
					<h3>Runs forever</h3>
					{forever.map((entry) => (
						<Row key={entry.id} entry={entry}>
							{entry.forever.join(', ')} · {entry.conditions.length ? 'while ' + entry.conditions.join(' and ') : 'always'}
						</Row>
					))}
				</section>
			)}

			<section>
				<h3>Updates when something changes</h3>
				{ticks.length === 0 && <div className="dim activity-none">Nothing: once drawn, only input or a reload moves it.</div>}
				{ticks.map((entry) => (
					<Row key={entry.id} entry={entry}>
						{describeTicks(entry).join('; ')}
					</Row>
				))}
			</section>

			<section>
				<h3>Layers</h3>
				{layers.length === 0 && <div className="dim activity-none">None. Every element draws straight to the screen.</div>}
				{layers.map((entry) => (
					<Row key={entry.id} entry={entry}>
						{entry.layer} · {entry.coverage < 0.01 ? 'under 1%' : Math.round(entry.coverage * 100) + '%'} of the screen
						{state.kind === 'never' ? ' · drawn whenever its content changes' : ' · drawn only on frames the document updates'}
					</Row>
				))}
			</section>
		</div>
	);
}
