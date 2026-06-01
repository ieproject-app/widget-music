#include <windows.h>

#include <windowsx.h>

#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <strsafe.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <docobj.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <oleacc.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Json.h"
#include "Utf8.h"
#include "Accessibility.h"
#include "WidgetMusicProtocol.h"
#include "WidgetMusicVisual.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "oleacc.lib")

namespace {

constexpr wchar_t kDeskbandTitle[] = L"Widget Music";
constexpr wchar_t kWindowClassName[] = L"WidgetMusicDeskbandWindow";
constexpr int kBandMinWidth = 280;
constexpr int kBandActualWidth = 300;
constexpr int kBandMaxWidth = 340;
constexpr int kBandCompactWidth = 132;
constexpr int kBandHeight = 40;
constexpr UINT_PTR kVisibleAuditTimerId = 0x4D58;
constexpr UINT_PTR kCompactTitleTimerId = 0x4D59;
constexpr UINT_PTR kPipeStartTimerId = 0x4D5B;
constexpr UINT_PTR kProgressTimerId = 0x4D5C;
constexpr UINT_PTR kTitleHoverIntentTimerId = 0x4D5E;
constexpr UINT kProgressTimerMs = 1000;
constexpr DWORD kVisibleAuditMinIntervalMs = 350;
constexpr DWORD kCompactTitleRevealMs = 3200;
constexpr DWORD kTitleHoverIntentDelayMs = 260;
constexpr DWORD kTitleSuppressAfterClickMs = 1400;
constexpr DWORD kStartupPipeDelayMs = 7000;
constexpr int kFullPad = 16;
constexpr int kFullTextInsetLeft = 10;
constexpr int kFullProgressTextTop = 2;
constexpr int kFullProgressTextSeekGap = 3;
constexpr int kFullSeekTrackTop = 27;
constexpr int kCompactTitlePopupMaxWidth = 280;
constexpr int kCompactTitlePopupGap = 6;
constexpr UINT_PTR kCompactTitlePopupToolId = 0x5A11;
constexpr int kSeekTrackHeight = 3;
constexpr int kPlayVisualSize = 28;
constexpr float kPlayRingWidth = 1.5f;
constexpr int kSideGlyphSize = 19;
constexpr UINT kMenuViewCompact = 0x5101;
constexpr UINT kMenuViewFull = 0x5102;
constexpr DWORD kMaxLogBytes = 512 * 1024;

// {0E716D1F-3D3D-4A57-878D-A7DFC29D9115}
constexpr CLSID CLSID_WidgetMusicDeskband = {
    0x0e716d1f, 0x3d3d, 0x4a57, {0x87, 0x8d, 0xa7, 0xdf, 0xc2, 0x9d, 0x91, 0x15}};

// {00021492-0000-0000-C000-000000000046}
constexpr GUID CATID_DeskBand_Impl = {
    0x00021492, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

HINSTANCE g_hInstance = nullptr;
std::atomic<long> g_dllRefCount{0};
std::mutex g_logMu;
std::wstring g_logPath;
std::once_flag g_gdiplusOnce;
ULONG_PTR g_gdiplusToken = 0;
bool g_gdiplusReady = false;

enum class BandDisplayMode {
  Full,
  Compact,
};

#define RETURN_IF_FAILED(expr)            \
  do {                                    \
    HRESULT _hr__ = (expr);               \
    if (FAILED(_hr__)) return _hr__;      \
  } while (0)

template <typename T>
void SafeRelease(T** pp) {
  if (!pp || !*pp) return;
  (*pp)->Release();
  *pp = nullptr;
}

std::wstring GuidToString(REFGUID guid) {
  wchar_t buf[64]{};
  int n = ::StringFromGUID2(guid, buf, static_cast<int>(std::size(buf)));
  if (n <= 0) return {};
  return std::wstring(buf);
}

std::wstring GetModulePath(HINSTANCE hInst) {
  wchar_t buf[MAX_PATH]{};
  DWORD n = ::GetModuleFileNameW(hInst, buf, static_cast<DWORD>(std::size(buf)));
  if (n == 0 || n >= std::size(buf)) return {};
  return std::wstring(buf, buf + n);
}

std::wstring GetSiblingPath(const wchar_t* filename) {
  std::wstring path = GetModulePath(g_hInstance);
  if (path.empty()) return {};
  wchar_t dir[MAX_PATH]{};
  StringCchCopyW(dir, std::size(dir), path.c_str());
  if (!::PathRemoveFileSpecW(dir)) return {};
  if (!::PathAppendW(dir, filename)) return {};
  return std::wstring(dir);
}

void InitLogPath() {
  if (!g_logPath.empty()) return;
  wchar_t tempDir[MAX_PATH]{};
  DWORD n = ::GetTempPathW(std::size(tempDir), tempDir);
  if (n == 0 || n >= std::size(tempDir)) return;
  g_logPath = tempDir;
  g_logPath += L"WidgetMusicDeskband.log";
}

void LogLine(std::wstring_view line) {
  InitLogPath();
  if (g_logPath.empty()) return;

  std::lock_guard<std::mutex> lock(g_logMu);
  WIN32_FILE_ATTRIBUTE_DATA logData{};
  if (::GetFileAttributesExW(g_logPath.c_str(), GetFileExInfoStandard, &logData) &&
      logData.nFileSizeHigh == 0 && logData.nFileSizeLow >= kMaxLogBytes) {
    std::wstring previous = g_logPath + L".1";
    (void)::DeleteFileW(previous.c_str());
    (void)::MoveFileExW(g_logPath.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING);
  }
  HANDLE h = ::CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;

  SYSTEMTIME st{};
  ::GetLocalTime(&st);

  wchar_t prefix[128]{};
  StringCchPrintfW(prefix, std::size(prefix), L"[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond,
                   st.wMilliseconds);

  DWORD written = 0;
  ::WriteFile(h, prefix, static_cast<DWORD>(wcslen(prefix) * sizeof(wchar_t)), &written, nullptr);
  ::WriteFile(h, line.data(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
  const wchar_t eol[] = L"\r\n";
  ::WriteFile(h, eol, static_cast<DWORD>(sizeof(eol) - sizeof(wchar_t)), &written, nullptr);
  ::CloseHandle(h);
}

bool DebugLoggingEnabled() {
  wchar_t env[16]{};
  DWORD envLen = ::GetEnvironmentVariableW(L"WIDGET_MUSIC_DEBUG", env, static_cast<DWORD>(std::size(env)));
  if (envLen > 0) return env[0] != L'0';

  DWORD value = 0;
  DWORD valueSize = sizeof(value);
  LONG rc = ::RegGetValueW(HKEY_CURRENT_USER, L"Software\\WidgetMusic", L"DebugLog", RRF_RT_REG_DWORD, nullptr,
                           &value, &valueSize);
  return rc == ERROR_SUCCESS && value != 0;
}

void LogDebugLine(std::wstring_view line) {
  if (DebugLoggingEnabled()) LogLine(line);
}

bool EnsureGdiplus() {
  std::call_once(g_gdiplusOnce, [] {
    Gdiplus::GdiplusStartupInput input{};
    g_gdiplusReady = (Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr) == Gdiplus::Ok);
  });
  return g_gdiplusReady;
}

Gdiplus::Color GpColor(COLORREF color, BYTE alpha = 255) {
  return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

bool IsHighContrast() {
  HIGHCONTRASTW hc{};
  hc.cbSize = sizeof(hc);
  if (!::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)) return false;
  return (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

COLORREF Blend(COLORREF a, COLORREF b, uint8_t alpha /*0..255*/) {
  auto lerp = [&](uint8_t ca, uint8_t cb) -> uint8_t {
    return static_cast<uint8_t>((static_cast<uint16_t>(ca) * (255 - alpha) + static_cast<uint16_t>(cb) * alpha) /
                                255);
  };
  return RGB(lerp(GetRValue(a), GetRValue(b)), lerp(GetGValue(a), GetGValue(b)), lerp(GetBValue(a), GetBValue(b)));
}

bool UseLightForegroundOn(COLORREF bg) {
  // Relative luminance-ish threshold.
  int y = (GetRValue(bg) * 299 + GetGValue(bg) * 587 + GetBValue(bg) * 114) / 1000;
  return y < 128;
}

bool IsReasonableThemeSample(COLORREF c) {
  if (c == CLR_INVALID) return false;
  int r = GetRValue(c);
  int g = GetGValue(c);
  int b = GetBValue(c);
  int maxc = max(r, max(g, b));
  int minc = min(r, min(g, b));
  // Avoid sampling bright tray icons/accent highlights; taskbar surfaces are usually low-saturation.
  return (maxc - minc) < 80;
}

std::wstring WindowClassName(HWND hwnd) {
  wchar_t cls[96]{};
  int n = ::GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
  if (n <= 0) return {};
  return std::wstring(cls, cls + n);
}

bool RectsOverlap(const RECT& a, const RECT& b) {
  return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

bool IntersectRectSafe(const RECT& a, const RECT& b, RECT* out) {
  if (!out || !RectsOverlap(a, b)) return false;
  out->left = max(a.left, b.left);
  out->top = max(a.top, b.top);
  out->right = min(a.right, b.right);
  out->bottom = min(a.bottom, b.bottom);
  return out->right > out->left && out->bottom > out->top;
}

bool RectContains(const RECT& outer, const RECT& inner) {
  return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right &&
         inner.bottom <= outer.bottom;
}

bool IsTaskListClass(const std::wstring& cls) {
  return cls == L"MSTaskSwWClass" || cls == L"MSTaskListWClass";
}

bool ScreenRegionLooksEmpty(const RECT& region) {
  const int width = region.right - region.left;
  const int height = region.bottom - region.top;
  if (width < 24 || height < 16) return false;

  HDC screen = ::GetDC(nullptr);
  if (!screen) return false;

  int64_t sumR = 0;
  int64_t sumG = 0;
  int64_t sumB = 0;
  int samples = 0;
  for (int y = region.top + 3; y < region.bottom - 3; y += 4) {
    for (int x = region.left + 3; x < region.right - 3; x += 4) {
      COLORREF c = ::GetPixel(screen, x, y);
      if (c == CLR_INVALID) continue;
      sumR += GetRValue(c);
      sumG += GetGValue(c);
      sumB += GetBValue(c);
      ++samples;
    }
  }

  if (samples < 20) {
    ::ReleaseDC(nullptr, screen);
    return false;
  }

  int avgR = static_cast<int>(sumR / samples);
  int avgG = static_cast<int>(sumG / samples);
  int avgB = static_cast<int>(sumB / samples);

  int busy = 0;
  for (int y = region.top + 3; y < region.bottom - 3; y += 4) {
    for (int x = region.left + 3; x < region.right - 3; x += 4) {
      COLORREF c = ::GetPixel(screen, x, y);
      if (c == CLR_INVALID) continue;
      int delta = abs(GetRValue(c) - avgR) + abs(GetGValue(c) - avgG) + abs(GetBValue(c) - avgB);
      if (delta > 90) ++busy;
    }
  }
  ::ReleaseDC(nullptr, screen);

  return (busy * 100) < samples * 3;
}

COLORREF SampleAdjacentTaskbarColor(HWND hwnd, COLORREF fallback) {
  RECT wr{};
  if (!hwnd || !::GetWindowRect(hwnd, &wr)) return fallback;

  HDC screen = ::GetDC(nullptr);
  if (!screen) return fallback;

  const int y = (wr.top + wr.bottom) / 2;
  const int yTop = wr.top + 4;
  const int yBottom = wr.bottom - 4;

  POINT points[] = {
      {wr.left - 40, y},
      {wr.left - 60, y},
      {wr.left - 80, y},
      {wr.right + 40, y},
      {wr.right + 60, y},
      {wr.right + 80, y},
      {wr.left - 50, yTop},
      {wr.left - 50, yBottom},
      {wr.right + 50, yTop},
      {wr.right + 50, yBottom},
  };

  std::vector<COLORREF> samples;
  samples.reserve(std::size(points));
  for (const auto& pt : points) {
    COLORREF c = ::GetPixel(screen, pt.x, pt.y);
    if (!IsReasonableThemeSample(c)) continue;
    samples.push_back(c);
  }

  ::ReleaseDC(nullptr, screen);
  return widgetmusic::MedianColor(samples, fallback);
}

COLORREF GetTaskbarColorViaDWM() {
  BOOL enabled = FALSE;
  if (FAILED(::DwmIsCompositionEnabled(&enabled)) || !enabled) {
    return CLR_INVALID;
  }
  
  // Get DWM colorization color
  DWORD color = 0;
  BOOL opaque = FALSE;
  if (SUCCEEDED(::DwmGetColorizationColor(&color, &opaque))) {
    // Extract RGB from ARGB
    BYTE r = (color >> 16) & 0xFF;
    BYTE g = (color >> 8) & 0xFF;
    BYTE b = color & 0xFF;
    
    // Get taskbar base color
    COLORREF baseColor = ::GetSysColor(COLOR_3DFACE);
    
    // Jika opaque, gunakan langsung
    if (opaque) {
      return RGB(r, g, b);
    }
    
    // Jika transparent, blend dengan base (20% accent)
    return Blend(baseColor, RGB(r, g, b), 20);
  }
  
  return CLR_INVALID;
}

struct BandState {
  bool connected = false;
  bool connecting = true;
  bool has_session = false;

  std::wstring app;
  std::wstring title;
  std::wstring artist;

  // "playing" | "paused" | "stopped" | "unknown"
  std::string playback;

  bool can_prev = false;
  bool can_next = false;
  bool can_play_pause = false;
  bool refreshing = false;
  bool has_timeline = false;
  int64_t position_ms = 0;
  int64_t duration_ms = 0;
};

bool SameBandState(const BandState& s,
                   bool connected,
                   bool hasSession,
                   const std::wstring& app,
                   const std::wstring& title,
                   const std::wstring& artist,
                   const std::string& playback,
                   bool canPrev,
                   bool canNext,
                   bool canPP,
                   bool refreshing,
                   bool hasTimeline,
                   int64_t positionMs,
                   int64_t durationMs) {
  return !s.connecting && s.connected == connected && s.has_session == hasSession && s.app == app &&
         s.title == title && s.artist == artist && s.playback == playback && s.can_prev == canPrev &&
         s.can_next == canNext && s.can_play_pause == canPP && s.refreshing == refreshing &&
         s.has_timeline == hasTimeline && s.position_ms == positionMs && s.duration_ms == durationMs;
}

std::wstring PrimaryTextForState(const BandState& s) {
  if (s.connecting) return L"Connecting...";
  if (!s.connected) return L"Disconnected";
  if (!s.has_session) return L"No media";
  if (s.refreshing && s.title.empty()) return L"Updating...";

  if (!s.title.empty()) {
    if (!s.artist.empty()) {
      std::wstring t = s.title;
      t.append(L" \x2014 ");
      t.append(s.artist);
      return t;
    }
    return s.title;
  }

  if (!s.app.empty()) return s.app;
  return L"Media active";
}

std::wstring FormatElapsedClock(int64_t ms) {
  if (ms < 0) ms = 0;
  int64_t totalSec = ms / 1000;
  int64_t hours = totalSec / 3600;
  int64_t minutes = (totalSec % 3600) / 60;
  int64_t seconds = totalSec % 60;
  wchar_t buf[32]{};
  if (hours > 0) {
    StringCchPrintfW(buf, std::size(buf), L"%lld:%02lld:%02lld", hours, minutes, seconds);
  } else {
    StringCchPrintfW(buf, std::size(buf), L"%02lld:%02lld", minutes, seconds);
  }
  return buf;
}

bool SameVisualBandState(const BandState& oldState, const BandState& nextState) {
  const bool oldPrevEnabled = oldState.connected && oldState.has_session && oldState.can_prev;
  const bool newPrevEnabled = nextState.connected && nextState.has_session && nextState.can_prev;
  const bool oldNextEnabled = oldState.connected && oldState.has_session && oldState.can_next;
  const bool newNextEnabled = nextState.connected && nextState.has_session && nextState.can_next;
  const bool oldPlayEnabled = oldState.connected && oldState.has_session && oldState.can_play_pause;
  const bool newPlayEnabled = nextState.connected && nextState.has_session && nextState.can_play_pause;

  const bool oldTimeline = oldState.has_timeline;
  const bool newTimeline = nextState.has_timeline;
  const int64_t oldPosSec = oldState.position_ms / 1000;
  const int64_t newPosSec = nextState.position_ms / 1000;
  const int64_t oldDurSec = oldState.duration_ms / 1000;
  const int64_t newDurSec = nextState.duration_ms / 1000;

  return PrimaryTextForState(oldState) == PrimaryTextForState(nextState) &&
         oldPrevEnabled == newPrevEnabled && oldNextEnabled == newNextEnabled &&
         oldPlayEnabled == newPlayEnabled && (oldState.playback == "playing") == (nextState.playback == "playing") &&
         oldTimeline == newTimeline && oldPosSec == newPosSec && oldDurSec == newDurSec;
}

constexpr UINT WM_APP_STATE = WM_APP + 0x4A1;

class PipeClient {
 public:
  PipeClient() = default;
  ~PipeClient() { Stop(); }

  PipeClient(const PipeClient&) = delete;
  PipeClient& operator=(const PipeClient&) = delete;

  void Start(HWND hwndNotify) {
    Stop();

    _hwndNotify = hwndNotify;
    _stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    _sendEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!_stopEvent || !_sendEvent) {
      Stop();
      return;
    }

    _thread = std::thread([this] { ThreadMain(); });
  }

  void Stop() {
    if (_stopEvent) ::SetEvent(_stopEvent);
    if (_thread.joinable()) _thread.join();

    ClosePipe();

    if (_sendEvent) {
      ::CloseHandle(_sendEvent);
      _sendEvent = nullptr;
    }
    if (_stopEvent) {
      ::CloseHandle(_stopEvent);
      _stopEvent = nullptr;
    }

    std::lock_guard<std::mutex> lock(_sendMu);
    _sendQueue.clear();
  }

  void SendJsonLine(std::string lineUtf8) {
    if (lineUtf8.size() > widgetmusic::kMaxPipeMessageBytes) return;
    std::lock_guard<std::mutex> lock(_sendMu);
    if (_sendQueue.size() >= kMaxQueuedPipeMessages) _sendQueue.pop_front();
    _sendQueue.emplace_back(std::move(lineUtf8));
    if (_sendEvent) ::SetEvent(_sendEvent);
  }

  void SetStateSink(BandState* state, std::mutex* stateMu) {
    _state = state;
    _stateMu = stateMu;
  }

 private:
  void ClosePipe() {
    if (_pipe != INVALID_HANDLE_VALUE) {
      ::CloseHandle(_pipe);
      _pipe = INVALID_HANDLE_VALUE;
    }
    _helloValidated = false;
  }

  void MaybeStartHost() {
    DWORD now = ::GetTickCount();
    if (now - _lastHostStartTick < 3000) return;
    _lastHostStartTick = now;

    std::wstring exe = GetSiblingPath(L"WidgetMusicHost.exe");
    if (exe.empty()) return;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    // Use lpApplicationName to avoid quoting/argv parsing hazards.
    if (::CreateProcessW(exe.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
      ::CloseHandle(pi.hThread);
      ::CloseHandle(pi.hProcess);
    }
  }

  bool ConnectPipe() {
    // Keep trying to connect; host might still be starting up.
    const std::wstring pipePath = widgetmusic::PipePathForCurrentSession();
    _pipe = ::CreateFileW(pipePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                          FILE_FLAG_OVERLAPPED, nullptr);
    if (_pipe == INVALID_HANDLE_VALUE) {
      DWORD err = ::GetLastError();
      if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PIPE_BUSY) {
        MaybeStartHost();
      }
      return false;
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    ::SetNamedPipeHandleState(_pipe, &mode, nullptr, nullptr);

    return true;
  }

  void SetConnectingState(bool connecting) {
    if (!_state || !_stateMu) return;
    {
      std::lock_guard<std::mutex> lock(*_stateMu);
      _state->connecting = connecting;
      _state->connected = !connecting;
      if (connecting) {
        _state->has_session = false;
        _state->app.clear();
        _state->title.clear();
        _state->artist.clear();
        _state->playback = "unknown";
        _state->can_prev = false;
        _state->can_next = false;
        _state->can_play_pause = false;
        _state->refreshing = false;
        _state->has_timeline = false;
        _state->position_ms = 0;
        _state->duration_ms = 0;
      }
    }
    if (_hwndNotify) ::PostMessageW(_hwndNotify, WM_APP_STATE, 0, 0);
  }

  bool ParseAndApplyMessage(std::string_view msg) {
    if (msg.size() > widgetmusic::kMaxPipeMessageBytes) return false;
    std::string type;
    if (!widgetmusic::JsonTryGetString(msg, widgetmusic::kMsgType, &type)) return false;
    if (type == widgetmusic::kTypeHello) {
      int64_t version = 0;
      if (!widgetmusic::JsonTryGetInt64(msg, widgetmusic::kKeyVersion, &version) ||
          !widgetmusic::IsSupportedProtocolVersion(version)) {
        LogLine(L"Pipe hello rejected: incompatible protocol version");
        return false;
      }
      _helloValidated = true;
      return true;
    }
    if (type != widgetmusic::kTypeState) return true;
    if (!_helloValidated) {
      LogLine(L"Pipe state rejected before hello handshake");
      return false;
    }

    bool connected = false;
    (void)widgetmusic::JsonTryGetBool(msg, widgetmusic::kKeyConnected, &connected);
    bool hasSession = false;
    (void)widgetmusic::JsonTryGetBool(msg, widgetmusic::kKeyHasSession, &hasSession);

    std::string appUtf8;
    std::string titleUtf8;
    std::string artistUtf8;
    std::string playback;
    bool canPrev = false, canNext = false, canPP = false;
    bool refreshing = false;
    bool hasTimeline = false;
    int64_t positionMs = 0;
    int64_t durationMs = 0;

    (void)widgetmusic::JsonTryGetString(msg, widgetmusic::kKeyApp, &appUtf8);
    (void)widgetmusic::JsonTryGetString(msg, widgetmusic::kKeyTitle, &titleUtf8);
    (void)widgetmusic::JsonTryGetString(msg, widgetmusic::kKeyArtist, &artistUtf8);
    (void)widgetmusic::JsonTryGetString(msg, widgetmusic::kKeyPlayback, &playback);
    (void)widgetmusic::JsonTryGetBool(msg, widgetmusic::kKeyCanPrev, &canPrev);
    (void)widgetmusic::JsonTryGetBool(msg, widgetmusic::kKeyCanNext, &canNext);
    (void)widgetmusic::JsonTryGetBool(msg, widgetmusic::kKeyCanPlayPause, &canPP);
    (void)widgetmusic::JsonTryGetBool(msg, widgetmusic::kKeyRefreshing, &refreshing);
    (void)widgetmusic::JsonTryGetBool(msg, widgetmusic::kKeyHasTimeline, &hasTimeline);
    (void)widgetmusic::JsonTryGetInt64(msg, widgetmusic::kKeyPositionMs, &positionMs);
    (void)widgetmusic::JsonTryGetInt64(msg, widgetmusic::kKeyDurationMs, &durationMs);
    if (positionMs < 0) positionMs = 0;
    if (durationMs < 0) durationMs = 0;
    if (durationMs > 0 && positionMs > durationMs) positionMs = durationMs;
    if (!hasTimeline) {
      positionMs = 0;
      durationMs = 0;
    }

    if (!_state || !_stateMu) return true;
    bool changed = true;
    bool visualChanged = true;
    std::wstring app = widgetmusic::ClampProtocolText(widgetmusic::Utf8ToWide(appUtf8), widgetmusic::kMaxAppChars);
    std::wstring title =
        widgetmusic::ClampProtocolText(widgetmusic::Utf8ToWide(titleUtf8), widgetmusic::kMaxTitleChars);
    std::wstring artist =
        widgetmusic::ClampProtocolText(widgetmusic::Utf8ToWide(artistUtf8), widgetmusic::kMaxArtistChars);
    {
      std::lock_guard<std::mutex> lock(*_stateMu);
      changed = !SameBandState(*_state, connected, hasSession, app, title, artist, playback, canPrev, canNext, canPP,
                                refreshing, hasTimeline, positionMs, durationMs);
      if (!changed) return true;

      BandState next = *_state;
      next.connecting = false;
      next.connected = connected;
      next.has_session = hasSession;
      next.app = app;
      next.title = title;
      next.artist = artist;
      next.playback = playback;
      next.can_prev = canPrev;
      next.can_next = canNext;
      next.can_play_pause = canPP;
      next.refreshing = refreshing;
      next.has_timeline = hasTimeline;
      next.position_ms = positionMs;
      next.duration_ms = durationMs;
      visualChanged = !SameVisualBandState(*_state, next);

      _state->connecting = false;
      _state->connected = connected;
      _state->has_session = hasSession;
      _state->app = std::move(app);
      _state->title = std::move(title);
      _state->artist = std::move(artist);
      _state->playback = playback;
      _state->can_prev = canPrev;
      _state->can_next = canNext;
      _state->can_play_pause = canPP;
      _state->refreshing = refreshing;
      _state->has_timeline = hasTimeline;
      _state->position_ms = positionMs;
      _state->duration_ms = durationMs;
    }
    if (visualChanged && _hwndNotify) ::PostMessageW(_hwndNotify, WM_APP_STATE, 0, 0);
    return true;
  }

  void ThreadMain() {
    SetConnectingState(true);

    std::vector<char> buf;
    buf.resize(widgetmusic::kMaxPipeMessageBytes);

    OVERLAPPED ovRead{};
    HANDLE hReadEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ovRead.hEvent = hReadEvent;

    OVERLAPPED ovWrite{};
    HANDLE hWriteEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ovWrite.hEvent = hWriteEvent;

    auto writeMsg = [&](std::string_view msg) -> bool {
      if (_pipe == INVALID_HANDLE_VALUE || !hWriteEvent) return false;
      ::ResetEvent(hWriteEvent);
      DWORD written = 0;
      BOOL ok = ::WriteFile(_pipe, msg.data(), static_cast<DWORD>(msg.size()), &written, &ovWrite);
      if (ok) return written == msg.size();

      DWORD err = ::GetLastError();
      if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) return false;
      if (err != ERROR_IO_PENDING) return false;

      HANDLE handles[2]{_stopEvent, hWriteEvent};
      DWORD w = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
      if (w == WAIT_OBJECT_0) {
        (void)::CancelIoEx(_pipe, &ovWrite);
        return false;
      }

      DWORD got = 0;
      if (!::GetOverlappedResult(_pipe, &ovWrite, &got, FALSE)) return false;
      return got == msg.size();
    };

    bool readInFlight = false;
    DWORD bytesRead = 0;

    while (_stopEvent && ::WaitForSingleObject(_stopEvent, 0) == WAIT_TIMEOUT) {
      if (_pipe == INVALID_HANDLE_VALUE) {
        if (!ConnectPipe()) {
          // Backoff a bit.
          ::WaitForSingleObject(_stopEvent, 250);
          continue;
        }

        SetConnectingState(false);
        readInFlight = false;

        // Ask host to send a full state immediately.
        SendJsonLine(std::string("{\"") + widgetmusic::kMsgType + "\":\"" + widgetmusic::kTypeCommand + "\",\"" +
                     widgetmusic::kKeyName + "\":\"refresh\"}\n");
      }

      if (!readInFlight) {
        // Start one overlapped read.
        ::ResetEvent(hReadEvent);
        bytesRead = 0;
        BOOL ok = ::ReadFile(_pipe, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead, &ovRead);
        if (!ok) {
          DWORD readErr = ::GetLastError();
          if (readErr == ERROR_IO_PENDING) {
            readInFlight = true;
          } else if (readErr == ERROR_BROKEN_PIPE || readErr == ERROR_PIPE_NOT_CONNECTED) {
            ClosePipe();
            SetConnectingState(true);
            continue;
          } else {
            ClosePipe();
            SetConnectingState(true);
            continue;
          }
        }
      }

      if (readInFlight) {
        HANDLE handles[3]{_stopEvent, hReadEvent, _sendEvent};
        DWORD wait = ::WaitForMultipleObjects(3, handles, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0) break;  // stop

        if (wait == WAIT_OBJECT_0 + 2) {
        // Flush send queue.
        for (;;) {
          std::string msg;
          {
            std::lock_guard<std::mutex> lock(_sendMu);
            if (_sendQueue.empty()) {
              ::ResetEvent(_sendEvent);
              break;
            }
            msg = std::move(_sendQueue.front());
            _sendQueue.pop_front();
          }
          if (!writeMsg(msg)) {
            ClosePipe();
            SetConnectingState(true);
            break;
          }
        }
        continue;
        }

        if (wait != WAIT_OBJECT_0 + 1) continue;

        DWORD got = 0;
        if (!::GetOverlappedResult(_pipe, &ovRead, &got, FALSE)) {
          ClosePipe();
          SetConnectingState(true);
          continue;
        }
        bytesRead = got;
        readInFlight = false;
      }

      if (bytesRead == 0) {
        ClosePipe();
        SetConnectingState(true);
        continue;
      }

      std::string_view msg(buf.data(), buf.data() + bytesRead);
      // Strip trailing newlines if present.
      while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.remove_suffix(1);
      if (!ParseAndApplyMessage(msg)) {
        ClosePipe();
        SetConnectingState(true);
      }
      continue;
    }

    if (readInFlight && _pipe != INVALID_HANDLE_VALUE) (void)::CancelIoEx(_pipe, &ovRead);

    if (hReadEvent) ::CloseHandle(hReadEvent);
    if (hWriteEvent) ::CloseHandle(hWriteEvent);
  }

  HWND _hwndNotify = nullptr;
  HANDLE _stopEvent = nullptr;
  HANDLE _sendEvent = nullptr;
  HANDLE _pipe = INVALID_HANDLE_VALUE;
  std::thread _thread;

  std::mutex _sendMu;
  std::deque<std::string> _sendQueue;
  static constexpr size_t kMaxQueuedPipeMessages = 24;

  BandState* _state = nullptr;
  std::mutex* _stateMu = nullptr;

  DWORD _lastHostStartTick = 0;
  bool _helloValidated = false;
};

struct Button {
  RECT rc{};
  bool enabled = false;
  bool hot = false;
  bool pressed = false;
  int kind = 0;  // 0=prev, 1=play/pause, 2=next
};

} // namespace

// Forward declaration for the window procedure; defined after deskband class.
static LRESULT CALLBACK WidgetMusicWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

namespace {

bool EnsureWindowClassRegisteredImpl() {
  static std::atomic<bool> s_registered{false};
  if (s_registered.load(std::memory_order_acquire)) return true;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WidgetMusicWndProc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = g_hInstance;
  wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = kWindowClassName;

  ATOM a = ::RegisterClassExW(&wc);
  if (a == 0) {
    DWORD err = ::GetLastError();
    if (err == ERROR_CLASS_ALREADY_EXISTS) {
      s_registered.store(true, std::memory_order_release);
      return true;
    }
    return false;
  }

  s_registered.store(true, std::memory_order_release);
  return true;
}

class WidgetMusicDeskband final : public IDeskBand2,
                                  public IObjectWithSite,
                                  public IPersistStream,
                                  public IInputObject,
                                  public widgetmusic::AccessibleHost {
 public:
  WidgetMusicDeskband() { g_dllRefCount.fetch_add(1); }
  ~WidgetMusicDeskband() {
    StopPipeClient(false);
    StopCompactTitleTimer(true);
    ReleaseAccessibleProvider();
    if (_compactTitlePopup) {
      ::DestroyWindow(_compactTitlePopup);
      _compactTitlePopup = nullptr;
    }
    if (_hwnd) ::DestroyWindow(_hwnd);
    ReleaseTextFont();
    ReleaseBackBuffer();
    SafeRelease(&_site);
    SafeRelease(&_inputSite);
    SafeRelease(&_commandTarget);
    g_dllRefCount.fetch_sub(1);
  }

  // IUnknown
  IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(IDeskBand2)) || IsEqualIID(riid, __uuidof(IDeskBand)) ||
        IsEqualIID(riid, __uuidof(IDockingWindow)) || IsEqualIID(riid, __uuidof(IOleWindow))) {
      *ppv = static_cast<IDeskBand2*>(this);
    } else if (IsEqualIID(riid, __uuidof(IObjectWithSite))) {
      *ppv = static_cast<IObjectWithSite*>(this);
    } else if (IsEqualIID(riid, __uuidof(IPersistStream)) || IsEqualIID(riid, __uuidof(IPersist))) {
      *ppv = static_cast<IPersistStream*>(this);
    } else if (IsEqualIID(riid, __uuidof(IInputObject))) {
      *ppv = static_cast<IInputObject*>(this);
    } else {
      return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
  }

  IFACEMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(_ref.fetch_add(1) + 1); }
  IFACEMETHODIMP_(ULONG) Release() override {
    ULONG v = static_cast<ULONG>(_ref.fetch_sub(1) - 1);
    if (v == 0) delete this;
    return v;
  }

  // IOleWindow
  IFACEMETHODIMP GetWindow(HWND* phwnd) override {
    if (!phwnd) return E_POINTER;
    *phwnd = _hwnd;
    LogDebugLine(L"GetWindow hwnd=" + std::to_wstring(reinterpret_cast<uintptr_t>(_hwnd)));
    return _hwnd ? S_OK : E_FAIL;
  }
  IFACEMETHODIMP ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }

  // IDockingWindow
  IFACEMETHODIMP ShowDW(BOOL fShow) override {
    LogDebugLine(std::wstring(L"ShowDW ") + (fShow ? L"show" : L"hide"));
    if (_hwnd) {
      ::ShowWindow(_hwnd, fShow ? SW_SHOW : SW_HIDE);
      if (fShow) {
        SchedulePipeStart(kStartupPipeDelayMs);
        DeferVisibleAudit();
        Layout();
        ::InvalidateRect(_hwnd, nullptr, FALSE);
      } else {
        StopCompactTitleTimer(true);
        StopProgressTimer();
        StopPipeClient(true);
        _bandMode = BandDisplayMode::Compact;
        _visibleInsetLeft = 0;
      }
    }
    return S_OK;
  }
  IFACEMETHODIMP CloseDW(DWORD) override {
    StopPipeClient(true);
    StopCompactTitleTimer(true);
    StopProgressTimer();
    if (_compactTitlePopup) {
      ::DestroyWindow(_compactTitlePopup);
      _compactTitlePopup = nullptr;
    }
    if (_tooltip) {
      ::DestroyWindow(_tooltip);
      _tooltip = nullptr;
    }
    if (_hwnd) {
      ::ShowWindow(_hwnd, SW_HIDE);
      ::DestroyWindow(_hwnd);
      _hwnd = nullptr;
    }
    ReleaseTextFont();
    ReleaseBackBuffer();
    return S_OK;
  }
  IFACEMETHODIMP ResizeBorderDW(const RECT*, IUnknown*, BOOL) override { return E_NOTIMPL; }

  // IDeskBand
  IFACEMETHODIMP GetBandInfo(DWORD dwBandID, DWORD dwViewMode, DESKBANDINFO* pdbi) override {
    _bandId = dwBandID;
    _viewMode = dwViewMode;
    if (!pdbi) return E_POINTER;
    LogDebugLine(L"GetBandInfo mask=" + std::to_wstring(pdbi->dwMask) + L" bandId=" + std::to_wstring(dwBandID) +
                 L" mode=" + BandModeName() + L" desired=" + std::to_wstring(DesiredBandWidth()));

    if (pdbi->dwMask & DBIM_TITLE) {
      // Keep the registry title for the Toolbars menu, but don't let Explorer render it inside the band.
      pdbi->wszTitle[0] = L'\0';
      pdbi->dwMask &= ~DBIM_TITLE;
    }
    if (pdbi->dwMask & DBIM_BKCOLOR) {
      pdbi->crBkgnd = ::GetSysColor(COLOR_3DFACE);
    }
    if (pdbi->dwMask & DBIM_MODEFLAGS) {
      pdbi->dwModeFlags = DBIMF_FIXED | DBIMF_NOGRIPPER | DBIMF_NOMARGINS;
    }
    const int targetWidth = DesiredBandWidth();
    if (pdbi->dwMask & DBIM_MINSIZE) {
      pdbi->ptMinSize.x = IsCompactMode() ? targetWidth : kBandMinWidth;
      pdbi->ptMinSize.y = kBandHeight;
    }
    if (pdbi->dwMask & DBIM_MAXSIZE) {
      pdbi->ptMaxSize.x = IsCompactMode() ? targetWidth : kBandMaxWidth;
      pdbi->ptMaxSize.y = kBandHeight;
    }
    if (pdbi->dwMask & DBIM_INTEGRAL) {
      pdbi->ptIntegral.x = 1;
      pdbi->ptIntegral.y = 1;
    }
    if (pdbi->dwMask & DBIM_ACTUAL) {
      pdbi->ptActual.x = targetWidth;
      pdbi->ptActual.y = kBandHeight;
    }
    return S_OK;
  }

  // IDeskBand2
  IFACEMETHODIMP CanRenderComposited(BOOL* pfCanRenderComposited) override {
    if (!pfCanRenderComposited) return E_POINTER;
    *pfCanRenderComposited = TRUE;
    return S_OK;
  }
  IFACEMETHODIMP SetCompositionState(BOOL fCompositionEnabled) override {
    _compositionEnabled = (fCompositionEnabled != FALSE);
    if (_hwnd) ::InvalidateRect(_hwnd, nullptr, FALSE);
    return S_OK;
  }
  IFACEMETHODIMP GetCompositionState(BOOL* pfCompositionEnabled) override {
    if (!pfCompositionEnabled) return E_POINTER;
    *pfCompositionEnabled = _compositionEnabled ? TRUE : FALSE;
    return S_OK;
  }

  // IObjectWithSite
  IFACEMETHODIMP SetSite(IUnknown* pUnkSite) override {
    LogDebugLine(pUnkSite ? L"SetSite(site!=null)" : L"SetSite(null)");
    if (_site) {
      SafeRelease(&_site);
    }
    if (_inputSite) {
      SafeRelease(&_inputSite);
    }
    if (_commandTarget) {
      SafeRelease(&_commandTarget);
    }

    if (!pUnkSite) {
      StopPipeClient(true);
      StopCompactTitleTimer(true);
      StopProgressTimer();
      if (_compactTitlePopup) {
        ::DestroyWindow(_compactTitlePopup);
        _compactTitlePopup = nullptr;
      }
      if (_tooltip) {
        ::DestroyWindow(_tooltip);
        _tooltip = nullptr;
      }
      if (_hwnd) {
        ::DestroyWindow(_hwnd);
        _hwnd = nullptr;
      }
      ReleaseTextFont();
      ReleaseBackBuffer();
      return S_OK;
    }

    _site = pUnkSite;
    _site->AddRef();
    _bandMode = BandDisplayMode::Compact;
    _mouseInClient = false;

    (void)pUnkSite->QueryInterface(__uuidof(IInputObjectSite), reinterpret_cast<void**>(&_inputSite));
    ResolveCommandTarget(pUnkSite);

    HWND hwndParent = nullptr;
    {
      IOleWindow* pow = nullptr;
      HRESULT hr = pUnkSite->QueryInterface(__uuidof(IOleWindow), reinterpret_cast<void**>(&pow));
      if (SUCCEEDED(hr) && pow) {
        pow->GetWindow(&hwndParent);
        pow->Release();
      }
    }
    if (!hwndParent) return E_FAIL;
    LogDebugLine(L"SetSite parent hwnd=" + std::to_wstring(reinterpret_cast<uintptr_t>(hwndParent)));
    {
      wchar_t cls[128]{};
      ::GetClassNameW(hwndParent, cls, std::size(cls));
      RECT pr{};
      ::GetWindowRect(hwndParent, &pr);
      LogDebugLine(std::wstring(L"SetSite parent class=") + cls + L" rect=" +
                   std::to_wstring(pr.left) + L"," + std::to_wstring(pr.top) + L"," +
                   std::to_wstring(pr.right) + L"," + std::to_wstring(pr.bottom));
    }

    if (!EnsureWindowClassRegisteredImpl()) return E_FAIL;

    if (!_hwnd) {
      _hwnd = ::CreateWindowExW(0, kWindowClassName, L"",
                                 WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP, 0, 0,
                                 DesiredBandWidth(), kBandHeight, hwndParent, nullptr, g_hInstance, this);
      LogDebugLine(L"CreateWindowExW hwnd=" + std::to_wstring(reinterpret_cast<uintptr_t>(_hwnd)));
      if (!_hwnd) return E_FAIL;

      // Optional: let Explorer theme our child window as needed.
      ::SetWindowTheme(_hwnd, L"Explorer", nullptr);
    } else {
      ::SetParent(_hwnd, hwndParent);
    }

    EnsureTooltip();
    EnsureCompactTitlePopup();
    DeferVisibleAudit();
    RequestBackgroundRefresh(true);
    ::SetWindowPos(_hwnd, nullptr, 0, 0, DesiredBandWidth(), kBandHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    LogDebugLine(L"SetWindowPos size=" + std::to_wstring(DesiredBandWidth()) + L"x" + std::to_wstring(kBandHeight));
    {
      RECT wr{};
      ::GetWindowRect(_hwnd, &wr);
      LogDebugLine(L"Band rect=" + std::to_wstring(wr.left) + L"," + std::to_wstring(wr.top) + L"," +
                   std::to_wstring(wr.right) + L"," + std::to_wstring(wr.bottom));
    }
    Layout();
    ::RedrawWindow(_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE | RDW_NOCHILDREN);
    SchedulePipeStart(kStartupPipeDelayMs);

    return S_OK;
  }

  IFACEMETHODIMP GetSite(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!_site) return E_FAIL;
    return _site->QueryInterface(riid, ppv);
  }

  // IPersist
  IFACEMETHODIMP GetClassID(CLSID* pClassID) override {
    if (!pClassID) return E_POINTER;
    *pClassID = CLSID_WidgetMusicDeskband;
    return S_OK;
  }

  // IPersistStream
  IFACEMETHODIMP IsDirty() override { return S_FALSE; }
  IFACEMETHODIMP Load(IStream*) override { return S_OK; }
  IFACEMETHODIMP Save(IStream*, BOOL) override { return S_OK; }
  IFACEMETHODIMP GetSizeMax(ULARGE_INTEGER* pcbSize) override {
    if (!pcbSize) return E_POINTER;
    pcbSize->QuadPart = 0;
    return S_OK;
  }

  // IInputObject
  IFACEMETHODIMP UIActivateIO(BOOL fActivate, MSG*) override {
    if (fActivate && _hwnd) ::SetFocus(_hwnd);
    return S_OK;
  }
  IFACEMETHODIMP HasFocusIO() override {
    HWND hwndFocus = ::GetFocus();
    if (!_hwnd) return S_FALSE;
    if (hwndFocus == _hwnd || ::IsChild(_hwnd, hwndFocus)) return S_OK;
    return S_FALSE;
  }
  IFACEMETHODIMP TranslateAcceleratorIO(MSG*) override { return S_FALSE; }

  // Window interaction
  LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
      case WM_NCCREATE: {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
      }
      case WM_GETOBJECT:
        if (static_cast<LONG>(lp) == OBJID_CLIENT) return HandleAccessibleObject(wp);
        break;
      case WM_ERASEBKGND:
        PaintImmediateBackground(hwnd, reinterpret_cast<HDC>(wp));
        return 1;
      case WM_SIZE:
        RequestBackgroundRefresh(false);
        Layout();
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      case WM_WINDOWPOSCHANGED: {
        auto* pos = reinterpret_cast<WINDOWPOS*>(lp);
        const UINT flags = pos ? pos->flags : 0;
        const bool movedOrSized = !pos || (flags & (SWP_NOMOVE | SWP_NOSIZE)) != (SWP_NOMOVE | SWP_NOSIZE);
        const bool visibilityChanged = (flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW | SWP_FRAMECHANGED)) != 0;
        if (movedOrSized || visibilityChanged) {
          RequestBackgroundRefresh(false);
          Layout();
          ::InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      case WM_SETTINGCHANGE:
      case WM_THEMECHANGED:
      case WM_DWMCOLORIZATIONCOLORCHANGED:
        RequestBackgroundRefresh(true);
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      case WM_APP_STATE:
        OnStateUpdated();
        return 0;
      case WM_SETFOCUS:
        NotifyAccessibleFocus();
        InvalidateButtons();
        return 0;
      case WM_KILLFOCUS:
        InvalidateButtons();
        return 0;
      case WM_KEYDOWN:
        if (OnKeyDown(static_cast<UINT>(wp))) return 0;
        break;
      case WM_LBUTTONDOWN:
        OnMouseDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
      case WM_LBUTTONUP:
        OnMouseUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
      case WM_RBUTTONUP: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ::ClientToScreen(hwnd, &pt);
        ShowModeContextMenu(pt.x, pt.y);
        return 0;
      }
      case WM_CONTEXTMENU: {
        int sx = GET_X_LPARAM(lp);
        int sy = GET_Y_LPARAM(lp);
        if (sx == -1 && sy == -1) {
          RECT wr{};
          ::GetWindowRect(hwnd, &wr);
          sx = wr.left + 18;
          sy = wr.top + 6;
        }
        ShowModeContextMenu(sx, sy);
        return 0;
      }
      case WM_COMMAND:
        if (LOWORD(wp) == kMenuViewCompact) {
          SetDisplayMode(BandDisplayMode::Compact);
          return 0;
        }
        if (LOWORD(wp) == kMenuViewFull) {
          SetDisplayMode(BandDisplayMode::Full);
          return 0;
        }
        break;
      case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
      case WM_MOUSELEAVE:
        OnMouseLeave();
        return 0;
      case WM_DESTROY:
        StopPipeClient(false);
        StopCompactTitleTimer(true);
        StopProgressTimer();
        ReleaseAccessibleProvider();
        if (_compactTitlePopup) {
          ::DestroyWindow(_compactTitlePopup);
          _compactTitlePopup = nullptr;
        }
        if (_tooltip) {
          ::DestroyWindow(_tooltip);
          _tooltip = nullptr;
        }
        ::KillTimer(hwnd, kVisibleAuditTimerId);
        ReleaseTextFont();
        ReleaseBackBuffer();
        return 0;
      case WM_TIMER:
        if (wp == kCompactTitleTimerId) {
          OnCompactTitleTimer();
          return 0;
        }
        if (wp == kPipeStartTimerId) {
          ::KillTimer(hwnd, kPipeStartTimerId);
          _pipeStartTimerOn = false;
          StartPipeNow();
          return 0;
        }
        if (wp == kVisibleAuditTimerId) {
          ::KillTimer(hwnd, kVisibleAuditTimerId);
          _deferVisibleAuditUntilTick = 0;
          Layout();
          ::InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (wp == kProgressTimerId) {
          OnProgressTimer();
          return 0;
        }
        if (wp == kTitleHoverIntentTimerId) {
          OnTitleHoverIntentTimer();
          return 0;
        }
        break;
      case WM_PAINT:
        Paint(nullptr);
        return 0;
      case WM_PRINTCLIENT:
        Paint(reinterpret_cast<HDC>(wp));
        return 0;
      case WM_PRINT:
        if ((lp & PRF_CLIENT) != 0) {
          Paint(reinterpret_cast<HDC>(wp));
        }
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
  }

 private:
  LRESULT HandleAccessibleObject(WPARAM wp) {
    if (!_accessibleButtons) {
      _accessibleButtons = new (std::nothrow) widgetmusic::AccessibleButtons(this);
      if (!_accessibleButtons) return 0;
    }
    return ::LresultFromObject(IID_IAccessible, wp, _accessibleButtons);
  }

  void ReleaseAccessibleProvider() {
    if (!_accessibleButtons) return;
    _accessibleButtons->Detach();
    _accessibleButtons->Release();
    _accessibleButtons = nullptr;
  }

  const Button* ButtonForAccessibleId(long childId) const {
    switch (childId) {
      case widgetmusic::kAccessiblePrevious:
        return &_btnPrev;
      case widgetmusic::kAccessiblePlayPause:
        return &_btnPlayPause;
      case widgetmusic::kAccessibleNext:
        return &_btnNext;
      default:
        return nullptr;
    }
  }

  Button* ButtonForAccessibleId(long childId) {
    return const_cast<Button*>(static_cast<const WidgetMusicDeskband*>(this)->ButtonForAccessibleId(childId));
  }

  long AccessibleIdAtPoint(POINT pt) const {
    if (::PtInRect(&_btnPrev.rc, pt)) return widgetmusic::kAccessiblePrevious;
    if (::PtInRect(&_btnPlayPause.rc, pt)) return widgetmusic::kAccessiblePlayPause;
    if (::PtInRect(&_btnNext.rc, pt)) return widgetmusic::kAccessibleNext;
    return 0;
  }

  HWND AccessibleWindow() const override { return _hwnd; }

  bool AccessibleButtonEnabled(long childId) const override {
    const Button* button = ButtonForAccessibleId(childId);
    return button && button->enabled;
  }

  std::wstring AccessibleButtonName(long childId) const override {
    switch (childId) {
      case widgetmusic::kAccessiblePrevious:
        return L"Previous";
      case widgetmusic::kAccessiblePlayPause:
        return L"Play/Pause";
      case widgetmusic::kAccessibleNext:
        return L"Next";
      default:
        return {};
    }
  }

  RECT AccessibleButtonScreenRect(long childId) const override {
    const Button* button = ButtonForAccessibleId(childId);
    RECT rect = button ? button->rc : RECT{};
    if (_hwnd) {
      ::MapWindowPoints(_hwnd, HWND_DESKTOP, reinterpret_cast<POINT*>(&rect), 2);
    }
    return rect;
  }

  long AccessibleFocusedButton() const override { return _focusedButton; }

  void AccessibleFocusButton(long childId) override {
    if (!ButtonForAccessibleId(childId)) return;
    _focusedButton = childId;
    if (_hwnd) ::SetFocus(_hwnd);
    NotifyAccessibleFocus();
    InvalidateButtons();
  }

  bool AccessibleInvokeButton(long childId) override {
    Button* button = ButtonForAccessibleId(childId);
    if (!button || !button->enabled) return false;
    AccessibleFocusButton(childId);
    if (childId == widgetmusic::kAccessiblePrevious) {
      SendCommand("previous");
      return true;
    }
    if (childId == widgetmusic::kAccessibleNext) {
      SendCommand("next");
      return true;
    }
    std::string target = OptimisticPlayPauseTarget();
    if (target.empty()) return false;
    SendCommand(target);
    InvalidateButtons();
    if (_hwnd) ::UpdateWindow(_hwnd);
    NotifyAccessibleState(widgetmusic::kAccessiblePlayPause);
    return true;
  }

  bool OnKeyDown(UINT key) {
    if (key == VK_LEFT || key == VK_RIGHT) {
      long next = _focusedButton + (key == VK_LEFT ? -1 : 1);
      if (next < widgetmusic::kAccessiblePrevious) next = widgetmusic::kAccessibleNext;
      if (next > widgetmusic::kAccessibleNext) next = widgetmusic::kAccessiblePrevious;
      AccessibleFocusButton(next);
      return true;
    }
    if (key == VK_RETURN || key == VK_SPACE) {
      (void)AccessibleInvokeButton(_focusedButton);
      return true;
    }
    return false;
  }

  void NotifyAccessibleFocus() const {
    if (_hwnd) ::NotifyWinEvent(EVENT_OBJECT_FOCUS, _hwnd, OBJID_CLIENT, _focusedButton);
  }

  void NotifyAccessibleState(long childId) const {
    if (_hwnd) ::NotifyWinEvent(EVENT_OBJECT_STATECHANGE, _hwnd, OBJID_CLIENT, childId);
  }

  bool IsCompactMode() const { return _bandMode == BandDisplayMode::Compact; }

  bool IsFullMode() const { return _bandMode == BandDisplayMode::Full; }

  int DesiredBandWidth() const { return IsCompactMode() ? kBandCompactWidth : kBandActualWidth; }

  const wchar_t* BandModeName() const {
    switch (_bandMode) {
      case BandDisplayMode::Full:
        return L"Full";
      case BandDisplayMode::Compact:
        return L"Compact";
    }
    return L"Unknown";
  }

  bool HasPressedButton() const {
    return _btnPrev.pressed || _btnPlayPause.pressed || _btnNext.pressed;
  }

  bool IsMouseOrCaptureActive() const {
    return _mouseInClient || HasPressedButton() || (_hwnd && ::GetCapture() == _hwnd);
  }

  std::string EffectivePlaybackLocked(DWORD now) const {
    if (_optimisticActive && now <= _optimisticUntilTick && !_optimisticPlayback.empty()) return _optimisticPlayback;
    return _state.playback;
  }

  void ResolveCommandTarget(IUnknown* site) {
    SafeRelease(&_commandTarget);
    if (!site) return;

    HRESULT hr = site->QueryInterface(__uuidof(IOleCommandTarget), reinterpret_cast<void**>(&_commandTarget));
    if (SUCCEEDED(hr) && _commandTarget) {
      LogDebugLine(L"CommandTarget direct ok");
      return;
    }

    IServiceProvider* serviceProvider = nullptr;
    hr = site->QueryInterface(__uuidof(IServiceProvider), reinterpret_cast<void**>(&serviceProvider));
    if (SUCCEEDED(hr) && serviceProvider) {
      hr = serviceProvider->QueryService(SID_SBandSite, __uuidof(IOleCommandTarget),
                                         reinterpret_cast<void**>(&_commandTarget));
      serviceProvider->Release();
      if (SUCCEEDED(hr) && _commandTarget) {
        LogDebugLine(L"CommandTarget via SID_SBandSite ok");
        return;
      }
    }

    LogDebugLine(L"CommandTarget unavailable");
  }

  void NotifyBandInfoChanged() {
    if (!_commandTarget) {
      LogDebugLine(L"BandInfoChanged skipped: no command target");
      return;
    }

    HRESULT hr = _commandTarget->Exec(&CGID_DeskBand, DBID_BANDINFOCHANGED, OLECMDEXECOPT_DONTPROMPTUSER, nullptr,
                                      nullptr);
    LogDebugLine(L"BandInfoChanged hr=" + std::to_wstring(static_cast<long>(hr)) +
                 L" width=" + std::to_wstring(DesiredBandWidth()));
  }

  void ApplyCurrentBandSize() {
    if (!_hwnd) return;
    const int targetWidth = DesiredBandWidth();
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    int x = 0;
    int y = 0;

    HWND parent = ::GetParent(_hwnd);
    RECT wr{};
    if (parent && ::GetWindowRect(_hwnd, &wr)) {
      POINT pts[2]{{wr.left, wr.top}, {wr.right, wr.bottom}};
      ::MapWindowPoints(HWND_DESKTOP, parent, pts, 2);
      x = pts[1].x - targetWidth;
      y = pts[0].y;
      if (x < 0) x = 0;
      LogDebugLine(L"ApplyBandSize right-anchor x=" + std::to_wstring(x) + L" y=" + std::to_wstring(y) +
                   L" width=" + std::to_wstring(targetWidth));
    } else {
      flags |= SWP_NOMOVE;
    }

    ::SetWindowPos(_hwnd, nullptr, x, y, targetWidth, kBandHeight, flags);
    Layout();
    ::InvalidateRect(_hwnd, nullptr, FALSE);
  }

  void ResetDisconnectedState() {
    std::lock_guard<std::mutex> lock(_stateMu);
    _state.connecting = false;
    _state.connected = false;
    _state.has_session = false;
    _state.app.clear();
    _state.title.clear();
    _state.artist.clear();
    _state.playback = "unknown";
    _state.can_prev = false;
    _state.can_next = false;
    _state.can_play_pause = false;
    _state.refreshing = false;
    _state.has_timeline = false;
    _state.position_ms = 0;
    _state.duration_ms = 0;
    _progressSnapshotTick.store(0, std::memory_order_release);
    _optimisticActive = false;
    _optimisticPlayback.clear();
  }

  void StartPipeNow() {
    if (!_hwnd || _pipeStarted) return;
    if (_pipeStartTimerOn) {
      ::KillTimer(_hwnd, kPipeStartTimerId);
      _pipeStartTimerOn = false;
    }
    _pipe.SetStateSink(&_state, &_stateMu);
    _pipe.Start(_hwnd);
    _pipeStarted = true;
  }

  void SchedulePipeStart(DWORD delayMs = kStartupPipeDelayMs) {
    if (!_hwnd || _pipeStarted) return;
    if (delayMs < 500) delayMs = 500;
    if (::SetTimer(_hwnd, kPipeStartTimerId, delayMs, nullptr) != 0) _pipeStartTimerOn = true;
  }

  void StopPipeClient(bool resetState) {
    if (_hwnd && _pipeStartTimerOn) ::KillTimer(_hwnd, kPipeStartTimerId);
    _pipeStartTimerOn = false;
    if (_pipeStarted) {
      _pipe.Stop();
      _pipeStarted = false;
    }
    StopProgressTimer();
    if (resetState) {
      ResetDisconnectedState();
      if (_hwnd) ::InvalidateRect(_hwnd, nullptr, FALSE);
    }
  }

  void StopCompactTitleTimer(bool clearText) {
    if (_hwnd && _compactTitleTimerOn) ::KillTimer(_hwnd, kCompactTitleTimerId);
    _compactTitleTimerOn = false;
    StopTitleHoverIntentTimer();
    HideCompactTitlePopup(clearText);
  }

  TOOLINFOW BuildCompactTitlePopupToolInfo() const {
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
    ti.hwnd = _hwnd;
    ti.uId = kCompactTitlePopupToolId;
    ti.hinst = g_hInstance;
    ti.lpszText = const_cast<LPWSTR>(_compactTitleText.c_str());
    return ti;
  }

  void UpdateCompactTitlePopupPosition() {
    if (!_compactTitlePopup || !_hwnd) return;
    RECT wr{};
    if (!::GetWindowRect(_hwnd, &wr)) return;

    TOOLINFOW ti = BuildCompactTitlePopupToolInfo();
    LRESULT bubble = ::SendMessageW(_compactTitlePopup, TTM_GETBUBBLESIZE, 0, reinterpret_cast<LPARAM>(&ti));
    int tipW = (bubble == 0) ? 0 : static_cast<int>(LOWORD(static_cast<DWORD_PTR>(bubble)));
    int tipH = (bubble == 0) ? 0 : static_cast<int>(HIWORD(static_cast<DWORD_PTR>(bubble)));

    MONITORINFO mi{sizeof(mi)};
    HMONITOR monitor = ::MonitorFromWindow(_hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor || !::GetMonitorInfoW(monitor, &mi)) {
      mi.rcMonitor = wr;
    }

    const int dpi = static_cast<int>(::GetDpiForWindow(_hwnd));
    const int padding = ScaleForDpi(4, dpi);
    const int gap = ScaleForDpi(kCompactTitlePopupGap, dpi);
    int widgetW = wr.right - wr.left;
    int x = wr.left + (widgetW / 2);
    if (tipW > 0) x = wr.left + ((widgetW - tipW) / 2);
    x = (std::max)(static_cast<int>(mi.rcMonitor.left) + padding,
                   (std::min)(x, static_cast<int>(mi.rcMonitor.right) - tipW - padding));
    const int above = wr.top - tipH - gap;
    const int below = wr.bottom + gap;
    int y = above >= mi.rcMonitor.top + padding ? above : below;
    y = (std::max)(static_cast<int>(mi.rcMonitor.top) + padding,
                   (std::min)(y, static_cast<int>(mi.rcMonitor.bottom) - tipH - padding));

    ::SendMessageW(_compactTitlePopup, TTM_TRACKPOSITION, 0, MAKELPARAM(x, y));
  }

  void EnsureCompactTitlePopup() {
    if (_compactTitlePopup || !_hwnd) return;

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES;
    ::InitCommonControlsEx(&icc);

    _compactTitlePopup = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                           WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                           CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                           _hwnd, nullptr, g_hInstance, nullptr);
    if (!_compactTitlePopup) return;

    ::SendMessageW(_compactTitlePopup, TTM_ACTIVATE, TRUE, 0);
    ::SendMessageW(_compactTitlePopup, TTM_SETMAXTIPWIDTH, 0, kCompactTitlePopupMaxWidth);
    ::SendMessageW(_compactTitlePopup, TTM_SETDELAYTIME, TTDT_INITIAL, 0);
    ::SendMessageW(_compactTitlePopup, TTM_SETDELAYTIME, TTDT_RESHOW, 0);
    ::SendMessageW(_compactTitlePopup, TTM_SETDELAYTIME, TTDT_AUTOPOP, kCompactTitleRevealMs + 1000);

    TOOLINFOW ti = BuildCompactTitlePopupToolInfo();
    ti.lpszText = const_cast<LPWSTR>(L"");
    ::SendMessageW(_compactTitlePopup, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
  }

  void StopTitleHoverIntentTimer() {
    if (_hwnd && _titleHoverIntentTimerOn) ::KillTimer(_hwnd, kTitleHoverIntentTimerId);
    _titleHoverIntentTimerOn = false;
  }

  void StartTitleHoverIntentTimer() {
    if (!_hwnd || _titleHoverIntentTimerOn) return;
    if (::SetTimer(_hwnd, kTitleHoverIntentTimerId, kTitleHoverIntentDelayMs, nullptr) != 0) {
      _titleHoverIntentTimerOn = true;
    }
  }

  bool IsPointInMediaButtons(POINT pt) const {
    return ::PtInRect(&_btnPrev.rc, pt) || ::PtInRect(&_btnPlayPause.rc, pt) || ::PtInRect(&_btnNext.rc, pt);
  }

  void OnTitleHoverIntentTimer() {
    StopTitleHoverIntentTimer();
    if (!_hwnd || ::GetCapture() == _hwnd || _hoverTitlePopupActive) return;
    DWORD now = ::GetTickCount();
    if (now < _titlePopupSuppressUntilTick) return;
    if (IsPointInMediaButtons(_lastMousePoint)) return;
    if (IsFullMode()) {
      if (_textRc.right <= _textRc.left || !::PtInRect(&_textRc, _lastMousePoint)) return;
    } else {
      RECT client{};
      ::GetClientRect(_hwnd, &client);
      if (!::PtInRect(&client, _lastMousePoint)) return;
    }

    BandState s;
    {
      std::lock_guard<std::mutex> lock(_stateMu);
      s = _state;
    }
    std::wstring hoverText = BuildTrackPopupText(s);
    if (hoverText.empty()) return;
    ShowCompactTitlePopup(hoverText);
    _hoverTitlePopupActive = true;
  }

  void HideCompactTitlePopup(bool clearText) {
    _compactTitlePopupVisible = false;
    _hoverTitlePopupActive = false;
    if (_compactTitlePopup && _hwnd) {
      TOOLINFOW ti = BuildCompactTitlePopupToolInfo();
      ::SendMessageW(_compactTitlePopup, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&ti));
      ::SendMessageW(_compactTitlePopup, TTM_POP, 0, 0);
    }
    if (clearText) {
      _compactTitleText.clear();
    }
  }

