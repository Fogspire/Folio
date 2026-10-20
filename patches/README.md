# Dependency patches

Any `.patch` in `patches/<dependency>/` is applied to that dependency when CMake fetches it, in sorted filename order. This is how a fix that exists upstream but not in the pinned release gets carried, without forking or hand-editing the build tree.

Generate one from a checkout of the dependency with the earlier patches applied, so it holds only the new change:

    git -C build/win64-RelWithDebInfo/_deps/rmlui-src diff > patches/RmlUi/0001-short-description.patch

A new patch is applied to an existing build tree at the next configure. Each applied patch is recorded with its hash in `.folio-patches` in the dependency's source tree, so changing a patch that is already applied needs a clean build tree:

    ./scripts/build.sh --clean

Every patch should say in its commit message why it exists and what upstream state makes it removable, so the pin can be moved forward without archaeology.
