#pragma once

#include "FolioCore/FolioCoreAPI.h"
#include "FolioCore/FolioFontEngine.h"
#include "FolioCore/FolioHost.h"
#include "FolioCore/FolioTextMarkup.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyIdSet.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StyleSheetSpecification.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/XMLNodeHandler.h>
#include <RmlUi/Core/XMLParser.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

/**
 * `<text>`: styled, localisable prose that can be revealed one character at a time.
 *
 * Everything in this file is inline for the reason given in FolioBehaviours.h - RmlUi is a static
 * library linked into two modules, each with its own globals, and only one of them ran
 * Rml::Initialise. An element that resolved a property, dispatched an event or asked for the font
 * engine through the other module's copy would read a table that was never filled in.
 *
 * The markup a `<text>` holds is not RML. It is read as CDATA and parsed by Folio, so `<angry>` is
 * a style rather than an element and `{tag:Place.X}` is a term rather than a data binding. That is
 * what lets a translated string carry it: a translation is a string, and a string cannot introduce
 * elements into a document without being escaped first.
 */
namespace Folio::Text
{
	/// What `reveal` can be set to.
	enum class ERevealMode
	{
		None = 0,
		Typewriter = 1,
	};

	/// What `reveal-start` can be set to.
	enum class ERevealStart
	{
		Auto = 0,
		Manual = 1,
	};

	/// Called once per character as it is revealed, if `reveal-sound` names one.
	///
	/// A function pointer rather than a std::function so that installing one costs nothing and the
	/// module that owns the sound system is the module that owns the callback. The core has no
	/// audio and the preview tool has no sound to play, so both leave it null.
	using FSoundCallback = void (*)(Rml::Element* Element, const Rml::String& Sound, int CharIndex);

	/// Called as a `<text>` finishes revealing, whether it ran to the end or was skipped.
	///
	/// A callback as well as the `revealcomplete` event, because the host has no element to attach a
	/// listener to until a document is up and a document is replaced on every reload. Process-wide
	/// for the same reason: the element that finished does not know what is hosting it.
	using FCompleteCallback = void (*)(Rml::Element* Element);

	/// How fast text types, and whether it types at all.
	///
	/// Deliberately not part of reduced motion. Text speed is its own well-known setting and players
	/// look for it separately: tying the two together means someone who wants menus still but
	/// dialogue paced cannot have both, and someone who wants instant text has to give up every
	/// transition to get it. A typewriter is also small, local, predictable movement, which is the
	/// opposite of what triggers a vestibular response.
	struct FRevealSettings
	{
		/// Multiplies the whole schedule, so `reveal-rate`, `{delay:0.2}` and the punctuation beats
		/// all move together. Two is twice as fast; a writer's pacing is preserved either way.
		float Speed = 1.0f;

		/// Every line arrives whole. What the setting a player finds as "instant text" sets.
		bool bInstant = false;
	};

	/// Process-wide, and one copy per module by design: a `<text>` belongs to the RmlUi instance of
	/// whichever module built it, and so does the setting it reads.
	inline FRevealSettings& RevealSettings()
	{
		static FRevealSettings Instance;
		return Instance;
	}

	namespace Detail
	{
		struct FProperties
		{
			Rml::PropertyId Reveal{};
			Rml::PropertyId Rate{};
			Rml::PropertyId Fade{};
			Rml::PropertyId Rise{};
			Rml::PropertyId Punctuation{};
			Rml::PropertyId Sound{};
			Rml::PropertyId Start{};
			bool bRegistered = false;
		};

		inline FProperties& Properties()
		{
			static FProperties Instance;
			return Instance;
		}

		inline FSoundCallback& Sound()
		{
			static FSoundCallback Callback = nullptr;
			return Callback;
		}

		inline FCompleteCallback& Complete()
		{
			static FCompleteCallback Callback = nullptr;
			return Callback;
		}

		/// Smooth at both ends, so a character neither appears at full strength nor lingers at
		/// almost nothing. At the rates dialogue actually runs at this is a handful of frames.
		inline float Smooth(float T)
		{
			return T * T * (3.0f - 2.0f * T);
		}

		/// Fades and lifts each glyph according to when its character is due.
		class FRevealPainter final : public IGlyphPainter
		{
		public:
			FRevealPainter(const std::vector<float>& InTimes, float InNow, float InFade, float InRise)
				: Times(InTimes), Now(InNow), Fade(InFade), Rise(InRise)
			{
			}

