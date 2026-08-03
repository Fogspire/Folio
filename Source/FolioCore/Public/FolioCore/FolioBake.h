#pragma once

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>

#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

/**
 * Rendering one element to a texture, so a document can become the art it draws.
 *
 * The case it exists for is an effect that is cheap to author and expensive to run: a full-screen
 * vignette out of an inset `box-shadow`, a soft glow behind a panel, a masked plate. Each of those
 * is a render-target pass every frame, and none of it changes - so it should be a texture, and the
 * document that describes it should be the thing that produces the texture.
 *
 * Shared rather than duplicated because both renderers bake, and a bake that isolated the element
 * differently in each would produce two different textures from one document. Which one is right
 * depends on the effect: see NeedsEngineBake.
 *
 * Header-only, like the rest of FolioCore's RmlUi code. See FolioBehaviours.h.
 */
namespace Folio::Bake
{
	/** One inline property a bake set, so a single pass can undo all of them. */
	struct FApplied
	{
		Rml::Element* Element = nullptr;
		const char* Property = nullptr;
	};

	/**
	 * What an ancestor of the baked element has to stop doing.
	 *
	 * It cannot simply be hidden. RmlUi drops an invisible element from the stacking context along
	 * with everything under it, so hiding the body hides the thing being baked - which is why this
	 * is a list of what an ancestor paints rather than one `visibility` on the root.
	 */
	inline constexpr const char* AncestorSilenced[] = { "background-color", "decorator", "box-shadow",
		"filter", "backdrop-filter", "mask-image", "border-width", "animation", "transition",
		"opacity" };
	inline constexpr const char* AncestorValues[] = { "transparent", "none", "none", "none", "none",
		"none", "0dp", "none", "none", "1" };

	inline void Set(Rml::Element* Element, const char* Property, const char* Value,
		std::vector<FApplied>& Applied)
	{
		Element->SetProperty(Property, Value);
		Applied.push_back({ Element, Property });
	}

	inline void IsolateInto(Rml::Element* Element, Rml::Element* Keep,
		const std::set<Rml::Element*>& Ancestors, std::vector<FApplied>& Applied)
	{
		if (!Element)
		{
			return;
		}

		if (Element == Keep)
		{
			// At full strength whatever the document rests at. A layer that is faded out until the
			// game turns it on would otherwise bake to nothing, and opacity is what the element
			// drawing the texture animates anyway.
			//
			// The animation goes first and is not optional: a running one drives the property
			// itself, so setting `opacity` under it changes nothing and a layer that spends most of
			// its cycle invisible bakes as an empty texture.
			Set(Element, "animation", "none", Applied);
			Set(Element, "transition", "none", Applied);
			Set(Element, "opacity", "1", Applied);
			return;
		}

		if (Ancestors.count(Element) != 0)
		{
			for (size_t i = 0; i < sizeof(AncestorSilenced) / sizeof(AncestorSilenced[0]); ++i)
			{
				Set(Element, AncestorSilenced[i], AncestorValues[i], Applied);
			}

			for (int i = 0; i < Element->GetNumChildren(); ++i)
			{
				IsolateInto(Element->GetChild(i), Keep, Ancestors, Applied);
			}

			return;
		}

		Set(Element, "visibility", "hidden", Applied);
	}

	/** Leaves only Keep and its subtree drawing. Undo with Restore. */
	inline std::vector<FApplied> Isolate(Rml::Element* Document, Rml::Element* Keep)
	{
		std::set<Rml::Element*> Ancestors;
		for (Rml::Element* Up = Keep ? Keep->GetParentNode() : nullptr; Up; Up = Up->GetParentNode())
		{
			Ancestors.insert(Up);
		}

		std::vector<FApplied> Applied;
		IsolateInto(Document, Keep, Ancestors, Applied);
		return Applied;
	}

	inline void Restore(const std::vector<FApplied>& Applied)
	{
		for (const FApplied& One : Applied)
		{
			One.Element->RemoveProperty(One.Property);
		}
	}

