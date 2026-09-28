#pragma once

#include "DiagnosticReport.h"

namespace Teste_HW
{
namespace Diagnostics
{
	class MemoryDiagnostics
	{
	public:
		static std::vector<DiagnosticResult> Run();
	};
}
}
