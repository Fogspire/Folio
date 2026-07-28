#include "ElementPath.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <vector>

namespace uipreview {

namespace {

std::string Escape(const std::string& text)
{
	std::string out;
	for (const char c : text)
	{
		if (c == '"' || c == '\\')
			out += '\\';
		out += c;
	}
	return out;
}

} // namespace

std::string ComputeElementPath(Rml::Element* element)
{
	if (!element)
		return {};

	// An id is both cheaper and far more robust across edits than a structural path.
	if (const Rml::String& id = element->GetId(); !id.empty())
		return "#" + id;

	std::vector<std::string> segments;
	for (Rml::Element* current = element; current && current->GetParentNode(); current = current->GetParentNode())
	{
		if (const Rml::String& id = current->GetId(); !id.empty())
		{
			segments.push_back("#" + id);
			break;
		}

		int index = 1;
		Rml::Element* parent = current->GetParentNode();
		for (int i = 0; i < parent->GetNumChildren(); ++i)
		{
			Rml::Element* sibling = parent->GetChild(i);
			if (sibling == current)
				break;
			if (sibling->GetTagName() == current->GetTagName())
				++index;
		}

		segments.push_back(current->GetTagName() + ":nth-of-type(" + std::to_string(index) + ")");
	}

	std::string path;
	for (auto it = segments.rbegin(); it != segments.rend(); ++it)
	{
		if (!path.empty())
			path += " > ";
		path += *it;
	}
	return path;
}

Rml::Element* ResolveElementPath(Rml::ElementDocument* document, const std::string& path)
{
	if (!document || path.empty())
		return nullptr;

	if (Rml::Element* found = document->QuerySelector(path))
		return found;

	// Fall back up the path, so an element whose subtree changed still lands somewhere sane.
	std::string shortened = path;
	for (;;)
	{
		const size_t last = shortened.rfind(" > ");
		if (last == std::string::npos)
			return nullptr;
		shortened.erase(last);
		if (Rml::Element* found = document->QuerySelector(shortened))
			return found;
	}
}

std::string DescribeElement(Rml::Element* element)
{
	if (!element)
		return "none";

	std::string out = ComputeElementPath(element);
	out += " tag=" + element->GetTagName();

	const Rml::String classes = element->GetClassNames();
	if (!classes.empty())
		out += " classes=\"" + Escape(classes) + "\"";

	Rml::String text;
	element->GetInnerRML(text);
	if (!text.empty() && text.size() < 64 && text.find('<') == std::string::npos)
		out += " text=\"" + Escape(text) + "\"";

	const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
	const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
	out += " rect=[" + std::to_string((int)offset.x) + "," + std::to_string((int)offset.y) + "," +
		std::to_string((int)size.x) + "," + std::to_string((int)size.y) + "]";

	return out;
}

} // namespace uipreview
