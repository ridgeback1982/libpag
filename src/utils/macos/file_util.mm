#include <iostream>
#import <Foundation/Foundation.h>
#include <CoreText/CoreText.h>
#include "pag/defines.h"

namespace pag {

PAG_API std::string getPlatformTemporaryDirectory() {
    NSArray *docPaths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *documentsDirectory = [docPaths firstObject];
    
    //NSString *tempDir = NSTemporaryDirectory();

    // 转换为 std::string
    return std::string([documentsDirectory UTF8String]);
}

std::string StringFromCFString(CFStringRef src) {
  static const CFIndex kCStringSize = 128;
  char temporaryCString[kCStringSize];
  bzero(temporaryCString, kCStringSize);
  CFStringGetCString(src, temporaryCString, kCStringSize, kCFStringEncodingUTF8);
  return {temporaryCString};
}

PAG_API std::string findEnglishFontName(const std::string& fontFamily) {
    std::string familyName;
    CFMutableDictionaryRef cfAttributes = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  if (!fontFamily.empty()) {
    auto cfFontName =
        CFStringCreateWithCString(kCFAllocatorDefault, fontFamily.c_str(), kCFStringEncodingUTF8);
    if (cfFontName) {
      CFDictionaryAddValue(cfAttributes, kCTFontFamilyNameAttribute, cfFontName);
      CFRelease(cfFontName);
    }
  }
//   if (!fontStyle.empty()) {
//     auto cfStyleName =
//         CFStringCreateWithCString(kCFAllocatorDefault, fontStyle.c_str(), kCFStringEncodingUTF8);
//     if (cfStyleName) {
//       CFDictionaryAddValue(cfAttributes, kCTFontStyleNameAttribute, cfStyleName);
//       CFRelease(cfStyleName);
//     }
//   }
  auto cfDesc = CTFontDescriptorCreateWithAttributes(cfAttributes);
  if (cfDesc) {
    auto ctFont = CTFontCreateWithFontDescriptor(cfDesc, 0, nullptr);
    if (ctFont) {
      auto ctFamilyName = CTFontCopyName(ctFont, kCTFontFamilyNameKey);
      if (ctFamilyName != nullptr) {
        familyName = StringFromCFString(ctFamilyName);
        CFRelease(ctFamilyName);
      }
      CFRelease(ctFont);
    }
    CFRelease(cfDesc);
  }
  CFRelease(cfAttributes);
  return familyName;
}


}  // namespace pag
