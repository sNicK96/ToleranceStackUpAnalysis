// After the calculator processes a ToleranceChain, it needs to hand back a structured answer that covers three things:
// 1. What is the worst that could happen?        -> WorstCaseResult
// 2. What is statistically likely to happen?     -> RSSResult
// 3. What does this mean for the assembly fit?   -> FitCondition
// All three are bundled into one StackUpResult per chain, and all chain results for a drawing are bundled into one StackUpReport.
#pragma once

#include "ToleranceChain.h"
#include <string>
#include <vector>
#include <optional>

namespace Application {

// -------------------------------------------------------------------
//  Fit Condition
//  Describes the assembly fit between mating
//  features based on calculated stack-up
// -------------------------------------------------------------------
    // This is derived from the stack-up result. After computing the min and max assembly values, the calculator checks:
    // min_value > 0 ->  Clearance     (always a gap)
    // max_value < 0 ->  Interference  (always press)
    // straddles 0   ->  Transition    (unpredictable)
enum class FitCondition
{
    Clearance,          // always a gap  — shaft always smaller than bore
    Interference,       // always press  — shaft always larger than bore
    Transition,         // could be either — overlap in tolerance zones
    Unknown             // cannot determine from available data
};

std::string fit_condition_to_string(FitCondition fc);

// -------------------------------------------------------------------
//  WorstCaseResult
//  Arithmetic sum of all tolerance deviations
//  Guaranteed to cover 100% of parts
// -------------------------------------------------------------------
    // nominal_closure  =  +30.0 - 30.0  =  0.0mm   (balanced fit)
    // upper_deviation  =  0.000 + 0.021 =  0.021mm  (from chain.worst_case_upper())
    // lower_deviation  =  0.013 + 0.000 =  0.013mm  (from chain.worst_case_lower())
    // total_band       =  0.021 + 0.013 =  0.034mm

    // max_value  =  0.0 + 0.021  =  +0.021mm  (max clearance)
    // min_value  =  0.0 - 0.013  =  -0.013mm  (max interference)
struct WorstCaseResult
{
    double  upper_deviation     = 0.0;  // max positive deviation from nominal
    double  lower_deviation     = 0.0;  // max negative deviation from nominal
    double  total_band          = 0.0;  // upper + lower

    double  nominal_closure     = 0.0;  // sum of signed nominals
    double  max_value           = 0.0;  // nominal_closure + upper_deviation
    double  min_value           = 0.0;  // nominal_closure - lower_deviation
};

// -------------------------------------------------------------------
//  RSSResult
//  Root Sum of Squares — statistical method
//  Typically covers ~99.73% (3σ) of assemblies
// -------------------------------------------------------------------
    // Same structure as `WorstCaseResult` but the deviations come from `chain.rss_upper()` and `chain.rss_lower()` instead of the arithmetic sum.
    // The extra fields:
    // sigma_level - defaults to 3σ (99.73%). Can be changed to 4σ (99.994%) or 6σ (99.9999966%) for high-precision applications like aerospace or medical devices.
    // cpk - Process Capability Index. Measures how well the process fits within design limits. It's `optional` because it can only be calculated if the engineer provides design limits. A `Cpk` of:
    // >= 1.67  →  Excellent  (6σ capable)
    // >= 1.33  →  Good       (4σ capable)
    // >= 1.00  →  Marginal   (3σ capable)
    // < 1.00  →  Poor       (process not capable)
    // confidence_percent - at 3σ this is 99.73%, meaning 2.7 out of every 1000 assemblies may fall outside the calculated range. For most mechanical assemblies that's perfectly acceptable.

struct RSSResult
{
    double  upper_deviation     = 0.0;
    double  lower_deviation     = 0.0;
    double  total_band          = 0.0;

    double  nominal_closure     = 0.0;
    double  max_value           = 0.0;
    double  min_value           = 0.0;

    // Statistical metrics
    double  sigma_level         = 3.0;  // default 3σ
    std::optional<double> cpk;          // process capability index (if limits provided)
    double  confidence_percent  = 99.73; // at 3σ
};

// -------------------------------------------------------------------
//  StackUpResult
//  Full output of a stack-up analysis run
//  on a single ToleranceChain
// -------------------------------------------------------------------
struct StackUpResult
{
    // Source
    std::string         chain_id;       // links back to ToleranceChain::id
    std::string         chain_name;

    // Results
    WorstCaseResult     worst_case;
    RSSResult           rss;

    // Fit condition
    FitCondition        fit_condition   = FitCondition::Unknown;

    // Design limits (optional)
    // If the engineer specifies allowable limits,
    // we can assess whether the stack-up passes
    // If an engineer says "the clearance must be between 0.005mm and 0.050mm", those become design_limit_upper = 0.050 and design_limit_lower = 0.005. The calculator then checks whether min_value and max_value fall within those limits and sets worst_case_pass / rss_pass accordingly.
    std::optional<double> design_limit_upper;
    std::optional<double> design_limit_lower;

    // Pass/fail
    bool    worst_case_pass = false;    // within design limits?
    bool    rss_pass        = false;

    // Diagnostics
    // Which links contribute most to the stack-up
    // For a chain with 6 dimensions, one dimension might contribute 45% of the total stack. The engineer can immediately see where to focus tightening efforts. This drives the chain breakdown panel in the UI.
    struct ContributorEntry
    {
        std::string dimension_id;
        std::string dimension_label;
        double      contribution_percent = 0.0;  // % of total WC band
    };
    std::vector<ContributorEntry> top_contributors;

    std::string to_string() const;
};

// -------------------------------------------------------------------
//  StackUpReport
//  Results for all chains in an analysis run
// -------------------------------------------------------------------
struct StackUpReport
{
    std::string                 source_file;
    std::vector<StackUpResult>  results;

    std::size_t count()         const { return results.size(); }
    std::size_t pass_count_wc() const;
    std::size_t fail_count_wc() const;

    const StackUpResult* find_by_chain(const std::string& chain_id) const;

    // `to_json()` is the exit point of the entire C++ engine - it serialises everything into a JSON string that gets passed through the IPC bridge to the UI. The UI never touches C++ structs directly - it only ever sees JSON.
    std::string to_json() const;    // serialize for IPC bridge -> UI
};

} // namespace Application
