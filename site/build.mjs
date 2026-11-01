// Builds the GitHub Pages site from the repository's markdown. The markdown is the only source:
// every page, the landing page's copy and the docs navigation are read from it on each build.
//
//   node build.mjs           write site/_site
//   node build.mjs --serve   build, then serve _site on http://localhost:4173

import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import { fileURLToPath } from 'node:url';
import { Marked } from 'marked';
import GithubSlugger from 'github-slugger';
import hljs from 'highlight.js';

const SITE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(SITE, '..');
const OUT = path.join(SITE, '_site');
const REPO = 'https://github.com/Fogspire/Folio';
const BRANCH = 'main';

const VIDEO_URL = /^https:\/\/github\.com\/user-attachments\/assets\/[0-9a-f-]+$/i;

// ---------------------------------------------------------------------------------------------
// Page set

/** Repo-relative markdown path -> site-relative output path, for every page the site publishes. */
function collectPages()
{
	const pages = new Map();
	pages.set('CHANGELOG.md', 'changelog.html');
	pages.set('UI_CONVENTIONS.md', 'UI_CONVENTIONS.html');
	pages.set('AGENTS.md', 'AGENTS.html');
	for (const dir of ['docs', 'docs/tutorials'])
	{
		for (const name of fs.readdirSync(path.join(ROOT, dir)).sort())
		{
			if (!name.endsWith('.md'))
			{
				continue;
			}
			const src = `${dir}/${name}`;
			pages.set(src, name === 'README.md' ? `${dir}/index.html` : src.replace(/\.md$/, '.html'));
		}
	}
	pages.set('docs/overview.md', 'index.html');
	return pages;
}

const PAGES = collectPages();
const read = (rel) => fs.readFileSync(path.join(ROOT, rel), 'utf8').replace(/\r\n/g, '\n');
const posix = (p) => p.split(path.sep).join('/');

function relativeUrl(fromOut, toOut)
{
	const rel = posix(path.relative(path.dirname(fromOut), toOut));
	return rel === '' ? path.basename(toOut) : rel;
}

const escapeHtml = (s) => String(s)
	.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

const stripTags = (html) => html.replace(/<[^>]+>/g, '').replace(/&lt;/g, '<').replace(/&gt;/g, '>')
	.replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, '&');

// ---------------------------------------------------------------------------------------------
// Links and assets

const copiedAssets = new Set();

/**
 * Resolves an href written in a markdown file to what the site should link. Pages map to their
 * output, files under docs/ (images) are copied beside them, and anything else in the repository
 * links to GitHub, since the site does not publish source.
 */
function resolveHref(href, srcRel, outRel)
{
	if (!href || /^[a-z]+:/i.test(href) || href.startsWith('#') || href.startsWith('//'))
	{
		return href;
	}
	const [pathPart, hash] = href.split('#');
	const target = posix(path.normalize(path.join(path.dirname(srcRel), decodeURI(pathPart))));
	const suffix = hash !== undefined ? `#${hash}` : '';

	if (PAGES.has(target))
	{
		return relativeUrl(outRel, PAGES.get(target)) + suffix;
	}
	const abs = path.join(ROOT, target);
	if (target.startsWith('docs/') && fs.existsSync(abs) && fs.statSync(abs).isFile())
	{
		copiedAssets.add(target);
		return relativeUrl(outRel, target) + suffix;
	}
	const isDir = fs.existsSync(abs) && fs.statSync(abs).isDirectory();
	return `${REPO}/${isDir ? 'tree' : 'blob'}/${BRANCH}/${target}${suffix}`;
}

// ---------------------------------------------------------------------------------------------
// Markdown

const LANGS = {
	css: ['css', 'RCSS'], rcss: ['css', 'RCSS'], html: ['xml', 'RML'], rml: ['xml', 'RML'], xml: ['xml', 'XML'],
	cpp: ['cpp', 'C++'], hlsl: ['cpp', 'HLSL'], bash: ['bash', 'Shell'], sh: ['bash', 'Shell'],
	ini: ['ini', 'INI'], json: ['json', 'JSON'], powershell: ['powershell', 'PowerShell'], python: ['python', 'Python'],
};

