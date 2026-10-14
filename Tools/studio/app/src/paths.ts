/** Host paths are absolute and forward slashed throughout the page, whatever the platform. */

export function dirname(path: string): string {
	const index = path.lastIndexOf('/');
	return index <= 0 ? path.slice(0, index + 1) : path.slice(0, index);
}

export function basename(path: string): string {
	return path.slice(path.lastIndexOf('/') + 1);
}

export function extension(path: string): string {
	const name = basename(path);
	const index = name.lastIndexOf('.');
	return index <= 0 ? '' : name.slice(index).toLowerCase();
}

export function stem(path: string): string {
	const name = basename(path);
	const index = name.lastIndexOf('.');
	return index <= 0 ? name : name.slice(0, index);
}

export function join(...parts: string[]): string {
	return parts
		.filter((part) => part !== '')
		.map((part, index) => (index === 0 ? part.replace(/\/+$/, '') : part.replace(/^\/+|\/+$/g, '')))
		.join('/');
}

/** Whether `path` is `directory` or sits under it. Case-insensitive, as the two desktop defaults are. */
export function isUnder(directory: string, path: string): boolean {
	const a = directory.toLowerCase().replace(/\/+$/, '');
	const b = path.toLowerCase();
	return b === a || b.startsWith(a + '/');
}

export function relativeTo(directory: string, path: string): string {
	return path.slice(directory.replace(/\/+$/, '').length + 1);
}
