#pragma once

#include "FolioCore/FolioCoreAPI.h"

#include <cstdint>

namespace Folio
{
	/// How the portable core reports timing and counters to whoever is hosting it.
	///
	/// FolioCore compiles into both Unreal and the standalone preview tool, so it cannot use
	/// Unreal's profiling macros directly - they would drag CoreMinimal into every translation
	/// unit, which is exactly what this module is arranged to avoid. Instead it names its scopes
	/// and counters through a table of hooks the host fills in: the Unreal module forwards them to
	/// Insights, the preview tool accumulates totals, and an unhosted build pays a null check.
	namespace Trace
	{
		/// A named scope. HostId is scratch space for the host to memoise whatever handle its
		/// profiler wants, so the name is only resolved once rather than on every entry.
		struct FScopeSpec
		{
			const char* Name = nullptr;
			std::uint32_t HostId = 0;
		};

		struct FCounterSpec
		{
			const char* Name = nullptr;
			std::uint32_t HostId = 0;
			/// True when the value is a byte count, which profilers display differently.
			bool bMemory = false;
		};

		struct FHooks
		{
			void (*BeginScope)(FScopeSpec&) = nullptr;
			void (*EndScope)() = nullptr;
			void (*SetCounter)(FCounterSpec&, std::int64_t) = nullptr;
			void (*AddCounter)(FCounterSpec&, std::int64_t) = nullptr;
		};

		/// Installed once, before anything is loaded. Not thread safe by design: it is set at
		/// startup and read thereafter.
		FOLIOCORE_API void SetHooks(const FHooks& InHooks);
		FOLIOCORE_API const FHooks& GetHooks();

		/// Opens a scope on construction and closes it on destruction, so an early return cannot
		/// leave one open.
		class FScope
		{
		public:
			explicit FScope(FScopeSpec& InSpec) : bOpen(GetHooks().BeginScope != nullptr)
			{
				if (bOpen)
				{
					GetHooks().BeginScope(InSpec);
				}
			}

			~FScope()
			{
				if (bOpen)
				{
					GetHooks().EndScope();
				}
			}

			FScope(const FScope&) = delete;
			FScope& operator=(const FScope&) = delete;

		private:
			bool bOpen = false;
		};

		inline void SetCounter(FCounterSpec& Spec, std::int64_t Value)
		{
			if (GetHooks().SetCounter != nullptr)
			{
				GetHooks().SetCounter(Spec, Value);
			}
		}

		inline void AddCounter(FCounterSpec& Spec, std::int64_t Delta)
		{
			if (GetHooks().AddCounter != nullptr)
			{
				GetHooks().AddCounter(Spec, Delta);
			}
		}
	}
}

#define FOLIO_TRACE_JOIN_INNER(A, B) A##B
#define FOLIO_TRACE_JOIN(A, B) FOLIO_TRACE_JOIN_INNER(A, B)

/// Times the enclosing block. The spec is function-local and static so the host resolves the name
/// once per site rather than once per call.
#define FOLIO_TRACE_SCOPE(NameLiteral)                                                  \
	static ::Folio::Trace::FScopeSpec FOLIO_TRACE_JOIN(FolioTraceSpec_, __LINE__){ NameLiteral, 0 };       \
	::Folio::Trace::FScope FOLIO_TRACE_JOIN(FolioTraceScope_, __LINE__)(FOLIO_TRACE_JOIN(FolioTraceSpec_, __LINE__))

#define FOLIO_TRACE_COUNTER_SET(NameLiteral, Value)                                     \
	do {                                                                                \
		static ::Folio::Trace::FCounterSpec FOLIO_TRACE_JOIN(FolioCounter_, __LINE__){ NameLiteral, 0, false }; \
		::Folio::Trace::SetCounter(FOLIO_TRACE_JOIN(FolioCounter_, __LINE__), (Value));                   \
	} while (false)

#define FOLIO_TRACE_COUNTER_ADD(NameLiteral, Delta)                                     \
	do {                                                                                \
		static ::Folio::Trace::FCounterSpec FOLIO_TRACE_JOIN(FolioCounter_, __LINE__){ NameLiteral, 0, false }; \
		::Folio::Trace::AddCounter(FOLIO_TRACE_JOIN(FolioCounter_, __LINE__), (Delta));                   \
	} while (false)

#define FOLIO_TRACE_MEMORY_SET(NameLiteral, Bytes)                                      \
	do {                                                                                \
		static ::Folio::Trace::FCounterSpec FOLIO_TRACE_JOIN(FolioCounter_, __LINE__){ NameLiteral, 0, true }; \
		::Folio::Trace::SetCounter(FOLIO_TRACE_JOIN(FolioCounter_, __LINE__), (Bytes));                   \
	} while (false)
