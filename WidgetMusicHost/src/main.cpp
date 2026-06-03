#include <windows.h>

#include <objbase.h>
#include <oleauto.h>
#include <shellapi.h>
#include <sddl.h>
#include <UIAutomationClient.h>

#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cwchar>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Json.h"
#include "Utf8.h"
#include "WidgetMusicProtocol.h"

#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "shell32.lib")

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media::Control;

namespace {

std::mutex g_logMu;
std::wstring g_logPath;
constexpr DWORD kPipeNoClientTimeoutMs = 8000;
constexpr DWORD kDefaultPrewarmStartupDelayMs = 15000;
constexpr DWORD kMaxStartupDelayMs = 10 * 60 * 1000;
constexpr int kFastRefreshWindowMs = 600;
constexpr int kPendingPlaybackWindowMs = 450;
constexpr int kTrackChangeWindowMs = 900;
constexpr int kRebindFastRefreshWindowMs = 1100;
constexpr int kFastPollIntervalMs = 70;
constexpr int kSlowPollIntervalMs = 250;
constexpr DWORD kMaxLogBytes = 512 * 1024;

struct HostState {
  bool has_session = false;
  std::wstring app;
  std::wstring title;
  std::wstring artist;
  std::string playback = "unknown";  // playing|paused|stopped|unknown
  bool can_prev = false;
  bool can_next = false;
  bool can_play_pause = false;
  bool refreshing = false;
  bool has_timeline = false;
  int64_t position_ms = 0;
  int64_t duration_ms = 0;
};

struct HostOptions {
  bool prewarm = false;
  DWORD startup_delay_ms = 0;
};

std::wstring ToWString(winrt::hstring const& h) { return std::wstring{h}; }

bool IsRecoverableMediaHr(HRESULT hr) {
  return hr == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE) || hr == RPC_E_DISCONNECTED ||
         hr == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
}

bool IsTrackCommand(std::string_view name) {
  return name == "previous" || name == "next";
}

bool IsPlaybackCommand(std::string_view name) {
  return name == "play" || name == "pause" || name == "playpause";
}

bool SendMediaKey(WORD vk) {
  INPUT inputs[2]{};
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = vk;
  inputs[1] = inputs[0];
  inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
  return ::SendInput(2, inputs, sizeof(INPUT)) == 2;
}

bool SendMediaKeyForCommand(std::string_view name) {
  if (name == "previous") return SendMediaKey(VK_MEDIA_PREV_TRACK);
  if (name == "next") return SendMediaKey(VK_MEDIA_NEXT_TRACK);
  if (IsPlaybackCommand(name)) return SendMediaKey(VK_MEDIA_PLAY_PAUSE);
  return false;
}

void InitLogPath() {
  if (!g_logPath.empty()) return;
  wchar_t tempDir[MAX_PATH]{};
  DWORD n = ::GetTempPathW(MAX_PATH, tempDir);
  if (n == 0 || n >= MAX_PATH) return;
  g_logPath = tempDir;
  g_logPath += L"WidgetMusicHost.log";
}

std::wstring HrText(HRESULT hr) {
  wchar_t buf[32]{};
  swprintf_s(buf, L"0x%08X", static_cast<unsigned int>(hr));
  return buf;
}

DWORD ClampStartupDelay(unsigned long value) {
  if (value > kMaxStartupDelayMs) return kMaxStartupDelayMs;
  return static_cast<DWORD>(value);
}

bool TryParseStartupDelay(std::wstring_view text, DWORD* out) {
  if (!out || text.empty()) return false;
  std::wstring tmp(text);
  wchar_t* end = nullptr;
  unsigned long value = wcstoul(tmp.c_str(), &end, 10);
  if (!end || *end != L'\0') return false;
  *out = ClampStartupDelay(value);
  return true;
}

