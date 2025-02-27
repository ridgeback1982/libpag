#include "file_util.h"

namespace pag {

void writeStringToFile(const std::string& str, const fs::path& filePath) {
    std::ofstream outFile(filePath);
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


} // namespace pag
