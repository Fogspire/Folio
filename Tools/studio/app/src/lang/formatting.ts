import { formatterFor, minimalEdits } from '../edit/format';
import { TextEdit } from '../edit/rcss';
import { monaco } from '../monaco';

function optionsOf(model: monaco.editor.ITextModel) {
	const { insertSpaces, tabSize } = model.getOptions();
	return { unit: insertSpaces ? ' '.repeat(tabSize) : '\t', tabSize };
}

/**
 * The model's text with LF line ends. Formatting works on this, whatever the file uses: an edit that
 * ended between a CR and its LF would insert a stray CR, which Monaco reads as a line break.
 */
function textOf(model: monaco.editor.ITextModel): string {
	return model.getValue(monaco.editor.EndOfLinePreference.LF);
}

/** The edits that format a model, as offsets into its LF text, or none for a file that is not RML or RCSS. */
export function formatEdits(model: monaco.editor.ITextModel): TextEdit[] {
	const format = formatterFor(model.uri.path);
	if (!format) return [];
	const text = textOf(model);
	return minimalEdits(text, format(text, optionsOf(model)));
}

/** Edits from formatEdits as Monaco edits. Lines and columns are the same in the LF text as in the model. */
export function toMonacoEdits(model: monaco.editor.ITextModel, edits: TextEdit[]): monaco.languages.TextEdit[] {
	const text = textOf(model);
	const starts = [0];
	for (let index = text.indexOf('\n'); index !== -1; index = text.indexOf('\n', index + 1)) starts.push(index + 1);
	const positionAt = (offset: number) => {
		let low = 0;
		let high = starts.length - 1;
		while (low < high) {
			const middle = (low + high + 1) >> 1;
			if (starts[middle] <= offset) low = middle;
			else high = middle - 1;
		}
		return { lineNumber: low + 1, column: offset - starts[low] + 1 };
	};
	return edits.map((edit) => {
		const start = positionAt(edit.from);
		const end = positionAt(edit.to);
		return { range: new monaco.Range(start.lineNumber, start.column, end.lineNumber, end.column), text: edit.text };
	});
}

/** Formats a model in place as one undoable step. Returns whether anything changed. */
export function formatModel(model: monaco.editor.ITextModel): boolean {
	const edits = formatEdits(model);
	if (!edits.length) return false;
	model.pushStackElement();
	model.pushEditOperations(null, toMonacoEdits(model, edits), () => null);
	model.pushStackElement();
	return true;
}
