#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "Json.h"
#include "Utf8.h"
#include "WidgetMusicProtocol.h"
#include "WidgetMusicVisual.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* name) {
  if (condition) {
    std::cout << "[OK] " << name << '\n';
  } else {
    std::cerr << "[FAIL] " << name << '\n';
    ++g_failures;
  }
}

bool AcceptHello(std::string_view json) {
  std::string type;
  int64_t version = 0;
  return widgetmusic::JsonTryGetString(json, widgetmusic::kMsgType, &type) &&
         type == widgetmusic::kTypeHello &&
         widgetmusic::JsonTryGetInt64(json, widgetmusic::kKeyVersion, &version) &&
         widgetmusic::IsSupportedProtocolVersion(version);
}

}  // namespace

int main() {
  using namespace widgetmusic;

  Check(JsonQuote("a\"b\n") == "\"a\\\"b\\n\"", "JSON quoting escapes control characters");
  std::string title;
  Check(JsonTryGetString(R"({"title":"hello \u2605"})", "title", &title) && title == "hello \xE2\x98\x85",
        "JSON parser decodes unicode escape");
  bool enabled = false;
  Check(JsonTryGetBool(R"({"enabled":true})", "enabled", &enabled) && enabled, "JSON parser reads booleans");
  int64_t number = 0;
  Check(JsonTryGetInt64(R"({"value":-9223372036854775808})", "value", &number) &&
            number == (std::numeric_limits<int64_t>::min)(),
        "JSON parser accepts int64 minimum");
  Check(!JsonTryGetInt64(R"({"value":9223372036854775808})", "value", &number),
        "JSON parser rejects int64 overflow");

  const std::wstring unicode = L"Musik \x00E9 \x4E16\x754C";
  Check(Utf8ToWide(WideToUtf8(unicode)) == unicode, "UTF-8 conversion round-trips metadata");
  Check(Utf8ToWide(std::string("\xC3", 1)).empty(), "UTF-8 decoder rejects truncated sequence");

  Check(PipePathForSessionId(17) == L"\\\\.\\pipe\\WidgetMusic.Pipe.v1.Session.17",
        "pipe endpoint contains Windows session id");
  Check(PipePathForSessionId(17) != PipePathForSessionId(18), "two sessions use different pipe endpoints");
  Check(IsSupportedProtocolVersion(1) && !IsSupportedProtocolVersion(2), "protocol version gate is strict");
  Check(AcceptHello(R"({"type":"hello","version":1})"), "hello handshake accepts supported protocol");
  Check(!AcceptHello(R"({"type":"hello","version":2})"), "hello handshake rejects unsupported protocol");
  Check(!AcceptHello(R"({"type":"state","version":1})"), "state message cannot substitute for hello handshake");

  Check(ClampProtocolText(std::wstring(kMaxTitleChars + 10, L'x'), kMaxTitleChars).size() == kMaxTitleChars,
        "title metadata is capped");
  const std::wstring surrogate{static_cast<wchar_t>(0xD83D), static_cast<wchar_t>(0xDE00), L'x'};
  Check(ClampProtocolText(surrogate, 1).empty(), "metadata clamp does not split surrogate pair");
  Check(kMaxPipeMessageBytes == 16 * 1024, "IPC payload cap remains 16 KB");

  const std::vector<COLORREF> surfaceSamples{
      RGB(44, 60, 65), RGB(43, 61, 64), RGB(44, 60, 66), RGB(255, 255, 255), RGB(44, 59, 65)};
  const COLORREF median = MedianColor(surfaceSamples, RGB(0, 0, 0));
  Check(MaxChannelDelta(median, RGB(44, 60, 65)) <= 1, "median taskbar color ignores icon outlier");
  Check(MedianColor({}, RGB(1, 2, 3)) == RGB(1, 2, 3), "median color keeps fallback for empty samples");
  Check(MaxChannelDelta(RGB(44, 60, 65), RGB(50, 55, 70)) == 6, "channel delta reports visual threshold");

  Check(SmoothStep(-0.5f) == 0.0f && SmoothStep(0.0f) == 0.0f && SmoothStep(1.0f) == 1.0f &&
            SmoothStep(1.5f) == 1.0f,
        "smoothstep clamps and preserves endpoints");
  int previousWidth = InterpolateSmoothInt(132, 300, 0.0f);
  bool widthsAreMonotonic = previousWidth == 132;
  for (int frame = 1; frame <= 20; ++frame) {
    const int width = InterpolateSmoothInt(132, 300, static_cast<float>(frame) / 20.0f);
    widthsAreMonotonic = widthsAreMonotonic && width >= previousWidth;
    previousWidth = width;
  }
  Check(widthsAreMonotonic && previousWidth == 300, "smooth interpolation grows monotonically to its endpoint");
  Check(InterpolateSmoothInt(132, 300, -1.0f) == 132 && InterpolateSmoothInt(132, 300, 2.0f) == 300,
        "smooth interpolation clamps out-of-range progress");
  Check(InterpolateSmoothInt(132, 300, 0.25f) == InterpolateSmoothInt(300, 132, 0.75f),
        "smooth interpolation reverses symmetrically");

  if (g_failures != 0) {
    std::cerr << g_failures << " test(s) failed.\n";
    return 1;
  }
  std::cout << "Widget Music lightweight tests passed.\n";
  return 0;
}
