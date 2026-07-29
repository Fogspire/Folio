#pragma once

#include "FolioCore/FolioCoreAPI.h"
#include "FolioCore/MsdfTypes.h"

#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/FontEffect.h>
#include <RmlUi/Core/FontEffectInstancer.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <RmlUi/Core/PropertySpecification.h>

#include <memory>
#include <vector>

namespace Folio
{
	/// A font effect resolved from the field rather than baked into the atlas.
	///
	/// RmlUi's own effects generate a second glyph texture per effect per size, which is exactly
	/// the size dependence distance fields exist to remove. These carry parameters instead: the
	/// glyph is drawn again with a different threshold, and the shader does the work.
	class FOLIOCORE_API FFolioFontEffect final : public Rml::FontEffect
	{
	public:
		FFolioFontEffect();
		~FFolioFontEffect() override;

		bool HasUniqueTexture() const override;

		FMsdfEffectParams Params;

		/// Displacement in the units the effect was written in, resolved against the font size
		/// when the string is generated.
		float OffsetX = 0.0f;
		float OffsetY = 0.0f;
	};

	/// Recognises one of ours without RTTI, which Unreal's RmlUi does not have. The registry lives
	/// in FolioCore, so an effect built by any module is recognised by the engine in this one.
	FOLIOCORE_API const FFolioFontEffect* AsFolioFontEffect(const Rml::FontEffect* Effect);

	//
	// The instancers are deliberately defined here rather than compiled into FolioCore.
	//
	// RmlUi is a static library, so in Unreal it is linked into both Folio and FolioCore and each
	// holds its own copy of its globals. An instancer's constructor calls RegisterProperty, which
	// looks its parsers up in StyleSheetSpecification - global, and only populated by the
	// Rml::Initialise that one module ran. Constructing one in the other module reads state that
	// was never built, and the editor dies during StartupModule with no callstack.
	//
	// Defined inline, each module compiles its own and registers them with the Factory it actually
	// initialised. The effect objects still come from FolioCore, so there is one registry of them.
	//
	namespace Detail
	{
		inline void ApplyColour(Rml::FontEffect& Effect, const Rml::Property* Property)
		{
			// Each instancer parses its own colour, exactly as RmlUi's do; the factory does not
			// apply it for them.
			if (Property != nullptr)
			{
				Effect.SetColour(Property->Get<Rml::Colourb>());
			}
		}

		inline float ToLength(const Rml::Property* Property, float Fallback)
		{
			return Property != nullptr ? Property->Get<float>() : Fallback;
		}

		/// Property specifications are copied from RmlUi's own instancers, so markup written
		/// against the documented effects keeps parsing exactly as before. Only what the effect
		/// does with the parsed values differs.
		class FOutlineInstancer final : public Rml::FontEffectInstancer
		{
		public:
			FOutlineInstancer()
			{
				IdWidth = RegisterProperty("width", "1px", true).AddParser("length").GetId();
				IdColour = RegisterProperty("color", "white", false).AddParser("color").GetId();
				RegisterShorthand("font-effect", "width, color", Rml::ShorthandType::FallThrough);
			}

			Rml::SharedPtr<Rml::FontEffect> InstanceFontEffect(const Rml::String&,
				const Rml::PropertyDictionary& Properties) override
			{
				auto Effect = Rml::MakeShared<FFolioFontEffect>();
				Effect->SetLayer(Rml::FontEffect::Layer::Back);

				// A width is a distance outward from the edge. Converting it to em happens in the
				// engine, which is what knows the size the handle was cut at.
				Effect->Params.Effect = EMsdfEffect::Outline;
				Effect->Params.OffsetEm = ToLength(Properties.GetProperty(IdWidth), 1.0f);
				Effect->Params.SoftnessEm = 0.0f;
				ApplyColour(*Effect, Properties.GetProperty(IdColour));
				return Effect;
			}

		private:
			Rml::PropertyId IdWidth = Rml::PropertyId::Invalid;
			Rml::PropertyId IdColour = Rml::PropertyId::Invalid;
		};

		class FGlowInstancer final : public Rml::FontEffectInstancer
		{
		public:
			FGlowInstancer()
			{
				IdWidthOutline = RegisterProperty("width-outline", "1px", true).AddParser("length").GetId();
				IdWidthBlur = RegisterProperty("width-blur", "-1px", true).AddParser("length").GetId();
				IdOffsetX = RegisterProperty("offset-x", "0px", true).AddParser("length").GetId();
				IdOffsetY = RegisterProperty("offset-y", "0px", true).AddParser("length").GetId();
				IdColour = RegisterProperty("color", "white", false).AddParser("color").GetId();
				RegisterShorthand("font-effect", "width-outline, width-blur, offset-x, offset-y, color",
					Rml::ShorthandType::FallThrough);
			}

			Rml::SharedPtr<Rml::FontEffect> InstanceFontEffect(const Rml::String&,
				const Rml::PropertyDictionary& Properties) override
			{
				auto Effect = Rml::MakeShared<FFolioFontEffect>();
				Effect->SetLayer(Rml::FontEffect::Layer::Back);

				const float Outline = ToLength(Properties.GetProperty(IdWidthOutline), 1.0f);
				const float Blur = ToLength(Properties.GetProperty(IdWidthBlur), -1.0f);

				Effect->Params.Effect = EMsdfEffect::Glow;
				Effect->Params.OffsetEm = Outline;
				// A negative blur width means "the same as the outline", which is RmlUi's default.
				Effect->Params.SoftnessEm = Blur < 0.0f ? Outline : Blur;
				Effect->OffsetX = ToLength(Properties.GetProperty(IdOffsetX), 0.0f);
				Effect->OffsetY = ToLength(Properties.GetProperty(IdOffsetY), 0.0f);
				ApplyColour(*Effect, Properties.GetProperty(IdColour));
				return Effect;
			}

