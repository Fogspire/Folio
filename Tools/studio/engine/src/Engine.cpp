#include "App.h"
#include "Diagnostics.h"
#include "Options.h"
#include "ProvenanceReport.h"
#include "backend/UipreviewBackendGLFW.h"

#include "FolioCore/FolioCoreVersion.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/StyleSheetSpecification.h>

#include <GLFW/glfw3.h>
#include <emscripten/bind.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using json = nlohmann::ordered_json;

namespace {

/** What an id resolves to when the element is one RmlUi generated, such as a scrollbar. */
const char* const kNoId = "?";

double Round2(float value)
{
	return std::round((double)value * 100.0) / 100.0;
}

json BoxRect(Rml::Element* element, Rml::BoxArea area)
{
	const Rml::Vector2f offset = element->GetAbsoluteOffset(area);
	const Rml::Vector2f size = element->GetBox().GetSize(area);
	return json::array({Round2(offset.x), Round2(offset.y), Round2(size.x), Round2(size.y)});
}

bool IsText(Rml::Element* element)
{
	return rmlui_dynamic_cast<Rml::ElementText*>(element) != nullptr;
}

/** Position among the parent's element children, text nodes not counted, or -1 for a generated child. */
int ElementIndex(Rml::Element* element)
{
	Rml::Element* parent = element->GetParentNode();
	if (!parent)
		return -1;

	int index = 0;
	for (int i = 0; i < parent->GetNumChildren(); ++i)
	{
		Rml::Element* child = parent->GetChild(i);
		if (child == element)
			return index;
		if (!IsText(child))
			++index;
	}
	return -1;
}

/**
 * Names an element by where it sits: element-child indices from the document down, as "0/2/1", and
 * the empty string for the document itself.
 *
 * Structural rather than the id-preferring selector the reports use, because the editor matches
 * elements against its own parse of the markup, and position is the one thing both sides can count.
 */
std::string StructuralId(Rml::Element* element, Rml::ElementDocument* document)
{
	std::vector<int> indices;
	for (Rml::Element* current = element; current && current != document; current = current->GetParentNode())
	{
		const int index = ElementIndex(current);
		if (index < 0)
			return kNoId;
		indices.push_back(index);
	}

	std::string id;
	for (auto it = indices.rbegin(); it != indices.rend(); ++it)
	{
		if (!id.empty())
			id += '/';
		id += std::to_string(*it);
	}
	return id;
}

Rml::Element* ResolveStructuralId(Rml::ElementDocument* document, const std::string& id)
{
	Rml::Element* current = document;
	size_t position = 0;

	while (current && position < id.size())
	{
		const size_t slash = id.find('/', position);
		const std::string segment = id.substr(position, slash == std::string::npos ? std::string::npos : slash - position);
		position = slash == std::string::npos ? id.size() : slash + 1;

		int wanted = 0;
		for (const char c : segment)
		{
			if (c < '0' || c > '9')
				return nullptr;
			wanted = wanted * 10 + (c - '0');
		}

		Rml::Element* found = nullptr;
		int index = 0;
		for (int i = 0; i < current->GetNumChildren() && !found; ++i)
		{
			Rml::Element* child = current->GetChild(i);
			if (IsText(child))
				continue;
			if (index++ == wanted)
				found = child;
		}
		current = found;
	}

	return current;
}

json Attributes(Rml::Element* element)
{
	json out = json::object();
	for (const auto& [name, value] : element->GetAttributes())
		out[name] = value.Get<Rml::String>();
	return out;
}

std::string OwnText(Rml::Element* element)
{
	std::string out;
	for (int i = 0; i < element->GetNumChildren(); ++i)
	{
		if (Rml::ElementText* text = rmlui_dynamic_cast<Rml::ElementText*>(element->GetChild(i)))
			out += text->GetText();
	}
	return out;
}

json DescribeTree(Rml::Element* element, const std::string& id)
{
	json node;
	node["id"] = id;
	node["tag"] = element->GetTagName();
	node["attributes"] = Attributes(element);
	node["rect"] = BoxRect(element, Rml::BoxArea::Border);
	node["visible"] = element->IsVisible(true);
	node["text"] = OwnText(element);

	json children = json::array();
	int index = 0;
	for (int i = 0; i < element->GetNumChildren(); ++i)
	{
		Rml::Element* child = element->GetChild(i);
		if (IsText(child))
			continue;
		children.push_back(DescribeTree(child, id.empty() ? std::to_string(index) : id + "/" + std::to_string(index)));
		++index;
	}
	node["children"] = std::move(children);
	return node;
}

const char* SeverityName(uipreview::Severity severity)
{
	switch (severity)
	{
	case uipreview::Severity::Error: return "error";
	case uipreview::Severity::Warning: return "warning";
	default: return "info";
	}
}

/** One document open on one canvas, driven a frame at a time by the page. */
class Engine {
public:
	/** Opens a document. Settings are JSON: root, document, width, height, and the optional dpRatio,
	    themes, culture, textScale, reducedMotion and data. */
	bool Open(const std::string& settings_text)
	{
		Close();

		const json settings = json::parse(settings_text, nullptr, false);
		if (!settings.is_object())
			return false;

		options = uipreview::Options();
		options.host_driven = true;
		options.watch = false;
		options.write_assets_wanted = false;
		options.root = settings.value("root", std::string());
		options.document = settings.value("document", std::string());
		options.width = settings.value("width", 1920);
		options.height = settings.value("height", 1080);
		options.culture = settings.value("culture", std::string());
		options.data_path = settings.value("data", std::string());
		options.text_scale = settings.value("textScale", 1.0f);
		options.reduced_motion = settings.value("reducedMotion", false);
		if (settings.contains("dpRatio") && settings["dpRatio"].is_number())
		{
			options.dp_ratio = settings["dpRatio"].get<float>();
			options.dp_ratio_fixed = options.dp_ratio > 0.0f;
		}
		if (settings.contains("themes") && settings["themes"].is_array())
		{
			for (const json& theme : settings["themes"])
				options.themes.push_back(theme.get<std::string>());
		}

		diagnostics.Reset();
		app = std::make_unique<uipreview::App>(options, diagnostics);

		if (!app->Initialise() || !app->GetDocument())
		{
			// The records outlive the app, so the page can still say why.
			app.reset();
			return false;
		}

		// Latches the context into the backend's input callbacks, which the browser calls between frames.
		Backend::ProcessEvents(app->GetContext());
		return true;
	}