			bool PaintGlyph(unsigned int CharIndex, float& OutAlpha, Rml::Vector2f& OutOffset) const override
			{
				// A glyph the shaper produced from characters past the end of what was harvested
				// cannot be scheduled, so it is treated as already shown rather than never shown.
				if (CharIndex >= Times.size())
				{
					return true;
				}

				const float Due = Times[CharIndex];
				if (Now < Due)
				{
					return false;
				}

				const float Progress = Fade > 0.0f ? std::min((Now - Due) / Fade, 1.0f) : 1.0f;
				const float Eased = Smooth(Progress);

				OutAlpha = Eased;
				OutOffset.y = Rise * (1.0f - Eased);
				return true;
			}

		private:
			const std::vector<float>& Times;
			float Now;
			float Fade;
			float Rise;
		};
	}

	/// One laid-out line of one run, with the schedule for the characters on it.
	struct FTextSegment
	{
		Rml::String Text;
		Rml::String Language;
		Rml::Vector2f Position;
		Rml::FontFaceHandle Face = 0;
		Rml::FontEffectsHandle Effects = 0;
		Rml::ColourbPremultiplied Colour;
		Rml::Style::Direction Direction = Rml::Style::Direction::Auto;
		Rml::Style::FontKerning Kerning = Rml::Style::FontKerning::Auto;
		float LetterSpacing = 0.0f;
		/// When each character of this line is due, in seconds from the start of the reveal.
		std::vector<float> Times;
	};

	class FElementRichText final : public Rml::Element
	{
	public:
		explicit FElementRichText(const Rml::String& Tag) : Rml::Element(Tag) {}

		/// Replaces the markup, which is what a translation and a line of dialogue both do. Resets
		/// the reveal, because the text it had got through is no longer the text there is.
		void SetMarkup(const Rml::String& InMarkup)
		{
			if (bMarkupKnown && Markup == InMarkup)
			{
				return;
			}

			Markup = InMarkup;
			bMarkupKnown = true;
			bRunsDirty = true;
		}

		const Rml::String& GetMarkup() const { return Markup; }

		/// The text with every tag resolved and removed. What a screen reader, a word count or a
		/// log line wants.
		Rml::String GetPlainText() const { return Parsed.Plain; }

		/// Starts, or starts again from the beginning.
		void Play()
		{
			if (bRunsDirty)
			{
				Build();
			}

			// A player who has turned typing off has turned it off everywhere, including on the line
			// that is about to start. Skipped rather than never started, so `revealcomplete` still
			// fires and whatever was waiting on it still runs.
			if (RevealSettings().bInstant)
			{
				bPlaying = true;
				bComplete = false;
				Skip();
				return;
			}

			Elapsed = 0.0f;
			StartTime = Now();
			LastSounded = -1;
			bPlaying = true;
			bComplete = false;
			HideRuns(true);
			SetRevealClasses();
			RequestUpdate();
		}

		/// Shows everything at once. What the player's skip button does, and what an unread line
		/// does when the conversation moves on.
		void Skip()
		{
			if (!bPlaying && bComplete)
			{
				return;
			}

			Elapsed = TotalTime;
			Finish();
		}

		bool IsRevealing() const { return bPlaying; }
		bool IsComplete() const { return bComplete; }

		/// Seconds from Play() to the last character being fully in. Zero where nothing is
		/// revealed, so a caller can treat it as "already done".
		float GetDuration() const { return TotalTime; }

		int GetCharacterCount() const { return Parsed.CharCount; }

		/// Anything the parse could not make sense of, already reported once to the log.
		const std::vector<std::string>& GetErrors() const { return Parsed.Errors; }

	protected:
		void OnUpdate() override
		{
			// Properties are computed after this call, so on the very first frame the stylesheet
			// has not been read yet and `reveal` would come back as its default. Building then
			// would show the whole line for one frame before it started typing, which is the one
			// artefact this element exists to avoid. One frame with nothing in it costs nothing.
			if (!bStyled)
			{
				return;
			}

			if (bRunsDirty)
			{
				Build();
			}

			if (GetRevealMode() != ERevealMode::Typewriter)
			{
				// Turned off after it was on, or never on. Either way the text is simply text.
				if (bHidden)
				{
					HideRuns(false);
				}
				bPlaying = false;
				SetRevealClasses();
				return;
			}

			if (!bPlaying && !bComplete && GetStartMode() == ERevealStart::Auto)
			{
				Play();
			}

			if (!bPlaying)
			{
				return;
			}

			Elapsed = float(Now() - StartTime);

			Sound();

			if (Elapsed >= TotalTime)
			{
				Finish();
				return;
			}

			// Nothing else in the document has changed, so without this the host skips the frame
			// and the text appears to stall. Asking for zero is asking for the next frame.
			RequestUpdate();
		}

