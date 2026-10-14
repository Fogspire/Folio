import type { TreeNode } from '../engine';
import { attributeOf, RmlElement } from './rml';

export interface SourceMap {
	/** The markup element behind each live element. */
	byId: Map<string, RmlElement>;
	/** The live elements each markup element produced: one, or one per row for a `data-for`. */
	ids: Map<RmlElement, string[]>;
}

/** Elements whose live children are not the markup's, so matching stops at them. */
const OPAQUE = new Set(['text', 'select', 'tabset']);

/**
 * Matches the renderer's live tree against the parsed markup, by position.
 *
 * The two trees agree child for child except where data binding multiplies an element: a `data-for`
 * leaves its template in place, hidden, and inserts one copy per row before it. Every copy maps back
 * to the one element that was written, because that is the element an edit to any of them changes.
 *
 * Matching stops, for that subtree only, wherever the trees disagree - a document built through a
 * template, for one. An element that is not matched can still be inspected, just not edited visually.
 */
export function mapTree(tree: TreeNode, body: RmlElement): SourceMap {
	const byId = new Map<string, RmlElement>();
	const ids = new Map<RmlElement, string[]>();

	const pair = (node: TreeNode, element: RmlElement) => {
		byId.set(node.id, element);
		if (!ids.has(element)) ids.set(element, []);
		ids.get(element)!.push(node.id);
	};

	const walk = (node: TreeNode, element: RmlElement) => {
		pair(node, element);
		if (OPAQUE.has(element.tag)) return;

		const live = node.children;
		let next = 0;
		for (let index = 0; index < element.children.length; index++) {
			const child = element.children[index];

			if (attributeOf(child, 'data-for')) {
				// Whatever is left over once the later siblings have theirs belongs to this one: its
				// rows, then the template itself.
				const reserved = element.children.length - index - 1;
				while (next < live.length - reserved) {
					const candidate = live[next];
					if (candidate.tag !== child.tag) return;
					next += 1;
					walk(candidate, child);
					if ('data-for' in candidate.attributes) break;
				}
				continue;
			}

			const candidate = live[next];
			if (!candidate || candidate.tag !== child.tag) return;
			next += 1;
			walk(candidate, child);
		}
	};

	walk(tree, body);
	return { byId, ids };
}
