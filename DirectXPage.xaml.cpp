//
// DirectXPage.xaml.cpp
// Implementação da classe DirectXPage.
//

#include "pch.h"
#include "DirectXPage.xaml.h"

using namespace Teste_HW;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Graphics::Display;
using namespace Windows::System::Threading;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace concurrency;

DirectXPage::DirectXPage():
	m_windowVisible(true)
{
	InitializeComponent();

	// Registre manipuladores de eventos para o ciclo de vida da página.
	CoreWindow^ window = Window::Current->CoreWindow;

	window->VisibilityChanged +=
		ref new TypedEventHandler<CoreWindow^, VisibilityChangedEventArgs^>(this, &DirectXPage::OnVisibilityChanged);

	DisplayInformation^ currentDisplayInformation = DisplayInformation::GetForCurrentView();

	currentDisplayInformation->DpiChanged +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &DirectXPage::OnDpiChanged);

	currentDisplayInformation->OrientationChanged +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &DirectXPage::OnOrientationChanged);

	DisplayInformation::DisplayContentsInvalidated +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &DirectXPage::OnDisplayContentsInvalidated);

	swapChainPanel->CompositionScaleChanged += 
		ref new TypedEventHandler<SwapChainPanel^, Object^>(this, &DirectXPage::OnCompositionScaleChanged);

	swapChainPanel->SizeChanged +=
		ref new SizeChangedEventHandler(this, &DirectXPage::OnSwapChainPanelSizeChanged);

	// Neste ponto, temos acesso ao dispositivo. 
	// É possível criar recursos dependentes do dispositivo.
	m_deviceResources = std::make_shared<DX::DeviceResources>();
	m_deviceResources->SetSwapChainPanel(swapChainPanel);

	m_main = std::unique_ptr<Teste_HWMain>(new Teste_HWMain(m_deviceResources));
	m_main->StartRenderLoop();

	m_diagnosticReport = std::make_shared<Diagnostics::DiagnosticReport>();
	Diagnostics::DiagnosticResult initialized;
	initialized.category = L"Infrastructure";
	initialized.name = L"Result collection and export";
	initialized.status = Diagnostics::DiagnosticStatus::Pass;
	initialized.summary = L"Thread-safe result collection and TXT/JSON serializers initialized.";
	initialized.details = L"Future hardware tests can record status, duration, HRESULT and Win32 error in this report.";
	m_diagnosticReport->Add(std::move(initialized));
	RefreshReportView();
}

DirectXPage::~DirectXPage()
{
	// Interrompa a renderização em destruição.
	m_main->StopRenderLoop();
}

// Salva o estado atual do aplicativo para eventos de suspensão e de encerramento.
void DirectXPage::SaveInternalState(IPropertySet^ state)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->Trim();

	// Interrompa a renderização quando o aplicativo for suspenso.
	m_main->StopRenderLoop();

	// Coloque aqui o código para salvar o estado do aplicativo.
}

// Carrega o estado atual do aplicativo para eventos de retomada.
void DirectXPage::LoadInternalState(IPropertySet^ state)
{
	// Coloque aqui o código para carregar o estado do aplicativo.

	// Inicie a renderização quando o aplicativo for retomado.
	m_main->StartRenderLoop();
}

// Manipuladores de eventos da janela.

void DirectXPage::OnVisibilityChanged(CoreWindow^ sender, VisibilityChangedEventArgs^ args)
{
	m_windowVisible = args->Visible;
	if (m_windowVisible)
	{
		m_main->StartRenderLoop();
	}
	else
	{
		m_main->StopRenderLoop();
	}
}

// Manipuladores de eventos DisplayInformation.

