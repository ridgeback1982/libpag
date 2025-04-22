#pragma once
#include "pag/pag.h"

#include <filesystem>
namespace fs = std::filesystem;

namespace pag {

std::string PAG_API generate_uuid();
std::string PAG_API getSysTime();
void PAG_API printSysTime();
void PAG_API stringReplace(std::string& str, const std::string& old_value, const std::string& new_value);
bool PAG_API starts_with(const std::string& str, const std::string& prefix);
void PAG_API removePunctuation(std::wstring &str);
void PAG_API eraseLeadingPunctuation(std::string& s);

}  // namespace pag
