#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "conv.hxx"

namespace conv
{

std::string fromwstring(const std::wstring &wstr)
{
	int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wstr.length(), nullptr, 0, nullptr, nullptr);
	std::vector<char> result(size);
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wstr.length(), result.data(), size, nullptr, nullptr);
	return std::string(result.begin(), result.end());
}

std::wstring fromstring(const std::string &str)
{
	int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.length(), nullptr, 0);
	std::vector<wchar_t> result(size);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.length(), result.data(), size);
	return std::wstring(result.begin(), result.end());
}

}
