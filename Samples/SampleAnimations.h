#pragma once
#include "../Diagnostics/DiagnosticReport.h"
namespace Teste_HW { namespace Samples {
enum class Animation { FrameGeneration, LowLatency, SuperResolution };
class SampleAnimations
{
public:
    static std::vector<Diagnostics::DiagnosticResult> Run(Animation animation,
        Windows::UI::Xaml::Controls::SwapChainPanel^ panel, HWND window, HRESULT windowResult,
        const std::wstring& tracePath);
    static void Begin();
    static bool IsCancelled();
    static void Cancel();
    static void BlockAfterNativeFault();
};
} }
