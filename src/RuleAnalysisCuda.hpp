// Optional CUDA implementation. Include/link this only when CUDA is enabled.
#ifndef RuleAnalysisCuda_hpp
#define RuleAnalysisCuda_hpp

#include "RuleAnalysis.hpp"

namespace RuleAnalysis
{

// Checks the current CUDA device, including whether this translation unit's
// kernel image can be loaded. Does not select a different device.
[[nodiscard]] bool CudaAvailable(std::string& Reason);

struct CudaOptions
{
    // 12 bits/target and 8 probes yield about 0.3% false positives.
    // False positives are always verified against the exact CPU target index.
    size_t bloomBitsPerTarget = 12;
};

// Requires a finalized TargetLookup; uses its read-only Bucket accessor.
// Inputs, a Bloom filter and supported instructions remain device-resident.
// The exact 128-bit target index stays on the CPU; all potential hits are verified.
// Memory/class commands, saved-position arguments, malformed rules and unknown
// commands use CPU Analyze for the entire rule, with counts reported to stderr.
// Qualification statistics cover exactly the CPU-equivalent input prefix, even
// though the device can speculatively evaluate the rest of the final chunk
// and one lookahead chunk. Only potential hits and aggregate counters download.
// Returns false on CUDA/resource/argument errors; Error describes the failure.
// In that case statistics/progress can be partially updated; do not combine a
// failed attempt with a CPU retry without resetting the progress counters.
// No automatic CPU retry on a device error or insufficient device memory.
[[nodiscard]] bool AnalyzeCuda(
    std::span<const std::string> Inputs,
    const TargetLookup& Targets,
    std::span<RuleStatistic> Statistics,
    size_t CpuThreads,
    AnalysisProgress* Progress,
    std::optional<size_t> QualificationBar,
    std::string& Error,
    const CudaOptions& Options = {}
);

}

#endif