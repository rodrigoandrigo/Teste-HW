#include "pch.h"
#include "DiagnosticReport.h"

#include <iomanip>
#include <sstream>

using namespace concurrency;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;

namespace
{
	std::wstring EscapeJson(const std::wstring& value)
	{
		std::wostringstream output;
		for (wchar_t character : value)
		{
			switch (character)
			{
			case L'"': output << L"\\\""; break;
			case L'\\': output << L"\\\\"; break;
			case L'\b': output << L"\\b"; break;
			case L'\f': output << L"\\f"; break;
			case L'\n': output << L"\\n"; break;
			case L'\r': output << L"\\r"; break;
			case L'\t': output << L"\\t"; break;
			default:
				if (character < 0x20)
				{
					output << L"\\u" << std::hex << std::setw(4) << std::setfill(L'0')
						<< static_cast<unsigned int>(character) << std::dec;
				}
				else
				{
					output << character;
				}
				break;
			}
		}
		return output.str();
	}

	std::wstring Hex32(uint32_t value)
	{
		std::wostringstream output;
		output << L"0x" << std::uppercase << std::hex << std::setw(8)
			<< std::setfill(L'0') << value;
		return output.str();
	}

	std::wstring ExportFileName(const std::wstring& extension)
	{
		SYSTEMTIME now = {};
		GetSystemTime(&now);
		wchar_t name[96] = {};
		swprintf_s(
			name,
			L"Teste-HW_%04u%02u%02u_%02u%02u%02u_UTC.%s",
			now.wYear,
			now.wMonth,
			now.wDay,
			now.wHour,
			now.wMinute,
			now.wSecond,
			extension.c_str());
		return name;
	}
}

namespace Teste_HW
{
namespace Diagnostics
{
	void DiagnosticReport::Add(DiagnosticResult result)
	{
		if (result.timestampUtc.empty())
		{
			result.timestampUtc = DiagnosticReportSerializer::UtcNowIso8601();
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		m_results.emplace_back(std::move(result));
	}

	void DiagnosticReport::Clear()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_results.clear();
	}

	std::vector<DiagnosticResult> DiagnosticReport::Snapshot() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_results;
	}

	size_t DiagnosticReport::Count() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_results.size();
	}

	std::wstring DiagnosticReportSerializer::StatusName(DiagnosticStatus status)
	{
		switch (status)
		{
		case DiagnosticStatus::Pass: return L"PASS";
		case DiagnosticStatus::Fail: return L"FAIL";
		case DiagnosticStatus::Blocked: return L"BLOCKED";
		case DiagnosticStatus::Skipped: return L"SKIPPED";
		default: return L"INCONCLUSIVE";
		}
	}

	std::wstring DiagnosticReportSerializer::UtcNowIso8601()
	{
		SYSTEMTIME now = {};
		GetSystemTime(&now);
		wchar_t timestamp[32] = {};
		swprintf_s(
			timestamp,
			L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
			now.wYear,
			now.wMonth,
			now.wDay,
			now.wHour,
			now.wMinute,
			now.wSecond,
			now.wMilliseconds);
		return timestamp;
	}

	std::wstring DiagnosticReportSerializer::ToText(const std::vector<DiagnosticResult>& results)
	{
		std::wostringstream output;
		output << L"Teste-HW hardware capability report\r\n"
			<< L"Schema: 1\r\n"
			<< L"Generated (UTC): " << UtcNowIso8601() << L"\r\n"
			<< L"Result count: " << results.size() << L"\r\n\r\n";

		for (size_t index = 0; index < results.size(); ++index)
		{
			const auto& result = results[index];
			output << L"[" << (index + 1) << L"] " << StatusName(result.status)
				<< L" | " << result.category << L" | " << result.name << L"\r\n"
				<< L"Timestamp (UTC): " << result.timestampUtc << L"\r\n"
				<< L"Duration (us): " << result.durationMicroseconds << L"\r\n"
				<< L"Summary: " << result.summary << L"\r\n";
			if (!result.details.empty()) output << L"Details: " << result.details << L"\r\n";
			if (result.hasHresult) output << L"HRESULT: " << Hex32(static_cast<uint32_t>(result.hresult)) << L"\r\n";
			if (result.hasWin32Error) output << L"Win32 error: " << result.win32Error << L" (" << Hex32(result.win32Error) << L")\r\n";
			output << L"\r\n";
		}
		return output.str();
	}

	std::wstring DiagnosticReportSerializer::ToJson(const std::vector<DiagnosticResult>& results)
	{
		std::wostringstream output;
		output << L"{\n  \"schemaVersion\": 1,\n"
			<< L"  \"application\": \"Teste-HW\",\n"
			<< L"  \"generatedAtUtc\": \"" << UtcNowIso8601() << L"\",\n"
			<< L"  \"results\": [\n";

		for (size_t index = 0; index < results.size(); ++index)
		{
			const auto& result = results[index];
			output << L"    {\n"
				<< L"      \"category\": \"" << EscapeJson(result.category) << L"\",\n"
				<< L"      \"name\": \"" << EscapeJson(result.name) << L"\",\n"
				<< L"      \"status\": \"" << StatusName(result.status) << L"\",\n"
				<< L"      \"summary\": \"" << EscapeJson(result.summary) << L"\",\n"
				<< L"      \"details\": \"" << EscapeJson(result.details) << L"\",\n"
				<< L"      \"timestampUtc\": \"" << result.timestampUtc << L"\",\n"
				<< L"      \"durationMicroseconds\": " << result.durationMicroseconds << L",\n"
				<< L"      \"hresult\": ";
			if (result.hasHresult) output << L"\"" << Hex32(static_cast<uint32_t>(result.hresult)) << L"\"";
			else output << L"null";
			output << L",\n      \"win32Error\": ";
			if (result.hasWin32Error) output << result.win32Error;
			else output << L"null";
			output << L"\n    }" << ((index + 1 < results.size()) ? L"," : L"") << L"\n";
		}

		output << L"  ]\n}\n";
		return output.str();
	}

	task<StorageFile^> DiagnosticReportExporter::ExportTextAsync(const std::vector<DiagnosticResult>& results)
	{
		return ExportAsync(L"txt", DiagnosticReportSerializer::ToText(results));
	}

	task<StorageFile^> DiagnosticReportExporter::ExportJsonAsync(const std::vector<DiagnosticResult>& results)
	{
		return ExportAsync(L"json", DiagnosticReportSerializer::ToJson(results));
	}

	task<StorageFile^> DiagnosticReportExporter::ExportAsync(
		const std::wstring& extension,
		const std::wstring& contents)
	{
		auto folder = ApplicationData::Current->LocalFolder;
		auto fileName = ref new Platform::String(ExportFileName(extension).c_str());
		auto text = ref new Platform::String(contents.c_str());

		return create_task(folder->CreateFileAsync(fileName, CreationCollisionOption::GenerateUniqueName))
			.then([text](StorageFile^ file)
			{
				return create_task(FileIO::WriteTextAsync(file, text, UnicodeEncoding::Utf8))
					.then([file]() { return file; });
			});
	}
}
}
