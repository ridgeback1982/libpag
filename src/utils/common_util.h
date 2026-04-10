#pragma once
#include "pag/pag.h"

#include <filesystem>
namespace fs = std::filesystem;

namespace pag {

//number functions
int PAG_API get_random_int(int min, int max);
std::string PAG_API generate_uuid();
std::string PAG_API getSysTime();
void PAG_API printSysTime();

//string functions
void PAG_API stringReplace(std::string& str, const std::string& old_value, const std::string& new_value);
bool PAG_API starts_with(const std::string& str, const std::string& prefix);
void PAG_API removePunctuation(std::wstring &str);
void PAG_API eraseLeadingPunctuation(std::string& s);
std::vector<std::string> PAG_API splitStringBy(const std::string &s, const std::string& delimiters);

//string functions
bool PAG_API isEndLinePunctuation(char32_t ch);
bool PAG_API isEnglishChar(char32_t ch);
bool PAG_API isChineseChar(char32_t ch);
bool PAG_API isRealChar(char32_t ch);
bool PAG_API isClosingPunctuation(char32_t ch);
bool PAG_API isOpeningPunctuation(char32_t ch);
bool PAG_API endsWithPunctuationOrEllipsis(const std::string& s);

}  // namespace pag
