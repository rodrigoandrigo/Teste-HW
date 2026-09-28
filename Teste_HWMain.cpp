#include "pch.h"
#include "Teste_HWMain.h"
#include "Common\DirectXHelper.h"

using namespace Teste_HW;
using namespace Windows::Foundation;
using namespace Windows::System::Threading;
using namespace Concurrency;

// Carrega e inicializa os ativos do aplicativo quando este é carregado.
Teste_HWMain::Teste_HWMain(const std::shared_ptr<DX::DeviceResources>& deviceResources) :
	m_deviceResources(deviceResources)
{
	// Registre-se para ser notificado se o Dispositivo for perdido ou recriado
	m_deviceResources->RegisterDeviceNotify(this);

	m_fpsTextRenderer = std::unique_ptr<SampleFpsTextRenderer>(new SampleFpsTextRenderer(m_deviceResources));

	// TODO: altere as configurações do temporizador se desejar outro modo além do TimeStep variável padrão.
	// Por exemplo: para lógica de atualização de TimeStep fixo de 60 FPS, chame:
	/*
	m_timer.SetFixedTimeStep(true);
	m_timer.SetTargetElapsedSeconds(1.0 / 60);
	*/
}

Teste_HWMain::~Teste_HWMain()
{
	// Cancele o registro de notificação de dispositivo
	m_deviceResources->RegisterDeviceNotify(nullptr);
}

// Atualiza o estado do aplicativo quando o tamanho da janela muda (por exemplo: alteração da orientação do dispositivo)
void Teste_HWMain::CreateWindowSizeDependentResources() 
{
}

void Teste_HWMain::StartRenderLoop()
{
	// Se o loop do renderizador de animação já estiver em execução, não inicie outro thread.
	if (m_renderLoopWorker != nullptr && m_renderLoopWorker->Status == AsyncStatus::Started)
	{
		return;
	}

	// Crie uma tarefa que será executada em um thread em segundo plano.
	auto workItemHandler = ref new WorkItemHandler([this](IAsyncAction ^ action)
	{
		// Calcule o quadro atualizado e o renderizador uma vez a cada intervalo de apagamento vertical.
		while (action->Status == AsyncStatus::Started)
		{
			critical_section::scoped_lock lock(m_criticalSection);
			Update();
			if (Render())
			{
				m_deviceResources->Present();
			}
		}
	});

	// Execute a tarefa em um thread em segundo plano de alta prioridade dedicado.
	m_renderLoopWorker = ThreadPool::RunAsync(workItemHandler, WorkItemPriority::High, WorkItemOptions::TimeSliced);
}

void Teste_HWMain::StopRenderLoop()
{
	m_renderLoopWorker->Cancel();
}

// Atualiza o estado do aplicativo uma vez por quadro.
void Teste_HWMain::Update() 
{
	// Atualize objetos de cena.
	m_timer.Tick([&]()
	{
		m_fpsTextRenderer->Update(m_timer);
	});
}

// Renderiza o quadro atual de acordo com o estado atual do aplicativo.
// Retorna true se o quadro foi renderizado e estiver pronto para ser exibido.
bool Teste_HWMain::Render() 
{
	// Não tente renderizar antes da primeira Atualização.
	if (m_timer.GetFrameCount() == 0)
	{
		return false;
	}

	auto context = m_deviceResources->GetD3DDeviceContext();

	// Redefina o visor como destino para toda a tela.
	auto viewport = m_deviceResources->GetScreenViewport();
	context->RSSetViewports(1, &viewport);

	// Redefina os destinos de renderização para a tela.
	ID3D11RenderTargetView *const targets[1] = { m_deviceResources->GetBackBufferRenderTargetView() };
	context->OMSetRenderTargets(1, targets, m_deviceResources->GetDepthStencilView());

	// Limpe o buffer de fundo e a exibição do estêncil de profundidade.
	context->ClearRenderTargetView(m_deviceResources->GetBackBufferRenderTargetView(), DirectX::Colors::CornflowerBlue);
	context->ClearDepthStencilView(m_deviceResources->GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	m_fpsTextRenderer->Render();

	return true;
}

// Notifica os renderizadores de que os recursos do dispositivo precisam ser liberados.
void Teste_HWMain::OnDeviceLost()
{
	m_fpsTextRenderer->ReleaseDeviceDependentResources();
}

// Notifica os renderizadores de que os recursos do dispositivo agora podem ser recriados.
void Teste_HWMain::OnDeviceRestored()
{
	m_fpsTextRenderer->CreateDeviceDependentResources();
	CreateWindowSizeDependentResources();
}
