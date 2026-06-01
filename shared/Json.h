#pragma once

#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace widgetmusic {

inline std::string JsonEscape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);

  for (unsigned char ch : s) {
    switch (ch) {
      case '\"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (ch < 0x20) {
          static const char* kHex = "0123456789ABCDEF";
          out += "\\u00";
          out += kHex[(ch >> 4) & 0xF];
          out += kHex[(ch >> 0) & 0xF];
        } else {
          out.push_back(static_cast<char>(ch));
        }
        break;
    }
  }

  return out;
}

inline std::string JsonQuote(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 2);
  out.push_back('\"');
  out += JsonEscape(s);
  out.push_back('\"');
  return out;
}

inline const char* JsonBool(bool v) { return v ? "true" : "false"; }

inline void JsonSkipWs(std::string_view s, size_t* i) {
  while (*i < s.size() && std::isspace(static_cast<unsigned char>(s[*i]))) ++(*i);
}

inline bool JsonHexVal(char ch, uint8_t* v) {
  if (ch >= '0' && ch <= '9') { *v = static_cast<uint8_t>(ch - '0'); return true; }
  if (ch >= 'a' && ch <= 'f') { *v = static_cast<uint8_t>(10 + (ch - 'a')); return true; }
  if (ch >= 'A' && ch <= 'F') { *v = static_cast<uint8_t>(10 + (ch - 'A')); return true; }
  return false;
}

inline void JsonAppendUtf8ForCodepoint(uint32_t cp, std::string* out) {
  if (cp <= 0x7F) {
    out->push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out->push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out->push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

inline bool JsonParseString(std::string_view s, size_t* i, std::string* out) {
  if (*i >= s.size() || s[*i] != '\"') return false;
  ++(*i);

  std::string tmp;
  while (*i < s.size()) {
    char ch = s[*i];
    if (ch == '\"') {
      ++(*i);
      *out = std::move(tmp);
      return true;
    }
    if (ch == '\\') {
      ++(*i);
      if (*i >= s.size()) return false;
      char esc = s[*i];
      ++(*i);
      switch (esc) {
        case '\"': tmp.push_back('\"'); break;
        case '\\': tmp.push_back('\\'); break;
        case '/': tmp.push_back('/'); break;
        case 'b': tmp.push_back('\b'); break;
        case 'f': tmp.push_back('\f'); break;
        case 'n': tmp.push_back('\n'); break;
        case 'r': tmp.push_back('\r'); break;
        case 't': tmp.push_back('\t'); break;
        case 'u': {
          if (*i + 4 > s.size()) return false;
          uint8_t h1 = 0, h2 = 0, h3 = 0, h4 = 0;
          if (!JsonHexVal(s[*i + 0], &h1) || !JsonHexVal(s[*i + 1], &h2) || !JsonHexVal(s[*i + 2], &h3) ||
              !JsonHexVal(s[*i + 3], &h4))
            return false;
          *i += 4;
          uint32_t cp = (static_cast<uint32_t>(h1) << 12) | (static_cast<uint32_t>(h2) << 8) |
                        (static_cast<uint32_t>(h3) << 4) | static_cast<uint32_t>(h4);
          JsonAppendUtf8ForCodepoint(cp, &tmp);
          break;
        }
        default: return false;
      }
      continue;
    }
    tmp.push_back(ch);
    ++(*i);
  }
  return false;
}

inline bool JsonTryGetString(std::string_view json, std::string_view key, std::string* out) {
  if (!out) return false;
  std::string pat;
  pat.reserve(key.size() + 2);
  pat.push_back('\"');
  pat.append(key);
  pat.push_back('\"');

  size_t pos = json.find(pat);
  if (pos == std::string_view::npos) return false;
  pos = json.find(':', pos + pat.size());
  if (pos == std::string_view::npos) return false;
  ++pos;
  JsonSkipWs(json, &pos);
  return JsonParseString(json, &pos, out);
}

inline bool JsonTryGetBool(std::string_view json, std::string_view key, bool* out) {
  if (!out) return false;
  std::string pat;
  pat.reserve(key.size() + 2);
  pat.push_back('\"');
  pat.append(key);
  pat.push_back('\"');

  size_t pos = json.find(pat);
  if (pos == std::string_view::npos) return false;
  pos = json.find(':', pos + pat.size());
  if (pos == std::string_view::npos) return false;
  ++pos;
  JsonSkipWs(json, &pos);

  if (json.substr(pos, 4) == "true") {
    *out = true;
    return true;
  }
  if (json.substr(pos, 5) == "false") {
    *out = false;
    return true;
  }
  return false;
}

inline bool JsonTryGetInt64(std::string_view json, std::string_view key, int64_t* out) {
  if (!out) return false;
  std::string pat;
  pat.reserve(key.size() + 2);
  pat.push_back('\"');
  pat.append(key);
  pat.push_back('\"');

  size_t pos = json.find(pat);
  if (pos == std::string_view::npos) return false;
  pos = json.find(':', pos + pat.size());
  if (pos == std::string_view::npos) return false;
  ++pos;
  JsonSkipWs(json, &pos);
  if (pos >= json.size()) return false;

  bool neg = false;
  if (json[pos] == '-') {
    neg = true;
    ++pos;
  }
  if (pos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[pos]))) return false;

  constexpr uint64_t kInt64Max = static_cast<uint64_t>((std::numeric_limits<int64_t>::max)());
  constexpr uint64_t kInt64MinAbs = kInt64Max + 1ull;
  const uint64_t limit = neg ? kInt64MinAbs : kInt64Max;

  uint64_t acc = 0;
  while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
    uint64_t digit = static_cast<uint64_t>(json[pos] - '0');
    if (acc > (limit - digit) / 10ull) return false;
    acc = acc * 10ull + digit;
    ++pos;
  }

  if (neg) {
    if (acc == kInt64MinAbs) {
      *out = (std::numeric_limits<int64_t>::min)();
    } else {
      *out = -static_cast<int64_t>(acc);
    }
  } else {
    *out = static_cast<int64_t>(acc);
  }
  return true;
}

} // namespace widgetmusic
