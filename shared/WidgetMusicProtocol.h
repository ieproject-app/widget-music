#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <windows.h>

// IPC is UTF-8 JSON lines over a local named pipe.
namespace widgetmusic {

inline constexpr int kProtocolVersion = 1;
inline constexpr wchar_t kPipePathPrefix[] = L"\\\\.\\pipe\\WidgetMusic.Pipe.v1.Session.";
inline constexpr size_t kMaxPipeMessageBytes = 16 * 1024;
inline constexpr size_t kMaxAppChars = 96;
inline constexpr size_t kMaxTitleChars = 256;
inline constexpr size_t kMaxArtistChars = 192;

inline std::wstring PipePathForSessionId(DWORD sessionId) {
  return std::wstring(kPipePathPrefix) + std::to_wstring(sessionId);
}

inline DWORD CurrentProcessSessionId() {
  DWORD sessionId = 0;
  if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &sessionId)) return 0;
  return sessionId;
}

inline std::wstring PipePathForCurrentSession() {
  return PipePathForSessionId(CurrentProcessSessionId());
}

inline bool IsSupportedProtocolVersion(int64_t version) {
  return version == kProtocolVersion;
}

inline std::wstring ClampProtocolText(std::wstring_view text, size_t maxChars) {
  if (text.size() <= maxChars) return std::wstring(text);
  if (maxChars == 0) return {};

  size_t count = maxChars;
  if (count < text.size() && count > 0) {
    const wchar_t previous = text[count - 1];
    if (previous >= 0xD800 && previous <= 0xDBFF) --count;
  }
  return std::wstring(text.substr(0, count));
}

// JSON keys (host -> client)
inline constexpr char kMsgType[] = "type";
inline constexpr char kTypeHello[] = "hello";
inline constexpr char kTypeState[] = "state";

inline constexpr char kKeyVersion[] = "version";
inline constexpr char kKeyPid[] = "pid";

inline constexpr char kKeyConnected[] = "connected";
inline constexpr char kKeyHasSession[] = "has_session";
inline constexpr char kKeyApp[] = "app";
inline constexpr char kKeyTitle[] = "title";
inline constexpr char kKeyArtist[] = "artist";
inline constexpr char kKeyPlayback[] = "playback"; // "playing" | "paused" | "stopped" | "unknown"
inline constexpr char kKeyCanPrev[] = "can_prev";
inline constexpr char kKeyCanNext[] = "can_next";
inline constexpr char kKeyCanPlayPause[] = "can_play_pause";
inline constexpr char kKeyRefreshing[] = "refreshing";
inline constexpr char kKeyHasTimeline[] = "has_timeline";
inline constexpr char kKeyPositionMs[] = "position_ms";
inline constexpr char kKeyDurationMs[] = "duration_ms";

// JSON keys (client -> host)
inline constexpr char kTypeCommand[] = "command";
inline constexpr char kKeyName[] = "name"; // "previous" | "next" | "play" | "pause" | "playpause" | "refresh"

} // namespace widgetmusic
