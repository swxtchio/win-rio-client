#include <cmath>
#include <cfenv>

#include "stringUtils.hpp"

namespace swxtch {
namespace str {

StrVec_t split(std::string s, const char delim) {
    StrVec_t result;
    std::stringstream ss(std::move(s));
    std::string item;
    while (std::getline(ss, item, delim)) {
        result.push_back(item);
    }
    return result;
}

StrVec_t split(const std::string& s, const std::string& delimiter) {
    size_t pos_start = 0;
    size_t pos_end;
    size_t delim_len = delimiter.length();
    std::string token;
    StrVec_t res;
    while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
        token = s.substr(pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back(token);
    }
    res.push_back(s.substr(pos_start));
    return res;
}

bool IsEqualCaseInsensitive(const std::string& a, const std::string& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](char a, char b) { return tolower(a) == tolower(b); });
}

// trim from end of string (right)
std::string& rtrim(std::string& s, const std::string& t) {
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

// trim from beginning of string (left)
std::string& ltrim(std::string& s, const std::string& t) {
    s.erase(0, s.find_first_not_of(t));
    return s;
}

// trim from both ends of string (right then left)
std::string& trim(std::string& s, const std::string& t) {
    return ltrim(rtrim(s, t), t);
}

/*
 * Safe C Lib internal string routine to consolidate error handling
 */
static inline void handle_error(char* orig_dest, rsize_t orig_dmax) {
    // null string to eliminate partial copy
    while (orig_dmax) {
        *orig_dest = '\0';
        orig_dmax--;
        orig_dest++;
    }
    return;
}

/**
 * The strncpy_s function copies not more than slen successive characters
 * (characters that follow a null character are not copied) from the array
 * pointed to by src to the array pointed to by dest.
 *
 * If no null character was copied from src, then dest[n] is set to a null character.
 *
 * PARAMETERS:
 * [out]    dest	pointer to string that will be replaced by src.
 * [in]     dmax	restricted maximum length of dest
 * [in]     src     pointer to the string that will be copied to dest
 * [in]     slen	the maximum number of characters to copy from src
 *
 * RETURNS:
 * If there is a runtime-constraint violation, then if dest is not a null pointer and
 * dmax less than RSIZE_MAX_STR, then strncpy_s nulls dest.
 *
 * EOK	    successful operation, the characters in src were copied to dest and the result is null
 *terminated. ESNULLP	when dest/src is NULL pointer ESZEROL	when dmax/slen = 0 ESLEMAX	when
 *dmax/slen > RSIZE_MAX_STR ESOVRLP	when strings overlap ESNOSPC	when dest < src
 *
 **/
errno_t strncpy_s(char* dest, rsize_t dmax, const char* src, rsize_t slen) {
    rsize_t orig_dmax;
    char* orig_dest;
    const char* overlap_bumper;

    if (dest == NULL) {
        return RCNEGATE(ESNULLP);
    }

    if (dmax == 0) {
        return RCNEGATE(ESZEROL);
    }

    if (dmax > RSIZE_MAX_STR) {
        return RCNEGATE(ESLEMAX);
    }

    // hold base in case src was not copied
    orig_dmax = dmax;
    orig_dest = dest;

    if (src == NULL) {
        handle_error(orig_dest, orig_dmax);
        return RCNEGATE(ESNULLP);
    }
    if (slen == 0) {
        handle_error(orig_dest, orig_dmax);
        return RCNEGATE(ESZEROL);
    }
    if (slen > RSIZE_MAX_STR) {
        handle_error(orig_dest, orig_dmax);
        return RCNEGATE(ESLEMAX);
    }

    if (dest < src) {
        overlap_bumper = src;
        while (dmax > 0) {
            if (dest == overlap_bumper) {
                handle_error(orig_dest, orig_dmax);
                return RCNEGATE(ESOVRLP);
            }
            if (slen == 0) {
                *dest = '\0';
                return RCNEGATE(EOK);
            }
            *dest = *src;
            if (*dest == '\0') {
                return RCNEGATE(EOK);
            }
            dmax--;
            slen--;
            dest++;
            src++;
        }
    } else {
        overlap_bumper = dest;
        while (dmax > 0) {
            if (src == overlap_bumper) {
                handle_error(orig_dest, orig_dmax);
                return RCNEGATE(ESOVRLP);
            }
            if (slen == 0) {
                *dest = '\0';
                return RCNEGATE(EOK);
            }
            *dest = *src;
            if (*dest == '\0') {
                return RCNEGATE(EOK);
            }
            dmax--;
            slen--;
            dest++;
            src++;
        }
    }
    // the entire src was not copied, so zero the string
    handle_error(orig_dest, orig_dmax);
    return RCNEGATE(ESNOSPC);
}

/**
 * The strnlen_s function computes the length of the string pointed to by dest.
 *
 * PARAMETERS:
 * * [in]   dest	pointer to string
 * * [in]   dmax	maximum length of string
 *
 * RETURNS:
 * The function returns the string length, excluding the terminating null
 * character. If dest is NULL, then strnlen_s returns 0.
 * Otherwise, the strnlen_s function returns the number of characters that
 * precede the terminating null character. If there is no null character in
 * the first dmax characters of dest then strnlen_s returns dmax. At most the
 * first dmax characters of dest are accessed by strnlen_s.
 **/
rsize_t strnlen_s(const char* dest, rsize_t dmax) {
    rsize_t count;

    if (dest == NULL) {
        return 0;
    }
    if (dmax == 0) {
        return 0;
    }
    if (dmax > RSIZE_MAX_STR) {
        return 0;
    }

    count = 0;
    while (*dest && dmax) {
        count++;
        dmax--;
        dest++;
    }
    return count;
}

/**
 * The strnterminate_s function will terminate the string if a null is not
 * encountered before dmax characters.
 *
 * PARAMETERS:
 *  * [in]  dest	pointer to string
 *  * [in]  dmax	maximum length of string
 *
 * RETURNS:
 * The function returns a terminated string. If a null is not encountered prior
 * to dmax characters, the dmax character is set to null terminating the string.
 * The string length is also returned
 *
 **/
rsize_t strnterminate_s(char* dest, rsize_t dmax) {
    rsize_t count;

    if (dest == NULL) {
        return (0);
    }
    if (dmax == 0) {
        return (0);
    }
    if (dmax > RSIZE_MAX_STR) {
        return (0);
    }

    count = 0;
    while (dmax > 1) {
        if (*dest) {
            count++;
            dmax--;
            dest++;
        } else {
            break;
        }
    }
    *dest = '\0';
    return count;
}

/**
 * @brief Formats a value to SI units of K,M, or G
 * If less than 1K, leave as is.
 *
 * @param value value to format
 * @param presision how many decimal places when values are between ranges
 * @return SI formated string of the value with K,M, or G suffix
 */
std::string FormatValueToSI(const double value, const int presision) {
    std::ostringstream result;
    const int BaseWidth = 4+presision;
    const auto prev_round = std::fegetround();
    std::fesetround(FE_DOWNWARD);
    if (value < 1e3) {
        result << std::setw(BaseWidth) << std::fixed << std::setprecision(0) << value;
    } else if (value < 1e6) {
        result << std::setw(BaseWidth-1) << std::fixed << std::setprecision((value < 1e5)?presision:0) << (value / 1e3) << "K";
    } else if (value < 1e9) {
        result << std::setw(BaseWidth-1) << std::fixed << std::setprecision((value < 1e8)?presision:0) << (value / 1e6) << "M";
    } else {
        result << std::setw(BaseWidth-1) << std::fixed << std::setprecision((value < 1e11)?presision:0) << (value / 1e9) << "G";
    }
    std::fesetround(prev_round);
    return result.str();
}

}  // end namespace str
}  // end namespace swxtch
