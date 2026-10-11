#pragma once
#include "DiagnosticReport.h"

namespace Teste_HW { namespace Diagnostics {
class IntelLatencyFgDiagnostics
{
public:
    static std::vector<DiagnosticResult> RunLowLatency();
    static std::vector<DiagnosticResult> RunFrameGeneration();
};
} }