  void ShowCompactTitlePopup(const std::wstring& text) {
    if (!_hwnd || text.empty()) return;
    EnsureCompactTitlePopup();
    _compactTitleText = text;
    _compactTitlePopupVisible = true;
    if (_compactTitlePopup && _hwnd) {
      TOOLINFOW ti = BuildCompactTitlePopupToolInfo();
      ti.lpszText = const_cast<LPWSTR>(_compactTitleText.c_str());
      ::SendMessageW(_compactTitlePopup, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
      UpdateCompactTitlePopupPosition();
      ::SendMessageW(_compactTitlePopup, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&ti));
    }
  }

  void StartCompactTitleReveal(const std::wstring& text) {
    if (!_hwnd || text.empty()) return;
    DWORD now = ::GetTickCount();
    if (now < _titlePopupSuppressUntilTick) return;
    if (_compactTitleTimerOn) {
      ::KillTimer(_hwnd, kCompactTitleTimerId);
      _compactTitleTimerOn = false;
    }
    _hoverTitlePopupActive = false;
    ShowCompactTitlePopup(text);
    if (::SetTimer(_hwnd, kCompactTitleTimerId, kCompactTitleRevealMs, nullptr) != 0) {
      _compactTitleTimerOn = true;
    }
  }

  void OnCompactTitleTimer() {
    if (_hwnd && _compactTitleTimerOn) ::KillTimer(_hwnd, kCompactTitleTimerId);
    _compactTitleTimerOn = false;
    if (_hoverTitlePopupActive) return;
    HideCompactTitlePopup(false);
  }

