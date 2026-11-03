// Preview of the `heat` shader filter: a horizontal ripple, like air over a fire.
//
//     filter: shader("heat 4dp 3 1.5");    amplitude, ripples down the element, ripples per second
//
// The engine runs the game's own pass under the same name. This file is only what uipreview and
// Folio Studio draw in its place.

void main()
{
	float amplitude = folio_args[0].x * folio_pixel_scale / folio_size.x;
	float ripples = max(folio_args[1].x, 0.0);
	float speed = folio_args[2].x;

	vec2 uv = folio_uv();
	uv.x += amplitude * sin((uv.y * ripples + folio_time * speed) * 6.2831853);
	folio_colour = folio_source(uv);
}