	/**
	 * Whether this element's appearance depends on a layer effect.
	 *
	 * `box-shadow`, `filter`, `backdrop-filter` and `mask-image` are the four the two renderers do
	 * not draw identically - a shadow's falloff and strength differ by several times. A texture
	 * baked from the preview and drawn in the engine therefore looks wrong in the game, faintly and
	 * in a way that reads as the art being bad rather than as a renderer difference.
	 *
	 * Everything else - gradients, images, colour, borders - matches, and either renderer will do.
	 */
	inline bool NeedsEngineBake(Rml::Element* Element)
	{
		if (!Element)
		{
			return false;
		}

		const Rml::ComputedValues& Computed = Element->GetComputedValues();
		if (Computed.has_box_shadow() || Computed.has_filter() || Computed.has_backdrop_filter()
			|| Computed.has_mask_image())
		{
			return true;
		}

		for (int i = 0; i < Element->GetNumChildren(); ++i)
		{
			if (NeedsEngineBake(Element->GetChild(i)))
			{
				return true;
			}
		}

		return false;
	}

	/**
	 * Whether this element's appearance depends on what is behind it.
	 *
	 * `backdrop-filter` reads the frame underneath and blurs that, so there is nothing to bake: the
	 * element is rendered alone, on nothing, and a blur of nothing is nothing. It is expensive for
	 * exactly the reason it cannot be a texture.
	 */
	inline bool ReadsTheBackdrop(Rml::Element* Element)
	{
		if (!Element)
		{
			return false;
		}

		if (Element->GetComputedValues().has_backdrop_filter())
		{
			return true;
		}

		for (int i = 0; i < Element->GetNumChildren(); ++i)
		{
			if (ReadsTheBackdrop(Element->GetChild(i)))
			{
				return true;
			}
		}

		return false;
	}

	/**
	 * Whether an animation is driving this element's properties.
	 *
	 * A bake sets `animation: none` before it sets `opacity`, because a running animation drives the
	 * property itself. RmlUi does not always release one that way, and when it does not the layer
	 * bakes at whatever the animation had it at - which for a hit flash is nothing at all, silently.
	 *
	 * So a baker document should carry no state classes: present every layer plainly and let each
	 * bake bring its own up to full strength. This is what warns when one does.
	 */
	inline bool HasAnimation(Rml::Element* Element)
	{
		if (!Element)
		{
			return false;
		}

		const Rml::AnimationList* Animation = Element->GetComputedValues().animation();
		return Animation && !Animation->empty();
	}

	/** The element's border box in framebuffer pixels, grown by Margin and clipped to the frame. */
	inline bool CropRect(Rml::Element* Element, int FrameWidth, int FrameHeight, int Margin,
		int& OutX, int& OutY, int& OutWidth, int& OutHeight)
	{
		const Rml::Vector2f Offset = Element->GetAbsoluteOffset(Rml::BoxArea::Border);
		const Rml::Vector2f Size = Element->GetBox().GetSize(Rml::BoxArea::Border);

		const int Left = (int)std::floor(Offset.x) - Margin;
		const int Top = (int)std::floor(Offset.y) - Margin;
		const int Right = (int)std::ceil(Offset.x + Size.x) + Margin;
		const int Bottom = (int)std::ceil(Offset.y + Size.y) + Margin;

		OutX = Left > 0 ? Left : 0;
		OutY = Top > 0 ? Top : 0;
		OutWidth = (Right < FrameWidth ? Right : FrameWidth) - OutX;
		OutHeight = (Bottom < FrameHeight ? Bottom : FrameHeight) - OutY;

		return OutWidth > 0 && OutHeight > 0;
	}

	/**
	 * Straight alpha, because Folio premultiplies a file texture as it loads.
	 *
	 * Both renderers composite premultiplied and both read the result back the same way, so writing
	 * it as it stands would have the engine multiply by alpha a second time. The symptom is a wash
	 * that looks right where it is opaque and too dark everywhere it is not, which reads as the
	 * colour being wrong rather than the encoding.
	 */
	inline void Unpremultiply(std::vector<uint8_t>& Rgba)
	{
		for (size_t i = 0; i + 3 < Rgba.size(); i += 4)
		{
			const unsigned Alpha = Rgba[i + 3];
			if (Alpha == 0 || Alpha == 255)
			{
				continue;
			}

			for (int Channel = 0; Channel < 3; ++Channel)
			{
				const unsigned Value = (Rgba[i + Channel] * 255u + Alpha / 2) / Alpha;
				Rgba[i + Channel] = (uint8_t)(Value < 255u ? Value : 255u);
			}
		}
	}
}
