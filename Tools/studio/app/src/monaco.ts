import * as monaco from 'monaco-editor';
import EditorWorker from 'monaco-editor/editor/editor.worker?worker';
import JsonWorker from 'monaco-editor/language/json/json.worker?worker';
import { conf as cssConf, language as cssLanguage } from 'monaco-editor/languages/definitions/css/css';
import { conf as htmlConf, language as htmlLanguage } from 'monaco-editor/languages/definitions/html/html';
import { extension } from './paths';

self.MonacoEnvironment = {
	getWorker: (_id: string, label: string) => (label === 'json' ? new JsonWorker() : new EditorWorker()),
};

/** Walks a tokenizer definition replacing one embedded language with another, regexes left intact. */
function retarget<T>(value: T, from: string, to: string): T {
	if (typeof value === 'string') return (value === from ? to : value) as T;
	if (Array.isArray(value)) return value.map((entry) => retarget(entry, from, to)) as T;
	if (value && typeof value === 'object' && !(value instanceof RegExp)) {
		const out: Record<string, unknown> = {};
		for (const [key, entry] of Object.entries(value)) out[key] = retarget(entry, from, to);
		return out as T;
	}
	return value;
}

// Their own languages rather than html and css: those arrive with validators that would flag every
// RCSS property CSS does not have, and miss every CSS property RCSS does not have.
monaco.languages.register({ id: 'rcss', extensions: ['.rcss'], aliases: ['RCSS'] });
monaco.languages.setLanguageConfiguration('rcss', cssConf);
monaco.languages.setMonarchTokensProvider('rcss', cssLanguage);

monaco.languages.register({ id: 'rml', extensions: ['.rml'], aliases: ['RML'] });
monaco.languages.setLanguageConfiguration('rml', htmlConf);
monaco.languages.setMonarchTokensProvider('rml', retarget(retarget(htmlLanguage, 'text/css', 'rcss'), 'text/javascript', 'plaintext'));

monaco.editor.defineTheme('studio', {
	base: 'vs-dark',
	inherit: true,
	rules: [
		{ token: 'comment', foreground: '6f7780' },
		{ token: 'tag', foreground: '8fb0d0' },
		{ token: 'metatag', foreground: '8fb0d0' },
		{ token: 'delimiter', foreground: '808a94' },
		{ token: 'attribute.name', foreground: 'b9c4d0' },
		{ token: 'attribute.value', foreground: 'a8c49a' },
		{ token: 'string', foreground: 'a8c49a' },
		{ token: 'number', foreground: 'c3b6d9' },
		{ token: 'keyword', foreground: '8fb0d0' },
		{ token: 'type', foreground: '9ac4c0' },
	],
	colors: {
		'editor.background': '#1e1e1e',
		'editor.foreground': '#d6d6d6',
		'editor.lineHighlightBackground': '#262626',
		'editorLineNumber.foreground': '#5a5a5a',
		'editorLineNumber.activeForeground': '#a0a0a0',
		'editor.selectionBackground': '#3d4f63',
		'editor.inactiveSelectionBackground': '#333a42',
		'editorCursor.foreground': '#d6d6d6',
		'editorWidget.background': '#262626',
		'editorWidget.border': '#3a3a3a',
		'focusBorder': '#5f7f9f',
		'scrollbarSlider.background': '#ffffff18',
		'scrollbarSlider.hoverBackground': '#ffffff28',
	},
});

export function languageFor(path: string): string {
	switch (extension(path)) {
		case '.rml': return 'rml';
		case '.rcss': return 'rcss';
		case '.json': return 'json';
		case '.md': return 'markdown';
		default: return 'plaintext';
	}
}

export { monaco };
