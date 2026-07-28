#pragma once

#include <string>

namespace Rml {
class Element;
class ElementDocument;
} // namespace Rml

namespace uipreview {

/**
 * A selector that identifies an element well enough to find it again after a reload.
 *
 * RmlUi gives no stable element identity across a document rebuild, so focus and scroll
 * restoration have to be expressed as something re-resolvable. An id is used when there is
 * one, since it survives edits elsewhere in the document; otherwise a structural path.
 */
std::string ComputeElementPath(Rml::Element* element);

/**
 * Resolves a path produced by ComputeElementPath, falling back to the nearest surviving
 * ancestor when the exact element is gone.
 */
Rml::Element* ResolveElementPath(Rml::ElementDocument* document, const std::string& path);

/** Short human readable summary of an element, for focus reporting. */
std::string DescribeElement(Rml::Element* element);

} // namespace uipreview