HostOptions ParseHostOptions() {
  HostOptions options{};

  int argc = 0;
  LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (!argv) return options;

  for (int i = 1; i < argc; ++i) {
    std::wstring_view arg(argv[i]);
    if (_wcsicmp(argv[i], L"--prewarm") == 0) {
      options.prewarm = true;
      if (options.startup_delay_ms == 0) options.startup_delay_ms = kDefaultPrewarmStartupDelayMs;
      continue;
    }

    constexpr std::wstring_view kDelayPrefix = L"--startup-delay-ms=";
    if (arg.rfind(kDelayPrefix, 0) == 0) {
      DWORD delay = 0;
      if (TryParseStartupDelay(arg.substr(kDelayPrefix.size()), &delay)) options.startup_delay_ms = delay;
      continue;
    }

    if (_wcsicmp(argv[i], L"--startup-delay-ms") == 0 && i + 1 < argc) {
      DWORD delay = 0;
      if (TryParseStartupDelay(argv[i + 1], &delay)) options.startup_delay_ms = delay;
      ++i;
    }
  }

  ::LocalFree(argv);
  return options;
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
  swprintf_s(prefix, L"[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

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
  if (envLen > 0 && envLen < std::size(env) && wcscmp(env, L"0") != 0) return true;

  DWORD value = 0;
  DWORD cb = sizeof(value);
  LONG rc = ::RegGetValueW(HKEY_CURRENT_USER, L"Software\\WidgetMusic", L"DebugLog", RRF_RT_REG_DWORD, nullptr,
                           &value, &cb);
  return rc == ERROR_SUCCESS && value != 0;
}

void LogDebugLine(std::wstring_view line) {
  if (DebugLoggingEnabled()) LogLine(line);
}

bool IContainsI(std::wstring_view hay, std::wstring_view needle) {
  if (needle.empty()) return true;
  std::wstring h(hay);
  std::wstring n(needle);
  for (auto& ch : h) ch = static_cast<wchar_t>(towlower(ch));
  for (auto& ch : n) ch = static_cast<wchar_t>(towlower(ch));
  return h.find(n) != std::wstring::npos;
}

size_t IFindI(std::wstring_view hay, std::wstring_view needle) {
  if (needle.empty()) return 0;
  std::wstring h(hay);
  std::wstring n(needle);
  for (auto& ch : h) ch = static_cast<wchar_t>(towlower(ch));
  for (auto& ch : n) ch = static_cast<wchar_t>(towlower(ch));
  return h.find(n);
}

std::wstring TrimMediaText(std::wstring s) {
  auto trimChar = [](wchar_t ch) {
    return iswspace(ch) || ch == L',' || ch == L'.' || ch == L'-';
  };
  while (!s.empty() && trimChar(s.front())) s.erase(s.begin());
  while (!s.empty() && trimChar(s.back())) s.pop_back();
  return s;
}

std::vector<std::wstring> SplitCommaParts(std::wstring_view text) {
  std::vector<std::wstring> parts;
  size_t start = 0;
  while (start <= text.size()) {
    size_t pos = text.find(L',', start);
    if (pos == std::wstring_view::npos) pos = text.size();
    parts.push_back(TrimMediaText(std::wstring(text.substr(start, pos - start))));
    if (pos >= text.size()) break;
    start = pos + 1;
  }
  return parts;
}

bool IsUsefulMediaPart(std::wstring_view s) {
  if (s.empty()) return false;
  if (IContainsI(s, L"unknown artist")) return false;
  if (IContainsI(s, L"unknown album")) return false;
  if (s == L".") return false;
  return true;
}

BOOL CALLBACK FindMediaPlayerWindowProc(HWND hwnd, LPARAM lp) {
  if (!::IsWindowVisible(hwnd)) return TRUE;
  wchar_t title[256]{};
  ::GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
  if (wcscmp(title, L"Media Player") != 0) return TRUE;

  wchar_t cls[128]{};
  ::GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
  if (wcscmp(cls, L"ApplicationFrameWindow") != 0) return TRUE;

  *reinterpret_cast<HWND*>(lp) = hwnd;
  return FALSE;
}

HWND FindMediaPlayerWindow() {
  HWND hwnd = nullptr;
  ::EnumWindows(FindMediaPlayerWindowProc, reinterpret_cast<LPARAM>(&hwnd));
  return hwnd;
}

bool ParseNowPlayingName(std::wstring name, std::wstring* title, std::wstring* artist) {
  if (!title || !artist) return false;
  size_t nowPlaying = IFindI(name, L"now playing");
  if (nowPlaying == std::wstring::npos) return false;
  name.resize(nowPlaying);
  name = TrimMediaText(name);
  if (name.empty()) return false;

  auto parts = SplitCommaParts(name);
  if (parts.empty() || !IsUsefulMediaPart(parts[0])) return false;

  *title = parts[0];
  artist->clear();
  for (size_t i = parts.size(); i > 1; --i) {
    if (IsUsefulMediaPart(parts[i - 1])) {
      *artist = parts[i - 1];
      break;
    }
  }
  return true;
}

bool TryReadMediaPlayerNowPlayingFromUIA(std::wstring* title, std::wstring* artist) {
  if (!title || !artist) return false;
  HWND hwnd = FindMediaPlayerWindow();
  if (!hwnd) return false;

  IUIAutomation* automation = nullptr;
  HRESULT hr = ::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
  if (FAILED(hr) || !automation) return false;

  IUIAutomationElement* root = nullptr;
  hr = automation->ElementFromHandle(hwnd, &root);
  if (FAILED(hr) || !root) {
    automation->Release();
    return false;
  }

  IUIAutomationCondition* condition = nullptr;
  hr = automation->CreateTrueCondition(&condition);
  if (FAILED(hr) || !condition) {
    root->Release();
    automation->Release();
    return false;
  }

  IUIAutomationElementArray* elements = nullptr;
  hr = root->FindAll(TreeScope_Descendants, condition, &elements);
  condition->Release();
  root->Release();
  automation->Release();
  if (FAILED(hr) || !elements) return false;

  int length = 0;
  elements->get_Length(&length);
  bool found = false;
  for (int i = 0; i < length && !found; ++i) {
    IUIAutomationElement* el = nullptr;
    if (FAILED(elements->GetElement(i, &el)) || !el) continue;

    BSTR bstrName = nullptr;
    if (SUCCEEDED(el->get_CurrentName(&bstrName)) && bstrName) {
      std::wstring name(bstrName, ::SysStringLen(bstrName));
      found = ParseNowPlayingName(name, title, artist);
      ::SysFreeString(bstrName);
    }
    el->Release();
  }
  elements->Release();
  return found;
}

std::wstring FriendlyAppName(winrt::hstring const& aumid) {
  try {
    auto info = AppInfo::GetFromAppUserModelId(aumid);
    auto name = info.DisplayInfo().DisplayName();
    std::wstring w = ToWString(name);
    if (!w.empty()) return w;
  } catch (...) {
  }

  std::wstring raw = ToWString(aumid);
  if (raw.empty()) return L"Now playing";

  // Friendly fallbacks; avoid leaking raw packaged ids.
  if (IContainsI(raw, L"Microsoft.ZuneMusic")) return L"Music";
  if (IContainsI(raw, L"wmplayer")) return L"Windows Media Player";
  if (IContainsI(raw, L"Microsoft.Media.Player") || IContainsI(raw, L"Microsoft.MediaPlayer")) return L"Media Player";
  if (IContainsI(raw, L"Spotify")) return L"Spotify";
  if (IContainsI(raw, L"msedge")) return L"Edge";
  if (IContainsI(raw, L"chrome")) return L"Chrome";
  if (IContainsI(raw, L"firefox")) return L"Firefox";

  // If it looks like a package family id, do not show it.
  if (raw.find(L'!') != std::wstring::npos) return L"Now playing";
  if (raw.find(L"_8wekyb3d8bbwe") != std::wstring::npos) return L"Now playing";

  // If it's an exe name, strip extension.
  if (raw.size() > 4 && (raw.ends_with(L".exe") || raw.ends_with(L".EXE"))) raw.resize(raw.size() - 4);
  if (!raw.empty()) {
    raw[0] = static_cast<wchar_t>(towupper(raw[0]));
    return raw;
  }
  return L"Now playing";
}

bool IsMusicAumidText(std::wstring_view raw) {
  return IContainsI(raw, L"Microsoft.ZuneMusic") || IContainsI(raw, L"Microsoft.Media.Player") ||
         IContainsI(raw, L"Microsoft.MediaPlayer") || IContainsI(raw, L"wmplayer") ||
         IContainsI(raw, L"Windows Media Player");
}

bool IsMusicAumid(winrt::hstring const& aumid) {
  return IsMusicAumidText(ToWString(aumid));
}

bool IsBrowserAumidText(std::wstring_view raw) {
  return IContainsI(raw, L"chrome") || IContainsI(raw, L"msedge") || IContainsI(raw, L"firefox") ||
         IContainsI(raw, L"brave") || IContainsI(raw, L"opera");
}

std::string PlaybackToString(GlobalSystemMediaTransportControlsSessionPlaybackStatus st) {
  if (st == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) return "playing";
  if (st == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused) return "paused";
  if (st == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped) return "stopped";
  return "unknown";
}

int64_t TimeSpanToMs(winrt::Windows::Foundation::TimeSpan ts) {
  // Windows TimeSpan is 100-ns units in C++/WinRT.
  int64_t ticks100ns = ts.count();
  if (ticks100ns <= 0) return 0;
  return ticks100ns / 10000;
}

std::string BuildStateLine(const HostState& s) {
  std::string app = widgetmusic::WideToUtf8(widgetmusic::ClampProtocolText(s.app, widgetmusic::kMaxAppChars));
  std::string title = widgetmusic::WideToUtf8(widgetmusic::ClampProtocolText(s.title, widgetmusic::kMaxTitleChars));
  std::string artist = widgetmusic::WideToUtf8(widgetmusic::ClampProtocolText(s.artist, widgetmusic::kMaxArtistChars));

  std::string j;
  j.reserve(512);
  j += "{\"";
  j += widgetmusic::kMsgType;
  j += "\":\"";
  j += widgetmusic::kTypeState;
  j += "\",\"";
  j += widgetmusic::kKeyConnected;
  j += "\":true,\"";
  j += widgetmusic::kKeyHasSession;
  j += "\":";
  j += widgetmusic::JsonBool(s.has_session);
  j += ",\"";
  j += widgetmusic::kKeyApp;
  j += "\":";
  j += widgetmusic::JsonQuote(app);
  j += ",\"";
  j += widgetmusic::kKeyTitle;
  j += "\":";
  j += widgetmusic::JsonQuote(title);
  j += ",\"";
  j += widgetmusic::kKeyArtist;
  j += "\":";
  j += widgetmusic::JsonQuote(artist);
  j += ",\"";
  j += widgetmusic::kKeyPlayback;
  j += "\":";
  j += widgetmusic::JsonQuote(s.playback);
  j += ",\"";
  j += widgetmusic::kKeyCanPrev;
  j += "\":";
  j += widgetmusic::JsonBool(s.can_prev);
  j += ",\"";
  j += widgetmusic::kKeyCanNext;
  j += "\":";
  j += widgetmusic::JsonBool(s.can_next);
  j += ",\"";
  j += widgetmusic::kKeyCanPlayPause;
  j += "\":";
  j += widgetmusic::JsonBool(s.can_play_pause);
  j += ",\"";
  j += widgetmusic::kKeyRefreshing;
  j += "\":";
  j += widgetmusic::JsonBool(s.refreshing);
  j += ",\"";
  j += widgetmusic::kKeyHasTimeline;
  j += "\":";
  j += widgetmusic::JsonBool(s.has_timeline);
  j += ",\"";
  j += widgetmusic::kKeyPositionMs;
  j += "\":";
  j += std::to_string(s.position_ms);
  j += ",\"";
  j += widgetmusic::kKeyDurationMs;
  j += "\":";
  j += std::to_string(s.duration_ms);
  j += "}\n";
  return j;
}

std::string BuildHelloLine() {
  std::string j;
  j.reserve(128);
  j += "{\"";
  j += widgetmusic::kMsgType;
  j += "\":\"";
  j += widgetmusic::kTypeHello;
  j += "\",\"";
  j += widgetmusic::kKeyVersion;
  j += "\":";
  j += std::to_string(widgetmusic::kProtocolVersion);
  j += ",\"";
  j += widgetmusic::kKeyPid;
  j += "\":";
  j += std::to_string(::GetCurrentProcessId());
  j += "}\n";
  return j;
}

std::wstring CurrentLogonSidString() {
  HANDLE token{};
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return {};

  DWORD bytes = 0;
  (void)::GetTokenInformation(token, TokenGroups, nullptr, 0, &bytes);
  if (bytes == 0) {
    ::CloseHandle(token);
    return {};
  }

  std::vector<uint8_t> buf(bytes);
  if (!::GetTokenInformation(token, TokenGroups, buf.data(), bytes, &bytes)) {
    ::CloseHandle(token);
    return {};
  }
  ::CloseHandle(token);

  auto* groups = reinterpret_cast<TOKEN_GROUPS*>(buf.data());
  PSID logonSid = nullptr;
  for (DWORD i = 0; i < groups->GroupCount; ++i) {
    if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID) {
      logonSid = groups->Groups[i].Sid;
      break;
    }
  }
  if (!logonSid) return {};

  LPWSTR sidStr = nullptr;
  if (!::ConvertSidToStringSidW(logonSid, &sidStr)) return {};

  std::wstring sid(sidStr);
  ::LocalFree(sidStr);
  return sid;
}

