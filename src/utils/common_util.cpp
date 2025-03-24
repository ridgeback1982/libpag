#include "common_util.h"

#include <iostream>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <random>

namespace pag {

 // 生成伪 UUID（不严格符合 UUID 标准，但唯一性足够）
std::string generate_uuid() {
     std::random_device rd;
     std::mt19937 gen(rd());
     std::uniform_int_distribution<int> dist(0, 15);
 
     std::stringstream ss;
     for (int i = 0; i < 32; ++i) {
         ss << std::hex << dist(gen);
         if (i == 7 || i == 11 || i == 15 || i == 19) {
             ss << "-";
         }
     }
     return ss.str();
 }

std::string getSysTime() {
     // Get the current system time
     auto now = std::chrono::system_clock::now();
         
     // Convert to time_t (seconds since epoch)
     std::time_t now_c = std::chrono::system_clock::to_time_t(now);
 
     // Convert to local time
     std::tm* local_time = std::localtime(&now_c);
 
     // Use std::ostringstream to store formatted time as a string
     std::ostringstream oss;
     oss << std::put_time(local_time, "%Y-%m-%d %H:%M:%S");
 
     return oss.str();  // Return the formatted string
 }

 void stringReplace(std::string& str, const std::string& old_value, const std::string& new_value) {
    size_t pos = 0;
    while ((pos = str.find(old_value, pos)) != std::string::npos) {
        str.replace(pos, old_value.length(), new_value);
        pos += new_value.length();  // 移动位置，避免无限循环
    }
}

bool starts_with(const std::string& str, const std::string& prefix) {
    return str.compare(0, prefix.size(), prefix) == 0;
}

// 判断字符是否是标点符号（包括中文和英文）
bool isPunctuationOrNewline(wchar_t ch) {
    // 标准 C++ iswpunct 适用于英文标点
    if (std::iswpunct(ch)) return true;

    // 中文标点符号的 Unicode 范围
    if ((ch >= 0x3000 && ch <= 0x303F) ||  // CJK 符号和标点
        (ch >= 0xFF00 && ch <= 0xFFEF) ||  // 全角标点
        (ch >= 0x2010 && ch <= 0x2027)) {  // 特殊标点
        return true;
    }

    // 过滤回车（\r）和换行（\n）
    return ch == L'\n' || ch == L'\r';
}

void removePunctuation(std::wstring &str) {
    str.erase(std::remove_if(str.begin(), str.end(), isPunctuationOrNewline), str.end());
}

}   // namespace pag