  void SetDisplayMode(BandDisplayMode nextMode) {
    if (!_hwnd || _bandMode == nextMode) return;
    StartPipeNow();
    StopCompactTitleTimer(true);
    StopProgressTimer();
    _bandMode = nextMode;
    _btnPrev.pressed = _btnPlayPause.pressed = _btnNext.pressed = false;
    _btnPrev.hot = _btnPlayPause.hot = _btnNext.hot = false;
    BandState stateSnapshot;
    {
      std::lock_guard<std::mutex> lock(_stateMu);
      stateSnapshot = _state;
    }
    UpdateProgressTimerState(stateSnapshot);
    NotifyBandInfoChanged();
    ApplyCurrentBandSize();
  }

  std::wstring BuildTrackPopupText(const BandState& s) const {
    if (!s.connected || !s.has_session || s.title.empty()) return {};
    if (s.artist.empty()) return s.title;
    std::wstring t = s.title;
    t.append(L" \x2014 ");
    t.append(s.artist);
    return t;
  }

  void ShowModeContextMenu(int sx, int sy) {
    if (!_hwnd) return;
    HMENU menu = ::CreatePopupMenu();
    if (!menu) return;

    const bool compactMode = IsCompactMode();
    const UINT compactFlags = MF_STRING | (compactMode ? (MF_GRAYED | MF_CHECKED) : 0);
    const UINT fullFlags = MF_STRING | (!compactMode ? (MF_GRAYED | MF_CHECKED) : 0);
    ::AppendMenuW(menu, compactFlags, kMenuViewCompact, L"Compact view");
    ::AppendMenuW(menu, fullFlags, kMenuViewFull, L"Full view");

    StartPipeNow();
    UINT cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, sx, sy, 0, _hwnd, nullptr);
    ::DestroyMenu(menu);

