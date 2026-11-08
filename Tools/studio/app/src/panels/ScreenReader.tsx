import { useEffect, useState } from 'react';
import { ReaderNode } from '../engine';
import { engine, select, setHover, store } from '../session';

/** How often the panel rereads the document in Play, where focus and values change without an edit. */
const PLAY_POLL_MS = 500;

interface Line {
	node: ReaderNode;
	depth: number;
}

function flatten(node: ReaderNode, depth: number, out: Line[]) {
	out.push({ node, depth });
	for (const child of node.children) flatten(child, depth + 1, out);
}

/** What is read after the name: the value, the place in a set, the state and the description. */
function readAfter(node: ReaderNode): string {
	const parts: string[] = [];
	if (node.value && node.value !== node.name) parts.push(node.value);
	if (node.setSize) parts.push(`${node.position} of ${node.setSize}`);
	if (node.checked !== undefined) parts.push(node.checked ? 'checked' : 'not checked');
	if (node.disabled) parts.push('unavailable');
	if (node.description) parts.push(node.description);
	return parts.join(', ');
}

/** What a screen reader reads out of the document, in the order it reads it. */
export function ScreenReaderPanel() {
	const open = store.use((state) => state.documentOpen);
	const revision = store.use((state) => state.revision);
	const mode = store.use((state) => state.mode);
	const [tree, setTree] = useState<ReaderNode | null>(null);

	useEffect(() => {
		if (!open) {
			setTree(null);
			return;
		}
		const read = () => setTree(engine.accessibilityTree());
		read();
		if (mode !== 'play') return;
		const timer = window.setInterval(read, PLAY_POLL_MS);
		return () => window.clearInterval(timer);
	}, [open, revision, mode]);

	if (!tree) return <div className="panel empty">Open a document to hear how it reads.</div>;

	const lines: Line[] = [];
	flatten(tree, 0, lines);
	const unnamed = lines.filter(({ node }) => node.interactive && !node.name).length;

	return (
		<div className="panel reader">
			<div className={'reader-summary' + (unnamed ? ' warning' : '')}>
				{unnamed
					? `${unnamed} ${unnamed === 1 ? 'control has' : 'controls have'} no name, so a screen reader says only what it is. Give each a folio-label.`
					: 'Read top to bottom, in this order. Click a line to select its element.'}
			</div>
			{lines.map(({ node, depth }, index) => {
				const after = readAfter(node);
				const missing = node.interactive && !node.name;
				return (
					<div
						key={index}
						className={'reader-row' + (missing ? ' unnamed' : '') + (node.interactive ? ' interactive' : '')}
						style={{ paddingLeft: 12 + depth * 14 }}
						onClick={() => node.id && select(node.id)}
						onMouseEnter={() => node.id && setHover(node.id)}
						onMouseLeave={() => setHover(null)}
					>
						{node.column && <span className="dim">{node.column}</span>}
						<span className="name">{missing ? 'No name' : node.name}</span>
						<span className="role">{node.role}</span>
						{after && <span className="after">{after}</span>}
					</div>
				);
			})}
		</div>
	);
}
