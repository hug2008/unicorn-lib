#include "unicorn/format.hpp"
#include "unicorn/regex.hpp"
#include <cmath>
#include <cstdio>

using namespace RS::Unicorn::Literals;
using namespace std::chrono;
using namespace std::literals;

namespace RS {

    namespace Unicorn {

        namespace UnicornDetail {

            namespace {

                // These will always be called with x>=0 and prec>=0

                U8string float_print(const char* format, long double x, int prec) {
                    std::vector<char> buf(32);
                    for (;;) {
                        if (snprintf(buf.data(), buf.size(), format, prec, x) < int(buf.size()))
                            return buf.data();
                        buf.resize(2 * buf.size());
                    }
                }

                void float_strip(U8string& str) {
                    static const Regex pattern("(.*)(\\.(?:\\d*[1-9])?)(0+)(e.*)?");
                    auto match = pattern.match(str);
                    if (match) {
                        U8string result = match[1];
                        if (match.count(2) != 1)
                            result += match[2];
                        result += match[4];
                        str.swap(result);
                    }
                }

                U8string float_digits(long double x, int prec) {
                    prec = std::max(prec, 1);
                    auto result = float_print("%.*Le", x, prec - 1);
                    auto epos = result.find_first_of("Ee");
                    auto exponent = strtol(result.data() + epos + 1, nullptr, 10);
                    result.resize(epos);
                    if (exponent < 0) {
                        if (prec > 1)
                            result.erase(1, 1);
                        result.insert(0, 1 - exponent, '0');
                        result[1] = '.';
                    } else if (exponent >= prec - 1) {
                        if (prec > 1)
                            result.erase(1, 1);
                        result.insert(result.end(), exponent - prec + 1, '0');
                    } else if (exponent > 0) {
                        result.erase(1, 1);
                        result.insert(exponent + 1, 1, '.');
                    }
                    return result;
                }

                U8string float_exp(long double x, int prec) {
                    prec = std::max(prec, 1);
                    auto result = float_print("%.*Le", x, prec - 1);
                    auto epos = result.find_first_of("Ee");
                    char esign = 0;
                    if (result[epos + 1] == '-')
                        esign = '-';
                    auto epos2 = result.find_first_not_of("+-0", epos + 1);
                    U8string exponent;
                    if (epos2 < result.size())
                        exponent = result.substr(epos2);
                    result.resize(epos);
                    result += 'e';
                    if (esign)
                        result += esign;
                    if (exponent.empty())
                        result += '0';
                    else
                        result += exponent;
                    return result;
                }

                U8string float_fixed(long double x, int prec) {
                    return float_print("%.*Lf", x, prec);
                }

                U8string float_general(long double x, int prec) {
                    using std::floor;
                    using std::log10;
                    if (x == 0)
                        return float_digits(x, prec);
                    auto exp = int(floor(log10(x)));
                    auto d_estimate = exp < 0 ? prec + 1 - exp : exp < prec - 1 ? prec + 1 : exp + 1;
                    auto e_estimate = exp < 0 ? prec + 4 : prec + 3;
                    auto e_vs_d = e_estimate - d_estimate;
                    if (e_vs_d <= -2)
                        return float_exp(x, prec);
                    if (e_vs_d >= 2)
                        return float_digits(x, prec);
                    auto dform = float_digits(x, prec);
                    auto eform = float_exp(x, prec);
                    return dform.size() <= eform.size() ? dform : eform;
                }

                U8string string_escape(const U8string& s, uint64_t mode) {
                    U8string result;
                    result.reserve(s.size() + 2);
                    if (mode & fx_quote)
                        result += '\"';
                    for (auto i = utf_begin(s), e = utf_end(s); i != e; ++i) {
                        switch (*i) {
                            case U'\0': result += "\\0"; break;
                            case U'\t': result += "\\t"; break;
                            case U'\n': result += "\\n"; break;
                            case U'\f': result += "\\f"; break;
                            case U'\r': result += "\\r"; break;
                            case U'\\': result += "\\\\"; break;
                            case U'\"':
                                if (mode & fx_quote)
                                    result += '\\';
                                result += '\"';
                                break;
                            default:
                                if (*i >= 32 && *i <= 126) {
                                    result += char(*i);
                                } else if (*i >= 0xa0 && ! (mode & fx_ascii)) {
                                    result.append(s, i.offset(), i.count());
                                } else if (*i <= 0xff) {
                                    result += "\\x";
                                    result += format_radix(*i, 16, 2);
                                } else {
                                    result += "\\x{";
                                    result += format_radix(*i, 16, 1);
                                    result += '}';
                                }
                                break;
                        }
                    }
                    if (mode & fx_quote)
                        result += '\"';
                    return result;
                }

