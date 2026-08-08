#include "testintegrationsupport.h"

#include <random>
#include <type_traits>
#include <variant>

#include "query/qcsqldialect.h"
#include "testdbconfig.h"

std::string placeholderList(std::size_t count, std::size_t startIndex)
{
    std::string result;
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += QcSqlDialect::placeholder(startIndex + i, primaryTestDriver());
    }
    return result;
}

double asDouble(const QcSqlBase::QcVariant & value)
{
    return std::visit([](const auto & alt) -> double {
        using T = std::decay_t<decltype(alt)>;
        if constexpr (std::is_same_v<T, std::string>) {
            return std::stod(alt);
        } else if constexpr (std::is_same_v<T, long long>) {
            return static_cast<double>(alt);
        } else if constexpr (std::is_same_v<T, double>) {
            return alt;
        } else {
            return 0.0;
        }
    }, value);
}

long long asInt64(const QcSqlBase::QcVariant & value)
{
    return std::visit([](const auto & alt) -> long long {
        using T = std::decay_t<decltype(alt)>;
        if constexpr (std::is_same_v<T, std::string>) {
            return std::stoll(alt);
        } else if constexpr (std::is_same_v<T, long long>) {
            return alt;
        } else if constexpr (std::is_same_v<T, double>) {
            return static_cast<long long>(alt);
        } else {
            return 0;
        }
    }, value);
}

bool asBool(const QcSqlBase::QcVariant & value)
{
    return std::visit([](const auto & alt) -> bool {
        using T = std::decay_t<decltype(alt)>;
        if constexpr (std::is_same_v<T, std::string>) {
            return alt == "t" || alt == "true" || alt == "1";
        } else if constexpr (std::is_same_v<T, long long>) {
            return alt != 0;
        } else {
            return false;
        }
    }, value);
}

std::vector<std::byte> asBytes(const QcSqlBase::QcVariant & value)
{
    if (std::holds_alternative<std::vector<std::byte>>(value)) {
        return std::get<std::vector<std::byte>>(value);
    }

    const std::string & hex = std::get<std::string>(value);
    std::vector<std::byte> bytes;
    if (hex.size() < 2 || hex[0] != '\\' || hex[1] != 'x') {
        return bytes;
    }

    auto hexDigit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return c - 'A' + 10;
    };

    bytes.reserve((hex.size() - 2) / 2);
    for (std::size_t i = 2; i + 1 < hex.size(); i += 2) {
        const int hi = hexDigit(hex[i]);
        const int lo = hexDigit(hex[i + 1]);
        bytes.push_back(static_cast<std::byte>((hi << 4) | lo));
    }
    return bytes;
}

std::string generateRandomText(std::size_t targetBytes, std::uint64_t seed)
{
    static const char * const words[] = {
        "alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf", "hotel",
        "india", "juliet", "kilo", "lima", "mike", "november", "oscar", "papa",
        "quebec", "romeo", "sierra", "tango", "uniform", "victor", "whiskey",
        "xray", "yankee", "zulu", "query", "builder", "dataset", "record", "table",
    };
    constexpr std::size_t wordCount = sizeof(words) / sizeof(words[0]);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::size_t> pick(0, wordCount - 1);

    std::string text;
    text.reserve(targetBytes + 16);
    while (text.size() < targetBytes) {
        text += words[pick(rng)];
        text += ' ';
    }
    text.resize(targetBytes);
    return text;
}

std::vector<std::byte> generateRandomBytes(std::size_t count, std::uint64_t seed)
{
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> byteDist(0, 255);

    std::vector<std::byte> bytes(count);
    for (std::size_t i = 0; i < count; ++i) {
        bytes[i] = static_cast<std::byte>(byteDist(rng));
    }
    return bytes;
}
