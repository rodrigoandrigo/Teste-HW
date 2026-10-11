//
// DirectXPage.xaml.h
// Declaração da classe DirectXPage.
//

#pragma once

#include "DirectXPage.g.h"

#include "Diagnostics\CpuDiagnostics.h"
#include "Diagnostics\AmdVDiagnostics.h"
#include "Diagnostics\ComActivationDiagnostics.h"
#include "Diagnostics\DiagnosticReport.h"
#include "Diagnostics\DirectMlDiagnostics.h"
#include "Diagnostics\ExceptionDiagnostics.h"
#include "Diagnostics\FileSystemDiagnostics.h"
#include "Diagnostics\HyperVWmiDiagnostics.h"
#include "Diagnostics\KvmDiagnostics.h"
#include "Diagnostics\MemoryDiagnostics.h"
#include "Diagnostics\MappingDiagnostics.h"
#include "Diagnostics\TimerSyncDiagnostics.h"
#include "Diagnostics\ThreadDiagnostics.h"
#include "Diagnostics\VmComputeDiagnostics.h"
#include "Diagnostics\WhpxDiagnostics.h"
#include "Diagnostics\XessDiagnostics.h"
#include "Diagnostics\IntelLatencyFgDiagnostics.h"
#include "Samples/SampleAnimations.h"

namespace Teste_HW
{
	/// <summary>
	/// Página XAML para testes e relatórios de hardware.
	/// </summary>
	public ref class DirectXPage sealed
	{
	public:
		DirectXPage();

	private:
        void RunFgSample_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void RunLlSample_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void RunFsr3Sample_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void RunFsr3Upscale_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void RunSrSample_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void RunSampleAnimation(Samples::Animation animation);
        void AnimationOverlay_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
        void UpdateAnimationLayout();
        bool m_animationRunning = false;
		// Outros manipuladores de eventos.
		void RunCpuTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RunComActivationTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RunDirectMlTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RunXessTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void RunXellTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void RunXessFgTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void RunIntelLatencyFgTests(bool frameGeneration);
		void RunMemoryTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RunExceptionTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RunWhpxTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RunMappingTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RunTimerSyncTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RunThreadTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RunFileSystemTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ExportText_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ExportJson_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ClearResults_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RefreshReportView();
		void SetExportStatus(Windows::Storage::StorageFile^ file);
		void SetExportError(Platform::Exception^ exception);
		std::shared_ptr<Diagnostics::DiagnosticReport> m_diagnosticReport;
	};
}