std::wstring HostMutexNameForCurrentSession() {
  return L"Local\\SnipTune10Host.Session." + std::to_wstring(widgetmusic::CurrentProcessSessionId());
}

HANDLE AcquireHostInstanceMutex() {
  std::wstring name = HostMutexNameForCurrentSession();
  HANDLE mutex = ::CreateMutexW(nullptr, FALSE, name.c_str());
  if (!mutex) {
    LogLine(L"Host single-instance mutex creation failed: " + HrText(HRESULT_FROM_WIN32(::GetLastError())));
    return nullptr;
  }

  DWORD err = ::GetLastError();
  if (err == ERROR_ALREADY_EXISTS) {
    LogLine(L"Another WidgetMusicHost instance is already running for this session");
    ::CloseHandle(mutex);
    return nullptr;
  }

  LogDebugLine(L"Host single-instance mutex acquired: " + name);
  return mutex;
}

class PipeServer {
 public:
  explicit PipeServer(HANDLE stopEvent, bool keepAliveWithoutClient)
      : _stopEvent(stopEvent), _keepAliveWithoutClient(keepAliveWithoutClient) {}
  ~PipeServer() { Stop(); }

  PipeServer(const PipeServer&) = delete;
  PipeServer& operator=(const PipeServer&) = delete;

