#pragma once

#include "DiagnosticReport.h"

namespace Teste_HW
{
namespace Diagnostics
{
	class HyperVWmiDiagnostics
	{
	public:
		static std::vector<DiagnosticResult> Run();
	};
}
}
