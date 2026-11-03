#pragma once

#include <RmlUi/Core/CompiledFilterShader.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Filter.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StyleSheetSpecification.h>

#include "FolioCore/FolioShaderFilters.h"

#include <cctype>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <vector>

namespace Folio
{
	//
	// Defined here rather than compiled into FolioCore, for the same reason the font effect
	// instancers are: RmlUi is a static library linked into both modules, each with its own
	// globals, and an instancer's constructor calls RegisterProperty, which resolves its parsers
	// through the StyleSheetSpecification that only one Rml::Initialise populated. Constructing
	// one in the other module reads state that was never built.
	//
	namespace Detail
	{
		/// `backdrop-filter: outside-blur(6dp)` - blurs everything the element is not.
		///
		/// The modal case. RmlUi's own `blur` in a backdrop-filter blurs what is behind the
		/// element and nothing else, which is right for a frosted panel and useless for a scrim:
		/// a dialog wants the screen behind it softened and itself left sharp.
		///
		/// The renderer does the inverting, because only it knows the viewport. This carries the
		/// radius and a name it can recognise.
		class FOutsideBlur final : public Rml::Filter
		{
		public:
			bool Initialise(const Rml::NumericValue InSigma)
			{
				SigmaValue = InSigma;
				return Rml::Any(InSigma.unit & Rml::Unit::LENGTH);
			}

			Rml::CompiledFilter CompileFilter(Rml::Element* Element) const override
			{
				const float Radius = Element->ResolveLength(SigmaValue);
				return Element->GetRenderManager()->CompileFilter("outside-blur",
					Rml::Dictionary{ { "sigma", Rml::Variant(Radius) } });
			}

			// Deliberately no ExtendInkOverflow. RmlUi's blur widens the scissor so the filter can
			// read past the element's edge; here the scissor is the hole, and widening it would
			// leave a ring of unblurred screen around the element.

		private:
			Rml::NumericValue SigmaValue;
		};

		class FOutsideBlurInstancer final : public Rml::FilterInstancer
		{
		public:
			FOutsideBlurInstancer()
			{
				SigmaId = RegisterProperty("sigma", "0px").AddParser("length").GetId();
				RegisterShorthand("filter", "sigma", Rml::ShorthandType::FallThrough);
			}

			Rml::SharedPtr<Rml::Filter> InstanceFilter(const Rml::String& /*Name*/,
				const Rml::PropertyDictionary& Properties) override
			{
				const Rml::Property* Sigma = Properties.GetProperty(SigmaId);
				if (Sigma == nullptr)
				{
					return nullptr;
				}

				auto Filter = Rml::MakeShared<FOutsideBlur>();
				return Filter->Initialise(Sigma->GetNumericValue()) ? Filter : nullptr;
			}

		private:
			Rml::PropertyId SigmaId = {};
		};

		/// One unresolved argument of a shader filter: a length waits for the element to resolve it.
		struct FShaderFilterToken
		{
			FShaderFilterArg Arg;
			Rml::NumericValue Length;
		};

