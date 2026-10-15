import { useState } from 'react';
import { popup } from '../commands';
import { MARKUP_MIME, SNIPPETS } from '../dnd';
import { showPanel } from '../dock';
import { insertNear } from '../edit/commands';
import { basename, dirname, extension, stem } from '../paths';
import { copyLibraryDocument, isLibraryDocument, openDocument, openFile, store } from '../session';

const GROUPS: { title: string; directory: string; hint: string }[] = [
	{ title: 'Recipes', directory: 'rcss/recipes', hint: 'Complete screens to start from.' },
	{ title: 'Examples', directory: 'examples', hint: 'Single techniques, shown working.' },
	{ title: 'Component styles', directory: 'rcss/lib', hint: 'The classes every document gets from lib.rml.' },
];

function Palette() {
	return (
		<section>
			<h3>Elements</h3>
			<p className="dim">Drag onto the canvas, or click to add to the selection.</p>
			<div className="palette">
				{SNIPPETS.map((snippet) => (
					<button
						key={snippet.name}
						className="palette-item"
						draggable
						title={snippet.hint}
						onDragStart={(event) => {
							event.dataTransfer.setData(MARKUP_MIME, snippet.markup);
							event.dataTransfer.effectAllowed = 'copy';
						}}
						onClick={() => {
							insertNear(snippet.markup);
							showPanel('canvas');
						}}
					>
						{snippet.name}
					</button>
				))}
			</div>
		</section>
	);
}

export function LibraryPanel() {
	const project = store.use((state) => state.project);
	const files = store.use((state) => state.libraryFiles);
	const documentPath = store.use((state) => state.document);
	const [name, setName] = useState('');

	if (!project) return <div className="panel empty">No project open.</div>;

	const previewing = documentPath && isLibraryDocument(documentPath) ? documentPath : null;
	const preview = (file: string) => {
		if (extension(file) === '.rml') {
			openDocument(file);
			showPanel('canvas');
		} else {
			openFile(file);
			showPanel('code');
		}
	};
	const defaultName = previewing ? stem(previewing) : '';

	return (
		<div className="panel library">
			{previewing && (
				<form
					className="library-use"
					onSubmit={(event) => {
						event.preventDefault();
						copyLibraryDocument(previewing, name.trim() || defaultName).then((created) => {
							if (created) setName('');
						});
					}}
				>
					<span className="dim">Copy <b>{basename(previewing)}</b> to UI/screens/ as</span>
					<div className="row">
						<input value={name} placeholder={defaultName} onChange={(event) => setName(event.target.value)} spellCheck={false} />
						<button className="button primary" type="submit">Use</button>
					</div>
				</form>
			)}
			<Palette />
			{GROUPS.map((group) => {
				const entries = files.filter((file) => dirname(file).endsWith(group.directory));
				if (!entries.length) return null;
				return (
					<section key={group.title}>
						<h3>{group.title}</h3>
						<p className="dim">{group.hint}</p>
						{entries.map((file) => (
							<div
								key={file}
								className={'tree-row file' + (file === documentPath ? ' selected' : '')}
								title={extension(file) === '.rml' ? 'Click to preview. Use copies it into your project.' : 'Open in Code'}
								onClick={() => preview(file)}
								onContextMenu={async (event) => {
									event.preventDefault();
									const isDocument = extension(file) === '.rml';
									const chosen = await popup([
										{ id: 'use', label: 'Use as a New Document', enabled: isDocument },
										{ id: 'preview', label: 'Preview on the Canvas', enabled: isDocument },
										{ id: 'code', label: 'Open in Code' },
									]);
									if (chosen === 'use') copyLibraryDocument(file, stem(file));
									else if (chosen === 'preview') preview(file);
									else if (chosen === 'code') {
										openFile(file);
										showPanel('code');
									}
								}}
							>
								<span className="grow">{stem(file)}</span>
								{extension(file) === '.rml' && (
									<button
										className="row-action"
										title={'Copy to UI/screens/' + basename(file) + ' and open it'}
										onClick={(event) => {
											event.stopPropagation();
											copyLibraryDocument(file, stem(file));
										}}
									>
										Use
									</button>
								)}
							</div>
						))}
					</section>
				);
			})}
		</div>
	);
}
