#pragma once

#include "DiagnosticReport.h"

namespace Teste_HW
{
namespace Diagnostics
{
	class KvmDiagnostics
	{
	public:
		static std::vector<DiagnosticResult> Run();
	};
}
}
