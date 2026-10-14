import { host } from './host';

export interface Settings {
	/** The program the terminal runs. Empty picks one for the platform. */
	terminalShell: string;
	terminalArguments: string;
	terminalFontSize: number;
	codeFontSize: number;
	updateCheck: boolean;
	/** Whether a start reopens the panels as they were left. */
	restoreLayout: boolean;
	/** Keys the person has chosen, by command. A command not listed keeps its defaults. */
	keys: Record<string, string[]>;
}

export const DEFAULT_SETTINGS: Settings = {
	terminalShell: '',
	terminalArguments: '',
	terminalFontSize: 13,
	codeFontSize: 13,
	updateCheck: true,
	restoreLayout: true,
	keys: {},
};

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