	void Close() { app.reset(); }

	bool IsOpen() const { return app != nullptr; }

	void Frame(double seconds)
	{
		if (app)
			app->StepLive(seconds);
	}

	/** A ratio of zero or less derives it from the size, the way a resized game window does. */
	void Resize(int width, int height, double dp_ratio)
	{
		if (!app || width <= 0 || height <= 0)
			return;

		options.width = width;
		options.height = height;
		options.dp_ratio_fixed = dp_ratio > 0.0;
		if (options.dp_ratio_fixed)
			options.dp_ratio = (float)dp_ratio;

		glfwSetWindowSize(Backend::GetWindow(), width, height);
		app->ApplyViewportSize(width, height);
	}

	/** Takes a JSON array of absolute paths and reloads as little as they require. */
	void FilesChanged(const std::string& paths_text)
	{
		const json paths = json::parse(paths_text, nullptr, false);
		if (!app || !paths.is_array())
			return;

		std::vector<std::string> changed;
		for (const json& path : paths)
		{
			std::string value = path.get<std::string>();
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			changed.push_back(std::move(value));
		}
		app->HandleFileChanges(changed);
	}

	/** The elements under a point, innermost first, as a JSON array of ids ending at the document. */
	std::string HitTest(double x, double y) const
	{
		json chain = json::array();
		if (!app)
			return chain.dump();

		Rml::ElementDocument* document = app->GetDocument();
		for (Rml::Element* element = app->GetContext()->GetElementAtPoint(Rml::Vector2f((float)x, (float)y));
			element && element != document; element = element->GetParentNode())
		{
			if (element->GetOwnerDocument() != document)
				break;

			const std::string id = StructuralId(element, document);
			if (id != kNoId)
				chain.push_back(id);
		}
		chain.push_back("");
		return chain.dump();
	}

