#include "DataModel.h"

#include "Diagnostics.h"

#include <RmlUi/Core/Context.h>

#include <fstream>

using ordered_json = nlohmann::ordered_json;

namespace uipreview {

namespace {

class JsonScalarDefinition;
class JsonArrayDefinition;
class JsonStructDefinition;

} // namespace

/**
 * One definition per JSON kind, shared by every node of that kind. The node pointer carries
 * all the state, so children returned from Child() need no registration of their own.
 */
class JsonDataModel::Definitions {
public:
	Rml::DataVariable For(ordered_json& node)
	{
		if (node.is_object())
			return Rml::DataVariable(&structure, &node);
		if (node.is_array())
			return Rml::DataVariable(&array, &node);
		return Rml::DataVariable(&scalar, &node);
	}

	class Scalar final : public Rml::VariableDefinition {
	public:
		Scalar() : VariableDefinition(Rml::DataVariableType::Scalar) {}

		bool Get(void* ptr, Rml::Variant& variant) override
		{
			const ordered_json& node = *static_cast<ordered_json*>(ptr);
			if (node.is_string())
				variant = node.get<std::string>();
			else if (node.is_boolean())
				variant = node.get<bool>();
			else if (node.is_number_integer())
				variant = node.get<int>();
			else if (node.is_number_float())
				variant = (float)node.get<double>();
			else if (node.is_null())
				variant = Rml::String();
			else
				variant = node.dump();
			return true;
		}

		bool Set(void* ptr, const Rml::Variant& variant) override
		{
			*static_cast<ordered_json*>(ptr) = variant.Get<Rml::String>();
			return true;
		}
	};

	/**
	 * How many entries an array has, as a scalar.
	 *
	 * RmlUi calls VariableDefinition::Size() from data-for and nowhere else, so nothing in a data
	 * expression can reach an array's length: `{{ items.size }}` and `{{ items | size }}` both
	 * fail, and a transform function cannot help because transforms take Variants and an array
	 * cannot produce one. Every screen that wants "3 items" or an empty-list message needs it.
	 */
	class Count final : public Rml::VariableDefinition {
	public:
		Count() : VariableDefinition(Rml::DataVariableType::Scalar) {}

		bool Get(void* ptr, Rml::Variant& variant) override
		{
			variant = (int)static_cast<ordered_json*>(ptr)->size();
			return true;
		}
	};

	class Array final : public Rml::VariableDefinition {
	public:
		explicit Array(Definitions& owner) : VariableDefinition(Rml::DataVariableType::Array), owner(owner) {}

		int Size(void* ptr) override { return (int)static_cast<ordered_json*>(ptr)->size(); }

		Rml::DataVariable Child(void* ptr, const Rml::DataAddressEntry& address) override
		{
			ordered_json& node = *static_cast<ordered_json*>(ptr);

			// An array has no named members of its own, so this shadows nothing.
			if (address.index < 0 && address.name == "size")
				return Rml::DataVariable(&owner.count, ptr);

			if (address.index < 0 || address.index >= (int)node.size())
				return Rml::DataVariable();
			return owner.For(node[(size_t)address.index]);
		}

	private:
		Definitions& owner;
	};

	class Struct final : public Rml::VariableDefinition {
	public:
		explicit Struct(Definitions& owner) : VariableDefinition(Rml::DataVariableType::Struct), owner(owner) {}

		Rml::DataVariable Child(void* ptr, const Rml::DataAddressEntry& address) override
		{
			ordered_json& node = *static_cast<ordered_json*>(ptr);
			const auto it = node.find(address.name);
			if (it == node.end())
				return Rml::DataVariable();
			return owner.For(*it);
		}

	private:
		Definitions& owner;
	};

