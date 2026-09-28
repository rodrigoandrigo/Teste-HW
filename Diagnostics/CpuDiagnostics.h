#pragma once

#include "DiagnosticReport.h"

namespace Teste_HW
{
namespace Diagnostics
{
	class CpuDiagnostics
	{
	public:
		static std::vector<DiagnosticResult> Run();
	};
}
}
