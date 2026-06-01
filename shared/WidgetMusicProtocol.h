#pragma once

// IPC is UTF-8 JSON lines over a local named pipe.
namespace widgetmusic {

inline constexpr int kProtocolVersion = 1;
inline constexpr wchar_t kPipePath[] = L"\\\\.\\pipe\\WidgetMusic.Pipe.v1";

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