void DirectXPage::OnDpiChanged(DisplayInformation^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	// Observação: o valor de LogicalDpi recuperado aqui pode não corresponder à DPI efetiva do aplicativo
	// se ele está sendo escalado para dispositivos de alta resolução. Depois que o DPI é definido em DeviceResources,
	// você sempre deve recuperá-la usando o método GetDpi.
	// Consulte DeviceResources.cpp para obter mais detalhes.
	m_deviceResources->SetDpi(sender->LogicalDpi);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::OnOrientationChanged(DisplayInformation^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetCurrentOrientation(sender->CurrentOrientation);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::OnDisplayContentsInvalidated(DisplayInformation^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->ValidateDevice();
}

void DirectXPage::RefreshReportView()
{
	auto snapshot = m_diagnosticReport->Snapshot();
	auto text = Diagnostics::DiagnosticReportSerializer::ToText(snapshot);
	reportTextBox->Text = ref new String(text.c_str());
	auto status = std::wstring(L"Resultados registrados: ") + std::to_wstring(snapshot.size());
	reportStatusText->Text = ref new String(status.c_str());
}

void DirectXPage::SetExportStatus(Windows::Storage::StorageFile^ file)
{
	reportStatusText->Text = L"Relatório salvo em: " + file->Path;
}

void DirectXPage::SetExportError(Platform::Exception^ exception)
{
	wchar_t code[16] = {};
	swprintf_s(code, L"0x%08X", static_cast<unsigned int>(exception->HResult));
	reportStatusText->Text = L"Falha ao exportar (" + ref new String(code) + L"): " + exception->Message;
}

void DirectXPage::RunCpuTests_Click(Object^ sender, RoutedEventArgs^ e)
{
	reportStatusText->Text = L"Executando testes de CPU e instruções...";
	auto results = Diagnostics::CpuDiagnostics::Run();
	for (auto& result : results)
	{
		m_diagnosticReport->Add(std::move(result));
	}
	RefreshReportView();
}

void DirectXPage::RunComActivationTests_Click(Object^ sender, RoutedEventArgs^ e)
{
	reportStatusText->Text = L"Executando matriz controlada de ativação COM e WinRT em background...";
	concurrency::create_task([]()
	{
		return Diagnostics::ComActivationDiagnostics::Run();
	}).then([this](concurrency::task<std::vector<Diagnostics::DiagnosticResult>> completed)
	{
		try
		{
			auto results = completed.get();
			for (auto& result : results)
				m_diagnosticReport->Add(std::move(result));
			RefreshReportView();
		}
		catch (Platform::Exception^ exception)
		{
			reportStatusText->Text = L"Falha ao executar a matriz COM/API: " + exception->Message;
		}
		catch (...)
		{
			reportStatusText->Text = L"Falha nativa inesperada durante a matriz COM/API.";
		}
	}, concurrency::task_continuation_context::use_current());
}

void DirectXPage::RunDirectMlTests_Click(Object^ sender, RoutedEventArgs^ e)
{
	reportStatusText->Text = L"Executando testes D3D12 e DirectML em background...";
	concurrency::create_task([]()
	{
		return Diagnostics::DirectMlDiagnostics::Run();
	}).then([this](concurrency::task<std::vector<Diagnostics::DiagnosticResult>> completed)
	{
		try
		{
			auto results = completed.get();
			for (auto& result : results)
				m_diagnosticReport->Add(std::move(result));
			RefreshReportView();
		}
		catch (Platform::Exception^ exception)
		{
			reportStatusText->Text = L"Falha ao executar os testes DirectML: " + exception->Message;
		}
		catch (...)
		{
			reportStatusText->Text = L"Falha nativa inesperada durante os testes DirectML.";
		}
	}, concurrency::task_continuation_context::use_current());
}

void DirectXPage::RunMemoryTests_Click(Object^ sender, RoutedEventArgs^ e)
{
	reportStatusText->Text = L"Executando testes de memória virtual e JIT...";
	auto results = Diagnostics::MemoryDiagnostics::Run();
	for (auto& result : results)
	{
		m_diagnosticReport->Add(std::move(result));
	}
	RefreshReportView();
}

void DirectXPage::RunExceptionTests_Click(Object^ sender, RoutedEventArgs^ e)
{
	reportStatusText->Text = L"Executando testes de exceções e traps...";
	auto results = Diagnostics::ExceptionDiagnostics::Run();
	for (auto& result : results)
	{
		m_diagnosticReport->Add(std::move(result));
	}
	RefreshReportView();
}

void DirectXPage::RunWhpxTests_Click(Object^ sender, RoutedEventArgs^ e)
{
	reportStatusText->Text = L"Executando WHPX, vmcompute e Hyper-V WMI em background...";
	concurrency::create_task([]()
	{
		std::vector<Diagnostics::DiagnosticResult> combined;
		auto append = [&combined](std::vector<Diagnostics::DiagnosticResult>&& source)
		{
			for (auto& result : source)
				combined.push_back(std::move(result));
		};
		append(Diagnostics::WhpxDiagnostics::Run());
		append(Diagnostics::VmComputeDiagnostics::Run());
		append(Diagnostics::HyperVWmiDiagnostics::Run());
		return combined;
	}).then([this](concurrency::task<std::vector<Diagnostics::DiagnosticResult>> completed)
	{
		try
		{
			auto results = completed.get();
			for (auto& result : results)
				m_diagnosticReport->Add(std::move(result));
			RefreshReportView();
		}
		catch (Platform::Exception^ exception)
		{
			reportStatusText->Text = L"Falha ao executar testes de virtualização: " + exception->Message;
		}
	}, concurrency::task_continuation_context::use_current());
}

void DirectXPage::RunMappingTests_Click(Object^ sender, RoutedEventArgs^ e)
{
	reportStatusText->Text = L"Executando testes de mappings...";
	auto results = Diagnostics::MappingDiagnostics::Run();
	for (auto& result : results)
	{
		m_diagnosticReport->Add(std::move(result));
	}
	RefreshReportView();
}

void DirectXPage::RunTimerSyncTests_Click(Object^ sender, RoutedEventArgs^ e)
{
	reportStatusText->Text = L"Executando testes de timers e sincronização...";
	auto results = Diagnostics::TimerSyncDiagnostics::Run();
	for (auto& result : results)
	{
		m_diagnosticReport->Add(std::move(result));
	}
	RefreshReportView();
}

void DirectXPage::RunThreadTests_Click(Object^ sender, RoutedEventArgs^ e)
{
	reportStatusText->Text = L"Executando capacidade de threads, afinidade, prioridades e SMP em background...";
	concurrency::create_task([]()
	{
		return Diagnostics::ThreadDiagnostics::Run();
	}).then([this](concurrency::task<std::vector<Diagnostics::DiagnosticResult>> completed)
	{
		try
		{
			auto results = completed.get();
			for (auto& result : results)
				m_diagnosticReport->Add(std::move(result));
			RefreshReportView();
		}
		catch (Platform::Exception^ exception)
		{
			reportStatusText->Text = L"Falha ao executar testes de threads/SMP: " + exception->Message;
		}
		catch (...)
		{
			reportStatusText->Text = L"Falha nativa inesperada durante os testes de threads/SMP.";
		}
	}, concurrency::task_continuation_context::use_current());
}

void DirectXPage::RunFileSystemTests_Click(Object^ sender, RoutedEventArgs^ e)
{
	reportStatusText->Text = L"Executando testes de filesystem e I/O em background...";
	concurrency::create_task([]()
	{
		return Diagnostics::FileSystemDiagnostics::Run();
	}).then([this](concurrency::task<std::vector<Diagnostics::DiagnosticResult>> completed)
	{
		try
		{
			auto results = completed.get();
			for (auto& result : results)
			{
				m_diagnosticReport->Add(std::move(result));
			}
			RefreshReportView();
		}
		catch (Platform::Exception^ exception)
		{
			SetExportError(exception);
		}
		catch (...)
		{
			reportStatusText->Text = L"Falha nativa inesperada durante os testes de filesystem/I/O.";
		}
	}, concurrency::task_continuation_context::use_current());
}

void DirectXPage::ExportText_Click(Object^ sender, RoutedEventArgs^ e)
{
	reportStatusText->Text = L"Exportando TXT...";
	Diagnostics::DiagnosticReportExporter::ExportTextAsync(m_diagnosticReport->Snapshot())
		.then([this](Windows::Storage::StorageFile^ file) { SetExportStatus(file); }, concurrency::task_continuation_context::use_current())
		.then([this](concurrency::task<void> completion)
		{
			try { completion.get(); }
			catch (Platform::Exception^ exception) { SetExportError(exception); }
		});
}

void DirectXPage::ExportJson_Click(Object^ sender, RoutedEventArgs^ e)
{
	reportStatusText->Text = L"Exportando JSON...";
	Diagnostics::DiagnosticReportExporter::ExportJsonAsync(m_diagnosticReport->Snapshot())
		.then([this](Windows::Storage::StorageFile^ file) { SetExportStatus(file); }, concurrency::task_continuation_context::use_current())
		.then([this](concurrency::task<void> completion)
		{
			try { completion.get(); }
			catch (Platform::Exception^ exception) { SetExportError(exception); }
		});
}

void DirectXPage::ClearResults_Click(Object^ sender, RoutedEventArgs^ e)
{
	m_diagnosticReport->Clear();
	RefreshReportView();
}

void DirectXPage::OnCompositionScaleChanged(SwapChainPanel^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetCompositionScale(sender->CompositionScaleX, sender->CompositionScaleY);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::OnSwapChainPanelSizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetLogicalSize(e->NewSize);
	m_main->CreateWindowSizeDependentResources();
}
