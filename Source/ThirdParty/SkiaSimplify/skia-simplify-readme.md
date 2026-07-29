
# Skia-Simplify

This is a reduced version of the Google Skia library,
consisting of a small subset of its source files,
selected to only support the minimum required functionality.
This functionality is primarily Skia's `Simplify` function
(hence the name Skia-Simplify), which simplifies overlapped geometry
of a vector shape.
Later, for the purposes of loading SVG files,
it has been extended to also provide `SkParsePath` and
`Op` (path boolean operations).
It also has to include all code which these functions depend on.

## Extraction from full Skia library

To gather the subset of source files from the full Skia library,
do the following:

1. Clone Skia's source [repository](https://github.com/google/skia/)
to a folder (which we will call `skia-source`).
    - Note: We have been using the `chrome/m120` branch.
2. Execute the Python script `get-source.py` with two additional arguments:
`skia-source` directory and `skia-simplify` output directory.
    - This will filter the source files and only copy the subset to `skia-simplify`.
    - Some include files will be reported as not found, this is normal.
    - **Beware**: If your version of the Skia library
    or its file structure is too different,
    it may be necessary to modify the Python script.

To successfully compile the sources into a library,
you need to implement the function `SkDebugf`
or redefine it in `include/config/SkUserConfig.h`.
Then you can simply pass all of the CPP files in `src` to a compiler
with C++17 enabled.

## All-in-one version

An optional next step is to combine all header files into one
and all source (CPP) files into one, which can then be easily
included in a project (much like a header-only library).
For this, you can simply execute the Python script `all-in-one.py`
with the output directory (`skia-simplify`) from the previous step
as its argument.
The output will be placed in the `all-in-one` subdirectory.