	std::string Tree() const
	{
		return app ? DescribeTree(app->GetDocument(), std::string()).dump() : std::string("null");
	}

	/**
	 * Boxes, rule-set properties with their sources, inline properties and attributes of one element.
	 * `names_text` is a JSON array of further properties to resolve whether or not a rule set them.
	 */
	std::string Inspect(const std::string& id, const std::string& names_text) const
	{
		Rml::Element* element = app ? ResolveStructuralId(app->GetDocument(), id) : nullptr;
		if (!element)
			return "null";

		json out;
		out["id"] = id;
		out["tag"] = element->GetTagName();
		out["attributes"] = Attributes(element);
		out["boxes"] = {
			{"margin", BoxRect(element, Rml::BoxArea::Margin)},
			{"border", BoxRect(element, Rml::BoxArea::Border)},
			{"padding", BoxRect(element, Rml::BoxArea::Padding)},
			{"content", BoxRect(element, Rml::BoxArea::Content)},
		};
		out["rules"] = uipreview::DescribeRuleProperties(element);
		// A property can reach an element from a rule that matched an ancestor. That rule is not one
		// an edit to this element should land on, so the two are told apart here.
		for (json& rule : out["rules"])
			rule["inherited"] = element->GetLocalProperty(rule["name"].get<std::string>()) == nullptr;

		json inline_properties = json::object();
		for (const auto& [property_id, property] : element->GetLocalStyleProperties())
			inline_properties[Rml::StyleSheetSpecification::GetPropertyName(property_id)] = property.ToString();
		out["inline"] = std::move(inline_properties);

		json computed = json::object();
		const json names = json::parse(names_text, nullptr, false);
		if (names.is_array())
		{
			for (const json& name : names)
			{
				const std::string property_name = name.get<std::string>();
				if (const Rml::Property* property = element->GetProperty(property_name))
					computed[property_name] = property->ToString();
			}
		}
		out["computed"] = std::move(computed);

		return out.dump();
	}

	std::string Diagnostics() const
	{
		json out = json::array();
		for (const uipreview::Record& record : diagnostics.Records())
		{
			out.push_back({
				{"severity", SeverityName(record.severity)},
				{"code", record.code},
				{"file", record.file},
				{"line", record.line},
				{"message", record.message},
			});
		}
		return out.dump();
	}

	/** Sets a property on the live element only, for previewing a gesture before it is written to source. */
	bool SetInlineProperty(const std::string& id, const std::string& name, const std::string& value)
	{
		Rml::Element* element = app ? ResolveStructuralId(app->GetDocument(), id) : nullptr;
		return element && element->SetProperty(name, value);
	}

	void RemoveInlineProperty(const std::string& id, const std::string& name)
	{
		if (Rml::Element* element = app ? ResolveStructuralId(app->GetDocument(), id) : nullptr)
			element->RemoveProperty(name);
	}

	bool Screenshot(const std::string& path) { return app && app->CaptureTo(path); }

private:
	uipreview::Options options;
	/** Declared before the app, which holds a reference to it. */
	uipreview::Diagnostics diagnostics;
	std::unique_ptr<uipreview::App> app;
};

std::string Version()
{
	return Folio::GetCoreVersionString();
}

} // namespace

EMSCRIPTEN_BINDINGS(folio_engine)
{
	emscripten::function("version", &Version);

	emscripten::class_<Engine>("Engine")
		.constructor<>()
		.function("open", &Engine::Open)
		.function("close", &Engine::Close)
		.function("isOpen", &Engine::IsOpen)
		.function("frame", &Engine::Frame)
		.function("resize", &Engine::Resize)
		.function("filesChanged", &Engine::FilesChanged)
		.function("hitTest", &Engine::HitTest)
		.function("tree", &Engine::Tree)
		.function("inspect", &Engine::Inspect)
		.function("diagnostics", &Engine::Diagnostics)
		.function("setInlineProperty", &Engine::SetInlineProperty)
		.function("removeInlineProperty", &Engine::RemoveInlineProperty)
		.function("screenshot", &Engine::Screenshot);
}
