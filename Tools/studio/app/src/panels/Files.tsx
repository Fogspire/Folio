import { useMemo, useState } from 'react';
import { popup } from '../commands';
import { showPanel } from '../dock';
import { host } from '../host';
import { basename, extension, join, relativeTo, stem } from '../paths';
import { copyLibraryDocument, createFile, deleteFile, openDocument, openFile, store } from '../session';

/** A file about to be made, waiting for its name. */
interface Creating {
	directory: string;
	kind: '.rml' | '.rcss';
	/** The recipe it is copied from, for a document that does not start empty. */
	recipe: string | null;
}

/** The context menu of a folder: what can be made in it. */
async function folderMenu(directory: string, begin: (creating: Creating) => void) {
	const recipes = store.get().libraryFiles.filter((file) => file.includes('/rcss/recipes/') && extension(file) === '.rml');
	const chosen = await popup([
		{ id: 'document', label: 'New Document…' },
		{ label: 'New Document from Recipe', enabled: recipes.length > 0, submenu: recipes.map((file) => ({ id: 'recipe:' + file, label: stem(file) })) },
		{ id: 'stylesheet', label: 'New Stylesheet…' },
		{ separator: true },
		{ id: 'reveal', label: 'Show in the File Manager' },
		{ id: 'path', label: 'Copy Path' },
	]);
	if (chosen === 'document') begin({ directory, kind: '.rml', recipe: null });
	else if (chosen === 'stylesheet') begin({ directory, kind: '.rcss', recipe: null });
	else if (chosen?.startsWith('recipe:')) begin({ directory, kind: '.rml', recipe: chosen.slice('recipe:'.length) });
	else if (chosen === 'reveal') host.reveal(directory);
	else if (chosen === 'path') navigator.clipboard.writeText(directory);
}

function NameForm({ creating, done }: { creating: Creating; done: () => void }) {
	const root = store.get().project!.root;
	const suggestion = creating.recipe ? stem(creating.recipe) : creating.kind === '.rml' ? 'screen' : 'styles';
	const [name, setName] = useState('');

	return (
		<form
			className="library-use"
			onSubmit={async (event) => {
				event.preventDefault();
				const chosen = name.trim() || suggestion;
				const created = creating.recipe
					? await copyLibraryDocument(creating.recipe, chosen, creating.directory)
					: await createFile(creating.directory, chosen, creating.kind);
				if (!created) return;
				showPanel(creating.kind === '.rml' ? 'canvas' : 'code');
				done();
			}}
		>
			<span className="dim">
				New {creating.recipe ? stem(creating.recipe) + ' document' : creating.kind === '.rml' ? 'document' : 'stylesheet'} in <b>{relativeTo(root, creating.directory)}/</b>
			</span>
			<div className="row">
				<input
					autoFocus
					value={name}
					placeholder={suggestion + creating.kind}
					spellCheck={false}
					onChange={(event) => setName(event.target.value)}
					onKeyDown={(event) => { if (event.key === 'Escape') done(); }}
				/>
				<button className="button primary" type="submit">Create</button>
			</div>
		</form>
	);
}

interface Folder {
	name: string;
	path: string;
	folders: Map<string, Folder>;
	files: string[];
}

function buildTree(root: string, files: string[]): Folder {
	const top: Folder = { name: '', path: '', folders: new Map(), files: [] };
	for (const file of files) {
		const parts = relativeTo(root, file).split('/');
		let folder = top;
		for (const part of parts.slice(0, -1)) {
			if (!folder.folders.has(part)) folder.folders.set(part, { name: part, path: folder.path + '/' + part, folders: new Map(), files: [] });
			folder = folder.folders.get(part)!;
		}
		folder.files.push(file);
	}
	return top;
}

interface RowProps {
	folder: Folder;
	depth: number;
	collapsed: Set<string>;
	toggle: (path: string) => void;
	begin: (creating: Creating) => void;
}

