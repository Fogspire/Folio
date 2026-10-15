import { useEffect, useRef, useState } from 'react';
import { currentSource, sourceElement } from '../edit/commands';
import { elementContaining } from '../edit/rml';
import { monaco } from '../monaco';
import { FileTabs } from '../FileTabs';
import { host } from '../host';
import { basename, extension, isUnder } from '../paths';
import { FONT_EXTENSIONS, IMAGE_EXTENSIONS, peekModel, save, select, store } from '../session';

const SPECIMEN = 'The quick brown fox jumps over the lazy dog 0123456789';
const SPECIMEN_SIZES = [14, 20, 32, 48, 72];
let specimenCount = 0;

/** An image as it is, or a font set at a few sizes. */
function AssetPreview({ path }: { path: string }) {
	const [image, setImage] = useState<string | null>(null);
	const [family, setFamily] = useState<string | null>(null);
	const [size, setSize] = useState('');
	const [failed, setFailed] = useState(false);
	const kind = extension(path);

	useEffect(() => {
		let cancelled = false;
		let url: string | null = null;
		let face: FontFace | null = null;
		setImage(null);
		setFamily(null);
		setSize('');
		setFailed(false);

		host.readFile(path).then(async (data: Uint8Array | null) => {
			if (cancelled) return;
			if (!data) return setFailed(true);
			if (FONT_EXTENSIONS.has(kind)) {
				face = new FontFace('specimen-' + ++specimenCount, data.slice().buffer);
				try {
					await face.load();
				} catch {
					return setFailed(true);
				}
				if (cancelled) return;
				document.fonts.add(face);
				setFamily(face.family);
				return;
			}
			const type = kind === '.svg' ? 'image/svg+xml' : kind === '.png' ? 'image/png' : 'image/jpeg';
			url = URL.createObjectURL(new Blob([data.slice()], { type }));
			setImage(url);
		});

		return () => {
			cancelled = true;
			if (url) URL.revokeObjectURL(url);
			if (face) document.fonts.delete(face);
		};
	}, [path, kind]);

	return (
		<div className="asset-preview">
			<div className="asset-caption">{basename(path)}{size && ' · ' + size}</div>
			{failed && <div className="dim">This file could not be read.</div>}
			{image && (
				<img
					src={image}
					alt=""
					onLoad={(event) => setSize(event.currentTarget.naturalWidth + ' × ' + event.currentTarget.naturalHeight)}
					onError={() => setFailed(true)}
				/>
			)}
			{family && SPECIMEN_SIZES.map((points) => (
				<div key={points} className="specimen" style={{ fontFamily: `'${family}'`, fontSize: points }}>{SPECIMEN}</div>
			))}
		</div>
	);
}

export function CodePanel() {
	const openFiles = store.use((state) => state.openFiles);
	const activeFile = store.use((state) => state.activeFile);
	const documentPath = store.use((state) => state.document);
	const reveal = store.use((state) => state.reveal);
	const project = store.use((state) => state.project);
	const selection = store.use((state) => state.selection);
	const revision = store.use((state) => state.revision);
	const fontSize = store.use((state) => state.settings.codeFontSize);
	const marks = useRef<monaco.editor.IEditorDecorationsCollection | null>(null);
	const container = useRef<HTMLDivElement>(null);
	const editor = useRef<monaco.editor.IStandaloneCodeEditor | null>(null);

	useEffect(() => {
		const instance = monaco.editor.create(container.current!, {
			model: null,
			theme: 'studio',
			automaticLayout: true,
			minimap: { enabled: false },
			fontSize: 13,
			fontFamily: "'Roboto Mono', Consolas, Menlo, monospace",
			insertSpaces: false,
			tabSize: 4,
			detectIndentation: true,
			scrollBeyondLastLine: false,
			renderWhitespace: 'selection',
			wordWrap: 'off',
			padding: { top: 6 },
		});
		instance.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, () => {
			const path = store.get().activeFile;
			if (path) save(path);
		});
		// Moving the text cursor into an element selects it on the canvas. Only a cursor the person
		// moved: one placed by a reveal or an edit is following the selection, not leading it.
		instance.onDidChangeCursorPosition((event) => {
			const state = store.get();
			const model = instance.getModel();
			if (!model || event.source === 'api' || event.source === 'modelChange' || !instance.hasTextFocus()) return;
			if (state.activeFile !== state.document) return;
			const source = currentSource();
			const element = source ? elementContaining(source.document, model.getOffsetAt(event.position)) : null;
			const id = element && source ? source.map.ids.get(element)?.[0] : undefined;
			if (id !== undefined && id !== state.selection) select(id);
		});
		// Monaco measures character widths once, so a face that finishes loading afterwards draws text
		// wider than the caret thinks it is.
		const remeasure = () => monaco.editor.remeasureFonts();
		document.fonts.addEventListener('loadingdone', remeasure);
		marks.current = instance.createDecorationsCollection();
		editor.current = instance;
		return () => {
			document.fonts.removeEventListener('loadingdone', remeasure);
			instance.dispose();
		};
	}, []);

	useEffect(() => {
		editor.current?.updateOptions({ fontSize });
	}, [fontSize]);

	useEffect(() => {
		const instance = editor.current;
		if (!instance) return;
		const model = activeFile ? peekModel(activeFile) : null;
		if (instance.getModel() !== model) instance.setModel(model);
		// What Folio ships is shared by every project that uses it, so it is shown, not edited, unless
		// the plugin itself is the project.
		const shipped = !!activeFile && !!project && project.folioRoot !== project.root && isUnder(project.folioRoot, activeFile);
		instance.updateOptions({ readOnly: shipped });
	}, [activeFile, project]);

	useEffect(() => {
		const instance = editor.current;
		if (!instance || !reveal || reveal.path !== activeFile) return;
		instance.revealLineInCenter(reveal.line);
		instance.setPosition({ lineNumber: reveal.line, column: reveal.column });
		instance.focus();
	}, [reveal, activeFile]);

	// The selected element's opening tag is marked in the margin, and scrolled to when it is out of view.
	useEffect(() => {
		const instance = editor.current;
		const model = instance?.getModel();
		const element = activeFile === documentPath ? sourceElement(selection) : null;
		if (!instance || !model || !element) {
			marks.current?.clear();
			return;
		}
		const start = model.getPositionAt(element.start);
		const end = model.getPositionAt(element.openEnd);
		marks.current?.set([{
			range: new monaco.Range(start.lineNumber, 1, end.lineNumber, 1),
			options: { isWholeLine: true, className: 'selected-source', linesDecorationsClassName: 'selected-source-margin' },
		}]);
		if (!instance.hasTextFocus()) instance.revealLineInCenterIfOutsideViewport(start.lineNumber);
	}, [selection, revision, activeFile, documentPath]);

	const asset = activeFile && (IMAGE_EXTENSIONS.has(extension(activeFile)) || FONT_EXTENSIONS.has(extension(activeFile))) ? activeFile : null;

	return (
		<div className="panel code-panel">
			<FileTabs files={openFiles} active={activeFile} />
			{asset && <AssetPreview path={asset} />}
			<div className="code-editor" ref={container} style={{ display: asset ? 'none' : undefined }} />
		</div>
	);
}
