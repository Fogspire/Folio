# Line graphs

`<line-graph>` draws a line through a series of values, coloured per point and blended smoothly between them, with a dot on the newest value and an optional average line. It is geometry, not elements, so a graph of any length costs one draw.

```html
<line-graph data-attr-values="g.Values" data-attr-tones="g.Tones" data-attr-average="g.Average" data-attr-length="60"></line-graph>
```

```css
line-graph { display: block; height: 40dp; }
```

Give it a size: it has no intrinsic one.

## Attributes

| | |
|---|---|
| `values` | Heights from 0 (bottom of the content box) to 1 (top), oldest first, separated by spaces or commas. |
| `tones` | One per value, -1 to 1. -1 draws in `graph-low-color`, 0 in `graph-mid-color`, 1 in `graph-high-color`. Missing tones draw at 0. |
| `average` | A height from 0 to 1 for a horizontal line. Leave it out for none. |
| `length` | How many values fill the width. Fewer sit at the right edge, so a history that is still filling grows leftwards instead of stretching. Leave it out to spread the values across the width. |

Normalise in the model, not the markup: the element draws what it is given and knows nothing about units.

## Troubleshooting

- **Nothing draws:** the element has no height, or `values` is empty or unparseable. Parsing stops at the first thing that is not a number.
- **The line stretches as data arrives:** set `length`.

## Properties

| | Default |
|---|---|
| `graph-low-color` | `#e5534b` |
| `graph-mid-color` | `#4ea1f3` |
| `graph-high-color` | `#57d38c` |
| `graph-average-color` | `#ffffff5a` |
| `graph-line-width` | `2dp` |
| `graph-dot-size` | `6dp`, the dot's diameter. 0 for none. |

`opacity` applies to the whole graph. The line and dot are inset by the dot's radius so neither is clipped at the edges.