		void OnRender() override
		{
			if (!bPlaying)
			{
				// Complete, or never revealing. The runs are visible and RmlUi draws them from
				// geometry it generated once, which is the whole reason to hand them back.
				return;
			}

			if (bSegmentsDirty)
			{
				Harvest();
			}

			FFolioFontEngine* Engine = FFolioFontEngine::Get();
			Rml::RenderManager* Manager = GetRenderManager();
			if (Engine == nullptr || Manager == nullptr)
			{
				return;
			}

			const float Fade = GetFade();
			const float Rise = GetRise();

			for (const FTextSegment& Segment : Segments)
			{
				if (Segment.Face == 0)
				{
					continue;
				}

				const Rml::TextShapingContext Shaping{
					Segment.Language, Segment.Direction, Segment.Kerning, Segment.LetterSpacing};

				const Detail::FRevealPainter Painter(Segment.Times, Elapsed, Fade, Rise);

				Rml::TexturedMeshList Meshes;
				Engine->GenerateStringPainted(*Manager, Segment.Face, Segment.Effects, Segment.Text,
					Segment.Position, Segment.Colour, Shaping, Painter, Meshes);

				for (Rml::TexturedMesh& Textured : Meshes)
				{
					if (Textured.mesh.vertices.empty())
					{
						continue;
					}

					// Positions are already absolute, so the translation is the one ElementText
					// would have applied and has here been folded in at generation instead.
					const Rml::Geometry Drawn = Manager->MakeGeometry(std::move(Textured.mesh));
					Drawn.Render(Rml::Vector2f(0.0f, 0.0f), Textured.texture);
				}
			}
		}

		void OnLayout() override
		{
			bSegmentsDirty = true;
		}

		/// Font effects written in dp are resolved against the ratio when segments are built.
		void OnDpRatioChange() override
		{
			bSegmentsDirty = true;
		}

		void OnPropertyChange(const Rml::PropertyIdSet& Changed) override
		{
			Rml::Element::OnPropertyChange(Changed);

			bStyled = true;

			const Detail::FProperties& Ids = Detail::Properties();
			if (Changed.Contains(Ids.Rate) || Changed.Contains(Ids.Fade)
				|| Changed.Contains(Ids.Punctuation) || Changed.Contains(Ids.Reveal))
			{
				RebuildSchedule();
				bSegmentsDirty = true;
			}
		}

		void OnChildAdd(Rml::Element* Child) override
		{
			Rml::Element::OnChildAdd(Child);

			// A child arriving while the runs are being built is one of ours.
			if (!bBuilding)
			{
				bSegmentsDirty = true;
			}
		}

	private:
		double Now() const
		{
			Rml::SystemInterface* System = Rml::GetSystemInterface();
			return System != nullptr ? System->GetElapsedTime() : 0.0;
		}

		void RequestUpdate()
		{
			if (Rml::Context* Ctx = GetContext())
			{
				Ctx->RequestNextUpdate(0.0);
			}
		}

		float GetNumber(Rml::PropertyId Id, float Fallback) const
		{
			const Rml::Property* Value = const_cast<FElementRichText*>(this)->GetProperty(Id);
			return Value != nullptr ? Value->Get<float>() : Fallback;
		}

		int GetKeyword(Rml::PropertyId Id, int Fallback) const
		{
			const Rml::Property* Value = const_cast<FElementRichText*>(this)->GetProperty(Id);
			return Value != nullptr ? Value->Get<int>() : Fallback;
		}

		ERevealMode GetRevealMode() const
		{
			return ERevealMode(GetKeyword(Detail::Properties().Reveal, int(ERevealMode::None)));
		}

		ERevealStart GetStartMode() const
		{
			return ERevealStart(GetKeyword(Detail::Properties().Start, int(ERevealStart::Auto)));
		}