function highlight(code, lang)
{
	const known = LANGS[(lang || '').toLowerCase()];
	if (!known)
	{
		return { html: escapeHtml(code), label: lang || '' };
	}
	return { html: hljs.highlight(code, { language: known[0], ignoreIllegals: true }).value, label: known[1] };
}

const ICON = {
	link: '<path d="M10 14a4 4 0 0 0 5.66 0l3-3a4 4 0 0 0-5.66-5.66l-1 1"/><path d="M14 10a4 4 0 0 0-5.66 0l-3 3a4 4 0 0 0 5.66 5.66l1-1"/>',
	copy: '<rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h2"/>',
	search: '<circle cx="11" cy="11" r="6.5"/><path d="m16 16 4.5 4.5"/>',
	menu: '<path d="M4 7h16M4 12h16M4 17h16"/>',
	close: '<path d="m6 6 12 12M18 6 6 18"/>',
	left: '<path d="M19 12H5m6-6-6 6 6 6"/>',
	right: '<path d="M5 12h14m-6-6 6 6-6 6"/>',
	external: '<path d="M14 4h6v6M20 4l-9 9"/><path d="M18 14v4a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4"/>',
	edit: '<path d="M4 20h4L19 9l-4-4L4 16v4z"/><path d="m13.5 6.5 4 4"/>',
	repo: '<path d="M5 4h11a2 2 0 0 1 2 2v14H7a2 2 0 0 1-2-2V4z"/><path d="M5 18a2 2 0 0 1 2-2h11"/><path d="M9 8h5"/>',
	play: '<path d="M8 5.5v13l11-6.5z" fill="currentColor" stroke="none"/>',
};
const icon = (name, cls = '') => `<svg class="icon ${cls}" viewBox="0 0 24 24" aria-hidden="true">${ICON[name]}</svg>`;

/**
 * Renders one markdown source. Heading ids follow GitHub's slugs so every anchor written for
 * github.com resolves on the site too.
 */
function renderMarkdown(md, srcRel, outRel, { headingShift = 0 } = {})
{
	const slugger = new GithubSlugger();
	const toc = [];
	const marked = new Marked({ gfm: true });
	marked.use({
		renderer: {
			heading({ tokens, depth, text })
			{
				const inner = this.parser.parseInline(tokens);
				const id = slugger.slug(stripTags(inner));
				const level = Math.min(6, depth + headingShift);
				if (depth === 2 || depth === 3)
				{
					toc.push({ id, depth, text: stripTags(inner) });
				}
				if (depth === 1)
				{
					return `<h${level} id="${id}">${inner}</h${level}>\n`;
				}
				return `<h${level} id="${id}"><a class="anchor" href="#${id}" aria-label="Link to this section">${icon('link')}</a>${inner}</h${level}>\n`;
			},
			code({ text, lang })
			{
				const { html, label } = highlight(text, (lang || '').split(/\s/)[0]);
				return `<figure class="screen"><figcaption><span>${escapeHtml(label)}</span>`
					+ `<button type="button" class="copy" aria-label="Copy code">${icon('copy')}<span>Copy</span></button></figcaption>`
					+ `<pre><code>${html}</code></pre></figure>\n`;
			},
			paragraph({ tokens })
			{
				if (tokens.length === 1 && tokens[0].type === 'link' && VIDEO_URL.test(tokens[0].href) && tokens[0].text === tokens[0].href)
				{
					return videoHtml(tokens[0].href);
				}
				return `<p>${this.parser.parseInline(tokens)}</p>\n`;
			},
			link({ href, title, tokens })
			{
				const url = resolveHref(href, srcRel, outRel);
				const external = /^https?:/.test(url);
				const t = title ? ` title="${escapeHtml(title)}"` : '';
				const rel = external ? ' rel="noopener"' : '';
				return `<a href="${escapeHtml(url)}"${t}${rel}>${this.parser.parseInline(tokens)}</a>`;
			},
			image({ href, title, text })
			{
				const url = resolveHref(href, srcRel, outRel);
				const t = title ? ` title="${escapeHtml(title)}"` : '';
				return `<img src="${escapeHtml(url)}" alt="${escapeHtml(text)}"${t} loading="lazy" decoding="async">`;
			},
			table(token)
			{
				const cell = (c, tag) =>
				{
					const align = c.align ? ` style="text-align:${c.align}"` : '';
					return `<${tag}${align}>${this.parser.parseInline(c.tokens)}</${tag}>`;
				};
				const head = token.header.map((c) => cell(c, 'th')).join('');
				const emptyHead = token.header.every((c) => c.text.trim() === '');
				const body = token.rows.map((r) => `<tr>${r.map((c) => cell(c, 'td')).join('')}</tr>`).join('\n');
				return `<div class="table"><table${emptyHead ? ' class="headless"' : ''}>`
					+ (emptyHead ? '' : `<thead><tr>${head}</tr></thead>`)
					+ `<tbody>${body}</tbody></table></div>\n`;
			},
		},
	});
	const html = marked.parse(md);
	return { html, toc };
}

