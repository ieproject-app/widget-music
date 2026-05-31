#pragma once

#include <string>
#include <windows.h>

namespace widgetmusic {

inline std::wstring Win32ErrorMessage(DWORD err) {
  wchar_t* buf = nullptr;
  DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
  DWORD len = ::FormatMessageW(flags, nullptr, err, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
  std::wstring msg;
  if (len && buf) msg.assign(buf, buf + len);
  if (buf) ::LocalFree(buf);
  return msg;
}

} // namespace widgetmusic

