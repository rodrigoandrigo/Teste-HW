#pragma once

#include "Common\StepTimer.h"
#include "Common\DeviceResources.h"
#include "Content\SampleFpsTextRenderer.h"

// Renderiza conteúdo Direct2D e 3D na tela.
namespace Teste_HW
{
	class Teste_HWMain : public DX::IDeviceNotify
	{
	public:
		Teste_HWMain(const std::shared_ptr<DX::DeviceResources>& deviceResources);
		~Teste_HWMain();
		void CreateWindowSizeDependentResources();
		void StartRenderLoop();
		void StopRenderLoop();
		Concurrency::critical_section& GetCriticalSection() { return m_criticalSection; }

		// IDeviceNotify
		virtual void OnDeviceLost();
		virtual void OnDeviceRestored();

	private:
		void Update();
		bool Render();

		// Ponteiro armazenado em cache para recursos de dispositivo.
		std::shared_ptr<DX::DeviceResources> m_deviceResources;

		std::unique_ptr<SampleFpsTextRenderer> m_fpsTextRenderer;

		Windows::Foundation::IAsyncAction^ m_renderLoopWorker;
		Concurrency::critical_section m_criticalSection;

		// Temporizador do loop de renderização.
		DX::StepTimer m_timer;
	};
}