		float GetFade() const { return std::max(GetNumber(Detail::Properties().Fade, 0.06f), 0.0f); }

		float GetRise()
		{
			const Rml::Property* Value = GetProperty(Detail::Properties().Rise);
			return Value != nullptr ? ResolveLength(Value->GetNumericValue()) : 0.0f;
		}

		Rml::String GetSoundName()
		{
			const Rml::Property* Value = GetProperty(Detail::Properties().Sound);
			return Value != nullptr ? Value->Get<Rml::String>() : Rml::String();
		}

		/// Parses the markup and turns its runs into elements, which is what gives the text its
		/// styling, its wrapping and its line breaks. Nothing here draws: the runs are laid out by
		/// RmlUi exactly as any other inline text would be, and only their drawing is taken over.
		void Build()
		{
			bRunsDirty = false;

			Parsed = TextMarkup::Parse(Markup);

			for (const std::string& Error : Parsed.Errors)
			{
				const Rml::String Where = GetId().empty()
					? GetAttribute<Rml::String>("data-loc", "<text>")
					: GetId();
				LogWarning(Format("folio text: %s: %s", Where.c_str(), Error.c_str()));
			}

			bBuilding = true;
			while (Rml::Element* Child = GetChild(0))
			{
				RemoveChild(Child);
			}

			for (const TextMarkup::FRun& Run : Parsed.Runs)
			{
				if (Run.bBreak)
				{
					AppendChild(Rml::Factory::InstanceElement(this, "br", "br", Rml::XMLAttributes()));
					continue;
				}

				Rml::ElementPtr Span = Rml::Factory::InstanceElement(this, "span", "span", Rml::XMLAttributes());
				if (!Span)
				{
					continue;
				}

				if (!Run.Classes.empty())
				{
					Span->SetClassNames(Run.Classes);
				}

				// Instanced rather than assigned as inner RML: the run's text is text, and giving
				// it to a parser again would read a stray '<' as an element and '{{' as a binding.
				Rml::ElementPtr TextNode = Rml::Factory::InstanceElement(Span.get(), "#text", "#text",
					Rml::XMLAttributes());
				if (auto* AsText = static_cast<Rml::ElementText*>(TextNode.get()))
				{
					AsText->SetText(Run.Text);
				}

				Span->AppendChild(std::move(TextNode));
				AppendChild(std::move(Span));
			}
			bBuilding = false;

			RebuildSchedule();

			bSegmentsDirty = true;
			bComplete = false;
			bPlaying = false;
			bHidden = false;

			if (GetRevealMode() == ERevealMode::Typewriter)
			{
				// Hidden from the first frame, so a line never flashes whole before it types.
				HideRuns(true);
			}
		}

		/// Turns the parsed delays into an absolute time per character. Separate from the parse
		/// because the rate and the pacing are RCSS, and a theme is allowed to change its mind
		/// about them without the text being read again.
		void RebuildSchedule()
		{
			const float Rate = std::max(GetNumber(Detail::Properties().Rate, 45.0f), 0.001f);
			const float Step = 1.0f / Rate;
			const float Speed = std::max(RevealSettings().Speed, 0.01f);
			const float Punctuation = std::max(GetNumber(Detail::Properties().Punctuation, 0.0f), 0.0f);

			CharTimes.assign(std::size_t(Parsed.CharCount), 0.0f);

			// The character before each one, where it is plain ASCII, which is all the pacing rule
			// needs to recognise.
			std::vector<char> Simple(std::size_t(Parsed.CharCount), 0);
			for (std::size_t Index = 0, Char = 0; Index < Parsed.Plain.size() && Char < Simple.size();)
			{
				const unsigned char Lead = (unsigned char)Parsed.Plain[Index];
				Simple[Char] = Lead < 0x80 ? char(Lead) : 0;
				++Char;
				Index += Lead < 0x80 ? 1 : ((Lead >> 5) == 0x6 ? 2 : ((Lead >> 4) == 0xE ? 3 : 4));
			}

			float Accumulated = 0.0f;
			for (int Index = 0; Index < Parsed.CharCount; ++Index)
			{
				float Wait = Parsed.Delays[std::size_t(Index)] >= 0.0f
					? Parsed.Delays[std::size_t(Index)]
					: Step;
				Wait += Parsed.Pauses[std::size_t(Index)];

				if (Index > 0 && Punctuation > 0.0f)
				{
					// A beat after a sentence and half of one after a clause, which is roughly how
					// a reader would say it and is why an unpaced typewriter reads flat.
					const char Previous = Simple[std::size_t(Index) - 1];
					if (Previous == '.' || Previous == '!' || Previous == '?')
					{
						Wait += Punctuation;
					}
					else if (Previous == ',' || Previous == ';' || Previous == ':')
					{
						Wait += Punctuation * 0.5f;
					}
				}

				// The player's speed divides the whole wait, authored delays and punctuation beats
				// included. Scaling only the per-character step would make a line with hand-written
				// pacing ignore the setting exactly where the pacing matters most.
				Accumulated += Wait / Speed;
				CharTimes[std::size_t(Index)] = Accumulated;
			}

			TotalTime = Parsed.CharCount > 0 ? Accumulated + GetFade() : 0.0f;
		}

