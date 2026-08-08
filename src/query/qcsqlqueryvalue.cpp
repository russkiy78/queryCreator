#include "qcsqlqueryvalue.h"

#include "qcsqldialect.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace {

// Named to avoid colliding with the public QcSqlQueryValue::trim() member
// below (a bare `trim(...)` call inside a member function resolves to the
// member first regardless of argument count, hiding this free function
// entirely -- not an overload-resolution choice C++ makes for you).
std::string trimWhitespace(const std::string & value)
{
    auto isSpace = [](unsigned char ch) { return std::isspace(ch); };
    auto begin = std::find_if_not(value.begin(), value.end(), isSpace);
    auto end = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return begin < end ? std::string(begin, end) : std::string();
}

} // namespace

QcSqlQueryValue::QcSqlQueryValue()
{
}

QcSqlQueryValue::QcSqlQueryValue(const std::string & name)
{
    static const std::regex aliasPattern("<(.+)>");

    const std::string trimmed = trimWhitespace(name);
    std::smatch match;
    if (std::regex_search(trimmed, match, aliasPattern)) {
        m_columnAlias = match[1].str();
        m_columnName = trimWhitespace(trimmed.substr(0, static_cast<std::size_t>(match.position(0))));
    } else {
        m_columnName = trimmed;
    }
}

