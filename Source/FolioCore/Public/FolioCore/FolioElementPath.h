#pragma once

#include <RmlUi/Core/Element.h>

#include <string>
#include <vector>

/**
 * A selector that identifies an element well enough to find it again after a reload.
 *
 * RmlUi gives no stable element identity across a document rebuild, so anything that has to survive
 * one - restored focus, a scroll position, a metrics entry compared against another renderer's - has
 * to be expressed as something re-resolvable. An id is used where there is one, since it survives
 * edits elsewhere in the document; otherwise a structural path.
 *
 * Shared rather than duplicated because two renderers comparing their reports must agree on what
 * names an element. Two implementations that drifted would report a difference that is only a naming
 * difference, which is worse than no comparison at all.
 *
 * Header-only for the same reason the rest of FolioCore's RmlUi code is: see FolioBehaviours.h. This
 * one touches no RmlUi globals, but the rule is cheaper to keep than to reason about each time.
 */
namespace Folio
{
	inline std::string ComputeElementPath(Rml::Element* Element)
	{
		if (!Element)
		{
			return {};
		}

		// An id is both cheaper and far more robust across edits than a structural path.
		if (const Rml::String& Id = Element->GetId(); !Id.empty())
		{
			return "#" + Id;
		}

		std::vector<std::string> Segments;
		for (Rml::Element* Current = Element; Current && Current->GetParentNode();
			Current = Current->GetParentNode())
		{
			if (const Rml::String& Id = Current->GetId(); !Id.empty())
			{
				Segments.push_back("#" + Id);
				break;
			}

			int Index = 1;
			Rml::Element* Parent = Current->GetParentNode();
			for (int i = 0; i < Parent->GetNumChildren(); ++i)
			{
				Rml::Element* Sibling = Parent->GetChild(i);
				if (Sibling == Current)
				{
					break;
				}
				if (Sibling->GetTagName() == Current->GetTagName())
				{
					++Index;
				}
			}

			Segments.push_back(Current->GetTagName() + ":nth-of-type(" + std::to_string(Index) + ")");
		}

		std::string Path;
		for (auto It = Segments.rbegin(); It != Segments.rend(); ++It)
		{
			if (!Path.empty())
			{
				Path += " > ";
			}
			Path += *It;
		}

		return Path;
	}
}