		private:
			Rml::PropertyId IdWidthOutline = Rml::PropertyId::Invalid;
			Rml::PropertyId IdWidthBlur = Rml::PropertyId::Invalid;
			Rml::PropertyId IdOffsetX = Rml::PropertyId::Invalid;
			Rml::PropertyId IdOffsetY = Rml::PropertyId::Invalid;
			Rml::PropertyId IdColour = Rml::PropertyId::Invalid;
		};

		class FShadowInstancer final : public Rml::FontEffectInstancer
		{
		public:
			FShadowInstancer()
			{
				IdOffsetX = RegisterProperty("offset-x", "0px", true).AddParser("length").GetId();
				IdOffsetY = RegisterProperty("offset-y", "0px", true).AddParser("length").GetId();
				IdColour = RegisterProperty("color", "white", false).AddParser("color").GetId();
				RegisterShorthand("offset", "offset-x, offset-y", Rml::ShorthandType::FallThrough);
				RegisterShorthand("font-effect", "offset-x, offset-y, color", Rml::ShorthandType::FallThrough);
			}

			Rml::SharedPtr<Rml::FontEffect> InstanceFontEffect(const Rml::String&,
				const Rml::PropertyDictionary& Properties) override
			{
				auto Effect = Rml::MakeShared<FFolioFontEffect>();
				Effect->SetLayer(Rml::FontEffect::Layer::Back);

				// A shadow is the glyph itself, moved. The threshold does not change at all.
				Effect->Params.Effect = EMsdfEffect::Fill;
				Effect->OffsetX = ToLength(Properties.GetProperty(IdOffsetX), 0.0f);
				Effect->OffsetY = ToLength(Properties.GetProperty(IdOffsetY), 0.0f);
				ApplyColour(*Effect, Properties.GetProperty(IdColour));
				return Effect;
			}

		private:
			Rml::PropertyId IdOffsetX = Rml::PropertyId::Invalid;
			Rml::PropertyId IdOffsetY = Rml::PropertyId::Invalid;
			Rml::PropertyId IdColour = Rml::PropertyId::Invalid;
		};

		class FBlurInstancer final : public Rml::FontEffectInstancer
		{
		public:
			FBlurInstancer()
			{
				IdWidth = RegisterProperty("width", "1px", true).AddParser("length").GetId();
				IdColour = RegisterProperty("color", "white", false).AddParser("color").GetId();
				RegisterShorthand("font-effect", "width, color", Rml::ShorthandType::FallThrough);
			}

			Rml::SharedPtr<Rml::FontEffect> InstanceFontEffect(const Rml::String&,
				const Rml::PropertyDictionary& Properties) override
			{
				auto Effect = Rml::MakeShared<FFolioFontEffect>();
				Effect->SetLayer(Rml::FontEffect::Layer::Back);

				// Softening the threshold band is not a gaussian, and a wide blur shows it. It is
				// the only blur a field can express without a second pass.
				Effect->Params.Effect = EMsdfEffect::Glow;
				Effect->Params.OffsetEm = 0.0f;
				Effect->Params.SoftnessEm = ToLength(Properties.GetProperty(IdWidth), 1.0f);
				ApplyColour(*Effect, Properties.GetProperty(IdColour));
				return Effect;
			}

		private:
			Rml::PropertyId IdWidth = Rml::PropertyId::Invalid;
			Rml::PropertyId IdColour = Rml::PropertyId::Invalid;
		};

		inline std::vector<std::unique_ptr<Rml::FontEffectInstancer>>& OwnedInstancers()
		{
			static std::vector<std::unique_ptr<Rml::FontEffectInstancer>> Owned;
			return Owned;
		}
	}

	/// Replaces RmlUi's outline, glow, shadow and blur instancers with ours.
	///
	/// Call from the module that called Rml::Initialise, after it. Markup is unchanged; only what
	/// the effect does with its properties differs.
	inline void RegisterFolioFontEffects()
	{
		auto& Owned = Detail::OwnedInstancers();
		if (!Owned.empty())
		{
			return;
		}

		// Registering over a name replaces what the factory put there during Initialise.
		const auto Add = [&Owned](const char* Name, std::unique_ptr<Rml::FontEffectInstancer> Instancer)
		{
			Rml::Factory::RegisterFontEffectInstancer(Name, Instancer.get());
			Owned.push_back(std::move(Instancer));
		};

		Add("outline", std::make_unique<Detail::FOutlineInstancer>());
		Add("glow", std::make_unique<Detail::FGlowInstancer>());
		Add("shadow", std::make_unique<Detail::FShadowInstancer>());
		Add("blur", std::make_unique<Detail::FBlurInstancer>());
	}

	/// Drops the instancers. RmlUi holds raw pointers, so this must happen after shutdown.
	inline void ReleaseFolioFontEffects()
	{
		Detail::OwnedInstancers().clear();
	}
}
