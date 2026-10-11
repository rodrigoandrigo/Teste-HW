//
// App.xaml.h
// Declaração da classe App.
//

#pragma once

#include "App.g.h"
#include "DirectXPage.xaml.h"

namespace Teste_HW
{
		/// <summary>
	/// Fornece o comportamento específico do aplicativo para complementar a classe Application padrão.
	/// </summary>
	ref class App sealed
	{
	public:
		App();
		virtual void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs^ e) override;

	private:
		void OnNavigationFailed(Platform::Object ^sender, Windows::UI::Xaml::Navigation::NavigationFailedEventArgs ^e);
	};
}
