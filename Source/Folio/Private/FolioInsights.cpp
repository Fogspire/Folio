#include "FolioInsights.h"

#include "FolioCore/FolioTrace.h"

#include "HAL/LowLevelMemTracker.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_STAT(STAT_FolioUpdate);
DEFINE_STAT(STAT_FolioRender);
DEFINE_STAT(STAT_FolioCompileGeometry);
DEFINE_STAT(STAT_FolioDrawGeometry);
DEFINE_STAT(STAT_FolioLoadTexture);
DEFINE_STAT(STAT_FolioResolveLayers);
DEFINE_STAT(STAT_FolioAccessibility);
DEFINE_STAT(STAT_FolioDraws);
DEFINE_STAT(STAT_FolioBatches);

UE_TRACE_CHANNEL_DEFINE(FolioChannel);

CSV_DEFINE_CATEGORY(Folio, false);

LLM_DEFINE_TAG(Folio);

namespace FolioInsights
{
	namespace
	{
#if COUNTERSTRACE_ENABLED
		/// What the core's HostId slot holds for a counter: the Insights id, plus the running
		/// total, because the core can ask to add to a counter and Insights only takes absolutes.
		struct FCounter
		{
			uint16 Id = 0;
			int64 Value = 0;
		};

		/// Indexed by HostId - 1. Never shrinks: a counter is declared once per call site and
		/// lives for the process.
		TArray<FCounter>& Counters()
		{
			static TArray<FCounter> Storage;
			return Storage;
		}
#endif

		// Shipping compiles the trace machinery out entirely, so FCpuProfilerTrace and
		// FCountersTrace do not exist there. The hooks still have to, because the core calls them
		// unconditionally, so they become empty rather than absent.
#if CPUPROFILERTRACE_ENABLED
		void BeginScope(Folio::Trace::FScopeSpec& Spec)
		{
			if (Spec.HostId == 0)
			{
				// Resolving a name is the expensive part of a dynamic event, so it happens once
				// per call site and the id lives in the slot the core carries.
				Spec.HostId = FCpuProfilerTrace::OutputDynamicEventType(Spec.Name);
			}
			FCpuProfilerTrace::OutputBeginEvent(Spec.HostId);
		}

		void EndScope()
		{
			FCpuProfilerTrace::OutputEndEvent();
		}
#else
		void BeginScope(Folio::Trace::FScopeSpec&) {}
		void EndScope() {}
#endif

#if COUNTERSTRACE_ENABLED
		FCounter& Resolve(Folio::Trace::FCounterSpec& Spec)
		{
			if (Spec.HostId == 0)
			{
				FCounter Counter;
				Counter.Id = FCountersTrace::OutputInitCounter(
					ANSI_TO_TCHAR(Spec.Name),
					TraceCounterType_Int,
					Spec.bMemory ? TraceCounterDisplayHint_Memory : TraceCounterDisplayHint_None);

				Counters().Add(Counter);
				Spec.HostId = uint32(Counters().Num());
			}
			return Counters()[int32(Spec.HostId) - 1];
		}

		void SetCounter(Folio::Trace::FCounterSpec& Spec, int64 Value)
		{
			FCounter& Counter = Resolve(Spec);
			Counter.Value = Value;
			FCountersTrace::OutputSetValue(Counter.Id, Counter.Value);
		}

		void AddCounter(Folio::Trace::FCounterSpec& Spec, int64 Delta)
		{
			FCounter& Counter = Resolve(Spec);
			Counter.Value += Delta;
			FCountersTrace::OutputSetValue(Counter.Id, Counter.Value);
		}
#else
		void SetCounter(Folio::Trace::FCounterSpec&, int64) {}
		void AddCounter(Folio::Trace::FCounterSpec&, int64) {}
#endif
	}

	void Install()
	{
		Folio::Trace::FHooks Hooks;
		Hooks.BeginScope = &BeginScope;
		Hooks.EndScope = &EndScope;
		Hooks.SetCounter = &SetCounter;
		Hooks.AddCounter = &AddCounter;
		Folio::Trace::SetHooks(Hooks);
	}
}
