#pragma once

#include "FolioCore/FolioCoreAPI.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Folio
{
	/// One name the game uses in more than one place: a location, a character, an item, a quest.
	struct FTermEntry
	{
		/// What is drawn. The host replaces this with a translation where it has one, so the text
		/// in the file is the source string rather than the final one.
		std::string Text;
		/// Copied from the table the entry came from, so every place name styles alike without
		/// anything being said at each use.
		std::string Class;
		/// The file it was read from, so a duplicate can name both sides.
		std::string File;

		/**
		 * Whether this name is translated somewhere else, and the text here only stands in.
		 *
		 * A game that already holds its names - a data asset, a table of FText, anything with its
		 * own localisation - mirrors them into a `.terms` file so the preview can resolve them, and
		 * marks the file `@external`. Nothing gathers those strings, because they are already
		 * gathered where they really live, and the host replaces the text as it loads.
		 */
		bool bExternal = false;
	};

	/// Every term table the host has loaded, keyed by tag.
	///
	/// A term exists so that a name is written once and every screen, every translation and every
	/// line of dialogue agrees about it. Markup reaches one with `{tag:Place.Mountain.Temple}`,
	/// which is substituted for the entry's text and styled with the table's class.
	///
	/// Tables are files rather than engine data because the preview tool has no engine, and a term
	/// that only resolves inside the game would make every previewed screen a lie.
	class FOLIOCORE_API FTermTables
	{
	public:
		static FTermTables& Get();

		/// Reads and parses one `.terms` file through the host. False on a file that cannot be
		/// read or does not parse; either way every entry it did contain is kept, because half a
		/// table renders better than none while the file is being fixed.
		bool LoadFile(const std::string& Path);

		void Clear();

		/// Tag is either `Place.X` or `Place.X:form`. A form that does not exist falls back to the
		/// base entry, so a translator asking for a case the table does not carry gets the plain
		/// name rather than nothing.
		const FTermEntry* Find(const std::string& Tag) const;

		/// Whether the base entry exists, ignoring forms. What a validity check wants.
		bool Contains(const std::string& Tag) const;

		/// Every entry, mutable, for a host that resolves the text through its own translation
		/// table after loading.
		void ForEach(const std::function<void(const std::string& Key, FTermEntry& Entry)>& Visitor);

		/// Replaces one entry's text, for a host supplying an `@external` name from wherever it
		/// really lives. False if there is no such entry, which is worth knowing: it means the
		/// mirror and the table it mirrors have drifted apart.
		bool SetText(const std::string& Tag, const std::string& Text);

		std::size_t GetCount() const { return Entries.size(); }
		const std::vector<std::string>& GetFiles() const { return Files; }

		/// One line naming the tables and what they hold, for the console command and the preview
		/// tool's report.
		std::string GetReport() const;

	private:
		/// Ordered, so a report and a gather both come out in the same order on every machine.
		std::map<std::string, FTermEntry> Entries;
		std::vector<std::string> Files;
	};
}
