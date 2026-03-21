// EngineeringIntent is about understanding — taking the raw numbers and asking "what was the designer trying to achieve?"
// Consider these two dimensions on a drawing:
// D01:  Ø30  +/-0.500mm   -> loose tolerance
// D02:  Ø30  +/-0.006mm   -> very tight tolerance
// A human engineer looks at D02 and immediately thinks "this must be a bearing seat or a precision fit — the designer cared deeply about this dimension." They don't need to be told — they infer it from the tightness of the tolerance relative to everything else.
// That inference process is exactly what EngineeringIntent tries to capture and quantify.
#pragma once

#include "Dimension.h"
#include <string>
#include <vector>
#include <optional>

namespace Applicaion {

// -------------------------------------------------------------------
//  Design Pattern
//  Recognised mechanical design patterns
//  the intent engine can detect
//	These are the recognisable mechanical patterns the inference engine looks for. Each pattern has known characteristics.
//	I have explained more on the 7 Design Patterns in the file Mech_Design_Pattern.txt and a pictorial representation of the same in Mech_Design_Pattern.png
// -------------------------------------------------------------------
enum class DesignPattern
{
    BearingFit,             // shaft/bore with running or press fit
    ThreadedConnection,     // threaded feature with pitch tolerance
    KeyKeyway,              // key and keyway dimensional relationship
    AlignmentFeature,       // dowel pin, alignment bore, locating surface
    SealingFeature,         // O-ring groove, gasket seat
    PressFit,               // interference fit for retention
    SlidingFit,             // clearance fit for sliding motion
    Unknown
};

std::string design_pattern_to_string(DesignPattern dp);

// -------------------------------------------------------------------
//  IntentFlag
//	When the engine detects one of the above patterns, it creates an IntentFlag.
//  A single inferred engineering intent item
//  attached to one or more dimensions
// -------------------------------------------------------------------
    // Think of each `IntentFlag` as the engine saying:
	//	"I noticed something. Here's what I think it means, here's how confident I am, and here's which dimensions triggered it."
	//	A concrete example — the engine finds `D01` (Ø30 −0.013/+0.000) and `D02` (Ø30 +0.021/−0.000) on mating features:
	//	IntentFlag {
    //			description         = "H7/k6 transition fit — likely bearing journal"
    //			pattern             = BearingFit
    //			confidence          = 0.94
    //			confidence_reason   = "ISO fit codes match H7/k6, features are mating cylindrical surfaces"
    //			related_dims        = ["D01", "D02"]
    //			implied_criticality = High
    //			recommendation      = "Verify surface finish Ra ≤ 0.8μm for bearing seat"
}

//	confidence is a score from 0.0 to 1.0. It's calculated from a combination of factors:

//	- Do the tolerance values match a known ISO fit code?        +0.40
//	- Are the features geometrically mating (bore + shaft)?     +0.25
//	- Is the tolerance significantly tighter than average?      +0.20
//	- Is there a GD&T runout or position callout nearby?        +0.09
//	TOTAL CONFIDENCE                                             0.94

struct IntentFlag
{
    // What was inferred
    std::string         description;        // human-readable e.g. "Bearing running fit"
    DesignPattern       pattern             = DesignPattern::Unknown;

    // Confidence
    double              confidence          = 0.0;  // 0.0 – 1.0
    std::string         confidence_reason;          // why this score was assigned

    // Related dimensions
    std::vector<std::string> related_dimension_ids; // D01, D02 ...

    // Criticality driven by this intent
    CriticalityLevel    implied_criticality = CriticalityLevel::Unassessed;

    // Additional notes
    std::optional<std::string> recommendation;  // e.g. "Verify H7/k6 fit table"

    std::string to_string() const;
};

// -------------------------------------------------------------------
//  CriticalDimension
//  A dimension flagged as critical with
//  supporting reasoning
//	This is a summary view of criticality — pulled together from all the IntentFlag objects. A dimension gets flagged as critical when:

//	Its tolerance is significantly tighter than the assembly average
//	It appears in a high-confidence IntentFlag
//	It controls a fit condition (clearance or interference)
//	It has GD&T callouts referencing it
// -------------------------------------------------------------------
struct CriticalDimension
{
    std::string         dimension_id;
    std::string         dimension_label;
    CriticalityLevel    level               = CriticalityLevel::Unassessed;
    double              confidence          = 0.0;      // 0.0 – 1.0
    std::string         reason;                         // e.g. "Tightest tolerance in assembly"
    std::vector<std::string> contributing_intent_ids;   // which IntentFlags flagged this
};

// -------------------------------------------------------------------
//  EngineeringIntent
//  Full output of the intent inference engine
//  for one drawing or assembly
// -------------------------------------------------------------------
struct EngineeringIntent
{
    // Source
    std::string                     source_file;

    // Detected patterns
    std::vector<IntentFlag>         intent_flags;

    // Critical dimensions list
    std::vector<CriticalDimension>  critical_dimensions;

    // Overall assessment
    std::string                     summary;        // one-line overall assessment Eg : "Assembly contains 2 bearing fits, 1 keyed connection. 3 dimensions are critical. Axial stack-up is the primary risk."
    double                          overall_confidence = 0.0; // This is an aggregate score across all flags — gives a quick sense of how much the engine trusts its own analysis.

    // Helpers
    std::size_t intent_count()    const { return intent_flags.size(); }
    std::size_t critical_count()  const { return critical_dimensions.size(); }

    // Get critical dims at or above a threshold
	// Filters critical dimensions by confidence. The UI uses this to decide what to show — for example, only show dimensions with confidence > 0.75 in the main panel, and show everything in the detailed view.
    std::vector<const CriticalDimension*>
    get_critical_above(double confidence_threshold) const;

    // Get all flags for a specific design pattern
    std::vector<const IntentFlag*>
    get_flags_for_pattern(DesignPattern pattern) const;

    std::string to_json() const;    // serialize for IPC bridge → UI
};

} // namespace Application