    if (cmd == kMenuViewCompact) {
      SetDisplayMode(BandDisplayMode::Compact);
    } else if (cmd == kMenuViewFull) {
      SetDisplayMode(BandDisplayMode::Full);
    }
  }

  void OnStateUpdated() {
    LogDebugLine(L"OnStateUpdated mode=" + std::wstring(BandModeName()));

    BandState current;
    {
      std::lock_guard<std::mutex> lock(_stateMu);
      current = _state;
    }
    _progressSnapshotTick.store(current.has_timeline ? ::GetTickCount() : 0, std::memory_order_release);
    std::wstring primary = BuildPrimaryText(current);
    std::wstring popupTrack = BuildTrackPopupText(current);
    const bool primaryChanged = primary != _lastPrimaryText;
    const bool popupTrackChanged = popupTrack != _lastTrackPopupText;
    const bool playbackChanged = current.playback != _lastPlaybackState;
    if (!popupTrack.empty() && popupTrackChanged) {
      StartCompactTitleReveal(popupTrack);
    }
    _lastPrimaryText = primary;
    _lastTrackPopupText = popupTrack;
    _lastPlaybackState = current.playback;
    UpdateProgressTimerState(current);

    if (_hwnd) {
      auto addRect = [](RECT* dirty, bool* hasDirty, RECT rr) {
        if (rr.right <= rr.left || rr.bottom <= rr.top) return;
        if (!*hasDirty) {
          *dirty = rr;
          *hasDirty = true;
        } else {
          ::UnionRect(dirty, dirty, &rr);
        }
      };

      const bool prevEnabled = _btnPrev.enabled;
      const bool playEnabled = _btnPlayPause.enabled;
      const bool nextEnabled = _btnNext.enabled;
      const bool actionableMedia = current.connected && current.has_session;
      _btnPrev.enabled = actionableMedia && current.can_prev;
      _btnPlayPause.enabled = actionableMedia && current.can_play_pause;
      _btnNext.enabled = actionableMedia && current.can_next;
      _btnPrev.kind = 0;
      _btnPlayPause.kind = 1;
      _btnNext.kind = 2;
      const bool buttonsChanged =
          (prevEnabled != _btnPrev.enabled) || (playEnabled != _btnPlayPause.enabled) || (nextEnabled != _btnNext.enabled);
      if (prevEnabled != _btnPrev.enabled) NotifyAccessibleState(widgetmusic::kAccessiblePrevious);
      if (playEnabled != _btnPlayPause.enabled || playbackChanged) {
        NotifyAccessibleState(widgetmusic::kAccessiblePlayPause);
      }
      if (nextEnabled != _btnNext.enabled) NotifyAccessibleState(widgetmusic::kAccessibleNext);

      RECT dirty{};
      bool hasDirty = false;
      if (IsFullMode()) {
        addRect(&dirty, &hasDirty, _seekRc);
        if (primaryChanged || playbackChanged) addRect(&dirty, &hasDirty, _textRc);
      } else if (primaryChanged || popupTrackChanged) {
        addRect(&dirty, &hasDirty, _textRc);
      }

      if (buttonsChanged) {
        RECT btnDirty = _btnPrev.rc;
        ::UnionRect(&btnDirty, &btnDirty, &_btnPlayPause.rc);
        ::UnionRect(&btnDirty, &btnDirty, &_btnNext.rc);
        ::InflateRect(&btnDirty, 2, 2);
        addRect(&dirty, &hasDirty, btnDirty);
      }

      if (!hasDirty) {
        if (IsFullMode()) addRect(&dirty, &hasDirty, _seekRc);
        addRect(&dirty, &hasDirty, _textRc);
      }

      if (hasDirty) {
        ::InvalidateRect(_hwnd, &dirty, FALSE);
      } else {
        ::InvalidateRect(_hwnd, nullptr, FALSE);
      }
    }
  }

  void EnsureTooltip() {
    if (_tooltip || !_hwnd) return;

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES;
    ::InitCommonControlsEx(&icc);

    _tooltip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                 WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                 _hwnd, nullptr, g_hInstance, nullptr);
    if (!_tooltip) return;

    ::SendMessageW(_tooltip, TTM_SETMAXTIPWIDTH, 0, 240);
    AddTooltipTool(1, _btnPrev.rc, const_cast<LPWSTR>(L"Previous"));
    AddTooltipTool(2, _btnPlayPause.rc, const_cast<LPWSTR>(L"Play / pause"));
    AddTooltipTool(3, _btnNext.rc, const_cast<LPWSTR>(L"Next"));
  }

  void AddTooltipTool(UINT_PTR id, const RECT& rc, LPWSTR text) {
    if (!_tooltip || !_hwnd) return;
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_SUBCLASS;
    ti.hwnd = _hwnd;
    ti.uId = id;
    ti.rect = rc;
    ti.lpszText = text;
    ::SendMessageW(_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
  }

  void UpdateTooltipTool(UINT_PTR id, const RECT& rc) {
    if (!_tooltip || !_hwnd) return;
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.hwnd = _hwnd;
    ti.uId = id;
    ti.rect = rc;
    ::SendMessageW(_tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));
  }

  void UpdateTooltipRects() {
    UpdateTooltipTool(1, _btnPrev.rc);
    UpdateTooltipTool(2, _btnPlayPause.rc);
    UpdateTooltipTool(3, _btnNext.rc);
  }

  void DeferVisibleAudit(DWORD delayMs = 350) {
    if (!_hwnd) return;
    _deferVisibleAuditUntilTick = ::GetTickCount() + delayMs;
    ::SetTimer(_hwnd, kVisibleAuditTimerId, delayMs, nullptr);
  }

  void RequestBackgroundRefresh(bool force) {
    (void)force;
    _cachedBgValid = false;
  }

  COLORREF ResolveImmediateBackground(HWND hwnd) {
    if (IsHighContrast()) return ::GetSysColor(COLOR_BTNFACE);
    if (!_cachedBgValid && hwnd) {
      // The visible taskbar surface can differ from DWM colorization after composition.
      COLORREF sampled = SampleAdjacentTaskbarColor(hwnd, CLR_INVALID);
      COLORREF dwmColor = GetTaskbarColorViaDWM();
      _cachedBg = sampled != CLR_INVALID ? sampled
                                        : (dwmColor != CLR_INVALID ? dwmColor : ::GetSysColor(COLOR_3DFACE));
      _cachedBgValid = true;
    }
    return _cachedBgValid ? _cachedBg : ::GetSysColor(COLOR_3DFACE);
  }

  void PaintImmediateBackground(HWND hwnd, HDC hdc) {
    if (!hwnd || !hdc) return;
    RECT rc{};
    if (!::GetClientRect(hwnd, &rc)) return;
    HBRUSH br = ::CreateSolidBrush(ResolveImmediateBackground(hwnd));
    if (!br) return;
    ::FillRect(hdc, &rc, br);
    ::DeleteObject(br);
  }

  void ReleaseBackBuffer() {
    if (_backDc && _backOldBmp) {
      ::SelectObject(_backDc, _backOldBmp);
    }
    if (_backBmp) {
      ::DeleteObject(_backBmp);
      _backBmp = nullptr;
    }
    if (_backDc) {
      ::DeleteDC(_backDc);
      _backDc = nullptr;
    }
    _backOldBmp = nullptr;
    _backBits = nullptr;
    _backW = 0;
    _backH = 0;
  }

  void ReleaseTextFont() {
    if (_textFont) {
      ::DeleteObject(_textFont);
      _textFont = nullptr;
    }
    _textFontDpiY = 0;
  }

  HFONT EnsureTextFont(HDC hdc) {
    int dpiY = ::GetDeviceCaps(hdc, LOGPIXELSY);
    if (_textFont && _textFontDpiY == dpiY) return _textFont;

    ReleaseTextFont();

    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(11, dpiY, 96);
    lf.lfWeight = FW_SEMIBOLD;
    lf.lfQuality = CLEARTYPE_QUALITY;
    StringCchCopyW(lf.lfFaceName, std::size(lf.lfFaceName), L"Segoe UI");
    _textFont = ::CreateFontIndirectW(&lf);
    _textFontDpiY = dpiY;
    return _textFont;
  }

  bool EnsureBackBuffer(HDC hdc, int w, int h) {
    if (_backDc && _backBmp && _backW == w && _backH == h && _backBits) return true;

    ReleaseBackBuffer();

    _backDc = ::CreateCompatibleDC(hdc);
    if (!_backDc) return false;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    _backBmp = ::CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &_backBits, nullptr, 0);
    if (!_backBmp || !_backBits) {
      ReleaseBackBuffer();
      return false;
    }

    _backOldBmp = ::SelectObject(_backDc, _backBmp);
    _backW = w;
    _backH = h;
    return true;
  }

  void InvalidateButtons() {
    if (!_hwnd) return;
    RECT dirty = _btnPrev.rc;
    ::UnionRect(&dirty, &dirty, &_btnPlayPause.rc);
    ::UnionRect(&dirty, &dirty, &_btnNext.rc);
    ::InflateRect(&dirty, 2, 2);
    ::InvalidateRect(_hwnd, &dirty, FALSE);
  }

  void AuditVisibleBand() {
    if (!_hwnd || _zOrderAdjusting) return;

    DWORD nowTick = ::GetTickCount();
    if (_deferVisibleAuditUntilTick != 0 && nowTick < _deferVisibleAuditUntilTick) return;
    DWORD auditInterval = kVisibleAuditMinIntervalMs;
    if (_lastVisibleAuditTick != 0 && nowTick - _lastVisibleAuditTick < auditInterval) return;
    _lastVisibleAuditTick = nowTick;

    RECT own{};
    if (!::GetWindowRect(_hwnd, &own)) return;
    const int ownWidth = own.right - own.left;
    if (ownWidth <= 0) return;

    HWND parent = ::GetParent(_hwnd);
    if (!parent) return;

    int occludedInset = 0;
    bool canPromoteOverBlankTaskList = false;
    for (HWND child = ::GetWindow(parent, GW_CHILD); child && child != _hwnd; child = ::GetWindow(child, GW_HWNDNEXT)) {
      if (!::IsWindowVisible(child)) continue;

      RECT other{};
      if (!::GetWindowRect(child, &other)) continue;

      RECT overlap{};
      if (!IntersectRectSafe(own, other, &overlap)) continue;

      occludedInset = max(occludedInset, min(overlap.right, own.right) - own.left);

      std::wstring cls = WindowClassName(child);
      if (IsTaskListClass(cls)) {
        if (ScreenRegionLooksEmpty(overlap)) canPromoteOverBlankTaskList = true;
      }
    }

    if (occludedInset < 0) occludedInset = 0;
    if (occludedInset > ownWidth) occludedInset = ownWidth;

    const int newInset = canPromoteOverBlankTaskList ? 0 : occludedInset;
    const bool reportedPromoted = canPromoteOverBlankTaskList || (_promotedOverBlankTaskList && occludedInset == 0);
    if (newInset != _visibleInsetLeft || reportedPromoted != _promotedOverBlankTaskList) {
      LogDebugLine(L"VisibleBand inset=" + std::to_wstring(newInset) +
                   L" occluded=" + std::to_wstring(occludedInset) +
                   L" promote=" + (reportedPromoted ? L"1" : L"0"));
    }
    _visibleInsetLeft = newInset;

    if (canPromoteOverBlankTaskList && !_promotedOverBlankTaskList) {
      _promotedOverBlankTaskList = true;
      _zOrderAdjusting = true;
      ::SetWindowPos(_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      _zOrderAdjusting = false;
    } else if (!canPromoteOverBlankTaskList && occludedInset > 0) {
      _promotedOverBlankTaskList = false;
    }
  }

  void Layout() {
    RECT rc{};
    ::GetClientRect(_hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) {
      _btnPrev.rc = {};
      _btnPlayPause.rc = {};
      _btnNext.rc = {};
      _textRc = {};
      _seekRc = {};
      return;
    }

    AuditVisibleBand();

    int visibleLeft = _visibleInsetLeft;
    if (visibleLeft < 0) visibleLeft = 0;
    if (visibleLeft > w - 1) visibleLeft = w - 1;
    const int visibleW = w - visibleLeft;

    const int pad = kFullPad;
    int gap = IsCompactMode() ? 2 : 4;
    int sideBtn = h - 16;
    if (sideBtn < 20) sideBtn = 20;
    if (sideBtn > 24) sideBtn = 24;
    int playBtn = h - 8;
    if (playBtn < 28) playBtn = 28;
    if (playBtn > 32) playBtn = 32;
    int usable = visibleW - (pad * 2);
    if (usable < 1) usable = 1;
    if (sideBtn * 2 + playBtn + gap * 2 > usable) {
      gap = 2;
      playBtn = min(playBtn, max(24, usable / 3));
      sideBtn = min(sideBtn, max(18, (usable - playBtn - gap * 2) / 2));
    }

    if (IsCompactMode()) {
      _textRc = {};
      _seekRc = {};
      int buttonsWidth = sideBtn * 2 + playBtn + gap * 2;
      int x = visibleLeft + (visibleW - buttonsWidth) / 2;
      if (x < visibleLeft + 2) x = visibleLeft + 2;
      int sideTop = (h - sideBtn) / 2;
      int playTop = (h - playBtn) / 2;
      _btnPrev.rc = {x, sideTop, x + sideBtn, sideTop + sideBtn};
      x += sideBtn + gap;
      _btnPlayPause.rc = {x, playTop, x + playBtn, playTop + playBtn};
      x += playBtn + gap;
      _btnNext.rc = {x, sideTop, x + sideBtn, sideTop + sideBtn};
    } else {
      int xRight = rc.right - pad;
      int sideTop = (h - sideBtn) / 2;
      int playTop = (h - playBtn) / 2;
      _btnNext.rc = {xRight - sideBtn, sideTop, xRight, sideTop + sideBtn};
      xRight -= sideBtn + gap;
      _btnPlayPause.rc = {xRight - playBtn, playTop, xRight, playTop + playBtn};
      xRight -= playBtn + gap;
      _btnPrev.rc = {xRight - sideBtn, sideTop, xRight, sideTop + sideBtn};

      int textRight = _btnPrev.rc.left - 8;
      int textLeft = visibleLeft + pad + kFullTextInsetLeft;
      if (textRight < textLeft) textRight = textLeft;
      int seekTop = kFullSeekTrackTop;
      const int maxSeekTop = h - kSeekTrackHeight;
      if (seekTop > maxSeekTop) seekTop = maxSeekTop;
      if (seekTop < 14) seekTop = 14;
      _textRc = {textLeft, kFullProgressTextTop, textRight, seekTop - kFullProgressTextSeekGap};
      if (_textRc.bottom <= _textRc.top) _textRc.bottom = _textRc.top + 1;
      _seekRc = {textLeft, seekTop, textRight, min(h, seekTop + kSeekTrackHeight)};
      if (_seekRc.right <= _seekRc.left || _seekRc.bottom <= _seekRc.top) _seekRc = {};
    }
    UpdateTooltipRects();
    if (_compactTitlePopupVisible) {
      UpdateCompactTitlePopupPosition();
    }
  }

  void OnMouseDown(int x, int y) {
    if (!_hwnd) return;
    _titlePopupSuppressUntilTick = ::GetTickCount() + kTitleSuppressAfterClickMs;
    StopTitleHoverIntentTimer();
    if (_compactTitlePopupVisible) HideCompactTitlePopup(false);
    _mouseInClient = true;
    TrackMouseLeave();
    ::SetCapture(_hwnd);
    POINT pt{ x, y };
    _lastMousePoint = pt;
    const long focused = AccessibleIdAtPoint(pt);
    if (focused != 0) AccessibleFocusButton(focused);
    UpdateHotButtons(pt);
    if (_btnPrev.enabled && ::PtInRect(&_btnPrev.rc, pt)) _btnPrev.pressed = true;
    if (_btnPlayPause.enabled && ::PtInRect(&_btnPlayPause.rc, pt)) _btnPlayPause.pressed = true;
    if (_btnNext.enabled && ::PtInRect(&_btnNext.rc, pt)) _btnNext.pressed = true;
    InvalidateButtons();
  }

  void OnMouseMove(int x, int y) {
    if (!_hwnd) return;
    POINT pt{ x, y };
    _lastMousePoint = pt;
    _mouseInClient = true;
    TrackMouseLeave();
    const bool capturing = (::GetCapture() == _hwnd);
    const bool inText = (_textRc.right > _textRc.left) && ::PtInRect(&_textRc, pt);
    const bool inButtons = IsPointInMediaButtons(pt);

    const DWORD now = ::GetTickCount();
    bool hoverZone = false;
    if (!capturing && !inButtons && now >= _titlePopupSuppressUntilTick) {
      if (IsFullMode()) {
        hoverZone = inText;
      } else {
        RECT client{};
        ::GetClientRect(_hwnd, &client);
        hoverZone = ::PtInRect(&client, pt) != FALSE;
      }
    }
    const bool allowHoverTitle = hoverZone;
    if (allowHoverTitle) {
      if (!_hoverTitlePopupActive && !_compactTitleTimerOn) StartTitleHoverIntentTimer();
    } else {
      StopTitleHoverIntentTimer();
      if (_hoverTitlePopupActive || (inButtons && _compactTitlePopupVisible)) {
        HideCompactTitlePopup(false);
      }
    }

    bool hPrev = _btnPrev.hot;
    bool hPP = _btnPlayPause.hot;
    bool hNext = _btnNext.hot;
    UpdateHotButtons(pt);

    if (!capturing) {
      if (hPrev != _btnPrev.hot || hPP != _btnPlayPause.hot || hNext != _btnNext.hot) {
        InvalidateButtons();
      }
      return;
    }

    bool pPrev = _btnPrev.pressed;
    bool pPP = _btnPlayPause.pressed;
    bool pNext = _btnNext.pressed;

    _btnPrev.pressed = _btnPrev.enabled && ::PtInRect(&_btnPrev.rc, pt);
    _btnPlayPause.pressed = _btnPlayPause.enabled && ::PtInRect(&_btnPlayPause.rc, pt);
    _btnNext.pressed = _btnNext.enabled && ::PtInRect(&_btnNext.rc, pt);

    if (pPrev != _btnPrev.pressed || pPP != _btnPlayPause.pressed || pNext != _btnNext.pressed ||
        hPrev != _btnPrev.hot || hPP != _btnPlayPause.hot || hNext != _btnNext.hot) {
      InvalidateButtons();
    }
  }

  void TrackMouseLeave() {
    if (_trackingMouse || !_hwnd) return;
    TRACKMOUSEEVENT tme{};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = _hwnd;
    if (::TrackMouseEvent(&tme)) _trackingMouse = true;
  }

  void UpdateHotButtons(POINT pt) {
    _btnPrev.hot = _btnPrev.enabled && ::PtInRect(&_btnPrev.rc, pt);
    _btnPlayPause.hot = _btnPlayPause.enabled && ::PtInRect(&_btnPlayPause.rc, pt);
    _btnNext.hot = _btnNext.enabled && ::PtInRect(&_btnNext.rc, pt);
  }

  void OnMouseLeave() {
    _trackingMouse = false;
    _mouseInClient = false;
    StopTitleHoverIntentTimer();
    if (_hoverTitlePopupActive) HideCompactTitlePopup(false);
    if (!_btnPrev.hot && !_btnPlayPause.hot && !_btnNext.hot) return;
    _btnPrev.hot = false;
    _btnPlayPause.hot = false;
    _btnNext.hot = false;
    InvalidateButtons();
  }

  void OnMouseUp(int x, int y) {
    if (!_hwnd) return;
    if (::GetCapture() == _hwnd) ::ReleaseCapture();
    POINT pt{ x, y };

    auto wasPressedPrev = _btnPrev.pressed;
    auto wasPressedPP = _btnPlayPause.pressed;
    auto wasPressedNext = _btnNext.pressed;

    _btnPrev.pressed = false;
    _btnPlayPause.pressed = false;
    _btnNext.pressed = false;

    InvalidateButtons();

    if (wasPressedPrev && _btnPrev.enabled && ::PtInRect(&_btnPrev.rc, pt)) {
      (void)AccessibleInvokeButton(widgetmusic::kAccessiblePrevious);
      return;
    }
    if (wasPressedNext && _btnNext.enabled && ::PtInRect(&_btnNext.rc, pt)) {
      (void)AccessibleInvokeButton(widgetmusic::kAccessibleNext);
      return;
    }
    if (wasPressedPP && _btnPlayPause.enabled && ::PtInRect(&_btnPlayPause.rc, pt)) {
      (void)AccessibleInvokeButton(widgetmusic::kAccessiblePlayPause);
      return;
    }
  }

  void SendCommand(std::string_view name) {
    StartPipeNow();
    std::string msg;
    msg.reserve(64);
    msg += "{\"";
    msg += widgetmusic::kMsgType;
    msg += "\":\"";
    msg += widgetmusic::kTypeCommand;
    msg += "\",\"";
    msg += widgetmusic::kKeyName;
    msg += "\":\"";
    msg += name;
    msg += "\"}\n";
    _pipe.SendJsonLine(std::move(msg));
  }

  std::string OptimisticPlayPauseTarget() {
    std::lock_guard<std::mutex> lock(_stateMu);
    if (!_state.connected || !_state.has_session || !_state.can_play_pause) return {};
    std::string playback = _state.playback;
    DWORD now = ::GetTickCount();
    if (_optimisticActive && now <= _optimisticUntilTick && !_optimisticPlayback.empty()) {
      playback = _optimisticPlayback;
    }

    const bool shouldPause = (playback == "playing");
    _optimisticPlayback = shouldPause ? "paused" : "playing";
    _state.playback = _optimisticPlayback;
    _optimisticUntilTick = now + 800;
    _optimisticActive = true;
    return shouldPause ? "pause" : "play";
  }

  int64_t EffectiveTimelinePositionMs(const BandState& s, DWORD nowTick) const {
    if (!s.has_timeline) return 0;
    int64_t pos = s.position_ms;
    DWORD snapshotTick = _progressSnapshotTick.load(std::memory_order_acquire);
    if (s.playback == "playing" && snapshotTick != 0 && nowTick >= snapshotTick) {
      DWORD deltaMs = nowTick - snapshotTick;
      pos += static_cast<int64_t>(deltaMs);
    }
    if (pos < 0) pos = 0;
    if (s.duration_ms > 0 && pos > s.duration_ms) pos = s.duration_ms;
    return pos;
  }

  std::wstring BuildProgressText(const BandState& s, DWORD nowTick) const {
    if (!s.connected || !s.has_session) return {};
    if (s.refreshing && !s.has_timeline) return L"Updating...";

    if (s.has_timeline) {
      int64_t posMs = EffectiveTimelinePositionMs(s, nowTick);
      std::wstring pos = FormatElapsedClock(posMs);
      if (s.duration_ms > 0) {
        return pos + L" / " + FormatElapsedClock(s.duration_ms);
      }
      return pos + L" \x2022 LIVE";
    }

    if (s.playback == "paused") return L"Paused \x2022 --:--";
    if (s.playback == "playing") return L"--:-- \x2022 LIVE";
    return {};
  }

  std::wstring BuildPrimaryText(const BandState& s) {
    if (IsFullMode()) {
      DWORD now = ::GetTickCount();
      std::wstring progress = BuildProgressText(s, now);
      if (!progress.empty()) return progress;
    }
    return PrimaryTextForState(s);
  }

  int ScaleForDpi(int value, int dpiY) const {
    if (value <= 0) return value;
    if (dpiY <= 0) dpiY = 96;
    int scaled = ::MulDiv(value, dpiY, 96);
    return max(1, scaled);
  }

  void StopProgressTimer() {
    if (_hwnd && _progressTimerOn) {
      ::KillTimer(_hwnd, kProgressTimerId);
    }
    _progressTimerOn = false;
  }

  void UpdateProgressTimerState(const BandState& s) {
    if (!_hwnd) return;
    const bool shouldRun = IsFullMode() && s.connected && s.has_session && s.has_timeline && s.playback == "playing";
    if (shouldRun) {
      if (!_progressTimerOn && ::SetTimer(_hwnd, kProgressTimerId, kProgressTimerMs, nullptr) != 0) {
        _progressTimerOn = true;
      }
    } else {
      StopProgressTimer();
    }
  }

  void OnProgressTimer() {
    if (!_hwnd || !IsFullMode()) {
      StopProgressTimer();
      return;
    }

    BandState s;
    {
      std::lock_guard<std::mutex> lock(_stateMu);
      s = _state;
    }
    if (!(s.connected && s.has_session && s.has_timeline && s.playback == "playing")) {
      StopProgressTimer();
      return;
    }

    RECT dirty = _seekRc;
    if (_textRc.right > _textRc.left) {
      if (dirty.right > dirty.left) {
        ::UnionRect(&dirty, &dirty, &_textRc);
      } else {
        dirty = _textRc;
      }
    }
    if (dirty.right > dirty.left) {
      ::InvalidateRect(_hwnd, &dirty, FALSE);
    } else {
      ::InvalidateRect(_hwnd, nullptr, FALSE);
    }
  }

  void Paint(HDC hdcIn) {
    PAINTSTRUCT ps{};
    HDC hdc = hdcIn ? hdcIn : ::BeginPaint(_hwnd, &ps);
    if (!hdc) return;

    RECT rc{};
    ::GetClientRect(_hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) {
      if (!hdcIn) ::EndPaint(_hwnd, &ps);
      return;
    }

    RECT dirtyRc = rc;
    if (!hdcIn && !::IsRectEmpty(&ps.rcPaint)) {
      RECT clipped{};
      if (::IntersectRect(&clipped, &ps.rcPaint, &rc)) {
        dirtyRc = clipped;
      }
    }

    RECT textRcClipped{};
    const bool hasTextRc = ::IntersectRect(&textRcClipped, &_textRc, &rc) != FALSE;
    const bool textOnlyPaint = !hdcIn && hasTextRc && RectContains(textRcClipped, dirtyRc);
    RECT seekRcClipped{};
    const bool hasSeekRc = ::IntersectRect(&seekRcClipped, &_seekRc, &rc) != FALSE;
    const bool seekOnlyPaint = !hdcIn && hasSeekRc && RectContains(seekRcClipped, dirtyRc);
    const RECT repaintRc = hdcIn ? rc : dirtyRc;

    if (!EnsureBackBuffer(hdc, w, h)) {
      if (!hdcIn) ::EndPaint(_hwnd, &ps);
      return;
    }

    HDC mem = _backDc;
    void* dibBits = _backBits;
    int savedDc = ::SaveDC(mem);
    if (savedDc != 0) {
      ::IntersectClipRect(mem, repaintRc.left, repaintRc.top, repaintRc.right, repaintRc.bottom);
    }

    // Background
    bool highContrast = IsHighContrast();
    COLORREF bgSample = RGB(32, 32, 32);
    COLORREF bgFill = highContrast ? ::GetSysColor(COLOR_BTNFACE) : ::GetSysColor(COLOR_3DFACE);
    if (highContrast) {
      HBRUSH br = ::CreateSolidBrush(::GetSysColor(COLOR_BTNFACE));
      ::FillRect(mem, &repaintRc, br);
      ::DeleteObject(br);
      bgSample = ::GetSysColor(COLOR_BTNFACE);
    } else {
      bgSample = ResolveImmediateBackground(_hwnd);
      bgFill = bgSample;
      HBRUSH br = ::CreateSolidBrush(bgFill);
      ::FillRect(mem, &repaintRc, br);
      ::DeleteObject(br);
    }

    COLORREF accent = ::GetSysColor(COLOR_HIGHLIGHT);
    COLORREF accentText = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
    COLORREF panelFill = highContrast ? ::GetSysColor(COLOR_BTNFACE) : bgFill;
    bool lightForeground = !highContrast && UseLightForegroundOn(panelFill);
    COLORREF fg = highContrast ? ::GetSysColor(COLOR_BTNTEXT) : (lightForeground ? RGB(245, 245, 245) : RGB(24, 24, 24));
    COLORREF fgDisabled = highContrast ? ::GetSysColor(COLOR_GRAYTEXT) : (lightForeground ? RGB(154, 154, 154) : RGB(98, 108, 118));
    COLORREF buttonFill = highContrast ? ::GetSysColor(COLOR_BTNFACE)
                                       : (lightForeground ? Blend(panelFill, RGB(255, 255, 255), 34)
                                                          : Blend(panelFill, RGB(255, 255, 255), 62));
    COLORREF outline = highContrast ? ::GetSysColor(COLOR_WINDOWTEXT)
                                    : (lightForeground ? Blend(panelFill, RGB(255, 255, 255), 72)
                                                       : Blend(panelFill, RGB(0, 0, 0), 40));

    if (highContrast) {
      RECT panelRc{0, 0, w, h};
      HBRUSH outlineBrush = ::CreateSolidBrush(outline);
      ::FrameRect(mem, &panelRc, outlineBrush);
      ::DeleteObject(outlineBrush);
    }

    // Snapshot state
    BandState s;
    DWORD nowTick = ::GetTickCount();
    {
      std::lock_guard<std::mutex> lock(_stateMu);
      s = _state;
      if (_optimisticActive && nowTick <= _optimisticUntilTick && !_optimisticPlayback.empty()) {
        s.playback = _optimisticPlayback;
      } else if (_optimisticActive && nowTick > _optimisticUntilTick) {
        _optimisticActive = false;
        _optimisticPlayback.clear();
      }
    }

    const bool actionableMedia = s.connected && s.has_session;
    _btnPrev.enabled = actionableMedia && s.can_prev;
    _btnNext.enabled = actionableMedia && s.can_next;
    _btnPlayPause.enabled = actionableMedia && s.can_play_pause;
    _btnPrev.kind = 0;
    _btnPlayPause.kind = 1;
    _btnNext.kind = 2;

    auto fillRectColor = [&](const RECT& rr, COLORREF color) {
      HBRUSH br = ::CreateSolidBrush(color);
      ::FillRect(mem, &rr, br);
      ::DeleteObject(br);
    };

    ::SetBkMode(mem, TRANSPARENT);

    // Text
    HFONT hTextFont = EnsureTextFont(hdc);
    int dpiY = ::GetDeviceCaps(hdc, LOGPIXELSY);
    if (dpiY <= 0) dpiY = 96;
    const int playVisualSizePx = ScaleForDpi(kPlayVisualSize, dpiY);
    const int sideGlyphSizePx = ScaleForDpi(kSideGlyphSize, dpiY);
    const int glyphInsetPx = max(3, ScaleForDpi(3, dpiY));
    const float ringWidthPx = highContrast ? 1.0f : max(kPlayRingWidth, static_cast<float>(dpiY) / 64.0f);
    HGDIOBJ oldFont = hTextFont ? ::SelectObject(mem, hTextFont) : nullptr;
    if (!seekOnlyPaint) {
      std::wstring text = BuildPrimaryText(s);
      ::SetTextColor(mem, fg);
      RECT tr = _textRc;
      if (tr.right > tr.left) {
        ::DrawTextW(mem, text.c_str(), static_cast<int>(text.size()), &tr,
                    DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
      }
    }

    if (!textOnlyPaint && IsFullMode() && _seekRc.right > _seekRc.left) {
      RECT track = _seekRc;
      int midY = (_seekRc.top + _seekRc.bottom) / 2;
      track.top = midY - (kSeekTrackHeight / 2);
      track.bottom = track.top + kSeekTrackHeight;
      if (track.bottom <= track.top) track.bottom = track.top + 1;

      COLORREF trackColor = highContrast ? outline : Blend(panelFill, lightForeground ? RGB(255, 255, 255) : RGB(0, 0, 0),
                                                            lightForeground ? 72 : 40);
      COLORREF progressColor = highContrast ? accent : Blend(panelFill, accent, 190);
      fillRectColor(track, trackColor);

      RECT progress = track;
      const int trackW = max(1, track.right - track.left);
      int fillW = 0;
      if (s.has_timeline && s.duration_ms > 0) {
        int64_t posMs = EffectiveTimelinePositionMs(s, nowTick);
        if (posMs < 0) posMs = 0;
        if (posMs > s.duration_ms) posMs = s.duration_ms;
        fillW = static_cast<int>((static_cast<double>(posMs) * static_cast<double>(trackW)) /
                                 static_cast<double>(s.duration_ms));
      } else {
        fillW = 0;
      }

      if (fillW > 0) {
        progress.right = min(track.right, track.left + max(0, fillW));
        if (progress.right > progress.left) fillRectColor(progress, progressColor);
      }

    }

    if (!textOnlyPaint && !seekOnlyPaint) {
      // Buttons
      const bool gpReady = EnsureGdiplus();
      Gdiplus::Graphics graphics(mem);
      if (gpReady) {
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
      }

    auto fillEllipseColor = [&](const RECT& r, COLORREF fillColor, COLORREF outlineColor, float outlineWidth) {
      if (gpReady) {
        Gdiplus::SolidBrush brush(GpColor(fillColor));
        Gdiplus::Pen pen(GpColor(outlineColor), outlineWidth);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::RectF rf(static_cast<Gdiplus::REAL>(r.left), static_cast<Gdiplus::REAL>(r.top),
                          static_cast<Gdiplus::REAL>(r.right - r.left),
                          static_cast<Gdiplus::REAL>(r.bottom - r.top));
        graphics.FillEllipse(&brush, rf);
        graphics.DrawEllipse(&pen, rf);
      } else {
        HBRUSH fill = ::CreateSolidBrush(fillColor);
        HGDIOBJ oldFill = ::SelectObject(mem, fill);
        HPEN pen = ::CreatePen(PS_SOLID, max(1, static_cast<int>(outlineWidth)), outlineColor);
        HGDIOBJ oldPen = ::SelectObject(mem, pen);
        ::Ellipse(mem, r.left, r.top, r.right, r.bottom);
        ::SelectObject(mem, oldPen);
        ::SelectObject(mem, oldFill);
        ::DeleteObject(pen);
        ::DeleteObject(fill);
      }
    };

    auto drawEllipseOutline = [&](const RECT& r, COLORREF outlineColor, float outlineWidth) {
      if (gpReady) {
        Gdiplus::Pen pen(GpColor(outlineColor), outlineWidth);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::RectF rf(static_cast<Gdiplus::REAL>(r.left), static_cast<Gdiplus::REAL>(r.top),
                          static_cast<Gdiplus::REAL>(r.right - r.left),
                          static_cast<Gdiplus::REAL>(r.bottom - r.top));
        graphics.DrawEllipse(&pen, rf);
      } else {
        HPEN ringPen = ::CreatePen(PS_SOLID, max(1, static_cast<int>(outlineWidth)), outlineColor);
        HGDIOBJ oldPen = ::SelectObject(mem, ringPen);
        HGDIOBJ oldBrush = ::SelectObject(mem, ::GetStockObject(NULL_BRUSH));
        ::Ellipse(mem, r.left, r.top, r.right, r.bottom);
        ::SelectObject(mem, oldBrush);
        ::SelectObject(mem, oldPen);
        ::DeleteObject(ringPen);
      }
    };

    auto drawPlayPauseGlyph = [&](RECT r, bool pause, COLORREF color) {
      if (gpReady) {
        Gdiplus::SolidBrush brush(GpColor(color));
        const Gdiplus::REAL cx = static_cast<Gdiplus::REAL>(r.left + r.right) / 2.0f;
        const Gdiplus::REAL cy = static_cast<Gdiplus::REAL>(r.top + r.bottom) / 2.0f;
        const Gdiplus::REAL size = static_cast<Gdiplus::REAL>(min(r.right - r.left, r.bottom - r.top));

        if (pause) {
          const Gdiplus::REAL barW = max(2.0f, size / 8.0f);
          const Gdiplus::REAL barH = max(8.0f, size / 2.0f);
          const Gdiplus::REAL gap = max(3.0f, size / 8.0f);
          graphics.FillRectangle(&brush, cx - gap - barW, cy - barH / 2.0f, barW, barH);
          graphics.FillRectangle(&brush, cx + gap, cy - barH / 2.0f, barW, barH);
        } else {
          Gdiplus::PointF pts[3]{
              {cx - size / 7.5f, cy - size / 4.2f},
              {cx - size / 7.5f, cy + size / 4.2f},
              {cx + size / 4.0f, cy},
          };
          graphics.FillPolygon(&brush, pts, 3);
        }
        return;
      }

      HBRUSH br = ::CreateSolidBrush(color);
      HGDIOBJ oldBrush = ::SelectObject(mem, br);
      const int cx = (r.left + r.right) / 2;
      const int cy = (r.top + r.bottom) / 2;
      const int size = min(r.right - r.left, r.bottom - r.top);

      if (pause) {
        const int barW = max(2, size / 8);
        const int barH = max(8, size / 2);
        const int gap = max(3, size / 8);
        RECT left{cx - gap - barW, cy - barH / 2, cx - gap, cy + barH / 2};
        RECT right{cx + gap, cy - barH / 2, cx + gap + barW, cy + barH / 2};
        ::FillRect(mem, &left, br);
        ::FillRect(mem, &right, br);
      } else {
        POINT pts[3]{
            {cx - size / 7, cy - size / 4},
            {cx - size / 7, cy + size / 4},
            {cx + size / 4, cy},
        };
        ::Polygon(mem, pts, 3);
      }

      ::SelectObject(mem, oldBrush);
      ::DeleteObject(br);
    };

    auto drawSkipGlyph = [&](RECT r, bool next, COLORREF color) {
      if (gpReady) {
        Gdiplus::SolidBrush brush(GpColor(color));
        const Gdiplus::REAL cx = static_cast<Gdiplus::REAL>(r.left + r.right) / 2.0f;
        const Gdiplus::REAL cy = static_cast<Gdiplus::REAL>(r.top + r.bottom) / 2.0f;
        const Gdiplus::REAL size = static_cast<Gdiplus::REAL>(min(r.right - r.left, r.bottom - r.top));
        const Gdiplus::REAL triW = max(7.0f, size / 3.0f);
        const Gdiplus::REAL triH = max(10.0f, size / 2.0f);
        const Gdiplus::REAL barW = max(2.0f, size / 10.0f);

        if (next) {
          Gdiplus::PointF pts[3]{
              {cx - triW / 2.0f, cy - triH / 2.0f},
              {cx - triW / 2.0f, cy + triH / 2.0f},
              {cx + triW / 2.0f, cy},
          };
          graphics.FillPolygon(&brush, pts, 3);
          graphics.FillRectangle(&brush, cx + triW / 2.0f + 2.0f, cy - triH / 2.0f, barW, triH);
        } else {
          Gdiplus::PointF pts[3]{
              {cx + triW / 2.0f, cy - triH / 2.0f},
              {cx + triW / 2.0f, cy + triH / 2.0f},
              {cx - triW / 2.0f, cy},
          };
          graphics.FillPolygon(&brush, pts, 3);
          graphics.FillRectangle(&brush, cx - triW / 2.0f - 2.0f - barW, cy - triH / 2.0f, barW, triH);
        }
        return;
      }

      HBRUSH br = ::CreateSolidBrush(color);
      HGDIOBJ oldBrush = ::SelectObject(mem, br);
      const int cx = (r.left + r.right) / 2;
      const int cy = (r.top + r.bottom) / 2;
      const int size = min(r.right - r.left, r.bottom - r.top);
      const int triW = max(7, size / 3);
      const int triH = max(10, size / 2);
      const int barW = max(2, size / 10);

      if (next) {
        POINT pts[3]{
            {cx - triW / 2, cy - triH / 2},
            {cx - triW / 2, cy + triH / 2},
            {cx + triW / 2, cy},
        };
        ::Polygon(mem, pts, 3);
        RECT bar{cx + triW / 2 + 2, cy - triH / 2, cx + triW / 2 + 2 + barW, cy + triH / 2};
        ::FillRect(mem, &bar, br);
      } else {
        POINT pts[3]{
            {cx + triW / 2, cy - triH / 2},
            {cx + triW / 2, cy + triH / 2},
            {cx - triW / 2, cy},
        };
        ::Polygon(mem, pts, 3);
        RECT bar{cx - triW / 2 - 2 - barW, cy - triH / 2, cx - triW / 2 - 2, cy + triH / 2};
        ::FillRect(mem, &bar, br);
      }

      ::SelectObject(mem, oldBrush);
      ::DeleteObject(br);
    };

    auto centerSquare = [](const RECT& r, int desired) {
      const int w = r.right - r.left;
      const int h = r.bottom - r.top;
      int size = min(desired, min(max(1, w), max(1, h)));
      int left = r.left + (w - size) / 2;
      int top = r.top + (h - size) / 2;
      return RECT{left, top, left + size, top + size};
    };

    auto drawBtn = [&](const Button& b) {
      COLORREF textCol = b.enabled ? fg : fgDisabled;
      COLORREF fillCol = b.enabled ? buttonFill : Blend(buttonFill, panelFill, 120);
      RECT r = b.rc;
      if (r.right <= r.left || r.bottom <= r.top) return;
      const bool keyboardFocused = (::GetFocus() == _hwnd) && (ButtonForAccessibleId(_focusedButton) == &b);
      auto drawKeyboardFocus = [&]() {
        if (!keyboardFocused) return;
        RECT focusRc = r;
        ::InflateRect(&focusRc, -2, -2);
        ::DrawFocusRect(mem, &focusRc);
      };

      if (b.kind == 1) {
        RECT visualRc = centerSquare(r, playVisualSizePx);
        COLORREF ring = b.enabled ? accent : fgDisabled;
        if (b.pressed && b.enabled) {
          RECT fillRc{visualRc.left + 1, visualRc.top + 1, visualRc.right - 1, visualRc.bottom - 1};
          COLORREF fillColor = highContrast ? accent : Blend(panelFill, accent, 96);
          fillEllipseColor(fillRc, fillColor, fillColor, 1.0f);
          textCol = highContrast ? accentText : fg;
        } else if (b.hot && b.enabled) {
          RECT fillRc{visualRc.left + 1, visualRc.top + 1, visualRc.right - 1, visualRc.bottom - 1};
          COLORREF fillColor = highContrast ? fillCol : Blend(panelFill, accent, 28);
          COLORREF outlineColor = highContrast ? outline : Blend(panelFill, accent, 28);
          fillEllipseColor(fillRc, fillColor, outlineColor, 1.0f);
        }

        RECT ringRc{visualRc.left + 1, visualRc.top + 1, visualRc.right - 1, visualRc.bottom - 1};
        drawEllipseOutline(ringRc, highContrast ? outline : ring, ringWidthPx);

        RECT glyphRc{visualRc.left + glyphInsetPx, visualRc.top + glyphInsetPx, visualRc.right - glyphInsetPx,
                     visualRc.bottom - glyphInsetPx};
        drawPlayPauseGlyph(glyphRc, s.playback == "playing", textCol);
        drawKeyboardFocus();
        return;
      }

      if (b.pressed && b.enabled) {
        fillRectColor(r, highContrast ? accent : Blend(panelFill, fg, 34));
        textCol = highContrast ? accentText : fg;
      } else if (b.hot && b.enabled) {
        fillRectColor(r, highContrast ? fillCol : Blend(panelFill, fg, 16));
      } else if (highContrast) {
        fillRectColor(r, fillCol);
      }

      if (highContrast) {
        HPEN outlinePen = ::CreatePen(PS_SOLID, 1, outline);
        HGDIOBJ oldOutline = ::SelectObject(mem, outlinePen);
        ::MoveToEx(mem, r.left, r.top, nullptr);
        ::LineTo(mem, r.right - 1, r.top);
        ::LineTo(mem, r.right - 1, r.bottom - 1);
        ::LineTo(mem, r.left, r.bottom - 1);
        ::LineTo(mem, r.left, r.top);
        ::SelectObject(mem, oldOutline);
        ::DeleteObject(outlinePen);
      }

      RECT glyphRc = centerSquare(r, sideGlyphSizePx);
      drawSkipGlyph(glyphRc, b.kind == 2, textCol);
      drawKeyboardFocus();
    };

      drawBtn(_btnPrev);
      drawBtn(_btnPlayPause);
      drawBtn(_btnNext);
    }

    if (savedDc != 0) ::RestoreDC(mem, savedDc);

    if (dibBits) {
      auto* pixels = static_cast<uint32_t*>(dibBits);
      RECT alphaRc = repaintRc;
      if (alphaRc.left < 0) alphaRc.left = 0;
      if (alphaRc.top < 0) alphaRc.top = 0;
      if (alphaRc.right > w) alphaRc.right = w;
      if (alphaRc.bottom > h) alphaRc.bottom = h;
      for (int y = alphaRc.top; y < alphaRc.bottom; ++y) {
        auto* row = pixels + static_cast<size_t>(y) * static_cast<size_t>(w);
        for (int x = alphaRc.left; x < alphaRc.right; ++x) {
          row[x] |= 0xFF000000u;
        }
      }
    }

    RECT blitRc = hdcIn ? rc : dirtyRc;
    ::BitBlt(hdc, blitRc.left, blitRc.top, blitRc.right - blitRc.left, blitRc.bottom - blitRc.top, mem,
             blitRc.left, blitRc.top, SRCCOPY);
    if (oldFont) ::SelectObject(mem, oldFont);

    if (!hdcIn) ::EndPaint(_hwnd, &ps);
  }

  std::atomic<long> _ref{1};
  DWORD _bandId = 0;
  DWORD _viewMode = 0;
  bool _compositionEnabled = true;

  IUnknown* _site = nullptr;
  IInputObjectSite* _inputSite = nullptr;
  IOleCommandTarget* _commandTarget = nullptr;
  HWND _hwnd = nullptr;
  HWND _tooltip = nullptr;
  HWND _compactTitlePopup = nullptr;
  widgetmusic::AccessibleButtons* _accessibleButtons = nullptr;
  long _focusedButton = widgetmusic::kAccessiblePlayPause;

  PipeClient _pipe;

  std::mutex _stateMu;
  BandState _state;
  RECT _textRc{};
  RECT _seekRc{};

  Button _btnPrev{};
  Button _btnPlayPause{};
  Button _btnNext{};

  int _visibleInsetLeft = 0;
  bool _promotedOverBlankTaskList = false;
  bool _zOrderAdjusting = false;
  DWORD _lastVisibleAuditTick = 0;
  DWORD _deferVisibleAuditUntilTick = 0;

  HDC _backDc = nullptr;
  HBITMAP _backBmp = nullptr;
  HGDIOBJ _backOldBmp = nullptr;
  void* _backBits = nullptr;
  int _backW = 0;
  int _backH = 0;
  HFONT _textFont = nullptr;
  int _textFontDpiY = 0;

  bool _cachedBgValid = false;
  COLORREF _cachedBg = RGB(32, 32, 32);

  bool _optimisticActive = false;
  std::string _optimisticPlayback;
  DWORD _optimisticUntilTick = 0;

  bool _trackingMouse = false;
  bool _mouseInClient = false;
  bool _compactTitlePopupVisible = false;
  bool _hoverTitlePopupActive = false;
  bool _compactTitleTimerOn = false;
  bool _titleHoverIntentTimerOn = false;
  bool _progressTimerOn = false;
  bool _pipeStartTimerOn = false;
  bool _pipeStarted = false;
  DWORD _titlePopupSuppressUntilTick = 0;
  POINT _lastMousePoint{};
  std::atomic<DWORD> _progressSnapshotTick{0};
  std::wstring _compactTitleText;
  std::wstring _lastPrimaryText;
  std::wstring _lastTrackPopupText;
  std::string _lastPlaybackState;
  BandDisplayMode _bandMode = BandDisplayMode::Compact;
};

class ClassFactory final : public IClassFactory {
 public:
  ClassFactory() {
    g_dllRefCount.fetch_add(1);
    LogDebugLine(L"ClassFactory ctor");
  }
  ~ClassFactory() { g_dllRefCount.fetch_sub(1); }

  IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(IClassFactory))) {
      *ppv = static_cast<IClassFactory*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }
  IFACEMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(_ref.fetch_add(1) + 1); }
  IFACEMETHODIMP_(ULONG) Release() override {
    ULONG v = static_cast<ULONG>(_ref.fetch_sub(1) - 1);
    if (v == 0) delete this;
    return v;
  }

  IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    LogDebugLine(L"CreateInstance");
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;

    auto* obj = new (std::nothrow) WidgetMusicDeskband();
    if (!obj) return E_OUTOFMEMORY;
    HRESULT hr = obj->QueryInterface(riid, ppv);
    obj->Release();
    return hr;
  }

  IFACEMETHODIMP LockServer(BOOL fLock) override {
    if (fLock) g_dllRefCount.fetch_add(1);
    else g_dllRefCount.fetch_sub(1);
    return S_OK;
  }

 private:
  std::atomic<long> _ref{1};
};

