#pragma once

#include <string>

namespace conv
{

std::string fromwstring(const std::wstring &wstr);
std::wstring fromstring(const std::string &str);

}
