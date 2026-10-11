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

DirectXPage::DirectXPage()
{
	InitializeComponent();

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

void DirectXPage::RunXessTests_Click(Object^ sender, RoutedEventArgs^ e)
{
	reportStatusText->Text = L"Executando testes Intel XeSS-SR em background...";
	concurrency::create_task([]()
	{
		return Diagnostics::XessDiagnostics::Run();
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
			reportStatusText->Text = L"Falha ao executar os testes XeSS: " + exception->Message;
		}
		catch (...)
		{
			reportStatusText->Text = L"Falha nativa inesperada durante os testes XeSS.";
		}
	}, concurrency::task_continuation_context::use_current());
}

void DirectXPage::RunXellTests_Click(Object^ sender, RoutedEventArgs^ e)
{
    RunIntelLatencyFgTests(false);
}

void DirectXPage::RunXessFgTests_Click(Object^ sender, RoutedEventArgs^ e)
{
    RunIntelLatencyFgTests(true);
}

void DirectXPage::RunIntelLatencyFgTests(bool frameGeneration)
{
    reportStatusText->Text = L"Executando testes Intel XeLL/XeSS-FG em background...";
    concurrency::create_task([frameGeneration]()
    {
        return frameGeneration ? Diagnostics::IntelLatencyFgDiagnostics::RunFrameGeneration() :
            Diagnostics::IntelLatencyFgDiagnostics::RunLowLatency();
    }).then([this](concurrency::task<std::vector<Diagnostics::DiagnosticResult>> completed)
    {
        try
        {
            auto results = completed.get();
            for (auto& result : results) m_diagnosticReport->Add(std::move(result));
            RefreshReportView();
        }
        catch (Platform::Exception^ exception)
        {
            reportStatusText->Text = L"Falha nos testes XeLL/XeSS-FG: " + exception->Message;
        }
        catch (...)
        {
            reportStatusText->Text = L"Falha nativa inesperada nos testes XeLL/XeSS-FG.";
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
	reportStatusText->Text = L"Executando WHPX, AMD-V, KVM, vmcompute e Hyper-V WMI em background...";
	concurrency::create_task([]()
	{
		std::vector<Diagnostics::DiagnosticResult> combined;
		auto append = [&combined](std::vector<Diagnostics::DiagnosticResult>&& source)
		{
			for (auto& result : source)
				combined.push_back(std::move(result));
		};
		append(Diagnostics::WhpxDiagnostics::Run());
		append(Diagnostics::AmdVDiagnostics::Run());
		append(Diagnostics::KvmDiagnostics::Run());
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
