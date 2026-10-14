import { execFile } from 'node:child_process';

export interface UpdateStatus {
	current: string;
	/** The newest release tag on the remote, without its leading v, or null when it could not be read. */
	latest: string | null;
	available: boolean;
}

export function parseVersion(text: string): number[] | null {
	const match = /^v?(\d+)\.(\d+)\.(\d+)$/.exec(text.trim());
	return match ? [Number(match[1]), Number(match[2]), Number(match[3])] : null;
}

export function compareVersions(a: number[], b: number[]): number {
	for (let i = 0; i < 3; i++) {
		if (a[i] !== b[i]) return a[i] - b[i];
	}
	return 0;
}

/** Picks the highest vX.Y.Z out of `git ls-remote --tags` output. */
export function latestTag(lsRemoteOutput: string): string | null {
	let best: number[] | null = null;
	for (const line of lsRemoteOutput.split('\n')) {
		// Annotated tags are listed twice, the second time peeled with a ^{} suffix.
		const match = /refs\/tags\/(v?\d+\.\d+\.\d+)(\^\{\})?\s*$/.exec(line);
		const version = match && parseVersion(match[1]);
		if (version && (!best || compareVersions(version, best) > 0)) best = version;
	}
	return best ? best.join('.') : null;
}

/**
 * Asks the checkout's own remote for its release tags.
 *
 * Through git rather than a host's API, so a private repository works with whatever credentials the
 * person already cloned it with. Never throws: offline, not a checkout and no git all mean "unknown".
 */
export function checkForUpdates(checkoutDirectory: string, current: string): Promise<UpdateStatus> {
	return new Promise((resolve) => {
		const unknown: UpdateStatus = { current, latest: null, available: false };
		execFile(
			'git',
			['ls-remote', '--tags', 'origin'],
			// BatchMode and the empty prompt stop a missing credential from hanging on a question nobody sees.
			{ cwd: checkoutDirectory, timeout: 15000, windowsHide: true, env: { ...process.env, GIT_TERMINAL_PROMPT: '0', GIT_SSH_COMMAND: 'ssh -o BatchMode=yes' } },
			(error, stdout) => {
				if (error) return resolve(unknown);
				const latest = latestTag(stdout);
				const currentVersion = parseVersion(current);
				const latestVersion = latest ? parseVersion(latest) : null;
				resolve({
					current,
					latest,
					available: !!currentVersion && !!latestVersion && compareVersions(latestVersion, currentVersion) > 0,
				});
			},
		);
	});
}