		void HideRuns(bool bHide)
		{
			for (int Index = 0; Index < GetNumChildren(); ++Index)
			{
				Rml::Element* Child = GetChild(Index);
				if (bHide)
				{
					// Hidden rather than removed or cleared: layout is unchanged, so the text
					// wraps once and every character stays where it will finally sit. Anything
					// that revealed by changing the text would rewrap on almost every character.
					Child->SetProperty(Rml::PropertyId::Visibility, Rml::Property(Rml::Style::Visibility::Hidden));
				}
				else
				{
					Child->RemoveProperty(Rml::PropertyId::Visibility);
				}
			}

			bHidden = bHide;
		}

		void Finish()
		{
			bPlaying = false;
			bComplete = true;
			HideRuns(false);
			SetRevealClasses();

			// The event first, so a document that reacts to it has already done so by the time game
			// code hears about the same line.
			DispatchEvent("revealcomplete", Rml::Dictionary());

			if (const FCompleteCallback Callback = Detail::Complete())
			{
				Callback(this);
			}
		}

		/// So a document can react to the reveal without any code at all: a continue prompt that
		/// appears once the line has finished is a stylesheet rule rather than an event listener.
		void SetRevealClasses()
		{
			SetClass("is-revealing", bPlaying);
			SetClass("is-revealed", !bPlaying);
		}

		void Sound()
		{
			const FSoundCallback Callback = Detail::Sound();
			if (Callback == nullptr || CharTimes.empty())
			{
				return;
			}

			// The newest character only. At any rate fast enough to outrun the frame, one click a
			// frame is what a player hears anyway and a click per character is a buzz.
			int Reached = LastSounded;
			while (Reached + 1 < int(CharTimes.size()) && CharTimes[std::size_t(Reached + 1)] <= Elapsed)
			{
				++Reached;
			}

			if (Reached == LastSounded)
			{
				return;
			}

			LastSounded = Reached;

			const Rml::String Name = GetSoundName();
			if (!Name.empty() && Name != "none")
			{
				Callback(this, Name, Reached);
			}
		}

		/// Reads back the lines RmlUi laid out and works out which character each one starts at.
		///
		/// The line text is not the run text: whitespace is collapsed and a break eats the space it
		/// happened at. So the two are walked together rather than assumed to line up, which also
		/// means a run whose text was transformed degrades to drawing in order rather than to
		/// drawing nonsense.
		void Harvest()
		{
			bSegmentsDirty = false;
			Segments.clear();

			// One child per run, in order, because that is how Build made them. A run knows which
			// character it starts at, so nothing has to count across the whole element.
			const int Count = std::min(GetNumChildren(), int(Parsed.Runs.size()));
			for (int Index = 0; Index < Count; ++Index)
			{
				if (!Parsed.Runs[std::size_t(Index)].bBreak)
				{
					HarvestElement(GetChild(Index), Parsed.Runs[std::size_t(Index)].FirstChar);
				}
			}
		}

		void HarvestElement(Rml::Element* Element, int FirstChar)
		{
			if (Element == nullptr)
			{
				return;
			}

			if (Element->GetTagName() == "#text")
			{
				HarvestText(static_cast<Rml::ElementText*>(Element), FirstChar);
				return;
			}

			for (int Index = 0; Index < Element->GetNumChildren(); ++Index)
			{
				HarvestElement(Element->GetChild(Index), FirstChar);
			}
		}

