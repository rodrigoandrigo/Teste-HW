#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <ppltasks.h>

namespace Teste_HW
{
namespace Diagnostics
{
	enum class DiagnosticStatus
	{
		Pass,
		Fail,
		Blocked,
		Skipped,
		Inconclusive
	};

	struct DiagnosticResult
	{
		std::wstring category;
		std::wstring name;
		DiagnosticStatus status = DiagnosticStatus::Inconclusive;
		std::wstring summary;
		std::wstring details;
		std::wstring timestampUtc;
		uint64_t durationMicroseconds = 0;
		bool hasHresult = false;
		HRESULT hresult = S_OK;
		bool hasWin32Error = false;
		DWORD win32Error = ERROR_SUCCESS;
	};

	class DiagnosticReport
	{
	public:
		void Add(DiagnosticResult result);
		void Clear();
		std::vector<DiagnosticResult> Snapshot() const;
		size_t Count() const;

	private:
		mutable std::mutex m_mutex;
		std::vector<DiagnosticResult> m_results;
	};

	class DiagnosticReportSerializer
	{
	public:
		static std::wstring ToText(const std::vector<DiagnosticResult>& results);
		static std::wstring ToJson(const std::vector<DiagnosticResult>& results);
		static std::wstring StatusName(DiagnosticStatus status);
		static std::wstring UtcNowIso8601();
	};

	class DiagnosticReportExporter
	{
	public:
		static concurrency::task<Windows::Storage::StorageFile^> ExportTextAsync(
			const std::vector<DiagnosticResult>& results);
		static concurrency::task<Windows::Storage::StorageFile^> ExportJsonAsync(
			const std::vector<DiagnosticResult>& results);

	private:
		static concurrency::task<Windows::Storage::StorageFile^> ExportAsync(
			const std::wstring& extension,
			const std::wstring& contents);
	};
}
}
