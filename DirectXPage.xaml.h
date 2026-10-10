//
// DirectXPage.xaml.h
// Declaração da classe DirectXPage.
//

#pragma once

#include "DirectXPage.g.h"

#include "Common\DeviceResources.h"
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
#include "Teste_HWMain.h"

namespace Teste_HW
{
	/// <summary>
	/// Uma página que hospeda um SwapChainPanel do DirectX.
	/// </summary>
	public ref class DirectXPage sealed
	{
	public:
		DirectXPage();
		virtual ~DirectXPage();

		void SaveInternalState(Windows::Foundation::Collections::IPropertySet^ state);
		void LoadInternalState(Windows::Foundation::Collections::IPropertySet^ state);

	private:
		// Manipulador de eventos de renderização de baixo nível XAML.
		void OnRendering(Platform::Object^ sender, Platform::Object^ args);

		// Manipuladores de eventos da janela.
		void OnVisibilityChanged(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::VisibilityChangedEventArgs^ args);

		// Manipuladores de eventos DisplayInformation.
		void OnDpiChanged(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);
		void OnOrientationChanged(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);
		void OnDisplayContentsInvalidated(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);

		// Outros manipuladores de eventos.
		void RunCpuTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RunComActivationTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RunDirectMlTests_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
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
		void OnCompositionScaleChanged(Windows::UI::Xaml::Controls::SwapChainPanel^ sender, Object^ args);
		void OnSwapChainPanelSizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);

		// Recursos usados para renderizar o conteúdo do DirectX na tela de fundo da página XAML.
		std::shared_ptr<DX::DeviceResources> m_deviceResources;
		std::unique_ptr<Teste_HWMain> m_main; 
		std::shared_ptr<Diagnostics::DiagnosticReport> m_diagnosticReport;
		bool m_windowVisible;
	};
}

