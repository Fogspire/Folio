import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

export default defineConfig({
	root: 'app',
	// Relative, because the app is served from a custom scheme rather than a web root.
	base: './',
	plugins: [react()],
	build: {
		outDir: '../out/app',
		emptyOutDir: true,
		chunkSizeWarningLimit: 8000,
	},
	test: {
		root: '.',
		include: ['test/**/*.test.ts'],
	},
});
