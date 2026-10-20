import { host } from './host';

export interface Settings {
	/** The program the terminal runs. Empty picks one for the platform. */
	terminalShell: string;
	terminalArguments: string;
	terminalFontSize: number;
	codeFontSize: number;
	/** Whether saving an .rml or .rcss formats it first. */
	formatOnSave: boolean;
	/** Whether Enter, `}` and `>` format the line they finish. */
	formatOnType: boolean;
	/** Whether pasted text is formatted. */
	formatOnPaste: boolean;
	/** Whether a file is formatted when the code editor loses focus. */
	formatOnBlur: boolean;
	updateCheck: boolean;
	/** Whether a start reopens the panels as they were left. */
	restoreLayout: boolean;
	/** Whether Shift on a resize handle writes the size to that element alone rather than the rule it shares. */
	shiftResize: boolean;
	/** How long the timeline runs, in seconds. */
	timelineDuration: number;
	/** Samples each way a bake is drawn with before it is averaged down to its size. */
	bakeSamples: number;
	/** What a bake is drawn over: 'none' for transparent, or a colour as #rrggbb. */
	bakeBackground: string;
	/** The colour last picked as Custom, kept while another background is chosen. */
	bakeCustomColour: string;
	/** What the canvas draws the document over: 'none' for a checkerboard showing transparency, or #rrggbb. */
	canvasBackground: string;
	canvasCustomColour: string;
	/** Keys the person has chosen, by command. A command not listed keeps its defaults. */
	keys: Record<string, string[]>;
}

export const DEFAULT_SETTINGS: Settings = {
	terminalShell: '',
	terminalArguments: '',
	terminalFontSize: 13,
	codeFontSize: 13,
	formatOnSave: false,
	formatOnType: false,
	formatOnPaste: false,
	formatOnBlur: false,
	updateCheck: true,
	restoreLayout: true,
	shiftResize: true,
	timelineDuration: 2,
	bakeSamples: 2,
	bakeBackground: 'none',
	bakeCustomColour: '#3c78b4',
	canvasBackground: '#000000',
	canvasCustomColour: '#3c78b4',
	keys: {},
};

/** The backgrounds a bake or the canvas can be drawn over. 'custom' stands for a picked colour. */
export const BACKGROUNDS: [string, string][] = [['none', 'None (transparent)'], ['#ffffff', 'White'], ['#000000', 'Black'], ['#808080', '50% grey'], ['custom', 'Custom']];

const SETTINGS_KEY = 'folio.studio.settings';

/** A scripted run starts from the defaults and leaves the person's own settings alone. */
export function loadSettings(): Settings {
	if (host.testing) return { ...DEFAULT_SETTINGS };
	try {
		return { ...DEFAULT_SETTINGS, ...JSON.parse(localStorage.getItem(SETTINGS_KEY) ?? '{}') };
	} catch {
		return { ...DEFAULT_SETTINGS };
	}
}

export function saveSettings(settings: Settings) {
	if (!host.testing) localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
}
