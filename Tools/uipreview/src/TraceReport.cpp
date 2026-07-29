#include "TraceReport.h"

#include "FolioCore/FolioTrace.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace uipreview {
namespace {

using Clock = std::chrono::steady_clock;

struct ScopeTotal {
	const char* name = nullptr;
	std::int64_t calls = 0;
	double seconds = 0.0;
};

struct CounterTotal {
	const char* name = nullptr;
	std::int64_t value = 0;
	bool memory = false;
};

std::vector<ScopeTotal> GScopes;
std::vector<CounterTotal> GCounters;
/** Open scopes, so a nested one is charged to itself rather than its parent. */
std::vector<std::pair<std::uint32_t, Clock::time_point>> GStack;

void BeginScope(Folio::Trace::FScopeSpec& spec)
{
	if (spec.HostId == 0)
	{
		GScopes.push_back({spec.Name, 0, 0.0});
		spec.HostId = std::uint32_t(GScopes.size());
	}
	GStack.emplace_back(spec.HostId, Clock::now());
}

void EndScope()
{
	if (GStack.empty())
		return;

	const auto [id, start] = GStack.back();
	GStack.pop_back();

	ScopeTotal& total = GScopes[id - 1];
	total.calls += 1;
	total.seconds += std::chrono::duration<double>(Clock::now() - start).count();
}

CounterTotal& Resolve(Folio::Trace::FCounterSpec& spec)
{
	if (spec.HostId == 0)
	{
		GCounters.push_back({spec.Name, 0, spec.bMemory});
		spec.HostId = std::uint32_t(GCounters.size());
	}
	return GCounters[spec.HostId - 1];
}

void SetCounter(Folio::Trace::FCounterSpec& spec, std::int64_t value) { Resolve(spec).value = value; }
void AddCounter(Folio::Trace::FCounterSpec& spec, std::int64_t delta) { Resolve(spec).value += delta; }

} // namespace

void TraceReport::Install()
{
	Folio::Trace::FHooks hooks;
	hooks.BeginScope = &BeginScope;
	hooks.EndScope = &EndScope;
	hooks.SetCounter = &SetCounter;
	hooks.AddCounter = &AddCounter;
	Folio::Trace::SetHooks(hooks);
}

std::string TraceReport::Format()
{
	if (GScopes.empty() && GCounters.empty())
		return {};

	std::vector<ScopeTotal> scopes = GScopes;
	std::sort(scopes.begin(), scopes.end(),
		[](const ScopeTotal& a, const ScopeTotal& b) { return a.seconds > b.seconds; });

	std::string out = "trace:\n";
	char line[256];

	for (const ScopeTotal& scope : scopes)
	{
		const double ms = scope.seconds * 1000.0;
		std::snprintf(line, sizeof(line), "  %-26s %8.2f ms  %7lld calls  %7.3f ms/call\n",
			scope.name, ms, (long long)scope.calls,
			scope.calls ? ms / double(scope.calls) : 0.0);
		out += line;
	}

	for (const CounterTotal& counter : GCounters)
	{
		if (counter.memory)
			std::snprintf(line, sizeof(line), "  %-26s %8.1f MB\n", counter.name, double(counter.value) / (1024.0 * 1024.0));
		else
			std::snprintf(line, sizeof(line), "  %-26s %8lld\n", counter.name, (long long)counter.value);
		out += line;
	}

	return out;
}

} // namespace uipreview
