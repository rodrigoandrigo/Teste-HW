#pragma once
#include "DiagnosticReport.h"

namespace Teste_HW { namespace Diagnostics {
std::mutex& IntelSdkRuntimeMutex();
class IntelLatencyFgDiagnostics
{
public:
    static std::vector<DiagnosticResult> RunLowLatency();
    static std::vector<DiagnosticResult> RunFrameGeneration();
};
} }
