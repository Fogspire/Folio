/** Where a style edit is written. Always shown to the person making it, never chosen out of sight. */
export type Target =
	| { kind: 'rule'; rule: string; file: string; line: number }
	| { kind: 'inline' }
	| { kind: 'new-rule'; selector: string };
