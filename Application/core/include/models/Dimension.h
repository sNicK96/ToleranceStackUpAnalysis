// Every single number you see on an engineering drawing becomes one Dimension object in our system.
#pragma once

#include "Tolerance.h"
#include <string>
#include <optional>
#include <vector>

namespace Application {

// --------------------------------------------------------------------
//  Dimension Type
//  What kind of measurement this dimension is
//  The DXF parser will set this based on the entity type it reads
// --------------------------------------------------------------------
enum class DimensionType
{
    Linear,         // straight-line distance
    Diameter,       // circular diameter (Ø)
    Radius,         // radius
    Angular,        // angle in degrees
    GDT             // purely a GD&T control (no nominal)
};

// --------------------------------------------------------------------
//  Chain Direction
//  Whether this dimension adds or subtracts
//  when traversed in a tolerance chain
//  Let's take an example of a shaft sitting inside a houseing :
//  Clearance = Housing Bore (additive) - Shaft Diameter (substractive)
// --------------------------------------------------------------------
enum class ChainDirection
{
    Additive,       // +  dimension adds to the stack
    Subtractive,    // -  dimension subtracts from the stack
    Undefined       // not yet assigned (before chain analysis)
};

// --------------------------------------------------------------------
//  Criticality Level
//  How significant this dimension is to
//  assembly function — inferred by IntentEngine
//  This will be helpful in the UI with RED for High, ORANGE for Medium, GREEN for Low
// --------------------------------------------------------------------
enum class CriticalityLevel
{
    High,
    Medium,
    Low,
    Unassessed
};

// --------------------------------------------------------------------
//  Feature Reference
//  Identifies a geometric feature on a part
//  (used to build the dimensional relationship graph)
//  Consider this Graph as an example:    shaft::shoulder --D03---> shaft::journal --D01---> housing::bore
// --------------------------------------------------------------------
struct FeatureRef
{
    std::string part_id;        // e.g. "shaft", "housing"
    std::string feature_id;     // e.g. "journal_dia", "bore_dia"

    bool operator==(const FeatureRef& other) const  // This will be used to compare equality between 2 objects of type FeatureRef
    {
        return part_id == other.part_id && feature_id == other.feature_id;
    }
};

// --------------------------------------------------------------------
//  Dimension
//  The core data unit — one dimension extracted
//  from a drawing with its full tolerance info
// --------------------------------------------------------------------
struct Dimension
{
    // Identity - The actual measurement
    std::string     id;                         // unique within a drawing e.g. "D01"
    std::string     label;                      // human-readable e.g. "Shaft journal diameter"
    DimensionType   dim_type    = DimensionType::Linear;

    // Nominal value is optional because pure GD&T entries like circularity have no nominal value - they just have a tolerance zone.
    std::optional<double> nominal;              // the number on the drawing e.g. 30.0

    // the +/- part uses our Tolerance struct
    Tolerance       tolerance;

    // Chain info set by ChainFinder
    ChainDirection  direction   = ChainDirection::Undefined;

    // Feature connectivity
    // The two features this dimension connects
    // Used by ChainFinder to build the graph
    std::optional<FeatureRef> feature_from;
    std::optional<FeatureRef> feature_to;

    // Criticality set by IntentInference
    CriticalityLevel criticality = CriticalityLevel::Unassessed;

    // Source traceability
    //  when the analysis flags a dimension as critical, the engineer can trace it back to the exact location in the original drawing.
    std::string     source_file;                // which drawing file Eg. : "shaft_assembly.dxf"
    int             source_line  = -1;          // line/entity in file (for DXF)
    int             source_page  = -1;          // page number (for PDF)

    // Helpers

    bool has_nominal()  const { return nominal.has_value(); }
    bool is_gdt()       const { return dim_type == DimensionType::GDT; }

    // Returns the signed contribution to a stack-up
    // Additive dims contribute +tolerance, subtractive dims contribute -nominal
    // This is used by the stack-up calculator when computing nominal closure - the sum of all nominals in a chain with their correct signs.
    // A closure of zero means the chain is balanced - which is expected for a mating fit.
    // For the shaft/housing example: +30.0 (shaft, additive) + (-30.0) (housing, subtractive) = 0.0 closure
    double signed_nominal() const
    {
        if (!nominal.has_value()) return 0.0;
        return (direction == ChainDirection::Subtractive)
            ? -nominal.value()
            :  nominal.value();
    }

    std::string to_string() const;
};

// --------------------------------------------------------------------
//  DimensionSet
//  Collection of all dimensions extracted
//  from a single drawing file with some convenience lookup methods. The `DxfParser` will return one of these.
// --------------------------------------------------------------------
struct DimensionSet
{
    std::string             source_file;
    std::vector<Dimension>  dimensions;

    // Convenience lookups
    const Dimension* find_by_id(const std::string& id) const;
    std::vector<const Dimension*> find_by_criticality(CriticalityLevel level) const;
    std::vector<const Dimension*> find_gdt() const;

    std::size_t count() const { return dimensions.size(); }
};

} // namespace Application