		void HarvestText(Rml::ElementText* Text, int RunFirstChar)
		{
			const Rml::FontFaceHandle Face = Text->GetFontFaceHandle();
			if (Face == 0 || Text->GetLines().empty())
			{
				return;
			}

			const auto& Computed = Text->GetComputedValues();
			const Rml::Vector2f Origin = Text->GetAbsoluteOffset();

			const Rml::String& Source = Text->GetText();
			std::size_t SourceByte = 0;
			int SourceChar = 0;

			for (const Rml::ElementText::Line& Line : Text->GetLines())
			{
				FTextSegment& Segment = Segments.emplace_back();
				Segment.Text = Line.text;
				Segment.Position = Origin + Line.position;
				Segment.Face = Face;
				Segment.Effects = PrepareEffects(Text, Face);
				Segment.Colour = Computed.color().ToPremultiplied(Computed.opacity());
				Segment.Language = Computed.language();
				Segment.Direction = Computed.direction();
				Segment.Kerning = Computed.font_kerning();
				Segment.LetterSpacing = Computed.letter_spacing();

				for (std::size_t Byte = 0; Byte < Line.text.size();)
				{
					const unsigned char Lead = (unsigned char)Line.text[Byte];
					const std::size_t Length = Lead < 0x80 ? 1
						: ((Lead >> 5) == 0x6 ? 2 : ((Lead >> 4) == 0xE ? 3 : 4));

					// Walk the run's own text forward until it says the same thing. Only ever
					// skips whitespace the layout collapsed away; anything else and the two have
					// diverged, and stepping one for one is the honest fallback.
					std::size_t Guard = 0;
					while (SourceByte < Source.size()
						&& Source.compare(SourceByte, Length, Line.text, Byte, Length) != 0
						&& Guard < Source.size())
					{
						const unsigned char SourceLead = (unsigned char)Source[SourceByte];
						SourceByte += SourceLead < 0x80 ? 1
							: ((SourceLead >> 5) == 0x6 ? 2 : ((SourceLead >> 4) == 0xE ? 3 : 4));
						++SourceChar;
						++Guard;
					}

					const int Absolute = RunFirstChar + SourceChar;
					Segment.Times.push_back(Absolute >= 0 && Absolute < int(CharTimes.size())
						? CharTimes[std::size_t(Absolute)]
						: 0.0f);

					if (SourceByte < Source.size())
					{
						SourceByte += Length;
						++SourceChar;
					}

					Byte += Length;
				}
			}
		}

		Rml::FontEffectsHandle PrepareEffects(Rml::ElementText* Text, Rml::FontFaceHandle Face)
		{
			static const Rml::FontEffectList Empty;
			const Rml::FontEffectList* Effects = &Empty;

			if (Text->GetComputedValues().has_font_effect())
			{
				if (const Rml::Property* Value = Text->GetProperty(Rml::PropertyId::FontEffect))
				{
					if (Rml::FontEffectsPtr List = Value->Get<Rml::FontEffectsPtr>())
					{
						Effects = &List->list;
					}
				}
			}

			const Rml::Context* Context = Text->GetContext();
			return Rml::GetFontEngineInterface()->PrepareFontEffects(Face, *Effects, Context ? Context->GetDensityIndependentPixelRatio() : 1.0f);
		}

		Rml::String Markup;
		TextMarkup::FParsed Parsed;

		/// Absolute time, in seconds from the start of the reveal, at which each character begins
		/// to appear.
		std::vector<float> CharTimes;
		std::vector<FTextSegment> Segments;

		double StartTime = 0.0;
		float Elapsed = 0.0f;
		float TotalTime = 0.0f;
		int LastSounded = -1;

		bool bMarkupKnown = false;
		bool bStyled = false;
		bool bRunsDirty = true;
		bool bSegmentsDirty = true;
		bool bBuilding = false;
		bool bHidden = false;
		bool bPlaying = false;
		bool bComplete = false;
	};

	namespace Detail
	{
		/// Hands the element its markup untouched.
		///
		/// The default handler would send it through Factory::InstanceElementText, which treats a
		/// '<' as the start of an element and '{{' as a data binding, and so would consume the
		/// markup this element exists to read.
		class FTextNodeHandler final : public Rml::XMLNodeHandler
		{
		public:
			Rml::Element* ElementStart(Rml::XMLParser* Parser, const Rml::String& Name,
				const Rml::XMLAttributes& Attributes) override
			{
				Rml::Element* Parent = Parser->GetParseFrame()->element;
				Rml::ElementPtr Element = Rml::Factory::InstanceElement(Parent, Name, Name, Attributes);
				return Element ? Parent->AppendChild(std::move(Element)) : nullptr;
			}

