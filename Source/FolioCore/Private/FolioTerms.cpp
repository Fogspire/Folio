#include "FolioCore/FolioTerms.h"

#include "FolioCore/FolioHost.h"

#include <algorithm>
#include <cctype>

namespace Folio
{
	namespace
	{
		std::string Trim(const std::string& Value)
		{
			std::size_t Begin = 0;
			std::size_t End = Value.size();
			while (Begin < End && std::isspace((unsigned char)Value[Begin]) != 0)
			{
				++Begin;
			}
			while (End > Begin && std::isspace((unsigned char)Value[End - 1]) != 0)
			{
				--End;
			}
			return Value.substr(Begin, End - Begin);
		}

		/// Letters, digits, dots, dashes and underscores, plus one colon for a form. What a
		/// gameplay tag looks like, and nothing that would need quoting anywhere else.
		bool IsValidTag(const std::string& Tag)
		{
			if (Tag.empty())
			{
				return false;
			}

			int Colons = 0;
			for (const char Character : Tag)
			{
				if (Character == ':')
				{
					++Colons;
					continue;
				}

				const bool bAllowed = std::isalnum((unsigned char)Character) != 0
					|| Character == '.' || Character == '_' || Character == '-';
				if (!bAllowed)
				{
					return false;
				}
			}

			return Colons <= 1 && Tag.front() != '.' && Tag.back() != '.' && Tag.back() != ':';
		}

		FTermTables& Tables()
		{
			static FTermTables Instance;
			return Instance;
		}
	}

	FTermTables& FTermTables::Get()
	{
		return Tables();
	}

	bool FTermTables::LoadFile(const std::string& Path)
	{
		IFolioHost* Host = GetHost();
		std::vector<unsigned char> Data;
		if (Host == nullptr || !Host->ReadFile(Path, Data))
		{
			LogError(Format("folio terms: cannot read '%s'", Path.c_str()));
			return false;
		}

		if (std::find(Files.begin(), Files.end(), Path) == Files.end())
		{
			Files.push_back(Path);
		}

		const std::string Source(Data.begin(), Data.end());
		std::string TableClass;
		bool bExternal = false;
		bool bClean = true;
		int LineNumber = 0;

		for (std::size_t Begin = 0; Begin <= Source.size();)
		{
			std::size_t End = Source.find('\n', Begin);
			if (End == std::string::npos)
			{
				End = Source.size();
			}

			const std::string Line = Trim(Source.substr(Begin, End - Begin));
			Begin = End + 1;
			++LineNumber;

			if (Line.empty() || Line[0] == '#')
			{
				continue;
			}

			if (Line[0] == '@')
			{
				const std::size_t Space = Line.find_first_of(" \t");
				const std::string Directive = Line.substr(1, Space == std::string::npos ? std::string::npos : Space - 1);
				const std::string Value = Space == std::string::npos ? std::string() : Trim(Line.substr(Space + 1));

				if (Directive == "class")
				{
					TableClass = Value;
				}
				else if (Directive == "external")
				{
					bExternal = true;
				}
				else
				{
					LogWarning(Format("folio terms: %s(%d): unknown directive '@%s'",
						Path.c_str(), LineNumber, Directive.c_str()));
					bClean = false;
				}
				continue;
			}

			const std::size_t Equals = Line.find('=');
			if (Equals == std::string::npos)
			{
				LogError(Format("folio terms: %s(%d): expected 'Tag = Text'", Path.c_str(), LineNumber));
				bClean = false;
				continue;
			}

			const std::string Tag = Trim(Line.substr(0, Equals));
			const std::string Text = Trim(Line.substr(Equals + 1));

			if (!IsValidTag(Tag))
			{
				LogError(Format("folio terms: %s(%d): '%s' is not a usable tag",
					Path.c_str(), LineNumber, Tag.c_str()));
				bClean = false;
				continue;
			}

			if (Text.empty())
			{
				LogError(Format("folio terms: %s(%d): '%s' has no text", Path.c_str(), LineNumber, Tag.c_str()));
				bClean = false;
				continue;
			}

			// A term meaning two things is the failure this whole mechanism exists to prevent, so
			// it is reported rather than resolved by whichever file happened to load last.
			if (const auto Found = Entries.find(Tag); Found != Entries.end())
			{
				LogError(Format("folio terms: %s(%d): '%s' is already defined in %s",
					Path.c_str(), LineNumber, Tag.c_str(), Found->second.File.c_str()));
				bClean = false;
				continue;
			}

			FTermEntry& Entry = Entries[Tag];
			Entry.Text = Text;
			Entry.Class = TableClass;
			Entry.File = Path;
			Entry.bExternal = bExternal;
		}

		return bClean;
	}

	void FTermTables::Clear()
	{
		Entries.clear();
		Files.clear();
	}

	const FTermEntry* FTermTables::Find(const std::string& Tag) const
	{
		if (const auto Found = Entries.find(Tag); Found != Entries.end())
		{
			return &Found->second;
		}

		// A form the table does not carry falls back to the base name. The alternative is a hole
		// in a sentence, and a translator who asked for a case that was never written is better
		// served by the wrong inflection than by nothing.
		const std::size_t Colon = Tag.find(':');
		if (Colon != std::string::npos)
		{
			if (const auto Base = Entries.find(Tag.substr(0, Colon)); Base != Entries.end())
			{
				return &Base->second;
			}
		}

		return nullptr;
	}

	bool FTermTables::Contains(const std::string& Tag) const
	{
		const std::size_t Colon = Tag.find(':');
		return Entries.find(Colon == std::string::npos ? Tag : Tag.substr(0, Colon)) != Entries.end();
	}

	bool FTermTables::SetText(const std::string& Tag, const std::string& Text)
	{
		const auto Found = Entries.find(Tag);
		if (Found == Entries.end())
		{
			return false;
		}

		Found->second.Text = Text;
		return true;
	}

	void FTermTables::ForEach(const std::function<void(const std::string&, FTermEntry&)>& Visitor)
	{
		for (auto& Entry : Entries)
		{
			Visitor(Entry.first, Entry.second);
		}
	}

	std::string FTermTables::GetReport() const
	{
		return Format("folio terms: %d entries from %d file(s)", int(Entries.size()), int(Files.size()));
	}
}