function videoHtml(url)
{
	return `<figure class="screen video"><video src="${url}" controls muted playsinline preload="metadata"></video></figure>\n`;
}

// ---------------------------------------------------------------------------------------------
// Structure read from the markdown

/** Splits markdown into heading sections: [{ depth, title, body, children }]. */
function sectionTree(md)
{
	const root = { depth: 0, title: '', body: '', children: [] };
	const stack = [root];
	let fence = false;
	for (const line of md.split('\n'))
	{
		if (/^```/.test(line))
		{
			fence = !fence;
		}
		const m = !fence && /^(#{1,6})\s+(.*)$/.exec(line);
		if (m)
		{
			const node = { depth: m[1].length, title: m[2].trim(), body: '', children: [] };
			while (stack[stack.length - 1].depth >= node.depth)
			{
				stack.pop();
			}
			stack[stack.length - 1].children.push(node);
			stack.push(node);
			continue;
		}
		stack[stack.length - 1].body += `${line}\n`;
	}
	return root;
}

const findSection = (node, title) =>
{
	for (const c of node.children)
	{
		if (c.title.toLowerCase() === title.toLowerCase())
		{
			return c;
		}
		const deep = findSection(c, title);
		if (deep)
		{
			return deep;
		}
	}
	return null;
};

const sectionMarkdown = (node) => node.body + node.children
	.map((c) => `${'#'.repeat(c.depth)} ${c.title}\n${sectionMarkdown(c)}`).join('');

/** The docs navigation, read from the tables in docs/README.md. */
function buildNav()
{
	const lexer = new Marked().lexer(read('docs/README.md'));
	const groups = [];
	const seen = new Set();
	let group = null;
	for (const tok of lexer)
	{
		if (tok.type === 'heading' && tok.depth === 2)
		{
			group = { title: tok.text, items: [] };
			groups.push(group);
		}
		else if (tok.type === 'table' && group)
		{
			for (const row of tok.rows)
			{
				const link = row[0].tokens.flatMap((t) => (t.tokens ? [t, ...t.tokens] : [t])).find((t) => t.type === 'link');
				if (!link)
				{
					continue;
				}
				const src = posix(path.normalize(path.join('docs', link.href.split('#')[0])));
				if (!PAGES.has(src) || seen.has(src))
				{
					continue;
				}
				seen.add(src);
				group.items.push({ src, title: link.text, desc: row[1]?.text ?? '' });
			}
		}
	}
	const navGroups = groups.filter((g) => g.items.length);
	const orphans = [...PAGES.keys()].filter((p) => !['docs/README.md', 'docs/overview.md', 'CHANGELOG.md'].includes(p) && !seen.has(p));
	if (orphans.length)
	{
		console.warn(`Not linked from docs/README.md, listed under "More": ${orphans.join(', ')}`);
		navGroups.push({ title: 'More', items: orphans.map((src) => ({ src, title: pageTitle(src), desc: '' })) });
	}
	return navGroups;
}

function pageTitle(src)
{
	const m = /^#\s+(.+)$/m.exec(read(src));
	return m ? stripTags(new Marked().parseInline(m[1])) : path.basename(src, '.md');
}

function pageDescription(md)
{
	const tok = new Marked().lexer(md).find((t) => t.type === 'paragraph');
	return tok ? stripTags(new Marked().parseInline(tok.text)).replace(/\s+/g, ' ').slice(0, 200) : '';
}

const VERSION = JSON.parse(read('Folio.uplugin')).VersionName;

// ---------------------------------------------------------------------------------------------
// Search index

const searchIndex = [];

function indexPage(md, outRel, title)
{
	const tokens = new Marked().lexer(md);
	let current = { h: title, id: '', text: [] };
	const flush = () =>
	{
		const text = current.text.join(' ').replace(/[`*_>#|[\]]/g, ' ').replace(/\(\S+?\)/g, '').replace(/\s+/g, ' ').trim();
		searchIndex.push({ p: title, h: current.h, u: outRel + (current.id ? `#${current.id}` : ''), x: text.slice(0, 600) });
	};
	const slugger = new GithubSlugger();
	for (const t of tokens)
	{
		if (t.type === 'heading')
		{
			const id = slugger.slug(stripTags(new Marked().parseInline(t.text)));
			if (t.depth === 1)
			{
				continue;
			}
			flush();
			current = { h: stripTags(new Marked().parseInline(t.text)), id, text: [] };
		}
		else if (t.type !== 'code' && t.raw)
		{
			current.text.push(t.raw);
		}
	}
	flush();
}

// ---------------------------------------------------------------------------------------------
// Templates

function head({ title, description, outRel, bodyClass })
{
	const r = (p) => relativeUrl(outRel, p);
	return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${escapeHtml(title)}</title>
<meta name="description" content="${escapeHtml(description)}">
<meta property="og:title" content="${escapeHtml(title)}">
<meta property="og:description" content="${escapeHtml(description)}">
<meta name="theme-color" content="#111114">
<link rel="icon" type="image/png" href="${r('assets/icon.png')}">
<link rel="preload" href="${r('assets/fonts/archivo-latin-wdth-normal.woff2')}" as="font" type="font/woff2" crossorigin>
<link rel="stylesheet" href="${r('assets/site.css')}">
<script>document.documentElement.classList.add('js')</script>
<script defer src="${r('assets/site.js')}" data-root="${r('index.html').replace(/index\.html$/, '')}"></script>
</head>
<body class="${bodyClass}">
<a class="skip" href="#main">Skip to content</a>
`;
}

const CLONE_URL = `${REPO}.git`;
const CLONE_CMD = `git clone ${CLONE_URL} Plugins/Folio`;

/** The repository link and the clone copier, in every page's sticky bar. */
function repoTools()
{
	return `<div class="repo-tools">
<a class="repo-link" href="${REPO}" rel="noopener">${icon('repo')}<span>Fogspire/Folio</span>${icon('external', 'ext')}</a>
<div class="clone" role="group" aria-label="Clone the repository">
<code class="clone-cmd" title="Run from your project's root">${escapeHtml(CLONE_CMD)}</code>
<button type="button" class="clone-btn" data-copy="${escapeHtml(CLONE_CMD)}" title="${escapeHtml(CLONE_CMD)}">${icon('copy')}<span data-idle="Copy clone command">Copy clone command</span></button>
<button type="button" class="clone-btn clone-url" data-copy="${escapeHtml(CLONE_URL)}" title="${escapeHtml(CLONE_URL)}"><span data-idle="URL">URL</span></button>
</div>
</div>`;
}

function searchBox()
{
	return `<div class="search" role="search">
<label class="search-field">${icon('search')}<span class="sr">Search the docs</span><input type="search" placeholder="Search the docs" autocomplete="off" spellcheck="false" aria-controls="search-results" aria-expanded="false"><kbd>/</kbd></label>
<ol class="search-results" id="search-results" role="listbox" hidden></ol>
</div>`;
}

function docsHeader(outRel)
{
	const r = (p) => relativeUrl(outRel, p);
	return `<header class="topbar">
<button type="button" class="rail-toggle" aria-controls="rail" aria-expanded="false">${icon('menu')}<span class="sr">Contents</span></button>
<a class="wordmark" href="${r('index.html')}" aria-label="Folio home"><span>Folio</span></a>
<a class="topbar-docs" href="${r('docs/index.html')}">Docs</a>
${searchBox()}
<nav class="topbar-links" aria-label="Site">
<a href="${r('changelog.html')}">${VERSION}</a>
</nav>
${repoTools()}
</header>`;
}

function railHtml(nav, outRel, currentSrc)
{
	const r = (p) => relativeUrl(outRel, p);
	const groups = nav.map((g) => `<section><h2>${escapeHtml(g.title)}</h2><ul>${g.items.map((it) =>
	{
		const cur = it.src === currentSrc ? ' aria-current="page"' : '';
		return `<li><a href="${r(PAGES.get(it.src))}"${cur}>${escapeHtml(it.title)}</a></li>`;
	}).join('')}</ul></section>`).join('');
	const overview = currentSrc === 'docs/README.md' ? ' aria-current="page"' : '';
	return `<nav class="rail" id="rail" aria-label="Documentation">
<div class="rail-inner">
<ul class="rail-top"><li><a href="${r('docs/index.html')}"${overview}>Overview</a></li><li><a href="${r('changelog.html')}"${currentSrc === 'CHANGELOG.md' ? ' aria-current="page"' : ''}>Changelog</a></li></ul>
${groups}
</div>
</nav>`;
}

function tocHtml(toc)
{
	const items = toc.filter((t) => t.depth === 2);
	if (items.length < 2)
	{
		return '';
	}
	return `<aside class="toc" aria-label="On this page"><h2>On this page</h2><ol>${items
		.map((t) => `<li><a href="#${t.id}">${escapeHtml(t.text)}</a></li>`).join('')}</ol></aside>`;
}

function pagerHtml(flat, outRel, currentSrc)
{
	const i = flat.findIndex((it) => it.src === currentSrc);
	if (i < 0)
	{
		return '';
	}
	const r = (p) => relativeUrl(outRel, PAGES.get(p));
	const prev = flat[i - 1];
	const next = flat[i + 1];
	return `<nav class="pager" aria-label="Previous and next">${prev
		? `<a class="prev" href="${r(prev.src)}" rel="prev"><span>${icon('left')}Previous</span><strong>${escapeHtml(prev.title)}</strong></a>` : '<span></span>'}${next
		? `<a class="next" href="${r(next.src)}" rel="next"><span>Next${icon('right')}</span><strong>${escapeHtml(next.title)}</strong></a>` : ''}</nav>`;
}

function footerHtml(outRel, editSrc)
{
	const r = (p) => relativeUrl(outRel, p);
	const edit = editSrc ? `<a href="${REPO}/edit/${BRANCH}/${editSrc}" rel="noopener">${icon('edit')}Edit this page on GitHub</a>` : '';
	return `<footer class="foot">
<div class="foot-inner">
${edit}
<p>Folio ${VERSION} by Fogspire. <a href="${REPO}/blob/${BRANCH}/LICENSE" rel="noopener">MIT with the Commons Clause</a>. <a href="${r('changelog.html')}">Changelog</a> · <a href="${REPO}/issues" rel="noopener">Issues</a></p>
</div>
</footer>`;
}

function docPage({ src, outRel, md, nav, flat, titleOverride, editSrc = src, extraClass = '' })
{
	let { html, toc } = renderMarkdown(md, src, outRel);
	if (src === 'docs/README.md')
	{
		html = docsHomeHtml(html, nav, outRel);
	}
	const title = titleOverride ?? pageTitle(src);
	return head({ title: `${title} · Folio`, description: pageDescription(md), outRel, bodyClass: `docs ${extraClass}`.trim() })
		+ docsHeader(outRel)
		+ `<div class="shell">
${railHtml(nav, outRel, src)}
<main id="main" class="page">
<article class="prose">
${html}
</article>
${pagerHtml(flat, outRel, src)}
</main>
${tocHtml(toc)}
</div>
${footerHtml(outRel, editSrc)}
<div class="scrim" hidden></div>
</body>
</html>
`;
}

/**
 * The docs home: a row of section doors under the intro, and the "one thing to know first"
 * section set as the page's single red block.
 */
function docsHomeHtml(html, nav, outRel)
{
	const slug = (t) => new GithubSlugger().slug(t);
	const doors = `<ul class="doors">${nav.filter((g) => g.title !== 'More').map((g) =>
		`<li><a href="#${slug(g.title)}"><strong>${escapeHtml(g.title)}</strong><span>${g.items.length} pages</span></a></li>`).join('')}</ul>
`;
	const start = html.indexOf('<h2 id="the-one-thing-to-know-first"');
	if (start < 0)
	{
		return html;
	}
	const end = html.indexOf('<h2 ', start + 4);
	return html.slice(0, start) + doors + `<section class="decisive">${html.slice(start, end)}</section>
` + html.slice(end);
}

// ---------------------------------------------------------------------------------------------
// The landing page: the arcade cabinet. Every word on it comes from docs/overview.md and CHANGELOG.md.

function landingPage(nav)
{
	const outRel = 'index.html';
	const md = read('docs/overview.md');
	const tree = sectionTree(md);
	const top = tree.children[0];
	const r = (p) => relativeUrl(outRel, p);
	const render = (s, opts) => renderMarkdown(s, 'docs/overview.md', outRel, opts).html;

	// The intro: a bold tagline, the rest of its paragraph, the first video, then more paragraphs.
	const introTokens = new Marked().lexer(top.body).filter((t) => t.type === 'paragraph');
	const lead = introTokens[0];
	const tagline = /^\*\*(.+?)\*\*\s*/.exec(lead.text);
	const leadRest = tagline ? lead.text.slice(tagline[0].length) : lead.text;
	const firstVideo = introTokens.find((t) => VIDEO_URL.test(t.text.trim()));
	const outro = introTokens.filter((t) => t !== lead && t !== firstVideo).map((t) => t.raw).join('\n');

	const start = findSection(tree, 'Get started');
	const startLexed = new Marked().lexer(start.body);
	const requirement = startLexed.find((t) => t.type === 'paragraph');
	const startTable = startLexed.find((t) => t.type === 'table');
	const startLinks = startTable.rows.map((row) =>
	{
		const link = row[0].tokens.flatMap((t) => (t.tokens ? [t, ...t.tokens] : [t])).find((t) => t.type === 'link');
		return link ? { href: resolveHref(link.href, 'docs/overview.md', outRel), label: link.text, desc: stripTags(new Marked().parseInline(row[1].text)) } : null;
	}).filter(Boolean);

	// One job per ink: red is the single action, blue goes somewhere, yellow is where traps live.
	const ink = (l, i) => (i === 0 ? 'red' : /troubleshoot|gap/i.test(l.label) ? 'yellow' : 'blue');
	const buttons = startLinks.map((l, i) => `<li class="ctl ctl-${ink(l, i)}${i === 0 ? ' ctl-start' : ''}">
<a href="${escapeHtml(l.href)}"><span class="ctl-cap" aria-hidden="true"></span><span class="ctl-label">${i === 0 ? '<b>Start</b>' : ''}${escapeHtml(l.label.replace(/\.md$/, ''))}</span></a>
<p>${escapeHtml(l.desc)}</p></li>`).join('\n');

	const showcase = findSection(tree, 'Showcase');
	const why = findSection(tree, 'Why Folio');
	const production = findSection(tree, 'Production Features');
	const inBox = findSection(tree, 'In the box');
	const platform = findSection(tree, 'Platform support');
	const license = findSection(tree, 'License');
	const latest = sectionTree(read('CHANGELOG.md')).children[0].children[0];

	const stages = why.children.map((c, i) =>
	{
		const body = render(c.body);
		const hasMedia = /<(video|img|figure)/.test(body);
		return `<section class="stage${hasMedia ? ' has-media' : ' text-only'}${i % 2 ? ' flip' : ''}" id="${new GithubSlugger().slug(c.title)}">
<h3>${escapeHtml(c.title)}</h3>
<div class="stage-body">${body}</div>
</section>`;
	}).join('\n');

	const games = inBox.children.map((c, i) =>
	{
		const id = `game-${new GithubSlugger().slug(c.title)}`;
		return { id, title: c.title, html: render(c.body), i };
	});
	const gameList = games.map((g) => `<li role="presentation"><button type="button" role="tab" id="${g.id}-tab" aria-controls="${g.id}" aria-selected="${g.i === 0}" tabindex="${g.i === 0 ? 0 : -1}">${escapeHtml(g.title)}</button></li>`).join('');
	const gamePanels = games.map((g) => `<div class="game-panel" role="tabpanel" id="${g.id}" aria-labelledby="${g.id}-tab"${g.i === 0 ? '' : ' hidden'}><h3>${escapeHtml(g.title)}</h3>${g.html}</div>`).join('');

	const platformHtml = render(sectionMarkdown(platform), { headingShift: 0 });

	const ladder = [64, 32, 16, 10, 7].map((s) => `<span style="font-size:${s}px">Folio</span>`).join('');

	const html = head({
		title: 'Folio · UI for Unreal Engine 5, written as markup',
		description: stripTags(new Marked().parseInline(lead.text)).slice(0, 200),
		outRel,
		bodyClass: 'landing',
	}) + `<header class="sitebar">
<nav class="marquee-nav" aria-label="Site">
<a class="mn-home" href="${r('index.html')}" aria-current="page">Folio <span>${VERSION}</span></a>
<a href="${r('docs/index.html')}">Docs</a>
<a href="${r('docs/studio.html')}">Studio</a>
<a href="${r('docs/known_gaps.html')}">Known gaps</a>
<a href="${r('changelog.html')}">Changelog</a>
</nav>
${repoTools()}
</header>
<div class="cabinet-top">
<div class="marquee">
<div class="marquee-light" aria-hidden="true"></div>
<h1 class="marquee-title">Folio</h1>
<p class="marquee-tag">${tagline ? escapeHtml(tagline[1]) : ''}</p>
<div class="ladder" aria-hidden="true"><div class="ladder-words">${ladder}</div><p>One outline, every size</p></div>
</div>
</div>
<main id="main">
<section class="cabinet">
<div class="bezel">
<div class="screen-glass">${firstVideo ? `<video src="${firstVideo.text.trim()}" autoplay muted loop playsinline preload="metadata" aria-label="Folio showcase"></video>
<button type="button" class="screen-play" aria-label="Play the showcase">${icon('play')}</button>` : ''}</div>
<button type="button" class="loop-toggle" aria-pressed="false">Pause</button>
</div>
<div class="deck">
<div class="instruction-card">
${render(leadRest)}
${requirement ? `<p class="req">${new Marked().parseInline(requirement.text)}</p>` : ''}
</div>
<ul class="controls" aria-label="Get started">
${buttons}
</ul>
</div>
</section>

<section class="attract" aria-labelledby="why">
<h2 id="why" class="band-head">Why Folio</h2>
${stages}
</section>

<section class="showcase" aria-labelledby="showcase">
<h2 id="showcase" class="band-head">Showcase</h2>
<div class="showcase-body">${render(showcase.body)}</div>
</section>

<section class="game-select" aria-labelledby="in-the-box">
<h2 id="in-the-box" class="band-head">In the box</h2>
<div class="gs">
<ul class="gs-list" role="tablist" aria-label="In the box" aria-orientation="vertical">${gameList}</ul>
<div class="gs-screen">${gamePanels}</div>
</div>
</section>

<div class="manual">
<section class="manual-sheet" aria-labelledby="production-features">
<h2 id="production-features">Production features</h2>
<div class="spec">${render(production.body)}</div>
</section>

<section class="manual-sheet" aria-labelledby="platform-support">
${platformHtml.replace('<h2 id="platform-support">', '<h2 id="platform-support" class="sheet-h">')}
</section>

<section class="manual-sheet" aria-labelledby="license">
<h2 id="license">License</h2>
${render(outro)}
${render(license.body)}
</section>

<section class="manual-sheet latest" aria-labelledby="changelog">
<h2 id="changelog">Latest: ${escapeHtml(latest.title)}</h2>
${render(latest.body)}
<p><a class="more" href="${r('changelog.html')}">Every release${icon('right')}</a></p>
</section>
</div>
</main>
${footerHtml(outRel, 'docs/overview.md')}
</body>
</html>
`;
	indexPage(md, outRel, 'Folio');
	return html;
}

// ---------------------------------------------------------------------------------------------
// Build

function write(rel, content)
{
	const abs = path.join(OUT, rel);
	fs.mkdirSync(path.dirname(abs), { recursive: true });
	fs.writeFileSync(abs, content);
}

function copyFile(fromAbs, toRel)
{
	const abs = path.join(OUT, toRel);
	fs.mkdirSync(path.dirname(abs), { recursive: true });
	fs.copyFileSync(fromAbs, abs);
}

function build()
{
	fs.rmSync(OUT, { recursive: true, force: true });
	copiedAssets.clear();
	searchIndex.length = 0;

	const nav = buildNav();
	const flat = nav.flatMap((g) => g.items);

	write('index.html', landingPage(nav));

	for (const [src, outRel] of PAGES)
	{
		if (src === 'docs/overview.md')
		{
			continue;
		}
		const md = read(src);
		write(outRel, docPage({ src, outRel, md, nav, flat, extraClass: src === 'docs/README.md' ? 'docs-home' : '' }));
		indexPage(md, outRel, pageTitle(src));
	}


	for (const rel of copiedAssets)
	{
		copyFile(path.join(ROOT, rel), rel);
	}
	for (const name of fs.readdirSync(path.join(SITE, 'assets'), { recursive: true }))
	{
		const abs = path.join(SITE, 'assets', name);
		if (fs.statSync(abs).isFile())
		{
			copyFile(abs, posix(path.join('assets', name)));
		}
	}
	copyFile(path.join(ROOT, 'Resources/Icon128.png'), 'assets/icon.png');
	write('assets/search.json', JSON.stringify(searchIndex));
	// Pages would otherwise run Jekyll over the output and drop files it considers private.
	write('.nojekyll', '');

	console.log(`Built ${PAGES.size + 1} pages, ${copiedAssets.size} images, ${searchIndex.length} search entries -> ${posix(path.relative(ROOT, OUT))}`);
}

function serve(port = 4173)
{
	const types = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.json': 'application/json', '.png': 'image/png', '.gif': 'image/gif', '.woff2': 'font/woff2', '.svg': 'image/svg+xml' };
	http.createServer((req, res) =>
	{
		let p = decodeURIComponent(new URL(req.url, 'http://x').pathname);
		if (p.endsWith('/'))
		{
			p += 'index.html';
		}
		const abs = path.join(OUT, p);
		if (!abs.startsWith(OUT) || !fs.existsSync(abs) || fs.statSync(abs).isDirectory())
		{
			res.writeHead(404).end('Not found');
			return;
		}
		res.writeHead(200, { 'content-type': types[path.extname(abs)] ?? 'application/octet-stream' });
		fs.createReadStream(abs).pipe(res);
	}).listen(port, () => console.log(`Serving http://localhost:${port}/`));
}

build();
if (process.argv.includes('--serve'))
{
	serve();
}