			bool ElementEnd(Rml::XMLParser*, const Rml::String&) override { return true; }

			bool ElementData(Rml::XMLParser* Parser, const Rml::String& Data, Rml::XMLDataType) override
			{
				if (auto* Element = static_cast<FElementRichText*>(Parser->GetParseFrame()->element))
				{
					Element->SetMarkup(Data);
				}
				return true;
			}
		};

		inline std::vector<std::unique_ptr<Rml::ElementInstancer>>& OwnedInstancers()
		{
			static std::vector<std::unique_ptr<Rml::ElementInstancer>> Owned;
			return Owned;
		}
	}

	/// True for a `<text>`, and the only safe thing to check before casting one.
	inline bool IsRichText(const Rml::Element* Element)
	{
		return Element != nullptr && Element->GetTagName() == "text";
	}

	inline FElementRichText* AsRichText(Rml::Element* Element)
	{
		return IsRichText(Element) ? static_cast<FElementRichText*>(Element) : nullptr;
	}

	/// Installs the callback that turns a revealed character into a sound. Null removes it.
	inline void SetSoundCallback(FSoundCallback Callback)
	{
		Detail::Sound() = Callback;
	}

	/// Installs the callback that reports a finished line. Null removes it.
	inline void SetCompleteCallback(FCompleteCallback Callback)
	{
		Detail::Complete() = Callback;
	}

	/// Adds `<text>` and the `reveal-*` properties.
	///
	/// Call from the module that called Rml::Initialise, after it, exactly as with the decorators
	/// and font effects. Inline deliberately: the factory, the parser and the property
	/// specification these reach are the calling module's own.
	inline void RegisterFolioText()
	{
		Detail::FProperties& Ids = Detail::Properties();
		if (Ids.bRegistered)
		{
			return;
		}
		Ids.bRegistered = true;

		// Inherited, so a dialogue box can set the feel once and every line inside it types the
		// same way. None of them affect layout: a reveal changes what is drawn, never where.
		Ids.Reveal = Rml::StyleSheetSpecification::RegisterProperty("reveal", "none", true, false)
			.AddParser("keyword", "none, typewriter")
			.GetId();
		Ids.Rate = Rml::StyleSheetSpecification::RegisterProperty("reveal-rate", "45", true, false)
			.AddParser("number")
			.GetId();
		Ids.Fade = Rml::StyleSheetSpecification::RegisterProperty("reveal-fade", "0.06", true, false)
			.AddParser("number")
			.GetId();
		Ids.Rise = Rml::StyleSheetSpecification::RegisterProperty("reveal-rise", "0px", true, false)
			.AddParser("length")
			.GetId();
		Ids.Punctuation = Rml::StyleSheetSpecification::RegisterProperty("reveal-punctuation", "0", true, false)
			.AddParser("number")
			.GetId();
		Ids.Sound = Rml::StyleSheetSpecification::RegisterProperty("reveal-sound", "none", true, false)
			.AddParser("string")
			.GetId();
		Ids.Start = Rml::StyleSheetSpecification::RegisterProperty("reveal-start", "auto", true, false)
			.AddParser("keyword", "auto, manual")
			.GetId();

		auto Instancer = std::make_unique<Rml::ElementInstancerGeneric<FElementRichText>>();
		Rml::Factory::RegisterElementInstancer("text", Instancer.get());
		Detail::OwnedInstancers().push_back(std::move(Instancer));

		// Read raw, so `<angry>` reaches the element as four characters rather than as an element
		// RmlUi could not instance and `</>` as a close tag it would reject outright.
		Rml::XMLParser::RegisterPersistentCDATATag("text");
		Rml::XMLParser::RegisterNodeHandler("text", std::make_shared<Detail::FTextNodeHandler>());
	}

	/// Drops the instancer. RmlUi holds a raw pointer, so this must happen after shutdown.
	inline void ReleaseFolioText()
	{
		Detail::OwnedInstancers().clear();
		Detail::Properties().bRegistered = false;
	}
}