function FolderRows({ folder, depth, collapsed, toggle, begin }: RowProps) {
	const root = store.use((state) => state.project?.root ?? '');
	const documentPath = store.use((state) => state.document);
	const activeFile = store.use((state) => state.activeFile);
	const dirty = store.use((state) => state.dirty);

	return (
		<>
			{[...folder.folders.values()].map((child) => (
				<div key={child.path}>
					<div
						className="tree-row folder"
						style={{ paddingLeft: 6 + depth * 12 }}
						onClick={() => toggle(child.path)}
						onContextMenu={(event) => {
							event.preventDefault();
							event.stopPropagation();
							folderMenu(root + child.path, begin);
						}}
					>
						<span className="twisty">{collapsed.has(child.path) ? '▸' : '▾'}</span>
						{child.name}
					</div>
					{!collapsed.has(child.path) && <FolderRows folder={child} depth={depth + 1} collapsed={collapsed} toggle={toggle} begin={begin} />}
				</div>
			))}
			{folder.files.map((file) => {
				const isDocument = extension(file) === '.rml';
				return (
					<div
						key={file}
						className={'tree-row file' + (file === documentPath ? ' selected' : '') + (file === activeFile ? ' active' : '') + (isDocument ? ' document' : '')}
						style={{ paddingLeft: 6 + depth * 12 + 14 }}
						title={isDocument ? 'Show on the canvas' : 'Open'}
						onClick={() => {
							if (isDocument) {
								openDocument(file);
							} else {
								openFile(file);
								showPanel('code');
							}
						}}
						onContextMenu={async (event) => {
							event.preventDefault();
							event.stopPropagation();
							const chosen = await popup([
								{ id: 'canvas', label: 'Show on the Canvas', enabled: isDocument },
								{ id: 'code', label: 'Open in Code' },
								{ separator: true },
								{ id: 'reveal', label: 'Show in the File Manager' },
								{ id: 'path', label: 'Copy Path' },
								{ separator: true },
								{ id: 'delete', label: 'Delete' },
							]);
							if (chosen === 'delete') deleteFile(file);
							else if (chosen === 'canvas') {
								openDocument(file);
								showPanel('canvas');
							} else if (chosen === 'code') {
								openFile(file);
								showPanel('code');
							} else if (chosen === 'reveal') host.reveal(file);
							else if (chosen === 'path') navigator.clipboard.writeText(file);
						}}
					>
						{basename(file)}
						{dirty[file] && <span className="dirty-dot" title="Unsaved changes">●</span>}
					</div>
				);
			})}
		</>
	);
}

export function FilesPanel() {
	const project = store.use((state) => state.project);
	const files = store.use((state) => state.projectFiles);
	const [collapsed, setCollapsed] = useState<Set<string>>(new Set());
	const [creating, setCreating] = useState<Creating | null>(null);
	const tree = useMemo(() => (project ? buildTree(project.root, files) : null), [project, files]);

	const toggle = (path: string) => setCollapsed((current) => {
		const next = new Set(current);
		if (!next.delete(path)) next.add(path);
		return next;
	});

	if (!project || !tree) return <div className="panel empty">No project open.</div>;
	// Empty space is the project's UI folder, so a project with nothing in it can still be started here.
	const onBackground = (event: React.MouseEvent) => {
		event.preventDefault();
		folderMenu(join(project.root, 'UI'), setCreating);
	};
	const form = creating && <NameForm key={creating.directory + creating.kind + creating.recipe} creating={creating} done={() => setCreating(null)} />;
	if (!files.length) {
		return (
			<div className="panel empty" onContextMenu={onBackground}>
				{form}
				No documents under <code>UI/</code>. Right-click here for a new one, or start from one in Library.
			</div>
		);
	}
	return (
		<div className="panel tree" onContextMenu={onBackground}>
			{form}
			<FolderRows folder={tree} depth={0} collapsed={collapsed} toggle={toggle} begin={setCreating} />
		</div>
	);
}
