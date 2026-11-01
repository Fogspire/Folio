(() =>
{
	const root = document.currentScript?.dataset.root ?? './';
	const reduceMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;

	// Copy buttons on code screens.
	document.addEventListener('click', async (e) =>
	{
		const btn = e.target.closest('.copy');
		if (!btn)
		{
			return;
		}
		const code = btn.closest('.screen').querySelector('code').innerText;
		try
		{
			await navigator.clipboard.writeText(code);
			const label = btn.querySelector('span');
			btn.classList.add('done');
			label.textContent = 'Copied';
			setTimeout(() => { btn.classList.remove('done'); label.textContent = 'Copy'; }, 1600);
		}
		catch
		{
			btn.querySelector('span').textContent = 'Select and copy';
		}
	});

	// Clone command and URL copiers in the sticky bar.
	document.addEventListener('click', async (e) =>
	{
		const btn = e.target.closest('.clone-btn');
		if (!btn)
		{
			return;
		}
		const label = btn.querySelector('span');
		try
		{
			await navigator.clipboard.writeText(btn.dataset.copy);
			label.textContent = 'Copied';
			btn.classList.add('done');
		}
		catch
		{
			label.textContent = 'Copy failed';
		}
		clearTimeout(btn._t);
		btn._t = setTimeout(() => { label.textContent = label.dataset.idle; btn.classList.remove('done'); }, 1600);
	});

	// Contents drawer on narrow screens.
	const rail = document.getElementById('rail');
	const toggle = document.querySelector('.rail-toggle');
	const scrim = document.querySelector('.scrim');
	if (rail && toggle && scrim)
	{
		const setOpen = (open) =>
		{
			rail.classList.toggle('open', open);
			scrim.hidden = !open;
			toggle.setAttribute('aria-expanded', String(open));
			document.body.style.overflow = open ? 'hidden' : '';
			if (open)
			{
				(rail.querySelector('[aria-current="page"]') ?? rail.querySelector('a'))?.focus();
			}
		};
		toggle.addEventListener('click', () => setOpen(!rail.classList.contains('open')));
		scrim.addEventListener('click', () => setOpen(false));
		document.addEventListener('keydown', (e) =>
		{
			if (e.key === 'Escape' && rail.classList.contains('open'))
			{
				setOpen(false);
				toggle.focus();
			}
		});
	}
	rail?.querySelector('[aria-current="page"]')?.scrollIntoView({ block: 'center' });

	// On-this-page highlighting.
	const tocLinks = [...document.querySelectorAll('.toc a')];
	if (tocLinks.length)
	{
		const targets = tocLinks.map((a) => document.getElementById(decodeURIComponent(a.hash.slice(1)))).filter(Boolean);
		const update = () =>
		{
			const line = 120;
			let current = targets[0];
			for (const t of targets)
			{
				if (t.getBoundingClientRect().top - line <= 0)
				{
					current = t;
				}
			}
			for (const a of tocLinks)
			{
				a.classList.toggle('active', a.hash === `#${current.id}`);
			}
		};
		addEventListener('scroll', update, { passive: true });
		update();
	}

	// Search.
	const search = document.querySelector('.search');
	if (search)
	{
		const input = search.querySelector('input');
		const list = search.querySelector('.search-results');
		let index = null;
		let results = [];
		let selected = -1;

		const load = async () =>
		{
			if (!index)
			{
				index = await fetch(`${root}assets/search.json`).then((r) => r.json()).catch(() => []);
			}
			return index;
		};

		const esc = (s) => s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
		const mark = (text, terms) =>
		{
			let out = esc(text);
			for (const t of terms)
			{
				out = out.replace(new RegExp(`(${t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')})`, 'gi'), '<mark>$1</mark>');
			}
			return out;
		};
		const snippet = (text, terms) =>
		{
			const lower = text.toLowerCase();
			const at = Math.min(...terms.map((t) => lower.indexOf(t)).filter((i) => i >= 0), text.length);
			const start = Math.max(0, at - 50);
			return (start > 0 ? '…' : '') + text.slice(start, start + 180);
		};

		const run = async () =>
		{
			const q = input.value.trim().toLowerCase();
			if (!q)
			{
				close();
				return;
			}
			const terms = q.split(/\s+/).filter(Boolean);
			const data = await load();
			results = data.map((e) =>
			{
				const h = e.h.toLowerCase();
				const p = e.p.toLowerCase();
				const x = e.x.toLowerCase();
				let score = 0;
				for (const t of terms)
				{
					const s = (h.includes(t) ? 6 : 0) + (p.includes(t) ? 3 : 0) + (x.includes(t) ? 1 : 0);
					if (!s)
					{
						return null;
					}
					score += s + (h.startsWith(t) ? 2 : 0);
				}
				if (h === q)
				{
					score += 10;
				}
				return { e, score };
			}).filter(Boolean).sort((a, b) => b.score - a.score).slice(0, 12);

			selected = results.length ? 0 : -1;
			list.innerHTML = results.length
				? results.map(({ e }, i) => `<li role="option" id="sr-${i}" aria-selected="${i === selected}"><a href="${root}${e.u}">`
					+ `<span class="sr-where">${esc(e.p)}</span><span class="sr-head">${mark(e.h, terms)}</span>`
					+ `<span class="sr-text">${mark(snippet(e.x, terms), terms)}</span></a></li>`).join('')
				: `<li class="sr-empty">Nothing matches “${esc(input.value.trim())}”. Try a property, an element or a console command.</li>`;
			list.hidden = false;
			input.setAttribute('aria-expanded', 'true');
			input.setAttribute('aria-activedescendant', selected >= 0 ? 'sr-0' : '');
		};

		const close = () =>
		{
			list.hidden = true;
			input.setAttribute('aria-expanded', 'false');
			input.removeAttribute('aria-activedescendant');
		};

		const move = (d) =>
		{
			if (!results.length)
			{
				return;
			}
			selected = (selected + d + results.length) % results.length;
			list.querySelectorAll('[role="option"]').forEach((li, i) => li.setAttribute('aria-selected', String(i === selected)));
			list.querySelector(`#sr-${selected}`)?.scrollIntoView({ block: 'nearest' });
			input.setAttribute('aria-activedescendant', `sr-${selected}`);
		};

		input.setAttribute('role', 'combobox');
		input.setAttribute('aria-autocomplete', 'list');
		input.addEventListener('input', run);
		input.addEventListener('focus', () => { load(); if (input.value.trim()) run(); });
		input.addEventListener('keydown', (e) =>
		{
			if (e.key === 'ArrowDown') { e.preventDefault(); move(1); }
			else if (e.key === 'ArrowUp') { e.preventDefault(); move(-1); }
			else if (e.key === 'Enter' && selected >= 0) { e.preventDefault(); location.href = list.querySelector(`#sr-${selected} a`).href; }
			else if (e.key === 'Escape') { if (input.value) { input.value = ''; close(); } else { input.blur(); } }
		});
		document.addEventListener('click', (e) => { if (!search.contains(e.target)) close(); });
		document.addEventListener('keydown', (e) =>
		{
			if (e.key === '/' && !e.ctrlKey && !e.metaKey && !/^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement?.tagName))
			{
				e.preventDefault();
				input.focus();
			}
		});
	}

	// The landing page's game-select: arrow keys move the cursor and the screen follows.
	const tabs = [...document.querySelectorAll('.gs-list [role="tab"]')];
	if (tabs.length)
	{
		const select = (tab, focus) =>
		{
			for (const t of tabs)
			{
				const on = t === tab;
				t.setAttribute('aria-selected', String(on));
				t.tabIndex = on ? 0 : -1;
				document.getElementById(t.getAttribute('aria-controls')).hidden = !on;
			}
			if (focus)
			{
				tab.focus();
			}
		};
		for (const t of tabs)
		{
			t.addEventListener('click', () => select(t, false));
			t.addEventListener('keydown', (e) =>
			{
				const i = tabs.indexOf(t);
				const next = { ArrowDown: i + 1, ArrowRight: i + 1, ArrowUp: i - 1, ArrowLeft: i - 1, Home: 0, End: tabs.length - 1 }[e.key];
				if (next === undefined)
				{
					return;
				}
				e.preventDefault();
				select(tabs[(next + tabs.length) % tabs.length], true);
			});
		}
	}

	// The attract loop plays muted; it can be paused, and starts paused for reduced motion.
	const loop = document.querySelector('.screen-glass video');
	const loopToggle = document.querySelector('.loop-toggle');
	const glass = document.querySelector('.screen-glass');
	if (loop && loopToggle)
	{
		const sync = () =>
		{
			loopToggle.textContent = loop.paused ? 'Play' : 'Pause';
			loopToggle.setAttribute('aria-pressed', String(loop.paused));
			glass.classList.toggle('paused', loop.paused);
		};
		glass.addEventListener('click', () => (loop.paused ? loop.play() : loop.pause()));
		if (reduceMotion)
		{
			loop.removeAttribute('autoplay');
			loop.pause();
		}
		loopToggle.addEventListener('click', () => (loop.paused ? loop.play() : loop.pause()));
		loop.addEventListener('play', sync);
		loop.addEventListener('pause', sync);
		sync();
	}
})();