HRESULT RegSetStringValue(HKEY root, const wchar_t* subkey, const wchar_t* valueName, std::wstring_view value) {
  std::wstring tmp(value);
  HKEY h{};
  LONG rc = ::RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr);
  if (rc != ERROR_SUCCESS) return HRESULT_FROM_WIN32(rc);
  rc = ::RegSetValueExW(h, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(tmp.c_str()),
                        static_cast<DWORD>((tmp.size() + 1) * sizeof(wchar_t)));
  ::RegCloseKey(h);
  return HRESULT_FROM_WIN32(rc);
}

HRESULT RegCreateKey(HKEY root, const wchar_t* subkey) {
  HKEY h{};
  LONG rc = ::RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr);
  if (rc == ERROR_SUCCESS) ::RegCloseKey(h);
  return HRESULT_FROM_WIN32(rc);
}

HRESULT RegDeleteTreeIfExists(HKEY root, const wchar_t* subkey) {
  LONG rc = ::RegDeleteTreeW(root, subkey);
  if (rc == ERROR_FILE_NOT_FOUND) return S_OK;
  return HRESULT_FROM_WIN32(rc);
}

HRESULT RegisterServerPerUser() {
  std::wstring clsid = GuidToString(CLSID_WidgetMusicDeskband);
  std::wstring dllPath = GetModulePath(g_hInstance);
  if (clsid.empty() || dllPath.empty()) return E_FAIL;

  std::wstring base = L"Software\\Classes\\CLSID\\";
  base += clsid;

  // Default value (menu text)
  RETURN_IF_FAILED(RegSetStringValue(HKEY_CURRENT_USER, base.c_str(), nullptr, kDeskbandTitle));

  std::wstring inproc = base + L"\\InprocServer32";
  RETURN_IF_FAILED(RegSetStringValue(HKEY_CURRENT_USER, inproc.c_str(), nullptr, dllPath));
  RETURN_IF_FAILED(RegSetStringValue(HKEY_CURRENT_USER, inproc.c_str(), L"ThreadingModel", L"Apartment"));

  std::wstring implCat = base + L"\\Implemented Categories\\";
  implCat += GuidToString(CATID_DeskBand_Impl);
  RETURN_IF_FAILED(RegCreateKey(HKEY_CURRENT_USER, implCat.c_str()));

  // Shell extension approval (best-effort; helps on systems that enforce it per-user).
  std::wstring approved = L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";
  RETURN_IF_FAILED(RegSetStringValue(HKEY_CURRENT_USER, approved.c_str(), clsid.c_str(), kDeskbandTitle));

  // Make it show up in "Taskbar > Toolbars" for the current user.
  std::wstring bandObjects = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\BandObjects\\";
  bandObjects += clsid;
  RETURN_IF_FAILED(RegSetStringValue(HKEY_CURRENT_USER, bandObjects.c_str(), nullptr, kDeskbandTitle));

  // Best-effort: clear the per-user deskband category cache so Explorer picks up new registrations.
  std::wstring cache = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Discardable\\PostSetup\\Component Categories\\";
  cache += GuidToString(CATID_DeskBand_Impl);
  cache += L"\\Enum";
  (void)RegDeleteTreeIfExists(HKEY_CURRENT_USER, cache.c_str());

  return S_OK;
}

