#pragma once

#include <string>

namespace uipreview {

/**
 * Accumulates the timings FolioCore reports, for a build with no Unreal to send them to.
 *
 * Insights is the real instrument; this exists so the same scopes mean something in the tool
 * where most of the iteration happens, and so the seam is exercised by both hosts rather than
 * only the one that is harder to run.
 */
namespace TraceReport {

/** Installs the hooks. Call once, before any font is loaded. */
void Install();

/** Totals per scope and counter, sorted by time spent. Empty if Install was never called. */
std::string Format();

} // namespace TraceReport
} // namespace uipreview
