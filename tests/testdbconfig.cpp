#include "testdbconfig.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>

namespace {

std::string trim(const std::string & value)
{
    auto isSpace = [](unsigned char ch) { return std::isspace(ch); };
    auto begin = std::find_if_not(value.begin(), value.end(), isSpace);
    auto end = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return begin < end ? std::string(begin, end) : std::string();
}

// QC_TEST_SOURCE_DIR is the tests/ directory's own CMAKE_CURRENT_SOURCE_DIR
// (see tests/CMakeLists.txt) -- an absolute path baked in at compile time so
// db_config.ini is found regardless of the working directory ctest happens
// to invoke the test binary from.
std::string configFilePath()
{
    return std::string(QC_TEST_SOURCE_DIR) + "/db_config.ini";
}

} // namespace

std::optional<QcConnectionParams> loadTestDbConfig(const std::string & section)
{
    std::ifstream file(configFilePath());
    if (!file) {
        return std::nullopt;
    }

    std::string currentSection;
    std::map<std::string, std::string> values;
    bool sectionFound = false;

    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            currentSection = trim(trimmed.substr(1, trimmed.size() - 2));
            if (currentSection == section) {
                sectionFound = true;
            }
            continue;
        }
        if (currentSection != section) {
            continue;
        }
        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        values[trim(trimmed.substr(0, eq))] = trim(trimmed.substr(eq + 1));
    }

    if (!sectionFound) {
        return std::nullopt;
    }

    QcConnectionParams params;
    if (auto it = values.find("host"); it != values.end()) {
        params.host = it->second;
    }
    if (auto it = values.find("port"); it != values.end()) {
        params.port = it->second;
    }
    if (auto it = values.find("database"); it != values.end()) {
        params.database = it->second;
    }
    if (auto it = values.find("user"); it != values.end()) {
        params.user = it->second;
    }
    if (auto it = values.find("password"); it != values.end()) {
        params.password = it->second;
    }
    if (auto it = values.find("connect_timeout_seconds"); it != values.end() && !it->second.empty()) {
        params.connectTimeoutSeconds = std::stoi(it->second);
    }
    return params;
}

std::vector<QcDbDriver> compiledInDrivers()
{
    std::vector<QcDbDriver> drivers;
#ifdef QC_DB_HAS_POSTGRESQL
    drivers.push_back(QcDbDriver::PostgreSQL);
#endif
#ifdef QC_DB_HAS_ORACLE
    drivers.push_back(QcDbDriver::Oracle);
#endif
#ifdef QC_DB_HAS_MYSQL
    drivers.push_back(QcDbDriver::MySQL);
#endif
#ifdef QC_DB_HAS_SQLITE
    drivers.push_back(QcDbDriver::SQLite);
#endif
#ifdef QC_DB_HAS_MSSQL
    drivers.push_back(QcDbDriver::MSSQL);
#endif
    return drivers;
}

QcDbDriver primaryTestDriver()
{
    if (const char * override = std::getenv("QC_TEST_DRIVER")) {
        std::string name = override;
        if (name == "PostgreSQL" || name == "postgresql") return QcDbDriver::PostgreSQL;
        if (name == "Oracle"    || name == "oracle")     return QcDbDriver::Oracle;
        if (name == "MySQL"     || name == "mysql")      return QcDbDriver::MySQL;
        if (name == "SQLite"    || name == "sqlite")     return QcDbDriver::SQLite;
        if (name == "MSSQL"     || name == "mssql")      return QcDbDriver::MSSQL;
    }
    const std::vector<QcDbDriver> drivers = compiledInDrivers();
    return drivers.empty() ? QcDbDriver::PostgreSQL : drivers.front();
}

namespace {

std::string configSection(QcDbDriver driver)
{
    switch (driver) {
        case QcDbDriver::PostgreSQL: return "postgresql";
        case QcDbDriver::Oracle: return "oracle";
        case QcDbDriver::MySQL: return "mysql";
        case QcDbDriver::SQLite: return "sqlite";
        case QcDbDriver::MSSQL: return "mssql";
    }
    return "postgresql";
}

} // namespace

QcConnectionParams testDbConfigOrDefault(QcDbDriver driver)
{
    if (std::optional<QcConnectionParams> fromFile = loadTestDbConfig(configSection(driver))) {
        fromFile->driver = driver;
        return *fromFile;
    }

    QcConnectionParams params;
    params.driver = driver;
    if (driver == QcDbDriver::SQLite) {
        params.database = "file:qc_tests_default?mode=memory&cache=shared";
        return params;
    }

    // Matches this project's long-standing local PostgreSQL convention (see
    // docs/testing.md) -- also the harmless-but-likely-unreachable
    // placeholder for MySQL/MSSQL/Oracle absent a db_config.ini entry (see
    // db_config.ini.example): connecting fails unless a server happens to be
    // reachable at these exact demo/demo/demo@127.0.0.1:5432-shaped
    // coordinates on that driver's own default port, which callers turn
    // into GTEST_SKIP() rather than a hard failure.
    params.host = "127.0.0.1";
    params.port = "5432";
    params.database = "demo";
    params.user = "demo";
    params.password = "demo";
    return params;
}

QcConnectionParams testDbConfigOrDefault()
{
    return testDbConfigOrDefault(primaryTestDriver());
}