		/// Splits `name arg arg ...` and types each argument with RCSS's own parsers.
		inline bool ParseShaderFilterTokens(const Rml::String& Value, Rml::String& OutName,
			std::vector<FShaderFilterToken>& OutTokens)
		{
			std::istringstream Stream(Value);
			if (!(Stream >> OutName))
			{
				return false;
			}

			Rml::PropertyParser* LengthParser = Rml::StyleSheetSpecification::GetParser("length");
			Rml::PropertyParser* ColourParser = Rml::StyleSheetSpecification::GetParser("color");
			const Rml::ParameterMap NoParameters;

			Rml::String Token;
			while (Stream >> Token)
			{
				FShaderFilterToken Parsed;
				Rml::Property Property;

				const bool bBareNumber = Token.find_first_not_of("0123456789.+-eE") == Rml::String::npos
					&& Token.find_first_of("0123456789") != Rml::String::npos;
				if (bBareNumber)
				{
					Parsed.Arg.Kind = FShaderFilterArg::EKind::Number;
					Parsed.Arg.Value = std::strtof(Token.c_str(), nullptr);
				}
				else if (LengthParser && LengthParser->ParseValue(Property, Token, NoParameters))
				{
					Parsed.Arg.Kind = FShaderFilterArg::EKind::Length;
					Parsed.Length = Property.GetNumericValue();
				}
				else if (ColourParser && ColourParser->ParseValue(Property, Token, NoParameters))
				{
					const Rml::Colourb Colour = Property.Get<Rml::Colourb>();
					Parsed.Arg.Kind = FShaderFilterArg::EKind::Colour;
					Parsed.Arg.Colour[0] = Colour.red / 255.0f;
					Parsed.Arg.Colour[1] = Colour.green / 255.0f;
					Parsed.Arg.Colour[2] = Colour.blue / 255.0f;
					Parsed.Arg.Colour[3] = Colour.alpha / 255.0f;
				}
				else
				{
					Parsed.Arg.Kind = FShaderFilterArg::EKind::Word;
					Parsed.Arg.Word = Token;
				}
				OutTokens.push_back(std::move(Parsed));
			}
			return true;
		}

		/// `filter: shader("name args")` and `backdrop-filter: shader(...)`, a GPU pass the game registered.
		///
		/// What the pass does is the game's business; this only resolves the arguments against the
		/// element and tells RmlUi how far past the box the result reaches.
		class FShaderFilter final : public Rml::Filter
		{
		public:
			bool Initialise(const Rml::String& Value)
			{
				return ParseShaderFilterTokens(Value, Name, Tokens);
			}

			std::vector<FShaderFilterArg> Resolve(Rml::Element* Element) const
			{
				std::vector<FShaderFilterArg> Args;
				Args.reserve(Tokens.size());
				for (const FShaderFilterToken& Token : Tokens)
				{
					FShaderFilterArg Arg = Token.Arg;
					if (Arg.Kind == FShaderFilterArg::EKind::Length)
					{
						Arg.Value = Element->ResolveLength(Token.Length);
					}
					Args.push_back(std::move(Arg));
				}
				return Args;
			}

			Rml::CompiledFilter CompileFilter(Rml::Element* Element) const override
			{
				return Element->GetRenderManager()->CompileFilter("shader", Rml::Dictionary{
					{ "name", Rml::Variant(Name) },
					{ "args", Rml::Variant(EncodeShaderFilterArgs(Resolve(Element))) },
				});
			}

			void ExtendInkOverflow(Rml::Element* Element, Rml::Rectanglef& Overflow) const override
			{
				Overflow = Overflow.Extend(ShaderFilterReach(Name, Resolve(Element)));
			}

		private:
			Rml::String Name;
			std::vector<FShaderFilterToken> Tokens;
		};

		class FShaderFilterInstancer final : public Rml::FilterInstancer
		{
		public:
			FShaderFilterInstancer()
			{
				ValueId = RegisterProperty("value", "").AddParser("string").GetId();
				RegisterShorthand("filter", "value", Rml::ShorthandType::FallThrough);
			}

			Rml::SharedPtr<Rml::Filter> InstanceFilter(const Rml::String& /*Name*/,
				const Rml::PropertyDictionary& Properties) override
			{
				const Rml::Property* Value = Properties.GetProperty(ValueId);
				if (Value == nullptr)
				{
					return nullptr;
				}

				auto Filter = Rml::MakeShared<FShaderFilter>();
				return Filter->Initialise(Value->Get<Rml::String>()) ? Filter : nullptr;
			}

		private:
			Rml::PropertyId ValueId = {};
		};

		/// `filter: glow(#ff8800 12dp 2)` - colour, radius, strength.
		///
		/// A soft halo behind whatever the element draws, of any size. Text effects stop at about a tenth
		/// of an em because they live in the glyph's distance field; this blurs the element's own pixels,
		/// so it reaches as far as the radius says. Strength multiplies the halo's coverage before it is
		/// clamped, which is what makes a thin outline glow as brightly as a solid shape.
		class FGlowFilter final : public Rml::Filter
		{
		public:
			bool Initialise(const Rml::Colourb InColour, const Rml::NumericValue InRadius, const float InStrength)
			{
				Colour = InColour;
				RadiusValue = InRadius;
				Strength = InStrength;
				return Rml::Any(InRadius.unit & Rml::Unit::LENGTH);
			}

