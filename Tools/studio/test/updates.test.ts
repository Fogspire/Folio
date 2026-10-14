import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { checkForUpdates, compareVersions, latestTag, parseVersion } from '../shell/updates';

const listing = [
	'1f0c\trefs/tags/v0.0.1',
	'2a9d\trefs/tags/v0.2.0',
	'3b7e\trefs/tags/v0.10.0',
	'3b7e\trefs/tags/v0.10.0^{}',
	'4c11\trefs/tags/v0.9.9',
	'5d22\trefs/tags/nightly',
	'6e33\trefs/tags/v1.0',
].join('\n');

describe('updates', () => {
	it('reads a version with or without its v', () => {
		expect(parseVersion('v1.2.3')).toEqual([1, 2, 3]);
		expect(parseVersion('1.2.3')).toEqual([1, 2, 3]);
		expect(parseVersion('1.2')).toBeNull();
		expect(parseVersion('nightly')).toBeNull();
	});

	it('orders versions by number, not as text', () => {
		expect(compareVersions([0, 10, 0], [0, 9, 9])).toBeGreaterThan(0);
		expect(compareVersions([1, 0, 0], [1, 0, 0])).toBe(0);
	});

	it('picks the highest release tag and ignores everything else', () => {
		expect(latestTag(listing)).toBe('0.10.0');
		expect(latestTag('')).toBeNull();
		expect(latestTag('abc\trefs/heads/main')).toBeNull();
	});

	it('finds a newer release on the remote a checkout was cloned from', async () => {
		const root = mkdtempSync(join(tmpdir(), 'folio-updates-'));
		const git = (cwd: string, ...args: string[]) => execFileSync('git', args, { cwd, stdio: 'ignore' });
		try {
			const remote = join(root, 'remote');
			git(root, 'init', '-q', remote);
			git(remote, '-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-q', '--allow-empty', '-m', 'one');
			git(remote, 'tag', 'v0.3.0');
			git(remote, 'tag', 'v0.12.1');
			git(root, 'clone', '-q', remote, join(root, 'checkout'));

			expect(await checkForUpdates(join(root, 'checkout'), '0.3.0')).toEqual({ current: '0.3.0', latest: '0.12.1', available: true });
			expect((await checkForUpdates(join(root, 'checkout'), '0.12.1')).available).toBe(false);
		} finally {
			rmSync(root, { recursive: true, force: true });
		}
	});

	it('reports nothing, rather than failing, outside a checkout', async () => {
		const status = await checkForUpdates('/nowhere/that/exists', '0.0.1');
		expect(status).toEqual({ current: '0.0.1', latest: null, available: false });
	});
});
