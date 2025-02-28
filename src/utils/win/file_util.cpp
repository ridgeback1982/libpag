#include <dwrite.h>
#include <wrl.h>
#include <cstdlib>  // for getenv
#include <iostream>
#include <string>
#include <codecvt>
#include "pag/defines.h"
using namespace Microsoft::WRL;

namespace pag {

PAG_API std::string getPlatformTemporaryDirectory() {
  const char* tmpdir = std::getenv("TEMP");
  if (tmpdir != nullptr) {
    return std::string(tmpdir);
  } else {
    return "C:/Users/Owner/Downloads/";
  }
}

std::wstring GetEnglishFontName(const std::wstring& chineseFontName) {
  // 初始化 COM
  CoInitialize(NULL);

  // 创建 DirectWrite 工厂
  ComPtr<IDWriteFactory> pDWriteFactory;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(pDWriteFactory.GetAddressOf())))) {
      std::wcerr << L"Failed to create DirectWrite factory." << std::endl;
      return L"";
  }

  // 获取系统字体集合
  ComPtr<IDWriteFontCollection> pFontCollection;
  if (FAILED(pDWriteFactory->GetSystemFontCollection(&pFontCollection, FALSE))) {
      std::wcerr << L"Failed to get system font collection." << std::endl;
      return L"";
  }

  UINT32 fontCount = pFontCollection->GetFontFamilyCount();
  for (UINT32 i = 0; i < fontCount; i++) {
      ComPtr<IDWriteFontFamily> pFontFamily;
      if (FAILED(pFontCollection->GetFontFamily(i, &pFontFamily))) {
          continue;
      }

      // 获取字体系列名称
      ComPtr<IDWriteLocalizedStrings> pFamilyNames;
      if (FAILED(pFontFamily->GetFamilyNames(&pFamilyNames))) {
          continue;
      }

      UINT32 nameCount = pFamilyNames->GetCount();
      for (UINT32 j = 0; j < nameCount; j++) {
          UINT32 nameLength = 0;
          if (FAILED(pFamilyNames->GetStringLength(j, &nameLength))) {
              continue;
          }

          std::wstring fontName(nameLength + 1, L'\0');
          if (FAILED(pFamilyNames->GetString(j, &fontName[0], nameLength + 1))) {
              continue;
          }

          // 去掉末尾的 '\0'
          fontName.resize(nameLength);

          // 检查是否匹配目标中文字体
          if (fontName == chineseFontName) {
              // 获取默认语言的字体名称
              UINT32 enIndex = 0;
              BOOL exists = FALSE;
              if (SUCCEEDED(pFamilyNames->FindLocaleName(L"en-us", &enIndex, &exists)) && exists) {
                  UINT32 enLength = 0;
                  pFamilyNames->GetStringLength(enIndex, &enLength);

                  std::wstring englishFontName(enLength + 1, L'\0');
                  pFamilyNames->GetString(enIndex, &englishFontName[0], enLength + 1);
                  englishFontName.resize(enLength);

                  CoUninitialize();
                  return englishFontName;
              } else {
                  CoUninitialize();
                  return fontName; // 没有英文名称，返回默认名称
              }
          }
      }
  }

  CoUninitialize();
  return L"";
}

PAG_API std::string findEnglishFontName(const std::string& chineseFontName) {
  // TODO: Implement this function for windows platform.
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
  std::wstring chineseFontNameW = converter.from_bytes(chineseFontName);
  std::wstring englishNameW = GetEnglishFontName(chineseFontNameW);
  std::string englishName = converter.to_bytes(englishNameW);
  return englishName;
}

}  // namespace pag