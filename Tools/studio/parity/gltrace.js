// Debug aid: logs WebGL errors by call name, and with FOLIO_GL_TRACE=calls the draw-relevant call stream.
if (process.env.FOLIO_GL_TRACE) {
	const { ipcRenderer } = require('electron');
	const verbose = process.env.FOLIO_GL_TRACE === 'calls';
	const watched = new Set(['useProgram', 'uniformMatrix4fv', 'uniform2fv', 'drawElements', 'bindFramebuffer',
		'blitFramebuffer', 'clear', 'clearColor', 'enable', 'disable', 'scissor', 'stencilFunc', 'stencilOp',
		'colorMask', 'blendFunc', 'viewport', 'bindVertexArray']);
	const original = HTMLCanvasElement.prototype.getContext;
	let reported = 0;
	let logged = 0;
	const ids = new Map();
	const idOf = (object) => {
		if (!ids.has(object)) ids.set(object, object.constructor.name.replace('WebGL', '') + '#' + ids.size);
		return ids.get(object);
	};
	HTMLCanvasElement.prototype.getContext = function (type, attributes) {
		const gl = original.call(this, type, attributes);
		if (!gl || gl.__traced) return gl;
		gl.__traced = true;
		const names = {};
		for (const key in gl) if (typeof gl[key] === 'number') names[gl[key]] = names[gl[key]] || key;
		const getError = gl.getError.bind(gl);
		for (const key in gl) {
			if (typeof gl[key] !== 'function' || key === 'getError') continue;
			const fn = gl[key].bind(gl);
			gl[key] = (...args) => {
				const result = fn(...args);
				const error = getError();
				const show = () => args.map((a) => {
					if (typeof a === 'number') return names[a] && a > 255 ? names[a] : a;
					if (a && a.length !== undefined) return '[' + Array.from(a).slice(0, 16).map((v) => +v.toFixed(3)).join(' ') + ']';
					if (a && typeof a === 'object') return idOf(a);
					return String(a);
				}).join(', ');
				if (error && reported++ < 25) ipcRenderer.send('log', `GL ${names[error] || error} in ${key}(${show()})`);
				if (verbose && watched.has(key) && logged++ < 400) ipcRenderer.send('log', `gl.${key}(${show()})`);
				return result;
			};
		}
		return gl;
	};
}
