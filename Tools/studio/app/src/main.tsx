import { createRoot } from 'react-dom/client';
import { App } from './App';
import { registerLanguageFeatures } from './lang/editor';
import { initialise } from './session';
import './styles.css';
import { runTestScript } from './testing';

// Folio's own faces, served from the plugin at run time, which is why they are not in the stylesheet.
for (const [family, file, weight] of [['Inter', 'Inter-Variable.ttf', '100 900'], ['Roboto Mono', 'RobotoMono-Variable.ttf', '100 700']]) {
	const face = new FontFace(family, `url(/folio/fonts/${file})`, { weight });
	document.fonts.add(face);
	face.load().catch(() => undefined);
}

registerLanguageFeatures();
createRoot(document.getElementById('root')!).render(<App />);
initialise().then(runTestScript);