                template <typename Range>
                U8string string_values(const Range& s, int base, int prec, int defprec) {
                    if (prec < 0)
                        prec = defprec;
                    U8string result;
                    for (auto c: s) {
                        result += format_radix(char_to_uint(c), base, prec);
                        result += ' ';
                    }
                    if (! result.empty())
                        result.pop_back();
                    return result;
                }

            }

            // Formatting for specific types

            void translate_flags(const U8string& str, uint64_t& flags, int& prec, size_t& width, char32_t& pad) {
                flags = 0;
                prec = -1;
                width = 0;
                pad = U' ';
                auto i = utf_begin(str), end = utf_end(str);
                while (i != end) {
                    if (*i == U'<' || *i == U'=' || *i == U'>') {
                        if (*i == U'<')
                            flags |= fx_left;
                        else if (*i == U'=')
                            flags |= fx_centre;
                        else
                            flags |= fx_right;
                        ++i;
                        if (i != end && ! char_is_digit(*i))
                            pad = *i++;
                        if (i != end && char_is_digit(*i))
                            i = str_to_int<size_t>(width, i);
                    } else if (char_is_ascii(*i) && ascii_isalpha(char(*i))) {
                        flags |= letter_to_mask(*i++);
                    } else if (char_is_digit(*i)) {
                        i = str_to_int<int>(prec, i);
                    } else {
                        ++i;
                    }
                }
            }

            U8string format_float(long double t, uint64_t flags, int prec) {
                using std::fabs;
                static constexpr auto format_flags = fx_digits | fx_exp | fx_fixed | fx_general;
                static constexpr auto sign_flags = fx_sign | fx_signz;
                if (ibits(flags & format_flags) > 1 || ibits(flags & sign_flags) > 1)
                    throw std::invalid_argument("Inconsistent formatting flags");
                if (prec < 0)
                    prec = 6;
                auto mag = fabs(t);
                U8string s;
                if (flags & fx_digits)
                    s = float_digits(mag, prec);
                else if (flags & fx_exp)
                    s = float_exp(mag, prec);
                else if (flags & fx_fixed)
                    s = float_fixed(mag, prec);
                else
                    s = float_general(mag, prec);
                if (flags & fx_stripz)
                    float_strip(s);
                if (t < 0 || (flags & fx_sign) || (t > 0 && (flags & fx_signz)))
                    s.insert(s.begin(), t < 0 ? '-' : '+');
                return s;
            }

            U8string format_roman(uint32_t n) {
                struct char_value { const char* str; unsigned num; };
                static const char_value table[] {
                    { "M", 1000 },
                    { "CM", 900 }, { "D", 500 }, { "CD", 400 }, { "C", 100 },
                    { "XC", 90 }, { "L", 50 }, { "XL", 40 }, { "X", 10 },
                    { "IX", 9 }, { "V", 5 }, { "IV", 4 }, { "I", 1 },
                };
                if (n == 0)
                    return "0";
                U8string s;
                for (auto& t: table) {
                    auto q = n / t.num;
                    n %= t.num;
                    for (unsigned long long i = 0; i < q; ++i)
                        s += t.str;
                }
                return s;
            }

            // Alignment and padding

            U8string format_align(U8string src, uint64_t flags, size_t width, char32_t pad) {
                if (ibits(flags & (fx_left | fx_centre | fx_right)) > 1)
                    throw std::invalid_argument("Inconsistent formatting alignment flags");
                if (ibits(flags & (fx_lower | fx_title | fx_upper)) > 1)
                    throw std::invalid_argument("Inconsistent formatting case conversion flags");
                if (flags & fx_lower)
                    str_lowercase_in(src);
                else if (flags & fx_title)
                    str_titlecase_in(src);
                else if (flags & fx_upper)
                    str_uppercase_in(src);
                size_t len = str_length(src, flags & fx_length_flags);
                if (width <= len)
                    return src;
                size_t extra = width - len;
                U8string dst;
                if (flags & fx_right)
                    str_append_chars(dst, extra, pad);
                else if (flags & fx_centre)
                    str_append_chars(dst, extra / 2, pad);
                dst += src;
                if (flags & fx_left)
                    str_append_chars(dst, extra, pad);
                else if (flags & fx_centre)
                    str_append_chars(dst, (extra + 1) / 2, pad);
                return dst;
            }

        }

