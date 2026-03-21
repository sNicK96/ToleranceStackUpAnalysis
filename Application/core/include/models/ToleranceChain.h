// ToleranceChain is the ordered sequence of those measurements that connects two features through an assembly.

// Housing left face
//       │
//       │ D04: Housing bore depth (170mm ±0.040)   [subtractive]
//       │
//       ▼
// Housing bore centre
//       │
//       │ D02: Housing bore diameter (Ø30 +0.021/-0.000)  [subtractive]
//       │
//       ▼
// Shaft journal surface
//       │
//       │ D01: Shaft journal diameter (Ø30 +0.000/-0.013)  [additive]
//       │
//       ▼
// Shaft shoulder
//       │
//       │ D03: Shaft shoulder length (180mm ±0.025)  [additive]
//       │
//       ▼
// Shaft end face
// That entire path is one ToleranceChain. Each step in the path is a ChainLink.

#pragma once

#include "Dimension.h"
#include <vector>
#include <string>

namespace Application {

// --------------------------------------------------------------------
//  ChainLink
//  One step in a tolerance chain.
//  Wraps a Dimension with its direction
//  as resolved during chain traversal.
// --------------------------------------------------------------------
struct ChainLink
{
    Dimension       dimension;          // full dimension data
    ChainDirection  direction;          // resolved direction in THIS chain
                                        // (may differ per chain if dim is shared)
   										// the same Dimension can appear in multiple chains with different directions depending on which way you traverse it. Storing direction on the link keeps the original Dimension clean and reusable.
    int             sequence_index = 0; // position in the chain (0-based) Eg : 0, 1, 2, 3...

    // Contribution of this link to the stack-up
	// These are what the calculator calls on each link when summing up the stack.
    double upper_contribution() const { return dimension.tolerance.upper; }
    double lower_contribution() const { return dimension.tolerance.lower; }

    // Signed nominal contribution
    double nominal_contribution() const
    {
        if (!dimension.nominal.has_value()) return 0.0;
        return (direction == ChainDirection::Subtractive)
            ? -dimension.nominal.value()
            :  dimension.nominal.value();
    }
};

// --------------------------------------------------------------------
//  ToleranceChain
//  An ordered sequence of ChainLinks that
//  connect two features through an assembly.
//  Multiple chains can exist between the same
//  two features via different paths.
// --------------------------------------------------------------------
struct ToleranceChain
{
    // Identity
    std::string     id;                     // e.g. "C01"
    std::string     name;                   // e.g. "Shaft → Housing"

    // Endpoints - where the chain starts and ends
    FeatureRef      start_feature;	// e.g. shaft::shoulder
    FeatureRef      end_feature;	// e.g. housing::bore

    // Links - ordered sequence of steps
    std::vector<ChainLink> links;

    // Metadata
    // closed loop chain — rare but valid
    // A closed loop chain is one where start and end are the same feature — used when analysing things like a bolted joint where you trace all the way around and come back to the start. For most analyses, chains are open, i.e, ( start != end )
    bool            is_closed   = false;    // closed loop chain
    std::string     description;

    // Helpers
    std::size_t length() const { return links.size(); }
    bool        empty()  const { return links.empty(); }

    // Sum of nominal values (with sign)
	// A non-zero closure means there's a designed-in offset - like a press fit where the shaft is intentionally larger.
	// For a balanced fit like Ø30 H7/k6:
    // +30.0 (shaft) - 30.0 (housing) = 0.0
    double nominal_closure() const
    {
        double sum = 0.0;
        for (const auto& link : links)
            sum += link.nominal_contribution();
        return sum;
    }

    // Worst-case upper deviation (sum of all upper tols)
	// This is the arithmetic sum - assumes every part in the chain is simultaneously at its worst extreme.

    // WC upper = 0.000 + 0.021 = 0.021mm  (max clearance)
    double worst_case_upper() const
    {
        double sum = 0.0;
        for (const auto& link : links)
            sum += link.upper_contribution();
        return sum;
    }

    // Worst-case lower deviation (sum of all lower tols)
    // WC lower = 0.013 + 0.000 = 0.013mm  (min clearance)
    double worst_case_lower() const
    {
        double sum = 0.0;
        for (const auto& link : links)
            sum += link.lower_contribution();
        return sum;
    }

    // RSS upper (root sum of squares of upper tols)
    // This is the statistical method - assumes tolerances are normally distributed and won't all hit their extremes simultaneously. Less conservative, but realistic for production. Covers ~99.73% of assemblies at 3σ.

	// RSS upper = √(0.000² + 0.021²) = 0.021mm
    double rss_upper() const
    {
        double sum_sq = 0.0;
        for (const auto& link : links)
            sum_sq += link.upper_contribution() * link.upper_contribution();
        return std::sqrt(sum_sq);
    }

    // RSS lower
	// RSS lower = √(0.013² + 0.000²) = 0.013mm
    double rss_lower() const
    {
        double sum_sq = 0.0;
        for (const auto& link : links)
            sum_sq += link.lower_contribution() * link.lower_contribution();
        return std::sqrt(sum_sq);
    }
    // NOTE : With only 2 links the difference is small. With 6+ links the RSS result is significantly tighter than worst-case - that's where it becomes valuable.

    std::string to_string() const;
};

// --------------------------------------------------------------------
//  ToleranceChainSet
//  All chains found in an assembly analysis
// --------------------------------------------------------------------
struct ToleranceChainSet
{
    std::vector<ToleranceChain> chains;

    std::size_t count() const { return chains.size(); }

    const ToleranceChain* find_by_id(const std::string& id) const;

    // All chains between two specific features
	// particularly useful — when the `ChainFinder` discovers multiple paths between the same two features, this lets the engineer compare all of them side by side in the UI.
    std::vector<const ToleranceChain*> find_between(
        const FeatureRef& from,
        const FeatureRef& to) const;
};

} // namespace Application