HRESULT UnregisterServerPerUser() {
  std::wstring clsid = GuidToString(CLSID_WidgetMusicDeskband);
  if (clsid.empty()) return E_FAIL;

  std::wstring base = L"Software\\Classes\\CLSID\\";
  base += clsid;
  (void)RegDeleteTreeIfExists(HKEY_CURRENT_USER, base.c_str());

  std::wstring approved = L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";
  HKEY h{};
  if (::RegOpenKeyExW(HKEY_CURRENT_USER, approved.c_str(), 0, KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
    (void)::RegDeleteValueW(h, clsid.c_str());
    ::RegCloseKey(h);
  }

  std::wstring bandObjects = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\BandObjects\\";
  bandObjects += clsid;
  (void)RegDeleteTreeIfExists(HKEY_CURRENT_USER, bandObjects.c_str());

  return S_OK;
}

} // namespace

static LRESULT CALLBACK WidgetMusicWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* self = reinterpret_cast<WidgetMusicDeskband*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    self = reinterpret_cast<WidgetMusicDeskband*>(cs->lpCreateParams);
    return self ? self->WndProc(hwnd, msg, wp, lp) : FALSE;
  }
  if (self) return self->WndProc(hwnd, msg, wp, lp);
  return ::DefWindowProcW(hwnd, msg, wp, lp);
}

// COM exports
extern "C" BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_hInstance = hInst;
    ::DisableThreadLibraryCalls(hInst);
    InitLogPath();
    LogDebugLine(L"DllMain attach");
  }
  return TRUE;
}

extern "C" STDAPI DllCanUnloadNow(void) {
  return (g_dllRefCount.load() == 0) ? S_OK : S_FALSE;
}

extern "C" STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = nullptr;
  LogDebugLine(L"DllGetClassObject");
  if (!IsEqualCLSID(rclsid, CLSID_WidgetMusicDeskband)) return CLASS_E_CLASSNOTAVAILABLE;

  auto* factory = new (std::nothrow) ClassFactory();
  if (!factory) return E_OUTOFMEMORY;

  HRESULT hr = factory->QueryInterface(riid, ppv);
  factory->Release();
  return hr;
}

extern "C" STDAPI DllRegisterServer(void) {
  LogLine(L"DllRegisterServer");
  return RegisterServerPerUser();
}

extern "C" STDAPI DllUnregisterServer(void) {
  LogLine(L"DllUnregisterServer");
  return UnregisterServerPerUser();
}
