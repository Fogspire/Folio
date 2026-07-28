#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataVariable.h>

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace Rml {
class Context;
}

namespace uipreview {

class Diagnostics;

/**
 * Mounts an arbitrary JSON file as a data model, with no compile-time structs.
 *
 * RmlUi re-walks a data address from its *bound* variable on every update, so each bound
 * name needs an address that stays valid for the life of the model. Binding pointers into
 * one root object and then reassigning that root would free every sub-object and leave the
 * bindings dangling, which shows up as the whole model going blank after a reload. Each
 * top-level key therefore owns a separate heap slot and a reload assigns through the slot.
 */
class JsonDataModel {
public:
	JsonDataModel(Diagnostics& diagnostics);
	/** Out of line: Definitions is incomplete here. */
	~JsonDataModel();

	/** Reads the file and creates the model. Must happen before the document is loaded. */
	bool Mount(Rml::Context* context, const std::string& model_name, const std::string& json_path);
	/** Mounts an empty model, so bindings resolve to blanks instead of warning per element. */
	bool MountEmpty(Rml::Context* context, const std::string& model_name);

	/** Re-reads the file into the existing slots. */
	bool Reload();

	/** Applies a transform to every string value, used by pseudo-localization. */
	void TransformStrings(const std::function<std::string(const std::string&)>& transform);

	/** Populates the reserved `preview` object describing the run itself. */
	void SetPreviewInfo(const std::string& culture, int width, int height, float dp_ratio);

	const std::string& Path() const { return json_path; }
	bool IsMounted() const { return mounted; }

private:
	bool Assign(const nlohmann::ordered_json& source, bool warn_about_new_keys);

	Diagnostics& diagnostics;
	std::string json_path;
	bool mounted = false;

	/** Addresses must stay put, so every bound value lives in its own allocation. */
	std::map<std::string, std::unique_ptr<nlohmann::ordered_json>> slots;
	Rml::DataModelHandle handle;

	class Definitions;
	std::unique_ptr<Definitions> definitions;
};

} // namespace uipreview
