#include "common_util.h"

#include <iostream>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <random>
#include <set>
#include <unordered_set>

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

 void printSysTime() {
    // 获取当前系统时间
    auto now = std::chrono::system_clock::now();
    
    // 转换为 time_t 类型
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    
    // 使用 std::put_time 来格式化输出
    std::cout << "当前时间是: " << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S") << std::endl;
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

void eraseLeadingPunctuation(std::string& s) {
    const std::string puncts[] = {",", "，", "。", "."};
    for (const auto& p : puncts) {
        if (s.compare(0, p.size(), p) == 0) {
            s.erase(0, p.size());
        }
    }
}

std::vector<std::string> splitStringBy(const std::string &s, char delimiter) {
  std::vector<std::string> tokens;
  std::string token;
  std::stringstream ss(s);

  // Split the string by the given delimiter
  while (std::getline(ss, token, delimiter)) {
    if (!token.empty()) {
      tokens.push_back(token);
    }
  }
  return tokens;
}

int get_random_int(int min, int max) {
    static std::random_device rd;  // 用于生成随机种子
    static std::mt19937 gen(rd()); // 使用 Mersenne Twister 伪随机数生成器
    std::uniform_int_distribution<int> dist(min, max); // 均匀分布
    return dist(gen);
}

bool isEndLinePunctuation(char32_t ch) {
    static const std::unordered_set<char32_t> endlinePunctuationSet = {
        U'。', U'？', U'！', U'；',
        U'.', U'?', U'!',  U';',
    };
    return endlinePunctuationSet.find(ch) != endlinePunctuationSet.end();
 }
 
 bool isEnglishChar(char32_t ch) {
     return (ch >= 0x0041 && ch <= 0x005A) ||  // A-Z
            (ch >= 0x0061 && ch <= 0x007A);   // a-z
 }
 
bool isChineseChar(char32_t ch) {
     return (ch >= 0x4E00 && ch <= 0x9FFF) ||  // 基本汉字
            (ch >= 0x3400 && ch <= 0x4DBF) ||  // 扩展A
            (ch >= 0x20000 && ch <= 0x2A6DF) || // 扩展B
            (ch >= 0x2A700 && ch <= 0x2B73F) || // 扩展C
            (ch >= 0x2B740 && ch <= 0x2B81F) || // 扩展D
            (ch >= 0x2B820 && ch <= 0x2CEAF) || // 扩展E
            (ch >= 0x2CEB0 && ch <= 0x2EBEF) || // 扩展F
            (ch >= 0x30000 && ch <= 0x3134F);   // 扩展G
 }
 
 bool isRealChar(char32_t ch) {
   return isEnglishChar(ch) || isChineseChar(ch);
 }

 bool isClosingPunctuation(char32_t ch) {
    static const std::unordered_set<char32_t> closingPunctuationSet = {
        U'”', U'’', U'}', U'」', U']', U'】', U'>', U'》', U')', U'）'
    };
    return closingPunctuationSet.find(ch) != closingPunctuationSet.end();
 }

 bool isOpeningPunctuation(char32_t ch) {
    static const std::unordered_set<char32_t> openingPunctuationSet = {
        U'“', U'‘', U'{', U'「', U'[', U'【', U'<', U'《', U'(', U'（'
    };
    return openingPunctuationSet.find(ch) != openingPunctuationSet.end();
 }

}   // namespace pag
