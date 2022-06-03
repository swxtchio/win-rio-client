#pragma once

//clang-format off
#include "NetworkUtils.hpp"
#include "StringUtils.hpp"
//clang-format on

/**
 * Template Range Class
 *
 * A range is an iterable with limits: "from" & "till", and from <= till
 * T has to be comparable, streamable and addable.
 *
 **/
namespace swxtch {
namespace utils {
template <class T>
class Range {
    class Iterator {
       public:
        Iterator(const T from) : _current(from) {
        }

        bool operator!=(const Iterator it) const {
            return _current != it._current;
        }
        Iterator& operator++() {
            _current = _current + 1;
            return *this;
        }
        auto operator*() const {
            return _current;
        }

       private:
        T _current;
    };

   public:
    Range(const T& from, const T& to) : _from(from), _till(to) {
        checkRange();
    }

    Range(const T& value) : Range(value, value) {
    }

    friend std::ostream& operator<<(std::ostream& stream, const Range& range) {
        return stream << range._from << "-" << range._till;
    }

    auto begin() const {
        return Iterator{_from};
    }
    auto end() const {
        return Iterator{static_cast<T>(_till + 1)};
    }

   private:
    void checkRange() const {
        if (_from > _till) {
            std::stringstream ss;
            ss << "from: " << _from << " > "
               << "till: " << _till;
            throw std::invalid_argument(ss.str());
        }
    }

    T _from;
    T _till;
};

using net::Ipv4Addr_t;
using str::split;
using parseStr_t = std::string (*)(std::string, std::string);

/**
 * Template Factory to produce Ranges out of a string: "from-till"
 *
 * @param range string to be converted into a range<T>
 * @param fromString function to convert from type string to type T
 * @param parseFrom  callback if "from" needs to be generated out of the string tuple (from, till)
 * @param parseTill  callback if "till" needs to be generated out of the string tuple (from, till)
 *
 * @return Range of type T
 **/
template <class T>
Range<T> MakeRange(const std::string& range,
                   T (*fromString)(std::string),
                   parseStr_t parseFrom = nullptr,
                   parseStr_t parseTill = nullptr) {
    auto parts = split(range, '-');
    if (parts.size() == 1) {
        parts.resize(2);
        parts[1] = parts[0];
    } else if (parts.size() != 2) {
        throw std::invalid_argument("invalid range format: " + range);
    }

    auto from = parseFrom ? parseFrom(parts[0], parts[1]) : parts[0];
    auto till = parseTill ? parseTill(parts[0], parts[1]) : parts[1];

    return Range(fromString(from), fromString(till));
};

/**
 *  An Ipv4 range can be created from an "incomplete" till.
 *  Example: 192.168.1.3-2.1  (it will complete the till parameter to: 192.168.2.1)
 */
Range<Ipv4Addr_t> MakeIpv4Range(const std::string& range) {
    auto completeIp = [](std::string from, std::string till) {
        size_t pos = 0;
        for (int dots = 0; dots < 3 - std::count(till.begin(), till.end(), '.'); dots++) {
            pos = from.find('.', pos);
            pos += 1;
        }
        return from.substr(0, pos) + till;
    };

    return MakeRange<Ipv4Addr_t>(
        range, [](auto val) { return Ipv4Addr_t{val}; }, nullptr, completeIp);
}

Range<uint16_t> MakePortRange(const std::string& range) {
    return MakeRange<uint16_t>(range, [](auto val) { return (uint16_t)std::stoi(val); });
}

}  // namespace utils
}  // namespace swxtch