	Scalar scalar;
	Count count;
	Array array {*this};
	Struct structure {*this};
};

JsonDataModel::JsonDataModel(Diagnostics& in_diagnostics)
	: diagnostics(in_diagnostics), definitions(std::make_unique<Definitions>())
{}

JsonDataModel::~JsonDataModel() = default;

bool JsonDataModel::MountEmpty(Rml::Context* context, const std::string& model_name)
{
	Rml::DataModelConstructor constructor = context->CreateDataModel(model_name);
	if (!constructor)
	{
		diagnostics.Report(Severity::Error, "E-DATA-MODEL", "could not create data model '" + model_name + "'");
		return false;
	}

	auto slot = std::make_unique<ordered_json>(ordered_json::object());
	constructor.BindCustomDataVariable("preview", definitions->For(*slot));
	slots["preview"] = std::move(slot);

	handle = constructor.GetModelHandle();
	mounted = true;
	return true;
}

bool JsonDataModel::Mount(Rml::Context* context, const std::string& model_name, const std::string& path)
{
	json_path = path;

	std::ifstream file(path);
	if (!file)
	{
		diagnostics.Report(Severity::Error, "E-DATA-NOTFOUND", "could not open data file: " + path);
		return false;
	}

	ordered_json parsed;
	try
	{
		file >> parsed;
	}
	catch (const std::exception& e)
	{
		diagnostics.Report(Severity::Error, "E-DATA-PARSE", std::string("could not parse ") + path + ": " + e.what());
		return false;
	}

	if (!parsed.is_object())
	{
		diagnostics.Report(Severity::Error, "E-DATA-SHAPE", "the data file must contain a JSON object at the top level");
		return false;
	}

	// A file may either hold the model directly, or hold several models keyed by name.
	if (const auto it = parsed.find(model_name); it != parsed.end() && it->is_object())
		parsed = *it;

	Rml::DataModelConstructor constructor = context->CreateDataModel(model_name);
	if (!constructor)
	{
		diagnostics.Report(Severity::Error, "E-DATA-MODEL", "could not create data model '" + model_name + "'");
		return false;
	}

	// Documents are expected to be able to bind run information regardless of what the mock
	// file happens to contain, and a slot cannot be added once the model is built.
	if (!parsed.contains("preview"))
		parsed["preview"] = ordered_json::object();

	for (auto& entry : parsed.items())
	{
		auto slot = std::make_unique<ordered_json>(entry.value());
		if (!constructor.BindCustomDataVariable(entry.key(), definitions->For(*slot)))
			diagnostics.Report(Severity::Warning, "W-DATA-BIND", "could not bind '" + entry.key() + "'");
		slots[entry.key()] = std::move(slot);
	}

	handle = constructor.GetModelHandle();
	mounted = true;
	return true;
}

bool JsonDataModel::Assign(const ordered_json& source, bool warn_about_new_keys)
{
	for (auto& entry : source.items())
	{
		const auto it = slots.find(entry.key());
		if (it == slots.end())
		{
			if (warn_about_new_keys)
			{
				// The constructor is gone once the model exists, so a name that was not
				// present at mount time cannot be bound without rebuilding the document.
				diagnostics.Report(Severity::Warning, "W-DATA-NEWKEY",
					"'" + entry.key() + "' appeared after the model was built and cannot be bound; reload the document");
			}
			continue;
		}
		*it->second = entry.value();
	}

	// Names that disappeared keep their slot, blanked, so bindings resolve rather than break.
	for (auto& [name, slot] : slots)
	{
		if (source.find(name) == source.end())
			*slot = nullptr;
	}

	if (handle)
		handle.DirtyAllVariables();
	return true;
}

bool JsonDataModel::Reload()
{
	if (json_path.empty())
		return false;

	std::ifstream file(json_path);
	if (!file)
	{
		diagnostics.Report(Severity::Warning, "W-DATA-REREAD", "could not reopen " + json_path);
		return false;
	}

	ordered_json parsed;
	try
	{
		file >> parsed;
	}
	catch (const std::exception& e)
	{
		diagnostics.Report(Severity::Error, "E-DATA-PARSE", std::string("could not parse ") + json_path + ": " + e.what());
		return false;
	}

	return Assign(parsed, true);
}

void JsonDataModel::TransformStrings(const std::function<std::string(const std::string&)>& transform)
{
	// Data-bound text never passes through the document's data-loc pass, so HUD strings would
	// otherwise escape pseudo-localization entirely, which is where overflow actually bites.
	const std::function<void(ordered_json&)> visit = [&](ordered_json& node) {
		if (node.is_string())
			node = transform(node.get<std::string>());
		else if (node.is_object() || node.is_array())
		{
			for (auto& child : node)
				visit(child);
		}
	};

	for (auto& [name, slot] : slots)
		visit(*slot);

	if (handle)
		handle.DirtyAllVariables();
}

void JsonDataModel::SetPreviewInfo(const std::string& culture, int width, int height, float dp_ratio)
{
	const auto it = slots.find("preview");
	if (it == slots.end())
		return;

	ordered_json& preview = *it->second;
	if (!preview.is_object())
		preview = ordered_json::object();

	// Only fill what the file did not, so a mock can override any of it.
	if (!preview.contains("culture"))
		preview["culture"] = culture;
	if (!preview.contains("width"))
		preview["width"] = width;
	if (!preview.contains("height"))
		preview["height"] = height;
	if (!preview.contains("dp_ratio"))
		preview["dp_ratio"] = dp_ratio;
	if (!preview.contains("device"))
		preview["device"] = "keyboard";

	if (handle)
		handle.DirtyAllVariables();
}

} // namespace uipreview
