import type { Host } from '../../shell/preload';

declare global {
	interface Window {
		host: Host;
	}
}

export const host: Host = window.host;

export interface HostState {
	version: string;
	recent: string[];
	initialProject: string | null;
	initialDocument: string | null;
	platform: string;
}

export interface ProjectInfo {
	root: string;
	folioRoot: string;
	name: string;
}

export interface TreeFile {
	path: string;
	size: number;
}

export interface UpdateStatus {
	current: string;
	latest: string | null;
	available: boolean;
}
