#include "pop/runtime.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
namespace pop {
Value::Value(double n) : data(n) {
  if (!std::isfinite(n))
    throw Fault("nonfinite real value");
}
std::vector<char32_t> decode(const std::string &s, std::size_t *valid_bytes) {
  if (valid_bytes)
    *valid_bytes = 0;
  std::vector<char32_t> out;
  for (std::size_t i = 0; i < s.size();) {
    unsigned char b = s[i++];
    char32_t c = b;
    unsigned n = 0;
    if (b >= 0x80) {
      if ((b & 0xe0) == 0xc0) {
        c = b & 31;
        n = 1;
      } else if ((b & 0xf0) == 0xe0) {
        c = b & 15;
        n = 2;
      } else if ((b & 0xf8) == 0xf0) {
        c = b & 7;
        n = 3;
      } else
        throw Fault("invalid UTF-8");
      if (i + n > s.size())
        throw NeedInput{};
      for (unsigned j = 0; j < n; ++j) {
        unsigned char d = s[i++];
        if ((d & 0xc0) != 0x80)
          throw Fault("invalid UTF-8");
        c = (c << 6) | (d & 63);
      }
      if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff) ||
          c < (n == 1   ? 0x80
               : n == 2 ? 0x800
                        : 0x10000))
        throw Fault("invalid UTF-8 scalar");
    }
    out.push_back(c);
    if (valid_bytes)
      *valid_bytes = i;
  }
  return out;
}
std::string encode(char32_t c) {
  if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
    throw Fault("invalid character");
  std::string s;
  if (c < 0x80)
    s += char(c);
  else if (c < 0x800) {
    s += char(0xc0 | (c >> 6));
    s += char(0x80 | (c & 63));
  } else if (c < 0x10000) {
    s += char(0xe0 | (c >> 12));
    s += char(0x80 | ((c >> 6) & 63));
    s += char(0x80 | (c & 63));
  } else {
    s += char(0xf0 | (c >> 18));
    s += char(0x80 | ((c >> 12) & 63));
    s += char(0x80 | ((c >> 6) & 63));
    s += char(0x80 | (c & 63));
  }
  return s;
}
std::string canonical(const std::string &s) {
  std::string out;
  unsigned count = 0;
  for (auto c : decode(s)) {
    if (count++ == 6)
      break;
    if (c >= 'a' && c <= 'z')
      c -= 32;
    if (c >= U'а' && c <= U'я')
      c -= 32;
    if (c == U'ё')
      c = U'Ё';
    const std::u32string russian = U"АВЕКМНОРСТУХ";
    const std::string latin = "ABEKMHOPCTYX";
    auto p = russian.find(c);
    if (p != std::u32string::npos)
      c = latin[p];
    out += encode(c);
  }
  return out;
}
std::int64_t integer(Value v) {
  if (!v.integer())
    throw Fault("integer required");
  return std::get<std::int64_t>(v.data);
}
double real(Value v) {
  if (v.integer())
    return double(integer(v));
  if (v.real())
    return std::get<double>(v.data);
  throw Fault("number required");
}
bool equal(Value a, Value b) {
  if (a.data.index() != b.data.index())
    return false;
  if (a.integer())
    return integer(a) == integer(b);
  if (a.real())
    return real(a) == real(b);
  if (a.object())
    return a.handle() == b.handle();
  return true;
}
int compare(Value a, Value b) {
  if (!a.numeric() || !b.numeric())
    throw Fault("numeric comparison requires numbers");
  if (a.integer() && b.integer())
    return (integer(a) > integer(b)) - (integer(a) < integer(b));
  if (a.real() && b.real())
    return (real(a) > real(b)) - (real(a) < real(b));
  if (a.real())
    return -compare(b, a);
  const auto i = integer(a);
  const auto d = real(b);
  if (d >= 9223372036854775808.0)
    return -1;
  if (d < -9223372036854775808.0)
    return 1;
  const auto t = static_cast<std::int64_t>(d);
  if (i != t)
    return (i > t) ? 1 : -1;
  const auto integral = std::trunc(d);
  return d > integral ? -1 : d < integral ? 1 : 0;
}
Value arithmetic(const std::string &op, Value a, Value b) {
  if (a.integer() && b.integer() && op == "!" && integer(b) >= 0) {
    auto exponent = std::uint64_t(integer(b));
    Value result(std::int64_t(1));
    while (exponent) {
      if (exponent & 1)
        result = arithmetic("*", result, a);
      exponent >>= 1;
      if (exponent)
        a = arithmetic("*", a, a);
    }
    return result;
  }
  if (a.integer() && b.integer() && op != "/" && op != "!") {
    const auto x = integer(a), y = integer(b);
    std::int64_t n = 0;
    bool overflow = false;
    if (op == "+")
      overflow = __builtin_add_overflow(x, y, &n);
    else if (op == "-")
      overflow = __builtin_sub_overflow(x, y, &n);
    else if (op == "*")
      overflow = __builtin_mul_overflow(x, y, &n);
    else
      throw Fault("unknown arithmetic operator");
    if (overflow)
      throw Fault("integer overflow");
    return Value(n);
  }
  const double x = real(a), y = real(b);
  if (op == "/" && y == 0)
    throw Fault("division by zero");
  const double n = op == "+"   ? x + y
                   : op == "-" ? x - y
                   : op == "*" ? x * y
                   : op == "/" ? x / y
                               : std::pow(x, y);
  if (!std::isfinite(n))
    throw Fault("real domain error or overflow");
  return Value(n);
}
std::string number_text(Value v) {
  if (v.integer())
    return std::to_string(integer(v));
  char buffer[128];
  auto r = std::to_chars(buffer, buffer + sizeof buffer, real(v));
  if (r.ec != std::errc{})
    throw Fault("number formatting failed");
  std::string s(buffer, r.ptr);
  if (s.find_first_of(".eE") == std::string::npos)
    s += ".0";
  return s;
}
std::vector<Token> tokenize(const std::string &source, const std::string &name,
                            bool eof, std::size_t limit) {
  auto chars = decode(source);
  std::vector<Token> tokens;
  std::size_t pos = 0, line = 1, col = 1;
  auto advance = [&] {
    if (chars[pos++] == '\n') {
      ++line;
      col = 1;
    } else
      ++col;
  };
  auto digit = [](char32_t c) { return c >= '0' && c <= '9'; };
  auto wordchar = [](char32_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
           c >= 128;
  };
  while (pos < chars.size()) {
    auto c = chars[pos];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      advance();
      continue;
    }
    Token t;
    t.where = {name, line, col};
    if (c == '_') {
      advance();
      if (pos == chars.size()) {
        if (!eof)
          throw NeedInput{};
        throw Fault("missing character after underscore");
      }
      t.kind = Token::Kind::number;
      t.value = Value(std::int64_t(chars[pos]));
      t.text = std::to_string(std::int64_t(chars[pos]));
      advance();
    } else if (c == '\'' || c == '"') {
      t.kind = c == '\'' ? Token::Kind::string : Token::Kind::quoted;
      advance();
      bool closed = false;
      while (pos < chars.size()) {
        if (chars[pos] == c) {
          if (c == '\'') {
            if (pos + 1 == chars.size() && !eof)
              throw NeedInput{};
            if (pos + 1 < chars.size() && chars[pos + 1] == c) {
              t.text += '\'';
              advance();
              advance();
              continue;
            }
          }
          advance();
          closed = true;
          break;
        }
        t.text += encode(chars[pos]);
        advance();
      }
      if (!closed) {
        if (!eof)
          throw NeedInput{};
        throw Fault("unterminated quoted text");
      }
    } else if (digit(c) ||
               (c == '.' && pos + 1 < chars.size() && digit(chars[pos + 1]))) {
      t.kind = Token::Kind::number;
      while (pos < chars.size() && digit(chars[pos])) {
        t.text += char(chars[pos]);
        advance();
      }
      if (pos < chars.size() && chars[pos] == ':' &&
          (t.text == "2" || t.text == "8")) {
        t.text += ':';
        advance();
        while (pos < chars.size() && digit(chars[pos])) {
          t.text += char(chars[pos]);
          advance();
        }
      } else {
        if (!eof && pos + 1 == chars.size() && chars[pos] == '.')
          throw NeedInput{};
        if (pos < chars.size() && chars[pos] == '.' && pos + 1 < chars.size() &&
            digit(chars[pos + 1])) {
          t.text += '.';
          advance();
          while (pos < chars.size() && digit(chars[pos])) {
            t.text += char(chars[pos]);
            advance();
          }
        }
        if (pos < chars.size() && (chars[pos] == '$' || chars[pos] == '&')) {
          t.text += 'e';
          advance();
          if (pos < chars.size() && (chars[pos] == '+' || chars[pos] == '-')) {
            t.text += char(chars[pos]);
            advance();
          }
          auto begin = pos;
          while (pos < chars.size() && digit(chars[pos])) {
            t.text += char(chars[pos]);
            advance();
          }
          if (begin == pos) {
            if (!eof)
              throw NeedInput{};
            throw Fault("missing real exponent");
          }
        }
      }
      if (pos == chars.size() && !eof)
        throw NeedInput{};
    } else if (wordchar(c)) {
      t.kind = Token::Kind::word;
      while (pos < chars.size() &&
             (wordchar(chars[pos]) || digit(chars[pos]))) {
        t.text += encode(chars[pos]);
        advance();
      }
      if (pos == chars.size() && !eof)
        throw NeedInput{};
      t.text = canonical(t.text);
      if (t.text == "COMMEN") {
        while (pos < chars.size() && chars[pos] != ';')
          advance();
        if (pos == chars.size() && !eof)
          throw NeedInput{};
        if (pos < chars.size())
          advance();
        continue;
      }
    } else {
      if (!eof && pos + 1 == chars.size() &&
          std::u32string(U"-=<>/:([%.").find(c) != std::u32string::npos)
        throw NeedInput{};
      t.kind = Token::Kind::symbol;
      t.text += char(c);
      advance();
      if (pos < chars.size()) {
        auto pair = t.text + char(chars[pos]);
        if (pair == "->" || pair == "=>" || pair == "=<" || pair == ">=" ||
            pair == "/=" || pair == "//" || pair == "::" || pair == "<>" ||
            pair == "(%" || pair == "%)" || pair == "[%" || pair == "%]") {
          t.text = pair;
          advance();
        }
      }
    }
    tokens.push_back(std::move(t));
    if (limit && tokens.size() >= limit)
      break;
  }
  tokens.push_back({Token::Kind::end, "", {name, line, col}});
  return tokens;
}
} // namespace pop
