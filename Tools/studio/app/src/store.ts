import { useSyncExternalStore } from 'react';

export interface Store<T> {
	get(): T;
	set(patch: Partial<T>): void;
	subscribe(listener: () => void): () => void;
	/** Selectors must return a value that is stable while the state is: a field, not a new object. */
	use<U>(selector: (state: T) => U): U;
}

export function createStore<T extends object>(initial: T): Store<T> {
	let state = initial;
	const listeners = new Set<() => void>();
	const subscribe = (listener: () => void) => {
		listeners.add(listener);
		return () => listeners.delete(listener);
	};
	return {
		get: () => state,
		set: (patch) => {
			state = { ...state, ...patch };
			for (const listener of [...listeners]) listener();
		},
		subscribe,
		use: (selector) => useSyncExternalStore(subscribe, () => selector(state)),
	};
}