        // Basic formattng functions

        U8string format_type(bool t, uint64_t flags, int /*prec*/) {
            static constexpr auto format_flags = fx_binary | fx_tf | fx_yesno;
            if (ibits(flags & format_flags) > 1)
                throw std::invalid_argument("Inconsistent formatting flags");
            if (flags & fx_binary)
                return t ? "1" : "0";
            else if (flags & fx_yesno)
                return t ? "yes" : "no";
            else
                return t ? "true" : "false";
        }

        U8string format_type(const U8string& t, uint64_t flags, int prec) {
            using namespace UnicornDetail;
            static constexpr auto format_flags = fx_ascii | fx_ascquote | fx_escape | fx_decimal | fx_hex | fx_hex8 | fx_hex16 | fx_quote;
            if (ibits(flags & format_flags) > 1)
                throw std::invalid_argument("Inconsistent formatting flags");
            if (flags & fx_quote)
                return string_escape(t, fx_quote);
            else if (flags & fx_ascquote)
                return string_escape(t, fx_quote | fx_ascii);
            else if (flags & fx_escape)
                return string_escape(t, 0);
            else if (flags & fx_ascii)
                return string_escape(t, fx_ascii);
            else if (flags & fx_decimal)
                return string_values(utf_range(t), 10, prec, 1);
            else if (flags & fx_hex8)
                return string_values(t, 16, prec, 2);
            else if (flags & fx_hex16)
                return string_values(to_utf16(t), 16, prec, 4);
            else if (flags & fx_hex)
                return string_values(utf_range(t), 16, prec, 1);
            else
                return t;
        }

        U8string format_type(system_clock::time_point t, uint64_t flags, int prec) {
            static constexpr auto format_flags = fx_iso | fx_common;
            if (ibits(flags & format_flags) > 1)
                throw std::invalid_argument("Inconsistent formatting flags");
            auto zone = flags & fx_local ? Zone::local : Zone::utc;
            if (flags & fx_common)
                return format_date(t, "%c"s, zone);
            auto result = format_date(t, prec, zone);
            if (flags & fx_iso) {
                auto pos = result.find(' ');
                if (pos != npos)
                    result[pos] = 'T';
            }
            return result;
        }

        // Formatter class

        Format::Format(const U8string& format):
        fmt(format), seq() {
            auto i = utf_begin(format), end = utf_end(format);
            while (i != end) {
                auto j = std::find(i, end, U'$');
                add_literal(u_str(i, j));
                i = std::next(j);
                if (i == end)
                    break;
                U8string prefix, suffix;
                if (char_is_digit(*i)) {
                    auto k = std::find_if_not(i, end, char_is_digit);
                    j = std::find_if_not(k, end, char_is_alphanumeric);
                    prefix = u_str(i, k);
                    suffix = u_str(k, j);
                } else if (*i == U'{') {
                    auto k = std::next(i);
                    if (char_is_digit(*k)) {
                        auto l = std::find_if_not(k, end, char_is_digit);
                        j = std::find(l, end, U'}');
                        if (j != end) {
                            prefix = u_str(k, l);
                            suffix = u_str(l, j);
                            ++j;
                        }
                    }
                }
                if (prefix.empty()) {
                    add_literal(i.str());
                    ++i;
                } else {
                    add_index(str_to_int<unsigned>(prefix), suffix);
                    i = j;
                }
            }
        }

        void Format::add_index(unsigned index, const U8string& flags) {
            using namespace UnicornDetail;
            element elem;
            elem.index = index;
            translate_flags(to_utf8(flags), elem.flags, elem.prec, elem.width, elem.pad);
            seq.push_back(elem);
            if (index > 0 && index > num)
                num = index;
        }

        void Format::add_literal(const U8string& text) {
            if (! text.empty()) {
                if (seq.empty() || seq.back().index != 0)
                    seq.push_back({0, text, 0, 0, 0, 0});
                else
                    seq.back().text += text;
            }
        }

    }

}
