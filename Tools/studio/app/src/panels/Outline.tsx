import { useEffect, useRef, useState } from 'react';
import { elementMenu } from '../commands';
import { ELEMENT_MIME, MARKUP_MIME } from '../dnd';
import { insertInto, isEditable, moveInto } from '../edit/commands';
import { TreeNode } from '../engine';
import { select, setHover, store } from '../session';

type Place = 'before' | 'after' | 'inside';

function label(node: TreeNode) {
	const classes = (node.attributes.class ?? '').split(/\s+/).filter(Boolean);
	return (
		<>
			<span className="tag-name">{node.id === '' ? 'body' : node.tag}</span>
			{node.attributes.id && <span className="tag-id">#{node.attributes.id}</span>}
			{classes.slice(0, 3).map((name) => <span key={name} className="tag-class">.{name}</span>)}
			{classes.length > 3 && <span className="tag-class">…</span>}
			{node.text.trim() && <span className="tag-text">{node.text.trim().slice(0, 40)}</span>}
		</>
	);
}

function Row({ node, depth, collapsed, toggle }: { node: TreeNode; depth: number; collapsed: Set<string>; toggle: (id: string) => void }) {
	const selection = store.use((state) => state.selection);
	const hover = store.use((state) => state.hover);
	const row = useRef<HTMLDivElement>(null);
	const [dropAt, setDropAt] = useState<Place | null>(null);
	const isCollapsed = collapsed.has(node.id);
	const selected = selection === node.id;

	useEffect(() => {
		if (selected) row.current?.scrollIntoView({ block: 'nearest' });
	}, [selected]);

	// The top and bottom quarters of a row mean beside it; the middle means inside it.
	const placeFor = (event: React.DragEvent): Place => {
		if (node.id === '') return 'inside';
		const bounds = event.currentTarget.getBoundingClientRect();
		const fraction = (event.clientY - bounds.top) / bounds.height;
		return fraction < 0.25 ? 'before' : fraction > 0.75 ? 'after' : 'inside';
	};

	const destination = (place: Place): [string, number] => {
		if (place === 'inside') return [node.id, node.children.length];
		const slash = node.id.lastIndexOf('/');
		const index = Number(node.id.slice(slash + 1));
		return [slash === -1 ? '' : node.id.slice(0, slash), place === 'before' ? index : index + 1];
	};

	return (
		<>
			<div
				ref={row}
				className={'tree-row' + (selected ? ' selected' : '') + (hover === node.id ? ' hovered' : '') + (node.visible ? '' : ' hidden') + (dropAt ? ' drop-' + dropAt : '')}
				style={{ paddingLeft: 6 + depth * 12 }}
				draggable={node.id !== '' && isEditable(node.id)}
				onDragStart={(event) => {
					event.dataTransfer.setData(ELEMENT_MIME, node.id);
					event.dataTransfer.effectAllowed = 'move';
				}}
				onDragOver={(event) => {
					const types = event.dataTransfer.types;
					if (!(types.includes(ELEMENT_MIME) || types.includes(MARKUP_MIME)) || !isEditable(node.id)) return;
					event.preventDefault();
					setDropAt(placeFor(event));
				}}
				onDragLeave={() => setDropAt(null)}
				onDrop={(event) => {
					event.preventDefault();
					setDropAt(null);
					const [parent, index] = destination(placeFor(event));
					const moved = event.dataTransfer.getData(ELEMENT_MIME);
					const markup = event.dataTransfer.getData(MARKUP_MIME);
					if (markup) insertInto(parent, index, markup);
					else if (moved && moved !== node.id) moveInto(moved, parent, index);
				}}
				onClick={() => select(node.id)}
				onContextMenu={(event) => {
					event.preventDefault();
					elementMenu(node.id);
				}}
				onMouseEnter={() => setHover(node.id)}
				onMouseLeave={() => setHover(null)}
			>
				<span
					className={'twisty' + (node.children.length ? '' : ' leaf')}
					onClick={(event) => {
						event.stopPropagation();
						toggle(node.id);
					}}
				>
					{node.children.length ? (isCollapsed ? '▸' : '▾') : ''}
				</span>
				{label(node)}
			</div>
			{!isCollapsed && node.children.map((child) => <Row key={child.id} node={child} depth={depth + 1} collapsed={collapsed} toggle={toggle} />)}
		</>
	);
}

export function OutlinePanel() {
	const tree = store.use((state) => state.tree);
	const selection = store.use((state) => state.selection);
	const [collapsed, setCollapsed] = useState<Set<string>>(new Set());

	// A selection made on the canvas has to be visible here, so its ancestors open.
	useEffect(() => {
		if (!selection) return;
		setCollapsed((current) => {
			const parts = selection.split('/');
			const ancestors = ['', ...parts.slice(0, -1).map((_, index) => parts.slice(0, index + 1).join('/'))];
			if (!ancestors.some((id) => current.has(id))) return current;
			const next = new Set(current);
			for (const id of ancestors) next.delete(id);
			return next;
		});
	}, [selection]);

	const toggle = (id: string) => setCollapsed((current) => {
		const next = new Set(current);
		if (!next.delete(id)) next.add(id);
		return next;
	});

	if (!tree) return <div className="panel empty">No document.</div>;
	return (
		<div className="panel tree" tabIndex={0} data-element-keys>
			<Row node={tree} depth={0} collapsed={collapsed} toggle={toggle} />
		</div>
	);
}
