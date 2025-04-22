#include "file_util.h"

namespace pag {

static const bool g_on_server = std::getenv("PAG_ON_SERVER") != nullptr;    //NOTE: check it always

bool runOnServer() {
  return g_on_server;
}

void writeStringToFile(const std::string& str, const fs::path& filePath, bool append) {
    std::ofstream outFile(filePath, append ? std::ios::app : std::ios::out);
    if (!outFile) {
        std::cerr << "Error: Unable to open file for writing: " << filePath << std::endl;
        return;
    }

    outFile << str;  // Write string to file
    outFile.close();     // Close file
}

std::string generate_random_suffix(size_t length = 6) {
   const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
   std::random_device rd;
   std::mt19937 generator(rd());
   std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);

  std::string suffix;
  for (size_t i = 0; i < length; ++i) {
     suffix += charset[dist(generator)];
   }
   return suffix;
}

fs::path create_temp_directory(const std::string& prefix) {
   fs::path temp_dir = fs::temp_directory_path();
   std::string random_suffix = generate_random_suffix();
   fs::path new_temp_dir = temp_dir / (prefix + random_suffix);

   if (fs::create_directory(new_temp_dir)) {
     std::cout << "Temporary directory created: " << new_temp_dir << std::endl;
   } else {
     std::cerr << "Failed to create temporary directory." << std::endl;
   }

   return new_temp_dir;
}

std::string getFileNameWithoutExtension(const std::string& filePath) {
  // 使用 std::filesystem 提取文件名
  std::filesystem::path path(filePath);
  return path.stem().string(); // stem() 返回不带扩展名的文件名
}

std::string getFileNameFromUrl(const std::string& url) {
  size_t pos1 = url.find_last_of('/');
  if (pos1 != std::string::npos && pos1 + 1 < url.size()) {
      size_t pos2 = url.find_last_of('?');
      if (pos2 != std::string::npos && pos1 < pos2) {
          //zzy, must drop words after "?" e.g. 1e491415e4c9.webp?time=1734489926920
          //because it will cause file auto deleted by sysmtem on windows
          return url.substr(pos1 + 1, pos2 - pos1 - 1);
      } else {
          return url.substr(pos1 + 1);
      }
  }
  return ""; // 没有后缀名时返回空字符串
}

bool remove_directory(const std::string& directory_path) {
  std::error_code ec;
  
  // 删除目录及其所有内容
  uintmax_t num_removed = fs::remove_all(directory_path, ec);
  
  if (ec) {
      std::cerr << "Error deleting directory: " << ec.message() << std::endl;
      return false;
  }
  
  // std::cout << "Successfully removed directory: " << directory_path << std::endl;
  std::cout << "Number of files/directories removed: " << num_removed << std::endl;
  return true;
}

} // namespace pag
