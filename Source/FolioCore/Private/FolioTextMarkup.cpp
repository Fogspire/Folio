#include "FolioCore/FolioTextMarkup.h"

#include "FolioCore/FolioHost.h"
#include "FolioCore/FolioTerms.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace Folio::TextMarkup
{
	namespace
	{
		int SequenceLength(unsigned char Lead)
		{
			if (Lead < 0x80)
			{
				return 1;
			}
			if ((Lead >> 5) == 0x6)
			{
				return 2;
			}
			if ((Lead >> 4) == 0xE)
			{
				return 3;
			}
			if ((Lead >> 3) == 0x1E)
			{
				return 4;
			}

			// A stray continuation byte. Consuming one keeps the parse moving; the text is already
			// malformed and stopping would lose the rest of the line as well.
			return 1;
		}

		bool IsNameCharacter(char Character)
		{
			return std::isalnum((unsigned char)Character) != 0 || Character == '-' || Character == '_';
		}

		bool IsName(const std::string& Value)
		{
			if (Value.empty() || std::isalpha((unsigned char)Value.front()) == 0)
			{
				return false;
			}
			return std::all_of(Value.begin(), Value.end(), IsNameCharacter);
		}

		void AddUnique(std::vector<std::string>& List, const std::string& Value)
		{
			if (std::find(List.begin(), List.end(), Value) == List.end())
			{
				List.push_back(Value);
			}
		}

		class FParser
		{
		public:
			explicit FParser(const std::string& InSource) : Source(InSource) {}

			FParsed Run();

		private:
			void ParseStyle();
			void ParseDirective();

			/// Appends one character's worth of bytes, giving it whatever delay and pause are in
			/// force. Every array the reveal reads is written here and nowhere else, so a
			/// character can never end up without a schedule entry.
			void Append(const char* Bytes, std::size_t Count);
			void AppendText(const std::string& Text);

			/// Collapsed whitespace is held rather than written, which trims the ends of the line
			/// for free and lets a document indent its markup. Emitted into the run that was open
			/// when it arrived, so a space between two styles is not styled by the second.
			/// bStyleChanging is set when the next character opens a new run, so the space would
			/// otherwise be a run of its own.
			void FlushSpace(bool bStyleChanging = false);
			void SetClasses();
			void OpenRun();
			void CloseRun();
			void Break();

			std::string CurrentClasses() const;
			float CurrentDelay() const;

			void Error(const std::string& Message) { Out.Errors.push_back(Message); }

			const std::string& Source;
			std::size_t Position = 0;

			FParsed Out;
			std::vector<std::string> Styles;
			std::vector<float> Delays;

			FRun Open;
			std::string OpenClasses;
			bool bRunOpen = false;
			bool bSpacePending = false;
			float PausePending = 0.0f;
		};

		std::string FParser::CurrentClasses() const
		{
			std::string Classes;
			for (const std::string& Style : Styles)
			{
				if (!Classes.empty())
				{
					Classes.push_back(' ');
				}
				Classes.append(Style);
			}
			return Classes;
		}

		float FParser::CurrentDelay() const
		{
			return Delays.empty() ? -1.0f : Delays.back();
		}

		void FParser::OpenRun()
		{
			if (bRunOpen)
			{
				return;
			}

			Open = FRun();
			Open.Classes = OpenClasses;
			Open.FirstChar = Out.CharCount;
			bRunOpen = true;
		}

		void FParser::CloseRun()
		{
			if (bRunOpen && Open.CharCount > 0)
			{
				Out.Runs.push_back(Open);
			}
			bRunOpen = false;
		}

		void FParser::SetClasses()
		{
			const std::string Classes = CurrentClasses();
			if (Classes == OpenClasses)
			{
				return;
			}

			// Before the run closes, so a space that arrived under the old styles stays with them.
			FlushSpace(true);
			CloseRun();
			OpenClasses = Classes;
		}

		void FParser::FlushSpace(const bool bStyleChanging)
		{
			if (!bSpacePending)
			{
				return;
			}

			bSpacePending = false;
			if (Out.CharCount == 0)
			{
				return;
			}

			// A run that is only a space lays out as nothing, so it goes on the end of the run before.
			if (bStyleChanging && !bRunOpen && !Out.Runs.empty() && !Out.Runs.back().bBreak)
			{
				Open = Out.Runs.back();
				Out.Runs.pop_back();
				bRunOpen = true;
				Append(" ", 1);
				CloseRun();
				return;
			}

			Append(" ", 1);
		}

		void FParser::Append(const char* Bytes, std::size_t Count)
		{
			OpenRun();

			Open.Text.append(Bytes, Count);
			Open.CharCount += 1;

			Out.Plain.append(Bytes, Count);
			Out.Delays.push_back(CurrentDelay());
			Out.Pauses.push_back(PausePending);
			Out.CharCount += 1;

			PausePending = 0.0f;
		}

		void FParser::AppendText(const std::string& Text)
		{
			for (std::size_t Index = 0; Index < Text.size();)
			{
				const int Length = SequenceLength((unsigned char)Text[Index]);
				const std::size_t Clamped = std::min(std::size_t(Length), Text.size() - Index);

				// Whitespace inside substituted text collapses the same way as whitespace the
				// author typed, so a term with a line break in the table cannot break a line.
				if (Length == 1 && std::isspace((unsigned char)Text[Index]) != 0)
				{
					bSpacePending = true;
				}
				else
				{
					FlushSpace();
					Append(Text.data() + Index, Clamped);
				}

				Index += Clamped;
			}
		}

		void FParser::Break()
		{
			// The space either side of a break would otherwise open the next line with one.
			bSpacePending = false;
			CloseRun();

			FRun& Line = Out.Runs.emplace_back();
			Line.bBreak = true;
			Line.Classes = OpenClasses;
			Line.FirstChar = Out.CharCount;
		}

		void FParser::ParseStyle()
		{
			const std::size_t Close = Source.find('>', Position);
			if (Close == std::string::npos)
			{
				Error("'<' with no closing '>'. Write \\< for a literal one");
				FlushSpace();
				Append("<", 1);
				Position += 1;
				return;
			}

			const std::string Inner = Source.substr(Position + 1, Close - Position - 1);
			Position = Close + 1;

			if (Inner == "/" || (!Inner.empty() && Inner[0] == '/'))
			{
				const std::string Named = Inner.substr(1);
				if (Styles.empty())
				{
					Error("'</" + Named + ">' closes a style that was never opened");
					return;
				}

				if (!Named.empty() && Named != Styles.back())
				{
					Error("'</" + Named + ">' closes '" + Styles.back() + "', which is not what is open");
				}

				Styles.pop_back();
				SetClasses();
				return;
			}

			if (!IsName(Inner))
			{
				Error("'<" + Inner + ">' is not a style name");
				return;
			}

			Styles.push_back(Inner);
			AddUnique(Out.Styles, Inner);
			SetClasses();
		}

		void FParser::ParseDirective()
		{
			const std::size_t Close = Source.find('}', Position);
			if (Close == std::string::npos)
			{
				Error("'{' with no closing '}'. Write \\{ for a literal one");
				FlushSpace();
				Append("{", 1);
				Position += 1;
				return;
			}

			const std::string Inner = Source.substr(Position + 1, Close - Position - 1);
			const std::string Literal = Source.substr(Position, Close - Position + 1);
			Position = Close + 1;

			if (Inner == "/")
			{
				if (Delays.empty())
				{
					Error("'{/}' closes a directive that was never opened");
					return;
				}
				Delays.pop_back();
				return;
			}

			const std::size_t Colon = Inner.find(':');
			const std::string Name = Inner.substr(0, Colon);
			const std::string Value = Colon == std::string::npos ? std::string() : Inner.substr(Colon + 1);

			if (Name == "br")
			{
				Break();
				return;
			}

			if (Value.empty())
			{
				Error("'" + Literal + "' needs a value, as {" + Name + ":...}");
				return;
			}

			if (Name == "tag")
			{
				AddUnique(Out.Terms, Value);

				const FTermEntry* Entry = FTermTables::Get().Find(Value);
				if (Entry == nullptr)
				{
					// Left in the text rather than dropped: a missing term is a content bug, and
					// one that renders as nothing is one nobody sees until a translator asks.
					Error("no term '" + Value + "' in any loaded table");
					AppendText(Literal);
					return;
				}

				const std::string Restore = OpenClasses;
				if (!Entry->Class.empty())
				{
					FlushSpace(true);
					CloseRun();
					OpenClasses = OpenClasses.empty() ? Entry->Class : OpenClasses + " " + Entry->Class;
				}

				AppendText(Entry->Text);

				if (OpenClasses != Restore)
				{
					FlushSpace(true);
					CloseRun();
					OpenClasses = Restore;
				}
				return;
			}

			char* End = nullptr;
			const float Seconds = std::strtof(Value.c_str(), &End);
			if (End == Value.c_str() || Seconds < 0.0f)
			{
				Error("'" + Literal + "' wants a number of seconds");
				return;
			}

			if (Name == "delay")
			{
				Delays.push_back(Seconds);
				return;
			}

			if (Name == "pause")
			{
				PausePending += Seconds;
				return;
			}

			Error("'" + Literal + "' is not a directive. There are {delay:}, {pause:}, {br} and {tag:}");
			AppendText(Literal);
		}

		FParsed FParser::Run()
		{
			while (Position < Source.size())
			{
				const char Character = Source[Position];

				if (Character == '\\' && Position + 1 < Source.size())
				{
					const char Escaped = Source[Position + 1];
					if (Escaped == '<' || Escaped == '>' || Escaped == '{' || Escaped == '}' || Escaped == '\\')
					{
						FlushSpace();
						Append(&Escaped, 1);
						Position += 2;
						continue;
					}
				}

				if (Character == '<')
				{
					ParseStyle();
					continue;
				}

				if (Character == '{')
				{
					ParseDirective();
					continue;
				}

				const int Length = SequenceLength((unsigned char)Character);
				if (Length == 1 && std::isspace((unsigned char)Character) != 0)
				{
					bSpacePending = true;
					Position += 1;
					continue;
				}

				const std::size_t Clamped = std::min(std::size_t(Length), Source.size() - Position);
				FlushSpace();
				Append(Source.data() + Position, Clamped);
				Position += Clamped;
			}

			for (const std::string& Style : Styles)
			{
				Error("'<" + Style + ">' was never closed. Close it with </>");
			}
			if (!Delays.empty())
			{
				Error("a '{delay:}' was never closed. Close it with {/}");
			}

			CloseRun();
			return std::move(Out);
		}
	}

	FParsed Parse(const std::string& Markup)
	{
		FParser Parser(Markup);
		return Parser.Run();
	}

	std::string StripMarkup(const std::string& Markup)
	{
		return Parse(Markup).Plain;
	}
}