  void Start() {
    Stop();
    _sendEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!_sendEvent) return;
    _thread = std::thread([this] { ThreadMain(); });
  }

  void Stop() {
    if (_sendEvent) ::SetEvent(_sendEvent);
    if (_thread.joinable()) _thread.join();
    if (_sendEvent) {
      ::CloseHandle(_sendEvent);
      _sendEvent = nullptr;
    }
  }

  void SetOnCommand(std::function<void(std::string_view)> cb) {
    std::lock_guard<std::mutex> lock(_mu);
    _onCommand = std::move(cb);
  }

  void SetLatestState(std::string stateLine) {
    if (stateLine.size() > widgetmusic::kMaxPipeMessageBytes) {
      LogLine(L"State payload rejected: exceeds IPC limit");
      return;
    }
    std::lock_guard<std::mutex> lock(_mu);
    if (stateLine == _latestStateLine) return;
    _latestStateLine = std::move(stateLine);
    // Keep only the freshest state payload to avoid queue growth during rapid updates.
    _sendQueue.clear();
    _sendQueue.emplace_back(_latestStateLine);
    if (_sendEvent) ::SetEvent(_sendEvent);
  }

 private:
  bool MakePipeSecurity(SECURITY_ATTRIBUTES* outSa, PSECURITY_DESCRIPTOR* outSd) {
    if (!outSa || !outSd) return false;
    *outSa = {};
    *outSd = nullptr;
    std::wstring sid = CurrentLogonSidString();
    if (sid.empty()) return false;

    std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;";
    sddl += sid;
    sddl += L")";

    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
      return false;
    }
    *outSd = sd;
    outSa->nLength = sizeof(*outSa);
    outSa->lpSecurityDescriptor = sd;
    outSa->bInheritHandle = FALSE;
    return true;
  }

  HANDLE CreateServerPipe() {
    PSECURITY_DESCRIPTOR sd = nullptr;
    SECURITY_ATTRIBUTES sa{};
    if (!MakePipeSecurity(&sa, &sd)) {
      LogLine(L"Pipe ACL creation failed; refusing insecure fallback");
      return INVALID_HANDLE_VALUE;
    }

    DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
    DWORD pipeMode = PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;

    const std::wstring pipePath = widgetmusic::PipePathForCurrentSession();
    HANDLE h = ::CreateNamedPipeW(pipePath.c_str(), openMode, pipeMode, 1,
                                  static_cast<DWORD>(widgetmusic::kMaxPipeMessageBytes),
                                  static_cast<DWORD>(widgetmusic::kMaxPipeMessageBytes), 0, &sa);
    if (sd) ::LocalFree(sd);
    return h;
  }

  void ThreadMain() {
    const std::string hello = BuildHelloLine();

    while (::WaitForSingleObject(_stopEvent, 0) == WAIT_TIMEOUT) {
      HANDLE pipe = CreateServerPipe();
      if (pipe == INVALID_HANDLE_VALUE) {
        ::WaitForSingleObject(_stopEvent, 250);
        continue;
      }

      OVERLAPPED ovConn{};
      HANDLE hConnEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
      ovConn.hEvent = hConnEvent;

      ::ResetEvent(hConnEvent);
      BOOL connOk = ::ConnectNamedPipe(pipe, &ovConn);
      DWORD connErr = connOk ? ERROR_SUCCESS : ::GetLastError();

      if (!connOk && connErr == ERROR_PIPE_CONNECTED) {
        // Client connected before ConnectNamedPipe. Treat as connected.
        ::SetEvent(hConnEvent);
        connErr = ERROR_SUCCESS;
      }

      if (!connOk && connErr == ERROR_IO_PENDING) {
        HANDLE handles[2]{_stopEvent, hConnEvent};
        DWORD connectTimeout = _keepAliveWithoutClient ? INFINITE : kPipeNoClientTimeoutMs;
        DWORD w = ::WaitForMultipleObjects(2, handles, FALSE, connectTimeout);
        if (w == WAIT_OBJECT_0) {
          ::CloseHandle(hConnEvent);
          ::CloseHandle(pipe);
          break;
        }
        if (w == WAIT_TIMEOUT) {
          (void)::CancelIoEx(pipe, &ovConn);
          ::CloseHandle(hConnEvent);
          ::CloseHandle(pipe);
          LogLine(L"Pipe connect timeout; host exiting");
          if (_stopEvent) ::SetEvent(_stopEvent);
          break;
        }
        DWORD dummy = 0;
        if (!::GetOverlappedResult(pipe, &ovConn, &dummy, FALSE)) {
          ::CloseHandle(hConnEvent);
          ::CloseHandle(pipe);
          continue;
        }
      } else if (!connOk && connErr != ERROR_SUCCESS) {
        ::CloseHandle(hConnEvent);
        ::CloseHandle(pipe);
        continue;
      }

      ::CloseHandle(hConnEvent);
      LogLine(L"Pipe client connected");

      // Connected: send hello + latest state snapshot (if any).
      OVERLAPPED ovWrite{};
      HANDLE hWriteEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
      ovWrite.hEvent = hWriteEvent;

      auto writeMsg = [&](std::string_view msg) -> bool {
        if (!hWriteEvent) return false;
        ::ResetEvent(hWriteEvent);
        DWORD written = 0;
        BOOL ok = ::WriteFile(pipe, msg.data(), static_cast<DWORD>(msg.size()), &written, &ovWrite);
        if (ok) return written == msg.size();

        DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING) return false;

        HANDLE handles[2]{_stopEvent, hWriteEvent};
        DWORD w = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0) {
          (void)::CancelIoEx(pipe, &ovWrite);
          return false;
        }

        DWORD got = 0;
        if (!::GetOverlappedResult(pipe, &ovWrite, &got, FALSE)) return false;
        return got == msg.size();
      };

      if (!writeMsg(hello)) {
        LogLine(L"Pipe write hello failed");
        if (hWriteEvent) ::CloseHandle(hWriteEvent);
        ::FlushFileBuffers(pipe);
        ::DisconnectNamedPipe(pipe);
        ::CloseHandle(pipe);
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(_mu);
        if (!_latestStateLine.empty()) {
          (void)writeMsg(_latestStateLine);
        }
        _sendQueue.clear();
        if (_sendEvent) ::ResetEvent(_sendEvent);
      }

      // Per-connection loop.
      std::vector<char> buf(widgetmusic::kMaxPipeMessageBytes);
      OVERLAPPED ovRead{};
      HANDLE hReadEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
      ovRead.hEvent = hReadEvent;

      bool connected = true;
      bool readInFlight = false;
      DWORD bytesRead = 0;

      while (connected && ::WaitForSingleObject(_stopEvent, 0) == WAIT_TIMEOUT) {
        if (!readInFlight) {
          ::ResetEvent(hReadEvent);
          bytesRead = 0;
          BOOL ok = ::ReadFile(pipe, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead, &ovRead);
          if (!ok) {
            DWORD readErr = ::GetLastError();
            if (readErr == ERROR_IO_PENDING) {
              readInFlight = true;
            } else if (readErr == ERROR_BROKEN_PIPE || readErr == ERROR_PIPE_NOT_CONNECTED) {
              connected = false;
              break;
            } else {
              connected = false;
              break;
            }
          }
        }

        if (readInFlight) {
          HANDLE handles[3]{_stopEvent, hReadEvent, _sendEvent};
          DWORD wait = ::WaitForMultipleObjects(3, handles, FALSE, INFINITE);
          if (wait == WAIT_OBJECT_0) break;

          if (wait == WAIT_OBJECT_0 + 2) {
            for (;;) {
              std::string msg;
              {
                std::lock_guard<std::mutex> lock(_mu);
                if (_sendQueue.empty()) {
                  ::ResetEvent(_sendEvent);
                  break;
                }
                msg = std::move(_sendQueue.front());
                _sendQueue.pop_front();
              }
              if (!writeMsg(msg)) {
                LogLine(L"Pipe write state failed");
                connected = false;
                break;
              }
            }
            continue;
          }

          if (wait != WAIT_OBJECT_0 + 1) continue;

          DWORD got = 0;
          if (!::GetOverlappedResult(pipe, &ovRead, &got, FALSE)) {
            connected = false;
            break;
          }
          bytesRead = got;
          readInFlight = false;
        }

        if (bytesRead == 0) {
          connected = false;
          break;
        }

        std::string_view msg(buf.data(), buf.data() + bytesRead);
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.remove_suffix(1);

        std::string type;
        if (widgetmusic::JsonTryGetString(msg, widgetmusic::kMsgType, &type) && type == widgetmusic::kTypeCommand) {
          std::string name;
          if (widgetmusic::JsonTryGetString(msg, widgetmusic::kKeyName, &name)) {
            std::function<void(std::string_view)> cb;
            {
              std::lock_guard<std::mutex> lock(_mu);
              cb = _onCommand;
            }
            LogLine(L"Command received: " + widgetmusic::Utf8ToWide(name));
            if (cb) cb(name);
          }
        }
      }

      if (readInFlight) (void)::CancelIoEx(pipe, &ovRead);

      if (hReadEvent) ::CloseHandle(hReadEvent);
      if (hWriteEvent) ::CloseHandle(hWriteEvent);
      ::FlushFileBuffers(pipe);
      ::DisconnectNamedPipe(pipe);
      ::CloseHandle(pipe);
      LogLine(L"Pipe client disconnected");
      if (!_keepAliveWithoutClient && _stopEvent) ::SetEvent(_stopEvent);
    }
  }

  HANDLE _stopEvent = nullptr;
  HANDLE _sendEvent = nullptr;
  bool _keepAliveWithoutClient = false;
  std::thread _thread;

  std::mutex _mu;
  std::deque<std::string> _sendQueue;
  std::string _latestStateLine;
  std::function<void(std::string_view)> _onCommand;
};

class MediaSessionTracker {
 public:
  using StateCallback = std::function<void(const HostState&)>;

