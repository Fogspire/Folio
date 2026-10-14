import { popup } from './commands';
import { host } from './host';
import { basename } from './paths';
import { activateFile, closeFiles, store } from './session';

/** A strip of open files. Middle-click closes one, and the context menu closes them in groups. */
export function FileTabs({ files, active }: { files: string[]; active: string | null }) {
	const dirty = store.use((state) => state.dirty);

	const menu = async (file: string) => {
		const index = files.indexOf(file);
		const chosen = await popup([
			{ id: 'close', label: 'Close' },
			{ id: 'others', label: 'Close All Other Tabs', enabled: files.length > 1 },
			{ id: 'left', label: 'Close All Tabs to the Left', enabled: index > 0 },
			{ id: 'right', label: 'Close All Tabs to the Right', enabled: index < files.length - 1 },
			{ id: 'all', label: 'Close All Tabs' },
			{ separator: true },
			{ id: 'reveal', label: 'Show in the File Manager' },
			{ id: 'path', label: 'Copy Path' },
		]);
		if (chosen === 'close') closeFiles([file]);
		else if (chosen === 'others') closeFiles(files.filter((entry) => entry !== file));
		else if (chosen === 'left') closeFiles(files.slice(0, index));
		else if (chosen === 'right') closeFiles(files.slice(index + 1));
		else if (chosen === 'all') closeFiles(files);
		else if (chosen === 'reveal') host.reveal(file);
		else if (chosen === 'path') navigator.clipboard.writeText(file);
	};

	return (
		<div className="code-tabs">
			{files.map((file) => (
				<div
					key={file}
					className={'code-tab' + (file === active ? ' active' : '')}
					title={file}
					onClick={() => activateFile(file)}
					// Stops the middle button starting the browser's scroll mode.
					onMouseDown={(event) => { if (event.button === 1) event.preventDefault(); }}
					onAuxClick={(event) => { if (event.button === 1) closeFiles([file]); }}
					onContextMenu={(event) => {
						event.preventDefault();
						menu(file);
					}}
				>
					{basename(file)}
					{dirty[file] && <span className="dirty-dot">●</span>}
					<span
						className="close"
						onClick={(event) => {
							event.stopPropagation();
							closeFiles([file]);
						}}
					>
						×
					</span>
				</div>
			))}
		</div>
	);
}
