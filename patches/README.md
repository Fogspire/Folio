# Dependency patches

Any `.patch` in `patches/<dependency>/` is applied to that dependency when CMake fetches it,
in sorted filename order. This is how a fix that exists upstream but not in the pinned release
gets carried, without forking or hand-editing the build tree.

Generate one from a modified checkout of the dependency:

    git -C build/win64-RelWithDebInfo/_deps/rmlui-src diff > patches/RmlUi/0001-short-description.patch

Adding or changing a patch needs a clean build tree, since the dependency is only fetched and
patched once:

    ./scripts/build.sh --clean

Every patch should say in its commit message why it exists and what upstream state makes it
removable, so the pin can be moved forward without archaeology.
