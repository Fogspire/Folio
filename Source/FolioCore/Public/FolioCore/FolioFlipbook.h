#pragma once

#include "FolioCore/FolioCoreAPI.h"
#include "FolioCore/FolioTrace.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Dictionary.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Math.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/RenderBox.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/Spritesheet.h>
#include <RmlUi/Core/StyleSheet.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Texture.h>
#include <RmlUi/Core/URL.h>

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace Folio
{
	enum class EFlipbookMode
	{
		Loop,
		Once,
		Alternate,
	};

	enum class EFlipbookFit
	{
		Fill,
		Contain,
		Cover,
		None,
	};

	namespace Detail
	{
		/// Added before flooring, so a clock that lands exactly on a frame boundary resolves the same way every
		/// time rather than by float rounding.
		inline constexpr double FlipbookEpsilon = 1e-6;

		/// Which frame shows `Position` seconds in, for `Count` frames at `Fps`.
		inline int FlipbookFrameAt(const double Position, const int Count, const float Fps, const EFlipbookMode Mode)
		{
			if (Count <= 1 || Fps <= 0.0f)
			{
				return 0;
			}

			const long long Step = static_cast<long long>(std::floor(Rml::Math::Max(Position, 0.0) * double(Fps) + FlipbookEpsilon));
			switch (Mode)
			{
			case EFlipbookMode::Once:
				return int(Step < Count ? Step : Count - 1);
			case EFlipbookMode::Alternate:
			{
				// Out and back without showing either end twice: 0 1 2 3 2 1 0 1 ...
				const long long Period = 2LL * (Count - 1);
				const long long Phase = Step % Period;
				return int(Phase < Count ? Phase : Period - Phase);
			}
			case EFlipbookMode::Loop:
			default:
				return int(Step % Count);
			}
		}

		/// Seconds from `Position` until the frame changes or a `once` ends, or infinity once it has.
		inline double FlipbookUntilNextFrame(const double Position, const int Count, const float Fps, const EFlipbookMode Mode)
		{
			if (Count <= 1 || Fps <= 0.0f)
			{
				return std::numeric_limits<double>::infinity();
			}

			const double Clamped = Rml::Math::Max(Position, 0.0);
			if (Mode == EFlipbookMode::Once && Clamped * double(Fps) + FlipbookEpsilon >= double(Count))
			{
				return std::numeric_limits<double>::infinity();
			}
			// For a `once` on its last frame this is the end, so the update that raises `flipbookend` is asked for.
			const double Next = (std::floor(Clamped * double(Fps) + FlipbookEpsilon) + 1.0) / double(Fps);
			return Rml::Math::Max(Next - Clamped, 0.0);
		}

		inline bool ParseFlipbookFlag(const Rml::String& Value)
		{
			return Value != "false" && Value != "0";
		}

		inline std::vector<std::unique_ptr<Rml::ElementInstancer>>& FlipbookInstancers()
		{
			static std::vector<std::unique_ptr<Rml::ElementInstancer>> Instancers;
			return Instancers;
		}
	}

	/// `<flipbook>`: an image that plays its frames in sequence, from a grid sheet or from numbered sprites.
	///
	/// Frames, one of:
	///  - `src` (a file) or `sprite` (a region of an @spritesheet), cut into a grid by `columns` and `rows`, read left
	///    to right then top to bottom. `frames` stops short of a grid's last row.
	///  - `sprites`: a prefix, read as `<prefix>0`, `<prefix>1` ... until a number is missing.
	///
	/// Playback, all bindable with `data-attr-*`: `fps` (default 12), `mode` (`loop`, `once`, `alternate`),
	/// `paused`, `frame` (jumps there, then holds if paused or plays on if not), `fit` (`fill`, `contain`, `cover`,
	/// `none`) and `still` (the frame shown under reduced motion, default 0).
	///
	/// Sized like an `<img>`: one frame at its natural size unless the stylesheet says otherwise. Tinted by
	/// `image-color`. A `once` raises `flipbookend` when it reaches its last frame.
	class FElementFlipbook : public Rml::Element
	{
	public:
		explicit FElementFlipbook(const Rml::String& Tag)
			: Rml::Element(Tag)
		{
		}

		int GetFrameCount() const { return int(Frames.size()); }

		/// The frame on screen as of the last update.
		int GetFrame() const { return ShownFrame; }

		bool IsPaused() const { return bPaused; }

		void Play()
		{
			if (bPaused)
			{
				bPaused = false;
				Origin = Now() - Position;
				RequestUpdate();
			}
		}

		void Pause()
		{
			if (!bPaused)
			{
				Position = CurrentPosition();
				bPaused = true;
				RequestUpdate();
			}
		}

		/// Jumps to a frame. Keeps playing from it unless paused.
		void Seek(const int Frame)
		{
			// The middle of the frame, so float error at the boundary cannot show its neighbour.
			Position = Fps > 0.0f ? (double(Rml::Math::Max(Frame, 0)) + 0.5) / double(Fps) : 0.0;
			Origin = Now() - Position;
			bEnded = false;
			RequestUpdate();
		}

	protected:
		void OnAttributeChange(const Rml::ElementAttributes& Changed) override
		{
			FOLIO_TRACE_SCOPE("Folio/Flipbook/AttributeChange");

			Rml::Element::OnAttributeChange(Changed);

			const auto Has = [&Changed](const char* Name) { return Changed.find(Name) != Changed.end(); };

			if (Has("src") || Has("sprite") || Has("sprites") || Has("columns") || Has("rows") || Has("frames"))
			{
				bSheetDirty = true;
				bEnded = false;
				DirtyLayout();
			}
			if (Has("fit"))
			{
				const Rml::String Fit = GetAttribute<Rml::String>("fit", "fill");
				FitMode = Fit == "contain" ? EFlipbookFit::Contain
					: Fit == "cover" ? EFlipbookFit::Cover
					: Fit == "none" ? EFlipbookFit::None
					: EFlipbookFit::Fill;
				bGeometryDirty = true;
			}
			if (Has("mode"))
			{
				const Rml::String Text = GetAttribute<Rml::String>("mode", "loop");
				Mode = Text == "once" ? EFlipbookMode::Once
					: Text == "alternate" ? EFlipbookMode::Alternate
					: EFlipbookMode::Loop;
				bEnded = false;
			}
			if (Has("fps"))
			{
				// The same frame stays on screen, so changing the rate mid-play does not jump.
				const float NewFps = Rml::Math::Max(GetAttribute<float>("fps", 12.0f), 0.0f);
				const double Held = CurrentPosition();
				Position = Fps > 0.0f && NewFps > 0.0f ? Held * double(Fps) / double(NewFps) : 0.0;
				Origin = Now() - Position;
				Fps = NewFps;
			}
			if (Has("still"))
			{
				StillFrame = Rml::Math::Max(GetAttribute<int>("still", 0), 0);
			}
			if (Has("paused"))
			{
				const bool bWantPaused = HasAttribute("paused") && Detail::ParseFlipbookFlag(GetAttribute<Rml::String>("paused", ""));
				bWantPaused ? Pause() : Play();
			}
			// After `paused`, so a frame bound alongside it lands on the state it was meant for.
			if (Has("frame") && HasAttribute("frame"))
			{
				Seek(GetAttribute<int>("frame", 0));
			}

			RequestUpdate();
		}

		void OnPropertyChange(const Rml::PropertyIdSet& Changed) override
		{
			Rml::Element::OnPropertyChange(Changed);

			if (Changed.Contains(Rml::PropertyId::ImageColor) || Changed.Contains(Rml::PropertyId::Opacity))
			{
				bGeometryDirty = true;
			}
		}

		void OnResize() override
		{
			bGeometryDirty = true;
		}

		void OnDpRatioChange() override
		{
			bSheetDirty = true;
			DirtyLayout();
		}

		bool GetIntrinsicDimensions(Rml::Vector2f& Dimensions, float& Ratio) override
		{
			EnsureSheet();

			if (Frames.empty())
			{
				Dimensions = Rml::Vector2f(0.0f);
				return true;
			}

			const Rml::Vector2f Size = Frames[0].Size();
			if (Size.y > 0.0f)
			{
				Ratio = Size.x / Size.y;
			}
			Dimensions = Size * DimensionsScale;
			return true;
		}

		void OnUpdate() override
		{
			const int Count = int(Frames.size());
			if (Count == 0)
			{
				return;
			}

			if (!bStarted)
			{
				bStarted = true;
				Origin = Now() - Position;
			}

			const bool bStill = IsMotionReduced();
			const double At = CurrentPosition();

			// Reduced motion holds a frame: the one the game bound, or the still.
			const int Frame = bStill ? (HasAttribute("frame") ? GetAttribute<int>("frame", 0) : StillFrame)
				: Detail::FlipbookFrameAt(At, Count, Fps, Mode);
			ShownFrame = Rml::Math::Clamp(Frame, 0, Count - 1);

			if (Mode == EFlipbookMode::Once && !bEnded && !bPaused && !bStill && Fps > 0.0f
				&& At * double(Fps) + Detail::FlipbookEpsilon >= double(Count))
			{
				bEnded = true;
				DispatchEvent("flipbookend", Rml::Dictionary());
			}

			if (!bPaused && !bStill)
			{
				if (Rml::Context* Ctx = GetContext())
				{
					const double Delay = Detail::FlipbookUntilNextFrame(At, Count, Fps, Mode);
					if (std::isfinite(Delay))
					{
						Ctx->RequestNextUpdate(Delay);
					}
				}
			}
		}

		void OnRender() override
		{
			Rml::RenderManager* Manager = GetRenderManager();
			if (Manager == nullptr)
			{
				return;
			}

			EnsureSheet();
			if (bGeometryDirty)
			{
				BuildGeometry(*Manager);
			}

			if (ShownFrame >= 0 && ShownFrame < int(Drawn.size()) && Drawn[ShownFrame])
			{
				Drawn[ShownFrame].Render(GetAbsoluteOffset(Rml::BoxArea::Border), Texture);
			}
		}

	private:
		/// Each frame's rectangle in the texture, in pixels.
		std::vector<Rml::Rectanglef> Frames;
		/// One quad per frame, so playing costs a pick rather than a rebuild.
		std::vector<Rml::Geometry> Drawn;
		Rml::Texture Texture;
		float DimensionsScale = 1.0f;

		float Fps = 12.0f;
		EFlipbookMode Mode = EFlipbookMode::Loop;
		EFlipbookFit FitMode = EFlipbookFit::Fill;
		int StillFrame = 0;

		/// Clock reading at which the first frame began. Meaningful while playing.
		double Origin = 0.0;
		/// Seconds into the sequence. Meaningful while paused or before the first update.
		double Position = 0.0;
		bool bPaused = false;
		bool bStarted = false;
		bool bEnded = false;
		int ShownFrame = 0;

		bool bSheetDirty = true;
		bool bGeometryDirty = true;

		static double Now()
		{
			Rml::SystemInterface* System = Rml::GetSystemInterface();
			return System != nullptr ? System->GetElapsedTime() : 0.0;
		}

		double CurrentPosition() const
		{
			return bPaused || !bStarted ? Position : Now() - Origin;
		}

		bool IsMotionReduced()
		{
			const Rml::Context* Ctx = GetContext();
			return Ctx != nullptr && Ctx->IsThemeActive("reduced-motion");
		}

		void RequestUpdate()
		{
			if (Rml::Context* Ctx = GetContext())
			{
				Ctx->RequestNextUpdate(0.0);
			}
		}

		const Rml::StyleSheet* FindStyleSheet()
		{
			Rml::ElementDocument* Document = GetOwnerDocument();
			return Document != nullptr ? Document->GetStyleSheet() : nullptr;
		}

		void EnsureSheet()
		{
			if (!bSheetDirty)
			{
				return;
			}
			bSheetDirty = false;
			bGeometryDirty = true;
			LoadSheet();
			ShownFrame = Rml::Math::Clamp(ShownFrame, 0, Rml::Math::Max(int(Frames.size()) - 1, 0));
		}

		void LoadSheet()
		{
			FOLIO_TRACE_SCOPE("Folio/Flipbook/LoadSheet");

			Frames.clear();
			Texture = {};
			DimensionsScale = 1.0f;

			Rml::RenderManager* Manager = GetRenderManager();
			if (Manager == nullptr)
			{
				return;
			}

			const float DpRatio = Rml::ElementUtilities::GetDensityIndependentPixelRatio(this);

			const Rml::String Prefix = GetAttribute<Rml::String>("sprites", "");
			if (!Prefix.empty())
			{
				LoadNumberedSprites(*Manager, Prefix, DpRatio);
				return;
			}

			Rml::Rectanglef Region;
			const Rml::String SpriteName = GetAttribute<Rml::String>("sprite", "");
			if (!SpriteName.empty())
			{
				const Rml::StyleSheet* Sheet = FindStyleSheet();
				const Rml::Sprite* Sprite = Sheet != nullptr ? Sheet->GetSprite(SpriteName) : nullptr;
				if (Sprite == nullptr)
				{
					Rml::Log::Message(Rml::Log::LT_WARNING, "Could not find sprite '%s' specified in flipbook %s",
						SpriteName.c_str(), GetAddress().c_str());
					return;
				}
				Texture = Sprite->sprite_sheet->texture_source.GetTexture(*Manager);
				Region = Sprite->rectangle;
				DimensionsScale = Sprite->sprite_sheet->display_scale * DpRatio;
			}
			else
			{
				const Rml::String Source = GetAttribute<Rml::String>("src", "");
				if (Source.empty())
				{
					return;
				}
				Rml::URL DocumentUrl;
				if (Rml::ElementDocument* Document = GetOwnerDocument())
				{
					DocumentUrl.SetURL(Document->GetSourceURL());
				}
				Texture = Manager->LoadTexture(Source, DocumentUrl.GetPath());
				Region = Rml::Rectanglef::FromPositionSize(Rml::Vector2f(0.0f), Rml::Vector2f(Texture.GetDimensions()));
				DimensionsScale = DpRatio;
			}

			const int Columns = Rml::Math::Max(GetAttribute<int>("columns", 1), 1);
			const int Rows = Rml::Math::Max(GetAttribute<int>("rows", 1), 1);
			const int Cells = Columns * Rows;
			const int Wanted = GetAttribute<int>("frames", Cells);
			const int Count = Rml::Math::Clamp(Wanted, 1, Cells);

			const Rml::Vector2f Cell = Region.Size() / Rml::Vector2f(float(Columns), float(Rows));
			if (Cell.x <= 0.0f || Cell.y <= 0.0f)
			{
				return;
			}

			Frames.reserve(Count);
			for (int Index = 0; Index < Count; ++Index)
			{
				const Rml::Vector2f At(float(Index % Columns), float(Index / Columns));
				Frames.push_back(Rml::Rectanglef::FromPositionSize(Region.Position() + At * Cell, Cell));
			}
		}

		void LoadNumberedSprites(Rml::RenderManager& Manager, const Rml::String& Prefix, const float DpRatio)
		{
			const Rml::StyleSheet* Sheet = FindStyleSheet();
			if (Sheet == nullptr)
			{
				return;
			}

			for (int Index = 0;; ++Index)
			{
				const Rml::Sprite* Sprite = Sheet->GetSprite(Prefix + std::to_string(Index));
				if (Sprite == nullptr)
				{
					break;
				}
				if (Index == 0)
				{
					Texture = Sprite->sprite_sheet->texture_source.GetTexture(Manager);
					DimensionsScale = Sprite->sprite_sheet->display_scale * DpRatio;
				}
				else if (!(Sprite->sprite_sheet->texture_source.GetTexture(Manager) == Texture))
				{
					// One texture per flipbook, so every frame draws with the same bind.
					Rml::Log::Message(Rml::Log::LT_WARNING,
						"Flipbook %s: sprite '%s%d' is on a different spritesheet from '%s0'; stopping there",
						GetAddress().c_str(), Prefix.c_str(), Index, Prefix.c_str());
					break;
				}
				Frames.push_back(Sprite->rectangle);
			}

			if (Frames.empty())
			{
				Rml::Log::Message(Rml::Log::LT_WARNING, "Could not find sprite '%s0' specified in flipbook %s",
					Prefix.c_str(), GetAddress().c_str());
			}
		}

		void BuildGeometry(Rml::RenderManager& Manager)
		{
			FOLIO_TRACE_SCOPE("Folio/Flipbook/BuildGeometry");

			bGeometryDirty = false;
			Drawn.clear();

			const Rml::Vector2f TextureSize(Texture.GetDimensions());
			if (Frames.empty() || TextureSize.x <= 0.0f || TextureSize.y <= 0.0f)
			{
				return;
			}

			const Rml::ComputedValues& Computed = GetComputedValues();
			const Rml::ColourbPremultiplied Colour = Computed.image_color().ToPremultiplied(Computed.opacity());
			const Rml::RenderBox Box = GetRenderBox(Rml::BoxArea::Content);

			Drawn.reserve(Frames.size());
			for (const Rml::Rectanglef& Frame : Frames)
			{
				Rml::Vector2f QuadOrigin;
				Rml::Vector2f QuadSize;
				Rml::Vector2f TexTopLeft;
				Rml::Vector2f TexBottomRight;
				Place(Box.GetFillOffset(), Box.GetFillSize(), Frame, TextureSize, QuadOrigin, QuadSize, TexTopLeft, TexBottomRight);

				Rml::Mesh Mesh;
				Rml::MeshUtilities::GenerateQuad(Mesh, QuadOrigin, QuadSize, Colour, TexTopLeft, TexBottomRight);
				Drawn.push_back(Manager.MakeGeometry(std::move(Mesh)));
			}
		}

		/// Where a frame lands in the content box under the fit mode, centred, with the texture cropped to what shows.
		void Place(const Rml::Vector2f BoxOrigin, const Rml::Vector2f BoxSize, const Rml::Rectanglef& Frame,
			const Rml::Vector2f TextureSize, Rml::Vector2f& OutOrigin, Rml::Vector2f& OutSize, Rml::Vector2f& OutTexTopLeft,
			Rml::Vector2f& OutTexBottomRight) const
		{
			const Rml::Vector2f Natural = Frame.Size() * DimensionsScale;

			Rml::Vector2f Scaled = BoxSize;
			if (Natural.x > 0.0f && Natural.y > 0.0f)
			{
				const Rml::Vector2f Scale = BoxSize / Natural;
				switch (FitMode)
				{
				case EFlipbookFit::Contain:
					Scaled = Natural * Rml::Math::Min(Scale.x, Scale.y);
					break;
				case EFlipbookFit::Cover:
					Scaled = Natural * Rml::Math::Max(Scale.x, Scale.y);
					break;
				case EFlipbookFit::None:
					Scaled = Natural;
					break;
				case EFlipbookFit::Fill:
				default:
					break;
				}
			}

			const Rml::Vector2f Visible = Rml::Math::Min(Scaled, BoxSize);
			OutOrigin = BoxOrigin + (BoxSize - Visible) * 0.5f;
			OutSize = Visible;

			// The share of the frame that survives the crop, taken from its middle.
			const Rml::Vector2f Kept(Scaled.x > 0.0f ? Visible.x / Scaled.x : 1.0f, Scaled.y > 0.0f ? Visible.y / Scaled.y : 1.0f);
			const Rml::Vector2f Inset = Frame.Size() * (Rml::Vector2f(1.0f) - Kept) * 0.5f;
			OutTexTopLeft = (Frame.Position() + Inset) / TextureSize;
			OutTexBottomRight = (Frame.Position() + Frame.Size() - Inset) / TextureSize;
		}
	};

	/// Adds `<flipbook>`. Call after Rml::Initialise, as with the decorators.
	inline void RegisterFolioFlipbook()
	{
		auto& Owned = Detail::FlipbookInstancers();
		if (!Owned.empty())
		{
			return;
		}

		auto Instancer = std::make_unique<Rml::ElementInstancerGeneric<FElementFlipbook>>();
		Rml::Factory::RegisterElementInstancer("flipbook", Instancer.get());
		Owned.push_back(std::move(Instancer));
	}

	/// Drops the instancer. RmlUi holds a raw pointer, so this must happen after shutdown.
	inline void ReleaseFolioFlipbook()
	{
		Detail::FlipbookInstancers().clear();
	}
}