  explicit MediaSessionTracker(StateCallback onState) : _onState(std::move(onState)) {}
  ~MediaSessionTracker() { Stop(); }

  MediaSessionTracker(const MediaSessionTracker&) = delete;
  MediaSessionTracker& operator=(const MediaSessionTracker&) = delete;

  void Start() {
    Stop();
    _stopping.store(false);
    {
      std::lock_guard<std::mutex> lock(_mu);
      _stop = false;
      _dirty = false;
      _forceRebind = false;
    }
    {
      std::lock_guard<std::mutex> lock(_commandMu);
      _commandQueue.clear();
    }
    _thread = std::thread([this] { UpdateThread(); });
    _commandThread = std::thread([this] { CommandThread(); });
    SignalUpdate();
  }

  void Stop() {
    _stopping.store(true);
    {
      std::lock_guard<std::mutex> lock(_mu);
      _stop = true;
    }
    {
      std::lock_guard<std::mutex> lock(_commandMu);
      _commandQueue.clear();
    }
    _cv.notify_all();
    _commandCv.notify_all();
    if (_commandThread.joinable()) _commandThread.join();
    if (_thread.joinable()) _thread.join();

    // Revoke manager/session event handlers on teardown.
    if (_manager) {
      try {
        if (_tokSessionsChanged.value) _manager.SessionsChanged(_tokSessionsChanged);
        if (_tokCurrentSessionChanged.value) _manager.CurrentSessionChanged(_tokCurrentSessionChanged);
      } catch (...) {
      }
    }
    _tokSessionsChanged = {};
    _tokCurrentSessionChanged = {};
    SetSession(nullptr);
    _manager = nullptr;
  }

  void HandleCommand(std::string_view name) {
    if (name == "refresh") {
      SignalUpdate(true);
      return;
    }

    if (!IsTrackCommand(name) && !IsPlaybackCommand(name)) return;

    if (IsTrackCommand(name)) {
      SetPendingTrackChange();
    }

    {
      std::lock_guard<std::mutex> lock(_commandMu);
      if (_commandQueue.size() >= 16) _commandQueue.pop_front();
      _commandQueue.emplace_back(name);
    }
    _commandCv.notify_one();
    SignalUpdate(true);
  }

 private:
  bool IsStopping() {
    return _stopping.load();
  }

  void CommandThread() {
    while (true) {
      std::string command;
      {
        std::unique_lock<std::mutex> lock(_commandMu);
        _commandCv.wait(lock, [&] { return IsStopping() || !_commandQueue.empty(); });
        if (_commandQueue.empty()) {
          if (IsStopping()) break;
          continue;
        }
        command = std::move(_commandQueue.front());
        _commandQueue.pop_front();
      }
      ExecuteCommand(command);
    }
  }

  void ExecuteCommand(std::string_view name) {

    GlobalSystemMediaTransportControlsSession session{nullptr};
    std::string lastPlayback = "unknown";
    {
      std::lock_guard<std::mutex> sessionLock(_sessionMu);
      session = _session;
    }
    {
      std::lock_guard<std::mutex> lock(_mu);
      lastPlayback = _lastPlayback;
    }

    bool commandOk = false;
    bool commandAttempted = false;
    bool allowFallbackMediaKey = session != nullptr;
    bool needsFallback = false;
    HRESULT failureHr = S_OK;

    try {
      if (session) {
        bool isKnownMusicSession = false;
        try {
          isKnownMusicSession = IsMusicAumid(session.SourceAppUserModelId());
        } catch (...) {
        }

        auto playbackInfo = session.GetPlaybackInfo();
        auto st = playbackInfo.PlaybackStatus();
        auto controls = playbackInfo.Controls();
        lastPlayback = PlaybackToString(st);

        if (name == "previous") {
          allowFallbackMediaKey = controls.IsPreviousEnabled() || isKnownMusicSession;
          commandAttempted = allowFallbackMediaKey;
          if (commandAttempted) commandOk = session.TrySkipPreviousAsync().get();
        } else if (name == "next") {
          allowFallbackMediaKey = controls.IsNextEnabled() || isKnownMusicSession;
          commandAttempted = allowFallbackMediaKey;
          if (commandAttempted) commandOk = session.TrySkipNextAsync().get();
        } else if (name == "play") {
          allowFallbackMediaKey = controls.IsPlayEnabled() || controls.IsPlayPauseToggleEnabled() || isKnownMusicSession;
          commandAttempted = allowFallbackMediaKey || st == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
          if (commandAttempted) SetPendingPlayback("playing");
          if (st == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
            commandOk = true;
          } else if (controls.IsPlayEnabled()) {
            commandOk = session.TryPlayAsync().get();
          }
          if (!commandOk && st != GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing &&
              controls.IsPlayPauseToggleEnabled()) {
            commandOk = session.TryTogglePlayPauseAsync().get();
          }
        } else if (name == "playpause") {
          allowFallbackMediaKey = controls.IsPlayPauseToggleEnabled() || controls.IsPlayEnabled() ||
                                  controls.IsPauseEnabled() || isKnownMusicSession;
          commandAttempted = allowFallbackMediaKey;
          if (commandAttempted) {
            SetPendingPlayback(st == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing ? "paused" : "playing");
            if (controls.IsPlayPauseToggleEnabled()) {
              commandOk = session.TryTogglePlayPauseAsync().get();
            } else if (st == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing && controls.IsPauseEnabled()) {
              commandOk = session.TryPauseAsync().get();
            } else if (controls.IsPlayEnabled()) {
              commandOk = session.TryPlayAsync().get();
            } else {
              commandOk = session.TryTogglePlayPauseAsync().get();
            }
          }
        } else if (name == "pause") {
          allowFallbackMediaKey = controls.IsPauseEnabled() || controls.IsPlayPauseToggleEnabled() || isKnownMusicSession;
          commandAttempted = allowFallbackMediaKey || st == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused;
          if (commandAttempted) SetPendingPlayback("paused");
          if (st == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused) {
            commandOk = true;
          } else if (controls.IsPauseEnabled()) {
            commandOk = session.TryPauseAsync().get();
          }
          if (!commandOk && st == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing &&
              controls.IsPlayPauseToggleEnabled()) {
            commandOk = session.TryTogglePlayPauseAsync().get();
          }
        }

        needsFallback = commandAttempted && !commandOk;
      }
    } catch (...) {
      failureHr = winrt::to_hresult();
      needsFallback = commandAttempted || session != nullptr;
      LogLine(L"Command failed: " + widgetmusic::Utf8ToWide(name) + L" hr=" + HrText(failureHr));
      if (IsRecoverableMediaHr(failureHr)) RequestRebind();
    }

    if (!session) {
      std::wstring uiTitle;
      std::wstring uiArtist;
      allowFallbackMediaKey = TryReadMediaPlayerNowPlayingFromUIA(&uiTitle, &uiArtist);
      needsFallback = allowFallbackMediaKey;
    }

    if (needsFallback) {
      bool allowPlaybackFallback = true;
      if (name == "pause" && lastPlayback == "paused") allowPlaybackFallback = false;
      if (name == "play" && lastPlayback == "playing") allowPlaybackFallback = false;

      if (allowFallbackMediaKey && (IsTrackCommand(name) || allowPlaybackFallback)) {
        bool sent = SendMediaKeyForCommand(name);
        LogLine(L"Fallback media key " + widgetmusic::Utf8ToWide(name) + (sent ? L" sent" : L" failed"));
      } else {
        LogLine(L"Command ignored without actionable media target: " + widgetmusic::Utf8ToWide(name));
      }
    }

    // Schedule a quick refresh after issuing a command.
    SignalUpdate(true);
  }