			Rml::CompiledFilter CompileFilter(Rml::Element* Element) const override
			{
				return Element->GetRenderManager()->CompileFilter("glow", Rml::Dictionary{
					{ "color", Rml::Variant(Colour) },
					{ "sigma", Rml::Variant(SigmaFor(Element)) },
					{ "strength", Rml::Variant(Strength) },
				});
			}

			void ExtendInkOverflow(Rml::Element* Element, Rml::Rectanglef& Overflow) const override
			{
				// Four sigmas rather than three: the engine blends in linear light, where the tail is still visible.
				Overflow = Overflow.Extend(4.0f * SigmaFor(Element));
			}

		private:
			/// The radius is where the halo has visibly faded, which a gaussian reaches at about two sigma.
			float SigmaFor(Rml::Element* Element) const
			{
				return 0.5f * Element->ResolveLength(RadiusValue);
			}

			Rml::Colourb Colour;
			Rml::NumericValue RadiusValue;
			float Strength = 1.0f;
		};

		class FGlowFilterInstancer final : public Rml::FilterInstancer
		{
		public:
			FGlowFilterInstancer()
			{
				Ids.Colour = RegisterProperty("color", "#ffffff").AddParser("color").GetId();
				Ids.Radius = RegisterProperty("radius", "8dp").AddParser("length").GetId();
				Ids.Strength = RegisterProperty("strength", "1").AddParser("number").GetId();
				RegisterShorthand("filter", "color, radius, strength", Rml::ShorthandType::FallThrough);
			}

			Rml::SharedPtr<Rml::Filter> InstanceFilter(const Rml::String& /*Name*/,
				const Rml::PropertyDictionary& Properties) override
			{
				const Rml::Property* Colour = Properties.GetProperty(Ids.Colour);
				const Rml::Property* Radius = Properties.GetProperty(Ids.Radius);
				const Rml::Property* Strength = Properties.GetProperty(Ids.Strength);
				if (!Colour || !Radius || !Strength)
				{
					return nullptr;
				}

				auto Filter = Rml::MakeShared<FGlowFilter>();
				return Filter->Initialise(Colour->Get<Rml::Colourb>(), Radius->GetNumericValue(),
					Strength->Get<float>()) ? Filter : nullptr;
			}

		private:
			struct
			{
				Rml::PropertyId Colour = {};
				Rml::PropertyId Radius = {};
				Rml::PropertyId Strength = {};
			} Ids;
		};

		inline std::vector<std::unique_ptr<Rml::FilterInstancer>>& OwnedFilterInstancers()
		{
			static std::vector<std::unique_ptr<Rml::FilterInstancer>> Owned;
			return Owned;
		}
	}

	/// Adds Folio's own filters to the factory.
	///
	/// Call from the module that called Rml::Initialise, after it.
	inline void RegisterFolioFilters()
	{
		auto& Owned = Detail::OwnedFilterInstancers();
		if (!Owned.empty())
		{
			return;
		}

		const auto Add = [&Owned](const char* Name, std::unique_ptr<Rml::FilterInstancer> Instancer)
		{
			Rml::Factory::RegisterFilterInstancer(Name, Instancer.get());
			Owned.push_back(std::move(Instancer));
		};

		Add("outside-blur", std::make_unique<Detail::FOutsideBlurInstancer>());
		Add("shader", std::make_unique<Detail::FShaderFilterInstancer>());
		Add("glow", std::make_unique<Detail::FGlowFilterInstancer>());
	}

	/// Drops the instancers. RmlUi holds raw pointers, so this must happen after shutdown.
	inline void ReleaseFolioFilters()
	{
		Detail::OwnedFilterInstancers().clear();
	}
}
