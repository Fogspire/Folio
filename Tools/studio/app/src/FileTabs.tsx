import { useLayoutEffect, useRef } from 'react';
import { popup } from './commands';
import { host } from './host';
import { basename } from './paths';
import { activateFile, closeFiles, revertFiles, store } from './session';

/** A strip of open files. Middle-click closes one, and the context menu closes them in groups. */
export function FileTabs({ files, active }: { files: string[]; active: string | null }) {
	const dirty = store.use((state) => state.dirty);
	const strip = useRef<HTMLDivElement>(null);

	// Also on resize, since a narrower panel can push the active tab out of view.
	useLayoutEffect(() => {
		const element = strip.current;
		if (!element) return;
		const reveal = () => {
			const tab = element.querySelector<HTMLElement>('.code-tab.active');
			if (!tab) return;
			const left = tab.offsetLeft;
			const right = left + tab.offsetWidth;
			if (left < element.scrollLeft) element.scrollLeft = left;
			else if (right > element.scrollLeft + element.clientWidth) element.scrollLeft = right - element.clientWidth;
		};
		reveal();
		const observer = new ResizeObserver(reveal);
		observer.observe(element);
		return () => observer.disconnect();
	}, [active, files]);

	const menu = async (file: string) => {
		const index = files.indexOf(file);
		const chosen = await popup([
			{ id: 'close', label: 'Close' },
			{ id: 'others', label: 'Close All Other Tabs', enabled: files.length > 1 },
			{ id: 'left', label: 'Close All Tabs to the Left', enabled: index > 0 },
			{ id: 'right', label: 'Close All Tabs to the Right', enabled: index < files.length - 1 },
			{ id: 'all', label: 'Close All Tabs' },
			{ separator: true },
			{ id: 'revert', label: 'Revert Unsaved Changes', enabled: !!dirty[file] },
			{ separator: true },
			{ id: 'reveal', label: 'Show in the File Manager' },
			{ id: 'path', label: 'Copy Path' },
		]);
		if (chosen === 'close') closeFiles([file]);
		else if (chosen === 'others') closeFiles(files.filter((entry) => entry !== file));
		else if (chosen === 'left') closeFiles(files.slice(0, index));
		else if (chosen === 'right') closeFiles(files.slice(index + 1));
		else if (chosen === 'all') closeFiles(files);
		else if (chosen === 'revert') revertFiles([file]);
		else if (chosen === 'reveal') host.reveal(file);
		else if (chosen === 'path') navigator.clipboard.writeText(file);
	};

	return (
		<div
			ref={strip}
			className="code-tabs"
			// The strip has no scrollbar, so the wheel scrolls it sideways.
			onWheel={(event) => {
				if (event.deltaY !== 0 && event.deltaX === 0) event.currentTarget.scrollLeft += event.deltaY;
			}}
		>
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