  void SignalUpdate(bool fast = false) {
    {
      std::lock_guard<std::mutex> lock(_mu);
      _dirty = true;
      if (fast) {
        _fastRefreshUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(kFastRefreshWindowMs);
      }
    }
    _cv.notify_one();
  }

  void SetPendingPlayback(std::string playback) {
    std::lock_guard<std::mutex> lock(_mu);
    _pendingPlayback = std::move(playback);
    _pendingPlaybackUntil =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kPendingPlaybackWindowMs);
    _fastRefreshUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(kFastRefreshWindowMs);
    _dirty = true;
  }

  void SetPendingTrackChange() {
    std::lock_guard<std::mutex> lock(_mu);
    auto now = std::chrono::steady_clock::now();
    _trackChangeUntil = now + std::chrono::milliseconds(kTrackChangeWindowMs);
    _fastRefreshUntil = now + std::chrono::milliseconds(kTrackChangeWindowMs);
    _dirty = true;
  }

  void RequestRebind() {
    bool shouldNotify = false;
    {
      std::lock_guard<std::mutex> lock(_mu);
      auto now = std::chrono::steady_clock::now();
      if (_forceRebind || now - _lastRebindRequest < std::chrono::milliseconds(1000)) return;
      _lastRebindRequest = now;
      _forceRebind = true;
      _dirty = true;
      _fastRefreshUntil = now + std::chrono::milliseconds(kRebindFastRefreshWindowMs);
      shouldNotify = true;
    }
    if (shouldNotify) _cv.notify_one();
  }

  bool ConsumeForceRebind() {
    std::lock_guard<std::mutex> lock(_mu);
    bool v = _forceRebind;
    _forceRebind = false;
    return v;
  }

  bool IsFastRefreshActive() {
    std::lock_guard<std::mutex> lock(_mu);
    return std::chrono::steady_clock::now() < _fastRefreshUntil;
  }

  bool IsTrackChangePending() {
    std::lock_guard<std::mutex> lock(_mu);
    return std::chrono::steady_clock::now() < _trackChangeUntil;
  }

  void ResetMediaObjects() {
    SetSession(nullptr);
    _manager = nullptr;
    _lastMediaSummary.clear();
    LogLine(L"Rebinding GSMTC session manager");
  }

  void LogMediaPropertiesFailure(HRESULT hr) {
    auto now = std::chrono::steady_clock::now();
    if (hr != _lastMediaPropsHr || now - _lastMediaPropsLog >= std::chrono::seconds(30)) {
      _lastMediaPropsHr = hr;
      _lastMediaPropsLog = now;
      LogLine(L"TryGetMediaProperties failed hr=" + HrText(hr));
    }
  }

  void LogUpdateFailure(HRESULT hr) {
    auto now = std::chrono::steady_clock::now();
    if (hr != _lastUpdateHr || now - _lastUpdateFailureLog >= std::chrono::seconds(15)) {
      _lastUpdateHr = hr;
      _lastUpdateFailureLog = now;
      LogLine(L"Update failed hr=" + HrText(hr));
    }
  }

  bool EnsureManager() {
    if (_manager) return true;

    auto now = std::chrono::steady_clock::now();
    if (now < _nextManagerAttempt) return false;

    LogLine(L"Requesting GSMTC session manager");
    try {
      _manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    } catch (...) {
      HRESULT hr = winrt::to_hresult();
      _manager = nullptr;
      ++_managerAttemptFailures;
      int delayMs = 250;
      if (_managerAttemptFailures == 2) delayMs = 500;
      else if (_managerAttemptFailures == 3) delayMs = 1000;
      else if (_managerAttemptFailures >= 4) delayMs = 2000;
      _nextManagerAttempt = now + std::chrono::milliseconds(delayMs);
      LogUpdateFailure(hr);
      return false;
    }

    _managerAttemptFailures = 0;
    _nextManagerAttempt = {};
    LogLine(L"GSMTC session manager ready");
    _tokSessionsChanged = _manager.SessionsChanged([this](auto&&, auto&&) { SignalUpdate(); });
    _tokCurrentSessionChanged = _manager.CurrentSessionChanged([this](auto&&, auto&&) { SignalUpdate(); });
    return true;
  }

  GlobalSystemMediaTransportControlsSession PickSession() {
    if (!_manager) return nullptr;
    auto current = _manager.GetCurrentSession();
    auto sessions = _manager.GetSessions();

    GlobalSystemMediaTransportControlsSession best{nullptr};
    int bestScore = -1;
    std::wstring summary = L"PickSession sessions=" + std::to_wstring(sessions.Size());

    auto consider = [&](GlobalSystemMediaTransportControlsSession const& s, bool isCurrent, uint32_t index) {
      if (!s) return;
      int score = 0;
      std::wstring source = L"(unknown)";
      std::string playback = "unknown";
      bool isMusic = false;
      bool isBrowser = false;

      try {
        auto aumid = s.SourceAppUserModelId();
        source = ToWString(aumid);
        isMusic = IsMusicAumidText(source);
        isBrowser = IsBrowserAumidText(source);
        if (isMusic) {
          score += 220;
        } else if (isBrowser) {
          score -= 10;
        }
      } catch (...) {
      }

      try {
        auto info = s.GetPlaybackInfo();
        auto st = info.PlaybackStatus();
        playback = PlaybackToString(st);
        if (st == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
          score += 70;
        } else if (st == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused) {
          score += 45;
        } else if (st == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped) {
          score -= isMusic ? 35 : 20;
        }

        auto controls = info.Controls();
        if (controls.IsPlayPauseToggleEnabled() || controls.IsPlayEnabled() || controls.IsPauseEnabled()) score += 5;
        if (controls.IsNextEnabled()) score += 3;
        if (controls.IsPreviousEnabled()) score += 3;
      } catch (...) {
      }

      if (isCurrent) score += isMusic ? 25 : 10;

      summary += L" [";
      summary += std::to_wstring(index);
      if (isCurrent) summary += L"/current";
      summary += L" score=" + std::to_wstring(score);
      if (isMusic) summary += L"/music";
      else if (isBrowser) summary += L"/browser";
      summary += L" playback=" + widgetmusic::Utf8ToWide(playback);
      summary += L" source=" + source;
      summary += L"]";

      if (score > bestScore) {
        bestScore = score;
        best = s;
      }
    };

    if (current) consider(current, true, 9999);
    uint32_t index = 0;
    for (auto const& s : sessions) {
      consider(s, current && s == current, index++);
    }

    if (summary != _lastMediaSummary) {
      _lastMediaSummary = summary;
      LogDebugLine(summary);
    }

    return best;
  }

  void SetSession(GlobalSystemMediaTransportControlsSession const& s) {
    GlobalSystemMediaTransportControlsSession previous{nullptr};
    {
      std::lock_guard<std::mutex> lock(_sessionMu);
      if (_session == s) return;
      previous = _session;
      _session = s;
    }

    if (previous) {
      try {
        if (_tokPlaybackChanged.value) previous.PlaybackInfoChanged(_tokPlaybackChanged);
        if (_tokMediaPropsChanged.value) previous.MediaPropertiesChanged(_tokMediaPropsChanged);
      } catch (...) {
      }
    }
    _tokPlaybackChanged = {};
    _tokMediaPropsChanged = {};
    _cachedTitle.clear();
    _cachedArtist.clear();
    _uiaTitle.clear();
    _uiaArtist.clear();
    _lastUiaProbe = {};
    if (s) {
      _tokPlaybackChanged = s.PlaybackInfoChanged([this](auto&&, auto&&) { SignalUpdate(); });
      _tokMediaPropsChanged = s.MediaPropertiesChanged([this](auto&&, auto&&) { SignalUpdate(); });
    }
  }

  HostState BuildState() {
    HostState out{};
    out.has_session = false;
    out.app.clear();
    out.title.clear();
    out.artist.clear();
    out.playback = "unknown";
    out.can_prev = false;
    out.can_next = false;
    out.can_play_pause = false;
    out.refreshing = false;
    out.has_timeline = false;
    out.position_ms = 0;
    out.duration_ms = 0;

    auto tryMediaPlayerUiFallback = [&]() -> bool {
      auto now = std::chrono::steady_clock::now();
      bool shouldProbe = now - _lastUiaProbe >= std::chrono::milliseconds(900);
      if (shouldProbe) {
        _lastUiaProbe = now;
        std::wstring uiTitle;
        std::wstring uiArtist;
        if (TryReadMediaPlayerNowPlayingFromUIA(&uiTitle, &uiArtist)) {
          _uiaTitle = uiTitle;
          _uiaArtist = uiArtist;
          _cachedTitle = uiTitle;
          _cachedArtist = uiArtist;
        } else {
          _uiaTitle.clear();
          _uiaArtist.clear();
        }
      }

      if (_uiaTitle.empty()) return false;
      out.has_session = true;
      out.app = L"Media Player";
      out.title = _uiaTitle;
      out.artist = _uiaArtist;
      out.playback = "playing";
      out.can_prev = true;
      out.can_next = true;
      out.can_play_pause = true;
      out.has_timeline = false;
      out.position_ms = 0;
      out.duration_ms = 0;
      return true;
    };

    auto tryMediaPlayerWindowFallback = [&]() -> bool {
      if (!FindMediaPlayerWindow()) return false;
      out.has_session = true;
      out.app = L"Media Player";
      out.title.clear();
      out.artist.clear();
      out.playback = "unknown";
      out.can_prev = false;
      out.can_next = false;
      out.can_play_pause = false;
      out.has_timeline = false;
      out.position_ms = 0;
      out.duration_ms = 0;
      return true;
    };

    if (!_manager) {
      if (!tryMediaPlayerUiFallback()) (void)tryMediaPlayerWindowFallback();
      return out;
    }

    auto picked = PickSession();
    GlobalSystemMediaTransportControlsSession session{nullptr};
    {
      std::lock_guard<std::mutex> lock(_sessionMu);
      session = _session;
    }
    if (picked != session) {
      SetSession(picked);
      session = picked;
    }
    if (!session) {
      if (!tryMediaPlayerUiFallback()) (void)tryMediaPlayerWindowFallback();
      return out;
    }
    out.has_session = true;

    bool isMusicSession = false;
    try {
      auto source = session.SourceAppUserModelId();
      isMusicSession = IsMusicAumid(source);
      out.app = FriendlyAppName(source);
    } catch (...) {
      out.app.clear();
      LogLine(L"SourceAppUserModelId failed hr=" + HrText(winrt::to_hresult()));
    }

    try {
      auto info = session.GetPlaybackInfo();
      out.playback = PlaybackToString(info.PlaybackStatus());
      auto controls = info.Controls();
      out.can_prev = controls.IsPreviousEnabled();
      out.can_next = controls.IsNextEnabled();
      out.can_play_pause = controls.IsPlayPauseToggleEnabled() || controls.IsPlayEnabled() || controls.IsPauseEnabled();
      if (out.has_session) {
        // Some known music players expose a session but under-report capabilities. Keep those controls usable;
        // unsupported/empty sessions stay disabled so the deskband cannot trigger global media keys by accident.
        if (isMusicSession) {
          if (!out.can_play_pause) out.can_play_pause = true;
          out.can_prev = true;
          out.can_next = true;
        }
      }
    } catch (...) {
      LogLine(L"GetPlaybackInfo failed hr=" + HrText(winrt::to_hresult()));
      if (out.has_session) {
        if (isMusicSession) {
          out.can_play_pause = true;
          out.can_prev = true;
          out.can_next = true;
        }
      }
    }

    try {
      auto timeline = session.GetTimelineProperties();
      int64_t positionMs = TimeSpanToMs(timeline.Position());
      int64_t startMs = TimeSpanToMs(timeline.StartTime());
      int64_t endMs = TimeSpanToMs(timeline.EndTime());
      int64_t durationMs = endMs - startMs;
      if (durationMs < 0) durationMs = 0;
      if (positionMs < 0) positionMs = 0;
      if (durationMs > 0 && positionMs > durationMs) positionMs = durationMs;
      if (durationMs > 0 || positionMs > 0) {
        out.has_timeline = true;
        out.position_ms = positionMs;
        out.duration_ms = durationMs;
      }
    } catch (...) {
      out.has_timeline = false;
      out.position_ms = 0;
      out.duration_ms = 0;
    }

    bool mediaPropsHasMetadata = false;
    bool mediaPropsFailed = false;
    bool trackChangePending = IsTrackChangePending();
    bool fastRefresh = IsFastRefreshActive();
    std::wstring priorCachedTitle = _cachedTitle;
    std::wstring priorCachedArtist = _cachedArtist;

    try {
      auto props = session.TryGetMediaPropertiesAsync().get();
      out.title = ToWString(props.Title());
      out.artist = ToWString(props.Artist());
      mediaPropsHasMetadata = !out.title.empty() || !out.artist.empty();
      if (mediaPropsHasMetadata) {
        _cachedTitle = out.title;
        _cachedArtist = out.artist;
      }
    } catch (...) {
      HRESULT hr = winrt::to_hresult();
      mediaPropsFailed = true;
      LogMediaPropertiesFailure(hr);
      if (IsRecoverableMediaHr(hr)) RequestRebind();
    }

    if (trackChangePending && mediaPropsHasMetadata && out.title == priorCachedTitle && out.artist == priorCachedArtist) {
      out.title.clear();
      out.artist.clear();
      mediaPropsHasMetadata = false;
    }
    if (trackChangePending) {
      // Avoid showing stale progress from the previous track during transition.
      out.has_timeline = false;
      out.position_ms = 0;
      out.duration_ms = 0;
    }

    bool uiaFreshThisCycle = false;
    if (isMusicSession && (out.title.empty() || mediaPropsFailed || trackChangePending || fastRefresh)) {
      auto now = std::chrono::steady_clock::now();
      auto interval = (trackChangePending || fastRefresh) ? std::chrono::milliseconds(250) : std::chrono::milliseconds(1000);
      if (now - _lastUiaProbe >= interval) {
        _lastUiaProbe = now;
        std::wstring uiTitle;
        std::wstring uiArtist;
        if (TryReadMediaPlayerNowPlayingFromUIA(&uiTitle, &uiArtist)) {
          uiaFreshThisCycle = true;
          if (uiTitle != _uiaTitle || uiArtist != _uiaArtist) {
            LogDebugLine(L"UIA now playing title=\"" + uiTitle + L"\" artist=\"" + uiArtist + L"\"");
          }
          _uiaTitle = uiTitle;
          _uiaArtist = uiArtist;
          _cachedTitle = uiTitle;
          _cachedArtist = uiArtist;
        }
      }
      if (!_uiaTitle.empty() &&
          (!mediaPropsHasMetadata || (mediaPropsFailed && !trackChangePending) || (trackChangePending && uiaFreshThisCycle))) {
        out.title = _uiaTitle;
        out.artist = _uiaArtist;
        mediaPropsHasMetadata = true;
      }
    }

    if (!mediaPropsHasMetadata && !trackChangePending && (!_cachedTitle.empty() || !_cachedArtist.empty())) {
      out.title = _cachedTitle;
      out.artist = _cachedArtist;
    }
    out.refreshing = trackChangePending && !mediaPropsHasMetadata;

    {
      std::lock_guard<std::mutex> lock(_mu);
      auto now = std::chrono::steady_clock::now();
      if (!_pendingPlayback.empty()) {
        if (now <= _pendingPlaybackUntil) {
          if (out.playback == _pendingPlayback) {
            _pendingPlayback.clear();
          } else {
            out.playback = _pendingPlayback;
          }
        } else {
          _pendingPlayback.clear();
        }
      }
      _lastPlayback = out.playback;
    }

    std::wstring stateSummary = L"State app=\"" + out.app + L"\" title=\"" + out.title + L"\" artist=\"" + out.artist +
                                L"\" playback=" + widgetmusic::Utf8ToWide(out.playback) +
                                L" session=" + (out.has_session ? L"1" : L"0") +
                                L" prev=" + (out.can_prev ? L"1" : L"0") +
                                L" pp=" + (out.can_play_pause ? L"1" : L"0") +
                                L" next=" + (out.can_next ? L"1" : L"0") +
                                L" refreshing=" + (out.refreshing ? L"1" : L"0") +
                                L" timeline=" + (out.has_timeline ? L"1" : L"0") +
                                L" posMs=" + std::to_wstring(out.position_ms) +
                                L" durMs=" + std::to_wstring(out.duration_ms);
    if (stateSummary != _lastStateSummary) {
      _lastStateSummary = stateSummary;
      LogDebugLine(stateSummary);
    }

    return out;
  }

  void UpdateThread() {
    // Light polling fallback for cases where events are flaky.
    auto lastPoll = std::chrono::steady_clock::now();

    while (true) {
      bool stop = false;
      bool doUpdate = false;
      bool fastActive = false;
      {
        std::unique_lock<std::mutex> lock(_mu);
        auto now = std::chrono::steady_clock::now();
        fastActive = now < _fastRefreshUntil;
        _cv.wait_for(lock,
                     fastActive ? std::chrono::milliseconds(kFastPollIntervalMs)
                                : std::chrono::milliseconds(kSlowPollIntervalMs),
                     [&] { return _stop || _dirty; });
        stop = _stop;
        doUpdate = _dirty;
        _dirty = false;
        fastActive = std::chrono::steady_clock::now() < _fastRefreshUntil;
      }
      if (stop) break;

      auto now = std::chrono::steady_clock::now();
      if (!doUpdate && !fastActive && (now - lastPoll) < std::chrono::seconds(2)) continue;
      lastPoll = now;

      try {
        if (ConsumeForceRebind()) {
          ResetMediaObjects();
        }
        (void)EnsureManager();
        HostState state = BuildState();
        if (_onState) _onState(state);
      } catch (...) {
        LogUpdateFailure(winrt::to_hresult());
      }
    }
  }

  std::mutex _mu;
  std::mutex _sessionMu;
  std::condition_variable _cv;
  std::atomic<bool> _stopping{true};
  bool _stop = false;
  bool _dirty = false;
  std::thread _thread;
  std::thread _commandThread;
  std::mutex _commandMu;
  std::condition_variable _commandCv;
  std::deque<std::string> _commandQueue;

  GlobalSystemMediaTransportControlsSessionManager _manager{nullptr};
  GlobalSystemMediaTransportControlsSession _session{nullptr};

  winrt::event_token _tokSessionsChanged{};
  winrt::event_token _tokCurrentSessionChanged{};
  winrt::event_token _tokPlaybackChanged{};
  winrt::event_token _tokMediaPropsChanged{};

  std::wstring _lastMediaSummary;
  std::wstring _lastStateSummary;
  std::wstring _cachedTitle;
  std::wstring _cachedArtist;
  std::wstring _uiaTitle;
  std::wstring _uiaArtist;

  std::chrono::steady_clock::time_point _lastUiaProbe{};
  std::chrono::steady_clock::time_point _lastMediaPropsLog{};
  std::chrono::steady_clock::time_point _lastUpdateFailureLog{};
  std::chrono::steady_clock::time_point _lastRebindRequest{};
  std::chrono::steady_clock::time_point _nextManagerAttempt{};
  std::chrono::steady_clock::time_point _fastRefreshUntil{};
  std::chrono::steady_clock::time_point _pendingPlaybackUntil{};
  std::chrono::steady_clock::time_point _trackChangeUntil{};
  HRESULT _lastMediaPropsHr = S_OK;
  HRESULT _lastUpdateHr = S_OK;
  std::string _pendingPlayback;
  std::string _lastPlayback = "unknown";
  int _managerAttemptFailures = 0;
  bool _forceRebind = false;

  StateCallback _onState;
};

} // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  HostOptions options = ParseHostOptions();
  if (options.startup_delay_ms > 0) {
    LogLine(L"Host startup delay " + std::to_wstring(options.startup_delay_ms) + L"ms");
    ::Sleep(options.startup_delay_ms);
  }

  HANDLE instanceMutex = AcquireHostInstanceMutex();
  if (!instanceMutex) return 0;

  LogLine(std::wstring(L"Host start prewarm=") + (options.prewarm ? L"1" : L"0"));
  winrt::init_apartment(winrt::apartment_type::multi_threaded);

  HANDLE stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!stopEvent) {
    ::CloseHandle(instanceMutex);
    return 1;
  }

  PipeServer server(stopEvent, options.prewarm);
  MediaSessionTracker tracker([&](const HostState& s) { server.SetLatestState(BuildStateLine(s)); });
  server.SetOnCommand([&](std::string_view name) { tracker.HandleCommand(name); });

  tracker.Start();
  server.Start();

  // Run while the deskband is connected; the deskband will launch a fresh host on-demand.
  ::WaitForSingleObject(stopEvent, INFINITE);

  server.Stop();
  tracker.Stop();

  ::CloseHandle(stopEvent);
  ::CloseHandle(instanceMutex);
  return 0;
}
