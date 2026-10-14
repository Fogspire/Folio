import MarkdownIt from 'markdown-it';
import { useEffect, useMemo, useRef, useState } from 'react';
import { host } from '../host';
import { dirname, join } from '../paths';
import { store } from '../session';

interface Page {
	/** Relative to the docs directory, such as `tutorials/inventory.md`. */
	path: string;
	title: string;
	section: string;
	text: string;
}

const markdown = new MarkdownIt({ html: false, linkify: true });

function slug(text: string): string {
	return text.toLowerCase().replace(/[^\w\s-]/g, '').trim().replace(/\s+/g, '-');
}

// Headings get ids, so a link to a section of a page lands on it.
markdown.renderer.rules.heading_open = (tokens, index, options, _env, self) => {
	const inline = tokens[index + 1];
	if (inline?.type === 'inline') tokens[index].attrSet('id', slug(inline.content));
	return self.renderToken(tokens, index, options);
};

function normalise(path: string): string {
	const out: string[] = [];
	for (const part of path.split('/')) {
		if (part === '..') out.pop();
		else if (part !== '.' && part !== '') out.push(part);
	}
	return out.join('/');
}

/** Reads the index, which is tables of links under section headings, into an ordered page list. */
function parseIndex(index: string): { path: string; title: string; section: string }[] {
	const pages: { path: string; title: string; section: string }[] = [];
	let section = '';
	for (const line of index.split('\n')) {
		const heading = /^##\s+(.*)$/.exec(line);
		if (heading) section = heading[1].trim();
		for (const match of line.matchAll(/\[([^\]]+)\]\(([^)#]+\.md)(#[^)]*)?\)/g)) {
			const path = normalise(match[2]);
			if (!path.startsWith('..') && !pages.some((page) => page.path === path)) pages.push({ path, title: match[1].replace(/[*`]/g, ''), section });
		}
	}
	return pages;
}

async function readText(path: string): Promise<string | null> {
	const data: Uint8Array | null = await host.readFile(path);
	return data ? new TextDecoder().decode(data) : null;
}

export function DocsPanel() {
	const project = store.use((state) => state.project);
	const [pages, setPages] = useState<Page[]>([]);
	const [current, setCurrent] = useState('README.md');
	const [anchor, setAnchor] = useState('');
	const [query, setQuery] = useState('');
	const content = useRef<HTMLDivElement>(null);
	const folioRoot = project?.folioRoot ?? null;

	useEffect(() => {
		if (!folioRoot) return;
		let cancelled = false;
		(async () => {
			const docs = join(folioRoot, 'docs');
			const index = await readText(join(docs, 'README.md'));
			if (!index || cancelled) return;
			const listed = [{ path: 'README.md', title: 'Contents', section: '' }, ...parseIndex(index)];
			// The conventions page sits beside docs/ rather than in it, and is the capability reference.
			const loaded: Page[] = [];
			for (const entry of listed) {
				const text = await readText(join(docs, entry.path));
				if (text !== null) loaded.push({ ...entry, text });
			}
			const conventions = await readText(join(folioRoot, 'UI_CONVENTIONS.md'));
			if (conventions !== null) loaded.push({ path: '../UI_CONVENTIONS.md', title: 'UI conventions', section: 'Reference', text: conventions });
			if (!cancelled) setPages(loaded);
		})();
		return () => {
			cancelled = true;
		};
	}, [folioRoot]);

	const page = pages.find((entry) => entry.path === current) ?? null;
	const html = useMemo(() => (page ? markdown.render(page.text) : ''), [page]);

	useEffect(() => {
		const element = anchor ? content.current?.querySelector('#' + CSS.escape(anchor)) : null;
		if (element) element.scrollIntoView();
		else content.current?.scrollTo(0, 0);
	}, [html, anchor]);

	const needle = query.trim().toLowerCase();
	const visible = needle ? pages.filter((entry) => entry.title.toLowerCase().includes(needle) || entry.text.toLowerCase().includes(needle)) : pages;

	const follow = (href: string) => {
		if (/^https?:\/\//.test(href)) {
			host.openExternal(href);
			return;
		}
		const [target, hash = ''] = href.split('#');
		if (!target) {
			setAnchor(hash);
			return;
		}
		const resolved = current === '../UI_CONVENTIONS.md' ? normalise(target.replace(/^docs\//, '')) : normalise(join(dirname(current), target));
		const known = pages.find((entry) => entry.path === resolved || (target.endsWith('UI_CONVENTIONS.md') && entry.path === '../UI_CONVENTIONS.md'));
		if (known) {
			setCurrent(known.path);
			setAnchor(hash);
		}
	};

	if (!project) return <div className="panel empty">No project open.</div>;

	let lastSection: string | null = null;
	return (
		<div className="panel docs">
			<nav className="docs-nav">
				<input className="docs-search" placeholder="Search the docs" value={query} onChange={(event) => setQuery(event.target.value)} spellCheck={false} />
				<div className="docs-list">
					{visible.map((entry) => {
						const heading = entry.section !== lastSection && entry.section ? entry.section : null;
						lastSection = entry.section;
						return (
							<div key={entry.path}>
								{heading && <h3>{heading}</h3>}
								<div
									className={'tree-row' + (entry.path === current ? ' selected' : '')}
									onClick={() => {
										setCurrent(entry.path);
										setAnchor('');
									}}
								>
									{entry.title}
								</div>
							</div>
						);
					})}
					{!visible.length && <p className="dim">Nothing matches.</p>}
				</div>
			</nav>
			<div
				className="docs-content markdown"
				ref={content}
				dangerouslySetInnerHTML={{ __html: html }}
				onClick={(event) => {
					const link = (event.target as HTMLElement).closest('a');
					if (!link) return;
					event.preventDefault();
					follow(link.getAttribute('href') ?? '');
				}}
			/>
		</div>
	);
}
