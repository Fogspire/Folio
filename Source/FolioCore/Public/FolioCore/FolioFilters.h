#pragma once

#include <RmlUi/Core/CompiledFilterShader.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Filter.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <RmlUi/Core/RenderManager.h>

#include <memory>
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

		auto Instancer = std::make_unique<Detail::FOutsideBlurInstancer>();
		Rml::Factory::RegisterFilterInstancer("outside-blur", Instancer.get());
		Owned.push_back(std::move(Instancer));
	}

	/// Drops the instancers. RmlUi holds raw pointers, so this must happen after shutdown.
	inline void ReleaseFolioFilters()
	{
		Detail::OwnedFilterInstancers().clear();
	}
}
