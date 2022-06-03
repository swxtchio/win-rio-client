#pragma once

#include <cstdio>
#include <cinttypes>
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstring>

namespace swxtch {
namespace str {

#ifndef RSIZE_MAX_STR
#define RSIZE_MAX_STR (4UL << 10) /* 4KB */
#endif

/// Safe Lib specific errno codes.
#pragma region ErrorNumberCodes

// null ptr
#ifndef ESNULLP
#define ESNULLP (400)
#endif

// length is zero
#ifndef ESZEROL
#define ESZEROL (401)
#endif

// length is below min
#ifndef ESLEMIN
#define ESLEMIN (402)
#endif

// length exceeds max
#ifndef ESLEMAX
#define ESLEMAX (403)
#endif

// overlap undefined
#ifndef ESOVRLP
#define ESOVRLP (404)
#endif

// empty string
#ifndef ESEMPTY
#define ESEMPTY (405)
#endif

// not enough space for s2
#ifndef ESNOSPC
#define ESNOSPC (406)
#endif

// unterminated string
#ifndef ESUNTERM
#define ESUNTERM (407)
#endif

// no difference
#ifndef ESNODIFF
#define ESNODIFF (408)
#endif

// not found
#ifndef ESNOTFND
#define ESNOTFND (409)
#endif

// Good things
#ifndef EOK
#define EOK (0)
#endif

// Defined this way so that error codes are returned as negative numbers.
#define RCNEGATE(x) (-(x))

#pragma endregion ErrorNumberCodes

typedef int errno_t;
typedef size_t rsize_t;

rsize_t strnterminate_s(char* dest, rsize_t dmax);
rsize_t strnlen_s(const char* dest, rsize_t dmax);
errno_t strncpy_s(char* dest, rsize_t dmax, const char* src, rsize_t slen);

using StrVec_t = std::vector<std::string>;

StrVec_t split(std::string s, const char delim = ' ');

StrVec_t split(const std::string& s, const std::string& delimiter);

bool IsEqualCaseInsensitive(const std::string& a, const std::string& b);

// trim from end of string (right)
std::string& rtrim(std::string& s, const std::string& t = " \t\n\r\f\v");

// trim from beginning of string (left)
std::string& ltrim(std::string& s, const std::string& t = " \t\n\r\f\v");

// trim from both ends of string (right then left)
std::string& trim(std::string& s, const std::string& t = " \t\n\r\f\v");

#pragma region Join functions

namespace separators {

// Join customization parameters, can be privided as {.sep = "-", .start="", .end+""}
struct join_params {
    std::string_view sep{ " " };
    std::string_view start;
    std::string_view end;
    std::string_view before;
    std::string_view after;
    std::string_view empty;
};

const join_params array_sep = {
    ", ",   // sep
    "[",    // start
    "]",    // end
    "",     // before
    "",     // after
    ""      // empty
};

const join_params string_sep_with_coma = {
    ", ",   // sep
    "",     // start
    "",     // end
    "\"",   // before
    "\"",   // after
    ""      // empty
};

const join_params huge_array_sep = {
    ", ",    // sep
    "[",     // start
    "]",     // end
    "\n    ",// before
    "",      // after
    ""       // empty
};

} // namespace separators

// Join function to print onto a generic ostream
template <typename FwdIt>
inline std::ostream& join(std::ostream& os, FwdIt beg_, FwdIt end_, const separators::join_params& params = {}) {
    if (beg_ == end_) {
        if (params.empty.data() != nullptr) {
            os <<  params.empty;
        }
        else {
            os << params.start << params.end;
        }
    }
    else {
        os << params.start;
        os << params.before << *beg_++ << params.after;
        while (!(beg_ == end_)) {
            os << params.sep << params.before << *beg_++ << params.after;
        }
        os << params.end;
    }
    return os;
}

template <typename Container>
inline std::ostream& join(std::ostream& os, const Container& container, const separators::join_params& params = {}) {
    return join(os, std::begin(container), std::end(container), params);
}

// Helper class to adapt join to work on streams and as strings
template <typename FwdIt>
struct joiner_ {
    FwdIt begin;
    FwdIt end;
    separators::join_params params;

    friend std::ostream& operator<<(std::ostream& os, const joiner_& j) {
        return join(os, j.begin, j.end, j.params);
    }
    std::string str() const {
        std::ostringstream oss;
        join(oss, begin, end, params);
        return oss.str();
    }
    operator std::string() const {
        return str();
    }
};

// When used to return a string on some cases you have to use the .str() method
// because on some situations multiple automatic conversions on are not permitted
template <typename FwdIt>
inline joiner_<FwdIt> join(FwdIt begin_, FwdIt end_, const separators::join_params& params_) {
    return joiner_<FwdIt>{ begin_, end_, params_ };
}

// When used to return a string on some cases you have to use the .str() method
// because on some situations multiple authomagic conversions on are not permitted
template <typename Container>
inline auto join(const Container& container, const separators::join_params& params_) {
    return join( std::begin(container), std::end(container), params_ );
}


struct CommaSeparated {
    const uint64_t value;
    int len = 20;
    char prettyInt[64];

    CommaSeparated(uint64_t _value, int _len) : value(_value), len(_len) {
        // Note: Do not work for negative values
        // Max: 18,446,744,073,709,551,615
        snprintf(prettyInt, sizeof prettyInt, "%" PRIu64, value);
        int len = 0;
        while (prettyInt[len] != 0) {
            ++len;
        }
        int nlen = len + (len - 1) / 3;
        if (len >= 0 && nlen >= 0 && nlen != len) {
            prettyInt[nlen--] = prettyInt[len--];
            int n = 0;
            while (len >= 0 && nlen >= 0 && nlen != len) {
                prettyInt[nlen--] = prettyInt[len--];
                if (nlen >= 0 && ++n % 3 == 0) {
                    prettyInt[nlen--] = ',';
                }
            }
        }
    }
    friend std::ostream& operator<<(std::ostream& os, const CommaSeparated& cs ) {
        return os << std::setw(std::strlen(cs.prettyInt)) << cs.prettyInt;
    }
    std::string str() {
        return prettyInt;
    }
};

struct Percent {
    const double ratio;
    int len = 5;
    int dec = 2;

    friend std::ostream& operator<<(std::ostream& os, const Percent& p ) {
        char buf[128];
        snprintf(buf, sizeof buf, "%*.*f %%", p.len, p.dec, p.ratio * 100);
        return os << buf;
    }
};

struct Duration {
    int64_t count;
    const char* unit = "ns";

    template <class Rep, class Period>
    Duration(std::chrono::duration<Rep, Period> d) : count{ std::chrono::duration_cast<std::chrono::nanoseconds>( d ).count() } {
        bool neg = (count < 0);
        if (neg) {
            count = -count;
        }
        if (count > 100'000'000'000) {
            count /= 1'000'000'000;
            unit = "s";
        }
        else if (count > 100'000'000) {
            count /= 1'000'000;
            unit = "ms";
        }
        else if (count > 100'000) {
            count /= 1'000;
            unit = "us";
        }
        if (neg) {
            count = -count;
        }
    }

    friend std::ostream& operator<<(std::ostream& os, const Duration& d ) {
        return os << d.count << ' ' << d.unit;
    }
};

std::string FormatValueToSI(const double value, const int presision = 1);

}  // end namespace str
}  // end namespace swxtch
