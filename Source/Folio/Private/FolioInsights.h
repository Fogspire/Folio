#pragma once

#include "CoreMinimal.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "Stats/Stats.h"

/// Everything Folio reports to Insights, `stat folio`, and CSV captures.
///
/// The portable core cannot use these macros - it compiles into the preview tool as well, where
/// there is no Unreal - so it names its scopes through a hook table instead and this module
/// forwards them. See FolioCore/FolioTrace.h. Everything on the Unreal side of the boundary uses
/// the macros directly, because it is already Unreal code.
DECLARE_STATS_GROUP(TEXT("Folio"), STATGROUP_Folio, STATCAT_Advanced);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Update"), STAT_FolioUpdate, STATGROUP_Folio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Render"), STAT_FolioRender, STATGROUP_Folio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Compile geometry"), STAT_FolioCompileGeometry, STATGROUP_Folio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Draw geometry"), STAT_FolioDrawGeometry, STATGROUP_Folio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Load texture"), STAT_FolioLoadTexture, STATGROUP_Folio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Resolve layers"), STAT_FolioResolveLayers, STATGROUP_Folio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Deferred text"), STAT_FolioDeferredText, STATGROUP_Folio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Bake decorator"), STAT_FolioBakeDecorator, STATGROUP_Folio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Allocate target"), STAT_FolioAllocateTarget, STATGROUP_Folio, );

/// Render thread. The work the game thread only queues: everything above ends at the enqueue, so a
/// layer that costs milliseconds to replay reports as microseconds without these.
DECLARE_CYCLE_STAT_EXTERN(TEXT("RT: replay layer"), STAT_FolioLayerReplay, STATGROUP_Folio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("RT: blur"), STAT_FolioBlur, STATGROUP_Folio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("RT: colour filter"), STAT_FolioColourFilter, STATGROUP_Folio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("RT: mask"), STAT_FolioMaskFilter, STATGROUP_Folio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("RT: resolve supersample"), STAT_FolioSupersample, STATGROUP_Folio, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("RT: world surfaces"), STAT_FolioWorldSurfaces, STATGROUP_Folio, );

DECLARE_CYCLE_STAT_EXTERN(TEXT("Accessibility walk"), STAT_FolioAccessibility, STATGROUP_Folio, );

DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Draws"), STAT_FolioDraws, STATGROUP_Folio, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Geometry batches"), STAT_FolioBatches, STATGROUP_Folio, );

/// What the layers cost to hold, which is the number a sample count multiplies by itself.
DECLARE_MEMORY_STAT_EXTERN(TEXT("Layer targets"), STAT_FolioLayerMemory, STATGROUP_Folio, );

/// A channel of its own, so a capture can ask for Folio without everything else:
///   -trace=default,folio
UE_TRACE_CHANNEL_EXTERN(FolioChannel);

/// `stat gpu`. The passes are already named for a GPU capture; these are what puts their time in a
/// number the game can watch every frame without one.
DECLARE_GPU_STAT_NAMED_EXTERN(FolioLayers, TEXT("Folio layers"));
DECLARE_GPU_STAT_NAMED_EXTERN(FolioEffects, TEXT("Folio effects"));
DECLARE_GPU_STAT_NAMED_EXTERN(FolioSupersample, TEXT("Folio supersample"));

CSV_DECLARE_CATEGORY_EXTERN(Folio);

/// Attributes the glyph atlases and RmlUi's own allocations, so `-llm` shows them as Folio rather
/// than as untagged texture memory.
LLM_DECLARE_TAG(Folio);

namespace FolioInsights
{
	/// Points the portable core's trace hooks at Insights. Call once, before any font is loaded.
	void Install();
}