QcSqlQueryValue & QcSqlQueryValue::useDriver(QcDbDriver driver)
{
    m_driver = driver;
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::concat(const QcStringList & concateVal)
{
    std::vector<QcVariant> params(concateVal.begin(), concateVal.end());
    m_functions.emplace_back(_concat_, std::move(params));
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::cast(const int & from, const int & to)
{
    m_functions.emplace_back(_cast_, std::vector<QcVariant>{static_cast<long long>(from), static_cast<long long>(to)});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::round(const int & precision)
{
    m_functions.emplace_back(_round_, std::vector<QcVariant>{static_cast<long long>(precision)});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::upperCase()
{
    m_functions.emplace_back(_upperCase_, std::vector<QcVariant>{});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::lowerCase()
{
    m_functions.emplace_back(_lowerCase_, std::vector<QcVariant>{});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::count(bool distinct)
{
    m_functions.emplace_back(_count_, std::vector<QcVariant>{static_cast<long long>(distinct ? 1 : 0)});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::sum()
{
    m_functions.emplace_back(_sum_, std::vector<QcVariant>{});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::avg()
{
    m_functions.emplace_back(_avg_, std::vector<QcVariant>{});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::min()
{
    m_functions.emplace_back(_min_, std::vector<QcVariant>{});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::max()
{
    m_functions.emplace_back(_max_, std::vector<QcVariant>{});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::coalesce(const QcStringList & fallbacks)
{
    std::vector<QcVariant> params(fallbacks.begin(), fallbacks.end());
    m_functions.emplace_back(_coalesce_, std::move(params));
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::nullIf(const std::string & compareExpr)
{
    m_functions.emplace_back(_nullIf_, std::vector<QcVariant>{compareExpr});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::trim()
{
    m_functions.emplace_back(_trim_, std::vector<QcVariant>{});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::substring(int start, int length)
{
    m_functions.emplace_back(_substring_, std::vector<QcVariant>{static_cast<long long>(start), static_cast<long long>(length)});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::length()
{
    m_functions.emplace_back(_length_, std::vector<QcVariant>{});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::replace(const std::string & search, const std::string & replacement)
{
    m_functions.emplace_back(_replace_, std::vector<QcVariant>{search, replacement});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::case_(const std::vector<std::pair<std::string, std::string>> & whenThenPairs,
                                          const std::optional<std::string> & elseResult)
{
    // Encoding: [hasElse, cond1, then1, cond2, then2, ..., elseResult?] --
    // hasElse (0/1) disambiguates "no ELSE clause" from "ELSE with an empty
    // string result", which an empty-string sentinel alone couldn't.
    std::vector<QcVariant> params;
    params.reserve(1 + whenThenPairs.size() * 2 + (elseResult.has_value() ? 1 : 0));
    params.emplace_back(static_cast<long long>(elseResult.has_value() ? 1 : 0));
    for (const auto & whenThen : whenThenPairs) {
        params.emplace_back(whenThen.first);
        params.emplace_back(whenThen.second);
    }
    if (elseResult.has_value()) {
        params.emplace_back(*elseResult);
    }
    m_functions.emplace_back(_case_, std::move(params));
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::extract(int part)
{
    m_functions.emplace_back(_extract_, std::vector<QcVariant>{static_cast<long long>(part)});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::dateAdd(int part, int amount)
{
    m_functions.emplace_back(_dateAdd_, std::vector<QcVariant>{static_cast<long long>(part), static_cast<long long>(amount)});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::jsonExtract(const std::string & jsonSearchPath)
{
    m_functions.emplace_back(_jsonExtract_, std::vector<QcVariant>{static_cast<long long>(_jsonAsText_), jsonSearchPath});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::jsonExtractNumber(const std::string & jsonSearchPath)
{
    m_functions.emplace_back(_jsonExtract_, std::vector<QcVariant>{static_cast<long long>(_jsonAsNumber_), jsonSearchPath});
    return *this;
}

QcSqlQueryValue & QcSqlQueryValue::jsonExtractRaw(const std::string & jsonSearchPath)
{
    m_functions.emplace_back(_jsonExtract_, std::vector<QcVariant>{static_cast<long long>(_jsonAsRaw_), jsonSearchPath});
    return *this;
}

std::string QcSqlQueryValue::toSql() const
{
    return toSql(m_driver);
}

std::string QcSqlQueryValue::toSql(QcDbDriver driver) const
{
    std::string expr = QcSqlDialect::quoteRef(m_columnName, driver);

    for (const auto & function : m_functions) {
        switch (function.first) {
            case _upperCase_:
                expr = "UPPER(" + expr + ")";
                break;
            case _lowerCase_:
                expr = "LOWER(" + expr + ")";
                break;
            case _round_:
                expr = "ROUND(" + expr + ", " + std::to_string(std::get<long long>(function.second[0])) + ")";
                break;
            case _cast_:
                // function.second[0] (the "from" type) has no place in CAST(expr AS to)
                // syntax -- only the target type is ever rendered.
                expr = "CAST(" + expr + " AS " + QcSqlDialect::dataTypeName(static_cast<int>(std::get<long long>(function.second[1])), driver) + ")";
                break;
            case _concat_: {
                // concat()'s QcStringList entries are rendered as raw SQL
                // expressions (column names, typically, but also string
                // literals like "' '" -- see the isPlainIdentifierSegment
                // callers below), the same way QcStringList is used
                // elsewhere in this API (groupBy(), orderAsc(), ...) -- not
                // as literal string data, so they are never bound as
                // parameters. Each operand is run through quoteRef() before
                // joining, same as the base column above -- it quotes an
                // operand that's actually a plain column reference (e.g.
                // "email") and leaves anything else (a string literal like
                // "' '", or any other expression) untouched, so a field
                // named inside a function argument is quoted exactly as
                // consistently as one used directly. The actual join syntax
                // (CONCAT(...) vs "||") is dialect-dependent -- see
                // QcSqlDialect::concatExpr.
                std::vector<std::string> operands{expr};
                for (const QcVariant & arg : function.second) {
                    operands.push_back(QcSqlDialect::quoteRef(std::get<std::string>(arg), driver));
                }
                expr = QcSqlDialect::concatExpr(operands, driver);
                break;
            }
            case _count_: {
                const bool distinct = std::get<long long>(function.second[0]) != 0;
                expr = "COUNT(" + std::string(distinct ? "DISTINCT " : "") + expr + ")";
                break;
            }
            case _sum_:
                expr = "SUM(" + expr + ")";
                break;
            case _avg_:
                expr = "AVG(" + expr + ")";
                break;
            case _min_:
                expr = "MIN(" + expr + ")";
                break;
            case _max_:
                expr = "MAX(" + expr + ")";
                break;
            case _coalesce_: {
                // Same raw-expression-via-quoteRef() convention as _concat_
                // above -- see coalesce()'s doc comment in qcsqlqueryvalue.h.
                std::string args = expr;
                for (const QcVariant & fallback : function.second) {
                    args += ", " + QcSqlDialect::quoteRef(std::get<std::string>(fallback), driver);
                }
                expr = "COALESCE(" + args + ")";
                break;
            }
            case _nullIf_:
                expr = "NULLIF(" + expr + ", " + QcSqlDialect::quoteRef(std::get<std::string>(function.second[0]), driver) + ")";
                break;
            case _trim_:
                expr = "TRIM(" + expr + ")";
                break;
            case _substring_:
                expr = QcSqlDialect::substringExpr(expr, static_cast<int>(std::get<long long>(function.second[0])),
                    static_cast<int>(std::get<long long>(function.second[1])), driver);
                break;
            case _length_:
                expr = QcSqlDialect::lengthExpr(expr, driver);
                break;
            case _replace_:
                expr = "REPLACE(" + expr + ", " + QcSqlDialect::quoteRef(std::get<std::string>(function.second[0]), driver) + ", "
                    + QcSqlDialect::quoteRef(std::get<std::string>(function.second[1]), driver) + ")";
                break;
            case _case_: {
                // See case_()'s doc comment in qcsqlqueryvalue.h: this
                // deliberately ignores/replaces `expr` rather than wrapping
                // it -- a searched CASE has no single subject to wrap.
                // Encoding matches case_(): [hasElse, cond1, then1, ...,
                // elseResult?].
                const bool hasElse = std::get<long long>(function.second[0]) != 0;
                const std::size_t pairsEnd = hasElse ? function.second.size() - 1 : function.second.size();
                std::string caseExpr = "CASE";
                for (std::size_t i = 1; i + 1 < pairsEnd; i += 2) {
                    caseExpr += " WHEN " + QcSqlDialect::quoteRef(std::get<std::string>(function.second[i]), driver)
                        + " THEN " + QcSqlDialect::quoteRef(std::get<std::string>(function.second[i + 1]), driver);
                }
                if (hasElse) {
                    caseExpr += " ELSE " + QcSqlDialect::quoteRef(std::get<std::string>(function.second.back()), driver);
                }
                caseExpr += " END";
                expr = caseExpr;
                break;
            }
            case _extract_:
                expr = QcSqlDialect::extractExpr(expr, static_cast<int>(std::get<long long>(function.second[0])), driver);
                break;
            case _dateAdd_:
                expr = QcSqlDialect::dateAddExpr(expr, static_cast<int>(std::get<long long>(function.second[0])),
                    static_cast<int>(std::get<long long>(function.second[1])), driver);
                break;
            case _jsonExtract_:
                expr = QcSqlDialect::jsonExtractExpr(expr, std::get<std::string>(function.second[1]),
                    static_cast<int>(std::get<long long>(function.second[0])), driver);
                break;
            default:
                // _no_function_ never gets pushed onto the chain -- nothing
                // to render for it.
                break;
        }
    }

    if (!m_columnAlias.empty()) {
        expr += " AS " + QcSqlDialect::quoteIdentifier(m_columnAlias, driver);
    }

    return expr;
}
