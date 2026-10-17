import type { AddPanelOptions, DockviewApi } from 'dockview-react';
import { host } from './host';
import { loadSettings } from './settings';

interface PanelPlace {
	id: string;
	title: string;
	/** The panel this one is docked against, when that one is open. */
	beside?: string;
	direction: 'within' | 'below' | 'left' | 'right';
	/** Docked behind its neighbour's tab rather than in front of it. */
	inactive?: boolean;
}

/** Every panel, in the order the default layout adds them. */
const PANELS: PanelPlace[] = [
	{ id: 'canvas', title: 'Canvas', direction: 'within' },
	{ id: 'docs', title: 'Docs', beside: 'canvas', direction: 'within', inactive: true },
	{ id: 'code', title: 'Code', beside: 'canvas', direction: 'below' },
	{ id: 'problems', title: 'Problems', beside: 'code', direction: 'within', inactive: true },
	{ id: 'activity', title: 'Activity', beside: 'code', direction: 'within', inactive: true },
	{ id: 'terminal', title: 'Terminal', beside: 'code', direction: 'within', inactive: true },
	{ id: 'files', title: 'Files', direction: 'left' },
	{ id: 'library', title: 'Library', beside: 'files', direction: 'within', inactive: true },
	{ id: 'outline', title: 'Outline', direction: 'right' },
	{ id: 'inspector', title: 'Properties', beside: 'outline', direction: 'below' },
	{ id: 'data', title: 'Data', beside: 'inspector', direction: 'within', inactive: true },
];

let api: DockviewApi | null = null;
const LAYOUT_KEY = 'folio.studio.layout';
let rememberTimer: number | null = null;

function addPanel(place: PanelPlace, fresh: boolean) {
	const options: AddPanelOptions = { id: place.id, component: place.id, title: place.title, inactive: fresh && place.inactive };
	// Always rendered: the canvas element is the renderer's surface and must not be torn down with a tab.
	if (place.id === 'canvas') options.renderer = 'always';

	const reference = place.beside && api!.getPanel(place.beside) ? place.beside : undefined;
	if (reference) options.position = { referencePanel: reference, direction: place.direction };
	else if (place.direction !== 'within' && api!.panels.length) options.position = { direction: place.direction };
	api!.addPanel(options);
}

export function setDock(value: DockviewApi) {
	api = value;
	const remembered = host.testing || !loadSettings().restoreLayout ? null : localStorage.getItem(LAYOUT_KEY);
	if (!remembered || !restoreLayout(remembered)) resetLayout();

	// The layout as it was left is the one the next start opens with.
	api.onDidLayoutChange(() => {
		if (host.testing) return;
		if (rememberTimer !== null) window.clearTimeout(rememberTimer);
		rememberTimer = window.setTimeout(() => localStorage.setItem(LAYOUT_KEY, serialiseLayout()), 500);
	});
}

export function serialiseLayout(): string {
	return api ? JSON.stringify(api.toJSON()) : '';
}

/** Applies a saved layout, and falls back to the default when it is not one. */
export function restoreLayout(text: string): boolean {
	if (!api) return false;
	try {
		api.fromJSON(JSON.parse(text));
		if (!api.panels.length) throw new Error('empty layout');
	} catch {
		resetLayout();
		return false;
	}
	api.getPanel('canvas')?.api.setRenderer('always');
	return true;
}

export function resetLayout() {
	if (!api) return;
	api.clear();
	for (const place of PANELS) addPanel(place, true);

	// Sized once everything exists: a width given while the grid is still being built is a share of a
	// layout that then changes under it.
	api.getPanel('files')!.group.api.setSize({ width: 250 });
	api.getPanel('outline')!.group.api.setSize({ width: 330 });
	api.getPanel('code')!.group.api.setSize({ height: 280 });
	api.getPanel('canvas')!.api.setActive();
}

/** Brings a panel to the front of its group, wherever the person has docked it, reopening it if it was closed. */
export function showPanel(id: string) {
	if (!api) return;
	const place = PANELS.find((entry) => entry.id === id);
	if (!api.getPanel(id) && place) addPanel(place, false);
	api.getPanel(id)?.api.setActive();
}
