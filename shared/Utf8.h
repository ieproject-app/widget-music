#pragma once

#include <string>
#include <string_view>
#include <windows.h>

namespace widgetmusic {

inline std::string WideToUtf8(std::wstring_view w) {
  if (w.empty()) return {};

  int needed = ::WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  if (needed <= 0) return {};

  std::string out;
  out.resize(static_cast<size_t>(needed));
  int written = ::WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
  if (written != needed) return {};
  return out;
}

inline std::wstring Utf8ToWide(std::string_view s) {
  if (s.empty()) return {};

  int needed =
      ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (needed <= 0) return {};

  std::wstring out;
  out.resize(static_cast<size_t>(needed));
  int written = ::MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), needed);
  if (written != needed) return {};
  return out;
}

} // namespace widgetmusic

