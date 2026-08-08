// "Battle" tests for QcSqlQuery's SELECT surface against a real database
// (PostgreSQL/SQLite -- see testDbConfigOrDefault() in testdbconfig.cpp for
// how to point this at a different local instance, and
// db_config.ini.example for adding MySQL/MSSQL/Oracle once those drivers are
// implemented in QcNativeConnection).
//
// Strategy: SetUpTestSuite() creates a small relational schema
// (departments/employees/notes) once, seeds it with a mix of hand-crafted
// "anchor" rows (for tests that need an exact, known value) and a larger
// batch of deterministically-generated filler rows (for tests that need
// volume -- pagination, GROUP BY, large IN-lists, ...), and keeps the
// generated rows in memory as `s_departments`/`s_employees`/`s_notes`. Every
// test computes its expected result by re-deriving it from that same
// in-memory data with plain C++ (filtering/aggregating), then compares
// against what QcSqlQuery-built SQL actually returned from the live
// database -- so a test failure means the builder produced the wrong SQL,
// not that a hardcoded expectation went stale.
//
// The whole suite is skipped (GTEST_SKIP()) if no database is reachable.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "query/qcnativeconnection.h"
#include "query/qcsqlbase.h"
#include "query/qcsqldialect.h"
#include "query/qcsqlquery.h"
#include "testdbconfig.h"
#include "testintegrationsupport.h"

namespace {

struct SeedDepartment
{
    long long id;
    std::string name;
    std::string region;
    double budget;
};

struct SeedEmployee
{
    long long id;
    long long departmentId;
    std::optional<long long> managerId;
    std::string fullName;
    std::string email;
    std::optional<std::string> nickname;
    double salary;
    std::string hireDate; // "YYYY-MM-DD"
    bool isActive;
    std::string bio;
    int metaLevel;
    bool metaRemote;
    std::vector<std::string> metaSkills;
    double metaRating;
};

struct SeedNote
{
    long long id;
    long long employeeId;
    std::string note;
    std::string createdAt; // "YYYY-MM-DD HH:MM:SS"
};

std::string metadataJson(int level, bool remote, const std::vector<std::string> & skills, double rating)
{
    std::ostringstream out;
    out << "{\"level\":" << level << ",\"remote\":" << (remote ? "true" : "false") << ",\"skills\":[";
    for (std::size_t i = 0; i < skills.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << '"' << skills[i] << '"';
    }
    out << "],\"rating\":" << std::fixed << std::setprecision(1) << rating << "}";
    return out.str();
}

// Shorthand for QcSqlDialect::quoteIdentifier() -- used below to build the
// hand-written SQL text (DDL, seed INSERTs, addFreeText correlated
// conditions) that never goes through the query builder and so is never
// quoted automatically. Quoting it explicitly this way is a no-op-equivalent
// on PostgreSQL/SQLite/MySQL/MSSQL (their unquoted-DDL default already
// matches this lowercase-snake_case text), but is required on Oracle: an
// *unquoted* reference to a table/column that was created with a *quoted*
// name doesn't resolve (Oracle folds unquoted identifiers to UPPERCASE,
// unlike PostgreSQL/SQLite's fold-to-lowercase -- see qcsqldialect.h). Using
// this uniformly on all five drivers, rather than only on Oracle, keeps
// these raw fragments consistent with each other instead of adding a
// separate Oracle-only branch to every one of them.
std::string qi(const std::string & name)
{
    return QcSqlDialect::quoteIdentifier(name, primaryTestDriver());
}

} // namespace

class QcIntegrationSelectTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite();
    static void TearDownTestSuite();

    static const SeedEmployee & employee(long long id);
    static const SeedDepartment & departmentById(long long id);
    static std::vector<SeedEmployee> employeesIn(long long departmentId);

    // Renders `query` and executes it against the shared connection.
    static std::optional<QcResultSet> run(const QcSqlQuery & query);
    // Named counterpart of run() above -- QcNativeConnection::executeNamed()
    // instead of execute(), see the "Named result rows" section below.
    static std::optional<QcNamedResultSet> runNamed(const QcSqlQuery & query);

    static std::unique_ptr<QcNativeConnection> s_conn;
    static std::vector<SeedDepartment> s_departments;
    static std::vector<SeedEmployee> s_employees;
    static std::vector<SeedNote> s_notes;

private:
    static void buildSeedData();
    static void createSchema();
    static void insertSeedData();
    // "DROP TABLE IF EXISTS" itself is not standard DDL every driver here
    // accepts -- Oracle rejects the IF EXISTS clause outright (ORA-00933),
    // so a best-effort "drop this table, don't care if it wasn't there"
    // needs a PL/SQL wrapper swallowing ORA-00942 there instead (see
    // dropTableIfExists's twin in test_integration_dml.cpp).
    static void dropTableIfExists(const std::string & table);
};

std::unique_ptr<QcNativeConnection> QcIntegrationSelectTest::s_conn;
std::vector<SeedDepartment> QcIntegrationSelectTest::s_departments;
std::vector<SeedEmployee> QcIntegrationSelectTest::s_employees;
std::vector<SeedNote> QcIntegrationSelectTest::s_notes;

void QcIntegrationSelectTest::SetUpTestSuite()
{
    try {
        s_conn = std::make_unique<QcNativeConnection>(testDbConfigOrDefault());
    } catch (const std::exception & e) {
        GTEST_SKIP() << "No local database reachable via testDbConfigOrDefault(): " << e.what();
    }

    dropTableIfExists("qc_bt_notes");
    dropTableIfExists("qc_bt_employees");
    dropTableIfExists("qc_bt_departments");

    createSchema();
    buildSeedData();
    insertSeedData();
}

void QcIntegrationSelectTest::TearDownTestSuite()
{
    if (!s_conn) {
        return;
    }
    dropTableIfExists("qc_bt_notes");
    dropTableIfExists("qc_bt_employees");
    dropTableIfExists("qc_bt_departments");
    s_conn.reset();
    s_departments.clear();
    s_employees.clear();
    s_notes.clear();
}

void QcIntegrationSelectTest::dropTableIfExists(const std::string & table)
{
    if (primaryTestDriver() == QcDbDriver::Oracle) {
        // Quoted, case-matching the same "..." this suite's CREATE TABLE below
        // uses -- Oracle's quoted identifiers are case-sensitive ANSI SQL (see
        // qcsqldialect.h), so an unquoted DROP TABLE here would fold to
        // uppercase and miss the quoted-lowercase table entirely.
        s_conn->execute("BEGIN EXECUTE IMMEDIATE 'DROP TABLE \"" + table + "\"'; EXCEPTION WHEN OTHERS THEN NULL; END;");
    } else {
        s_conn->execute("DROP TABLE IF EXISTS " + table);
    }
}

void QcIntegrationSelectTest::createSchema()
{
    switch (primaryTestDriver()) {
    case QcDbDriver::PostgreSQL:
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE qc_bt_departments ("
        "  id INTEGER PRIMARY KEY,"
        "  name VARCHAR(100) NOT NULL,"
        "  region VARCHAR(50) NOT NULL,"
        "  budget NUMERIC(14,2) NOT NULL"
        ")").has_value());
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE qc_bt_employees ("
        "  id INTEGER PRIMARY KEY,"
        "  department_id INTEGER NOT NULL REFERENCES qc_bt_departments(id),"
        "  manager_id INTEGER NULL REFERENCES qc_bt_employees(id),"
        "  full_name VARCHAR(200) NOT NULL,"
        "  email VARCHAR(200) NOT NULL,"
        "  nickname VARCHAR(100) NULL,"
        "  salary NUMERIC(12,2) NOT NULL,"
        "  hire_date DATE NOT NULL,"
        "  is_active INTEGER NOT NULL,"
        "  bio TEXT NOT NULL,"
        "  metadata JSONB NOT NULL"
        ")").has_value());
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE qc_bt_notes ("
        "  id INTEGER PRIMARY KEY,"
        "  employee_id INTEGER NOT NULL REFERENCES qc_bt_employees(id),"
        "  note VARCHAR(500) NOT NULL,"
        "  created_at TIMESTAMP NOT NULL"
        ")").has_value());
    break;
    case QcDbDriver::SQLite:
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE qc_bt_departments ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  region TEXT NOT NULL,"
        "  budget NUMERIC NOT NULL"
        ")").has_value());
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE qc_bt_employees ("
        "  id INTEGER PRIMARY KEY,"
        "  department_id INTEGER NOT NULL REFERENCES qc_bt_departments(id),"
        "  manager_id INTEGER NULL REFERENCES qc_bt_employees(id),"
        "  full_name TEXT NOT NULL,"
        "  email TEXT NOT NULL,"
        "  nickname TEXT NULL,"
        "  salary NUMERIC NOT NULL,"
        "  hire_date TEXT NOT NULL,"
        "  is_active INTEGER NOT NULL,"
        "  bio TEXT NOT NULL,"
        "  metadata TEXT NOT NULL CHECK (json_valid(metadata))"
        ")").has_value());
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE qc_bt_notes ("
        "  id INTEGER PRIMARY KEY,"
        "  employee_id INTEGER NOT NULL REFERENCES qc_bt_employees(id),"
        "  note TEXT NOT NULL,"
        "  created_at TEXT NOT NULL"
        ")").has_value());
    break;
    case QcDbDriver::Oracle:
    // NUMBER(1) for is_active (no native BOOLEAN in Oracle SQL, same
    // convention as SQLite's INTEGER 0/1 above); JSON is a native datatype
    // since 21c (verified directly against this XE instance). Every
    // table/column identifier is double-quoted, lowercase, matching exactly
    // what QcSqlDialect::quoteIdentifier()/quoteRef() now emit for this same
    // text -- unquoted DDL here would fold to UPPERCASE at creation time
    // (Oracle's default, unlike PostgreSQL/SQLite's fold-to-lowercase) and
    // the two would no longer refer to the same catalog object; see the doc
    // comment on quoteIdentifier() in qcsqldialect.h for the full story.
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE \"qc_bt_departments\" ("
        "  \"id\" NUMBER PRIMARY KEY,"
        "  \"name\" VARCHAR2(100) NOT NULL,"
        "  \"region\" VARCHAR2(50) NOT NULL,"
        "  \"budget\" NUMBER(14,2) NOT NULL"
        ")").has_value());
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE \"qc_bt_employees\" ("
        "  \"id\" NUMBER PRIMARY KEY,"
        "  \"department_id\" NUMBER NOT NULL REFERENCES \"qc_bt_departments\"(\"id\"),"
        "  \"manager_id\" NUMBER NULL REFERENCES \"qc_bt_employees\"(\"id\"),"
        "  \"full_name\" VARCHAR2(200) NOT NULL,"
        "  \"email\" VARCHAR2(200) NOT NULL,"
        "  \"nickname\" VARCHAR2(100) NULL,"
        "  \"salary\" NUMBER(12,2) NOT NULL,"
        "  \"hire_date\" DATE NOT NULL,"
        "  \"is_active\" NUMBER(1) NOT NULL,"
        "  \"bio\" VARCHAR2(4000) NOT NULL,"
        "  \"metadata\" JSON NOT NULL"
        ")").has_value());
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE \"qc_bt_notes\" ("
        "  \"id\" NUMBER PRIMARY KEY,"
        "  \"employee_id\" NUMBER NOT NULL REFERENCES \"qc_bt_employees\"(\"id\"),"
        "  \"note\" VARCHAR2(500) NOT NULL,"
        "  \"created_at\" TIMESTAMP NOT NULL"
        ")").has_value());
    break;
    case QcDbDriver::MSSQL:
    // NVARCHAR (not VARCHAR) for every text column -- this suite's own seed
    // data includes non-ASCII text (José Ñandú García, a 🚀 emoji in a bio),
    // and plain VARCHAR's codepage-dependent narrow encoding can't represent
    // that (see the UTF-16 round-tripping comment in qcnativeconnection.cpp);
    // INTEGER 0/1 for is_active, same convention as SQLite/Oracle above (no
    // native BOOLEAN in T-SQL either, and BIT buys nothing extra here); JSON
    // has no native datatype, so metadata gets an explicit ISJSON() CHECK for
    // the same validate-on-write guarantee PostgreSQL's JSONB gives natively.
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE qc_bt_departments ("
        "  id INT PRIMARY KEY,"
        "  name NVARCHAR(100) NOT NULL,"
        "  region NVARCHAR(50) NOT NULL,"
        "  budget DECIMAL(14,2) NOT NULL"
        ")").has_value());
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE qc_bt_employees ("
        "  id INT PRIMARY KEY,"
        "  department_id INT NOT NULL REFERENCES qc_bt_departments(id),"
        "  manager_id INT NULL REFERENCES qc_bt_employees(id),"
        "  full_name NVARCHAR(200) NOT NULL,"
        "  email NVARCHAR(200) NOT NULL,"
        "  nickname NVARCHAR(100) NULL,"
        "  salary DECIMAL(12,2) NOT NULL,"
        "  hire_date DATE NOT NULL,"
        "  is_active INT NOT NULL,"
        "  bio NVARCHAR(MAX) NOT NULL,"
        "  metadata NVARCHAR(MAX) NOT NULL CHECK (ISJSON(metadata) = 1)"
        ")").has_value());
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE qc_bt_notes ("
        "  id INT PRIMARY KEY,"
        "  employee_id INT NOT NULL REFERENCES qc_bt_employees(id),"
        "  note NVARCHAR(500) NOT NULL,"
        "  created_at DATETIME2 NOT NULL"
        ")").has_value());
    break;
    case QcDbDriver::MySQL:
    // Unlike every other driver here, MySQL/InnoDB silently ignores an
    // inline column-level "REFERENCES other(id)" -- it parses but creates no
    // actual constraint (verified directly: SHOW CREATE TABLE shows no FK at
    // all, and an insert with a bogus parent id goes through uncomplaining).
    // A real, enforced foreign key needs the explicit table-level
    // "FOREIGN KEY (col) REFERENCES other(col)" form used below instead. No
    // native BOOLEAN (BOOL is just an alias for TINYINT(1)) -- INT 0/1 for
    // is_active, same convention as SQLite/Oracle/MSSQL above. JSON is a
    // native datatype (5.7.8+) and validates on write by itself, same as
    // Oracle's native JSON column above -- no separate CHECK needed. utf8mb4
    // is the database's own default charset (see tests/db_config.ini), so
    // every VARCHAR/TEXT column here already stores full Unicode without an
    // explicit per-column CHARACTER SET clause.
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE qc_bt_departments ("
        "  id INT PRIMARY KEY,"
        "  name VARCHAR(100) NOT NULL,"
        "  region VARCHAR(50) NOT NULL,"
        "  budget DECIMAL(14,2) NOT NULL"
        ")").has_value());
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE qc_bt_employees ("
        "  id INT PRIMARY KEY,"
        "  department_id INT NOT NULL,"
        "  manager_id INT NULL,"
        "  full_name VARCHAR(200) NOT NULL,"
        "  email VARCHAR(200) NOT NULL,"
        "  nickname VARCHAR(100) NULL,"
        "  salary DECIMAL(12,2) NOT NULL,"
        "  hire_date DATE NOT NULL,"
        "  is_active INT NOT NULL,"
        "  bio TEXT NOT NULL,"
        "  metadata JSON NOT NULL,"
        "  FOREIGN KEY (department_id) REFERENCES qc_bt_departments(id),"
        "  FOREIGN KEY (manager_id) REFERENCES qc_bt_employees(id)"
        ")").has_value());
    ASSERT_TRUE(s_conn->execute(
        "CREATE TABLE qc_bt_notes ("
        "  id INT PRIMARY KEY,"
        "  employee_id INT NOT NULL,"
        "  note VARCHAR(500) NOT NULL,"
        "  created_at DATETIME NOT NULL,"
        "  FOREIGN KEY (employee_id) REFERENCES qc_bt_employees(id)"
        ")").has_value());
    break;
    }
}

void QcIntegrationSelectTest::buildSeedData()
{
    // ---- departments: fixed, hand-crafted -- id 7 ("Legal") deliberately
    // has zero employees, for RIGHT/FULL JOIN unmatched-right-side tests. ----
    s_departments = {
        {1, "Engineering", "EU", 2500000.00},
        {2, "Sales", "NA", 1800000.00},
        {3, "Marketing", "NA", 900000.00},
        {4, "Support", "APAC", 650000.00},
        {5, "Finance", "EU", 1200000.00},
        {6, "Research", "APAC", 3000000.00},
        {7, "Legal", "EU", 400000.00},
    };

    // ---- employees 1-10: hand-crafted "anchor" rows with exact known
    // values, used by tests that need a precise expected value (CAST,
    // CONCAT, ROUND, JSON field extraction, self-join, special characters).
    s_employees = {
        {1, 1, std::nullopt, "Alice Johnson", "alice.johnson@example.com", std::string("AJ"),
         145000.00, "2015-03-10", true,
         "Engineering lead with a focus on distributed systems and reliability.",
         5, false, {"c++", "postgres", "distributed-systems"}, 4.8},
        {2, 1, 1LL, "Bob Martinez", "bob.martinez@example.com", std::nullopt,
         118000.00, "2018-06-01", true,
         "Backend engineer working on the query builder core.",
         3, true, {"c++", "sql"}, 4.2},
        {3, 1, 1LL, "Carol O'Brien", "carol.obrien@example.com", std::string("Caro"),
         121500.50, "2019-09-23", true,
         "Focused on connection pooling and driver internals; keeps a tidy desk.",
         3, false, {"c++", "postgresql", "sqlite"}, 4.5},
        {4, 2, std::nullopt, "Dmitri Volkov", "dmitri.volkov@example.com", std::string("Dima"),
         98000.00, "2016-01-15", true,
         "Sales director for the northern region accounts.",
         4, false, {"negotiation", "crm"}, 4.0},
        {5, 2, 4LL, "José Ñandú García", "jose.garcia@example.com", std::nullopt,
         76000.00, "2020-11-02", true,
         "Account executive covering LATAM expansion. \xF0\x9F\x9A\x80",
         2, true, {"sales", "spanish"}, 3.9},
        {6, 3, std::nullopt, "Emma Chen", "emma.chen@example.com", std::string("Em"),
         89000.00, "2017-04-18", false,
         "On extended leave; former head of brand marketing.",
         4, false, {"branding", "seo"}, 4.1},
        {7, 4, std::nullopt, "Frank Ibrahim", "frank.ibrahim@example.com", std::nullopt,
         61000.00, "2021-02-08", true,
         "Tier 2 support specialist for APAC customers.",
         2, true, {"support", "zendesk"}, 3.7},
        {8, 5, std::nullopt, "Grace Kim", "grace.kim@example.com", std::string("Gigi"),
         132000.00, "2014-07-30", true,
         "Finance controller overseeing EU-region budgets.",
         5, false, {"accounting", "budgeting"}, 4.6},
        {9, 6, std::nullopt, "Hiro Tanaka", "hiro.tanaka@example.com", std::nullopt,
         142999.60, "2013-05-05", true,
         "Principal researcher, published extensively on query optimization.",
         5, false, {"research", "optimization"}, 4.9},
        {10, 6, 9LL, "Ivy Novak", "ivy.novak@example.com", std::string("Iv"),
         101250.25, "2022-08-12", true,
         "Research associate specializing in indexing strategies.",
         3, true, {"indexing", "benchmarking"}, 4.3},
    };

    // ---- employees 11-150: procedurally generated filler, deterministic
    // (fixed RNG seed) so failures reproduce exactly. ----
    static const char * const firstNames[] = {
        "James", "Mary", "Robert", "Patricia", "John", "Jennifer", "Michael", "Linda",
        "William", "Elizabeth", "David", "Barbara", "Richard", "Susan", "Joseph", "Jessica",
        "Thomas", "Sarah", "Charles", "Karen", "Daniel", "Nancy", "Matthew", "Lisa",
        "Anthony", "Betty", "Mark", "Margaret", "Paul", "Sandra",
    };
    static const char * const lastNames[] = {
        "Smith", "Johnson", "Williams", "Brown", "Jones", "Garcia", "Miller", "Davis",
        "Rodriguez", "Martinez", "Hernandez", "Lopez", "Wilson", "Anderson", "Thomas", "Taylor",
        "Moore", "Jackson", "Martin", "Lee", "Perez", "Thompson", "White", "Harris",
        "Sanchez", "Clark", "Ramirez", "Lewis", "Robinson", "Walker",
    };
    static const char * const skillPool[] = {
        "c++", "sql", "python", "leadership", "sales", "marketing", "support",
        "accounting", "research", "design", "testing", "devops", "security", "analytics",
    };
    static const char * const bioTemplates[] = {
        "Works closely with cross-functional teams to deliver results.",
        "Enjoys mentoring newer colleagues and improving internal tooling.",
        "Focused on process improvements and customer satisfaction.",
        "Has a strong track record of hitting quarterly goals.",
        "Recently completed an internal certification program.",
        "Known for clear documentation and thorough reviews.",
    };

    std::mt19937_64 rng(20260807ULL);
    std::uniform_int_distribution<int> deptDist(1, 6); // department 7 stays empty on purpose
    std::uniform_int_distribution<std::size_t> firstNameDist(0, std::size(firstNames) - 1);
    std::uniform_int_distribution<std::size_t> lastNameDist(0, std::size(lastNames) - 1);
    std::uniform_int_distribution<std::size_t> bioDist(0, std::size(bioTemplates) - 1);
    std::uniform_int_distribution<std::size_t> skillDist(0, std::size(skillPool) - 1);
    std::uniform_real_distribution<double> salaryDist(48000.0, 155000.0);
    std::uniform_real_distribution<double> ratingDist(3.0, 5.0);
    std::bernoulli_distribution activeDist(0.85);
    std::bernoulli_distribution remoteDist(0.4);
    std::bernoulli_distribution hasNicknameDist(0.35);
    std::bernoulli_distribution hasManagerDist(0.5);
    std::uniform_int_distribution<int> levelDist(1, 5);
    std::uniform_int_distribution<int> skillCountDist(2, 3);
    std::uniform_int_distribution<int> yearDist(2012, 2025);
    std::uniform_int_distribution<int> monthDist(1, 12);
    std::uniform_int_distribution<int> dayDist(1, 28);

    std::map<long long, std::vector<long long>> idsByDepartment;
    for (const SeedEmployee & e : s_employees) {
        idsByDepartment[e.departmentId].push_back(e.id);
    }

    constexpr long long fillerStart = 11;
    constexpr long long fillerCount = 140;
    for (long long id = fillerStart; id < fillerStart + fillerCount; ++id) {
        const long long deptId = deptDist(rng);
        std::vector<long long> & sameDept = idsByDepartment[deptId];

        std::optional<long long> managerId;
        if (hasManagerDist(rng) && !sameDept.empty()) {
            std::uniform_int_distribution<std::size_t> pickIdx(0, sameDept.size() - 1);
            managerId = sameDept[pickIdx(rng)];
        }

        const std::string first = firstNames[firstNameDist(rng)];
        const std::string last = lastNames[lastNameDist(rng)];
        const std::string fullName = first + " " + last;
        std::string email = first + "." + last + std::to_string(id) + "@example.com";
        std::transform(email.begin(), email.end(), email.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        std::optional<std::string> nickname;
        if (hasNicknameDist(rng)) {
            nickname = first.substr(0, 1) + last.substr(0, 1) + std::to_string(id);
        }

        std::ostringstream dateOut;
        dateOut << yearDist(rng) << '-' << std::setw(2) << std::setfill('0') << monthDist(rng)
                << '-' << std::setw(2) << std::setfill('0') << dayDist(rng);

        const int skillCount = skillCountDist(rng);
        std::vector<std::string> skills;
        for (int i = 0; i < skillCount; ++i) {
            skills.push_back(skillPool[skillDist(rng)]);
        }

        s_employees.push_back(SeedEmployee{
            id, deptId, managerId, fullName, email, nickname,
            std::round(salaryDist(rng) * 100.0) / 100.0, dateOut.str(), activeDist(rng),
            bioTemplates[bioDist(rng)], levelDist(rng), remoteDist(rng), skills,
            std::round(ratingDist(rng) * 10.0) / 10.0,
        });

        idsByDepartment[deptId].push_back(id);
    }

    // ---- notes: 2 hand-crafted notes per anchor employee, 0-3 random notes
    // per filler employee. ----
    static const char * const anchorNoteTexts[] = {
        "Completed onboarding checklist ahead of schedule.",
        "Received positive feedback from the last quarterly review.",
    };
    long long noteId = 1;
    for (long long empId = 1; empId <= 10; ++empId) {
        const SeedEmployee & emp = employee(empId);
        for (const char * text : anchorNoteTexts) {
            s_notes.push_back(SeedNote{noteId++, empId, text, emp.hireDate + " 09:00:00"});
        }
    }

    std::uniform_int_distribution<int> noteCountDist(0, 3);
    std::uniform_int_distribution<std::size_t> noteTextDist(0, std::size(bioTemplates) - 1);
    std::uniform_int_distribution<int> hourDist(8, 18);
    for (long long empId = fillerStart; empId < fillerStart + fillerCount; ++empId) {
        const SeedEmployee & emp = employee(empId);
        const int count = noteCountDist(rng);
        for (int i = 0; i < count; ++i) {
            std::ostringstream ts;
            ts << emp.hireDate << ' ' << std::setw(2) << std::setfill('0') << hourDist(rng) << ":00:00";
            s_notes.push_back(SeedNote{noteId++, empId, bioTemplates[noteTextDist(rng)], ts.str()});
        }
    }
}

void QcIntegrationSelectTest::insertSeedData()
{
    ASSERT_TRUE(s_conn->execute("BEGIN").has_value());

    const std::string insertDeptSql =
        "INSERT INTO " + qi("qc_bt_departments") + " (" + qi("id") + ", " + qi("name") + ", " + qi("region") + ", " + qi("budget")
        + ") VALUES (" + placeholderList(4) + ")";
    for (const SeedDepartment & d : s_departments) {
        ASSERT_TRUE(s_conn->execute(insertDeptSql, {d.id, d.name, d.region, d.budget}).has_value());
    }

    const std::string insertEmpSql =
        "INSERT INTO " + qi("qc_bt_employees") + " (" + qi("id") + ", " + qi("department_id") + ", " + qi("manager_id") + ", "
        + qi("full_name") + ", " + qi("email") + ", " + qi("nickname") + ", " + qi("salary") + ", "
        + qi("hire_date") + ", " + qi("is_active") + ", " + qi("bio") + ", " + qi("metadata")
        + ") VALUES (" + placeholderList(11) + ")";
    for (const SeedEmployee & e : s_employees) {
        QcSqlBase::QcVariantList params;
        params.push_back(e.id);
        params.push_back(e.departmentId);
        params.push_back(e.managerId ? QcSqlBase::QcVariant(*e.managerId) : QcSqlBase::QcVariant{});
        params.push_back(e.fullName);
        params.push_back(e.email);
        params.push_back(e.nickname ? QcSqlBase::QcVariant(*e.nickname) : QcSqlBase::QcVariant{});
        params.push_back(e.salary);
        params.push_back(e.hireDate);
        params.push_back(static_cast<long long>(e.isActive ? 1 : 0));
        params.push_back(e.bio);
        params.push_back(metadataJson(e.metaLevel, e.metaRemote, e.metaSkills, e.metaRating));
        ASSERT_TRUE(s_conn->execute(insertEmpSql, params).has_value());
    }

    const std::string insertNoteSql =
        "INSERT INTO " + qi("qc_bt_notes") + " (" + qi("id") + ", " + qi("employee_id") + ", " + qi("note") + ", " + qi("created_at")
        + ") VALUES (" + placeholderList(4) + ")";
    for (const SeedNote & n : s_notes) {
        ASSERT_TRUE(s_conn->execute(insertNoteSql, {n.id, n.employeeId, n.note, n.createdAt}).has_value());
    }

    ASSERT_TRUE(s_conn->execute("COMMIT").has_value());
}

const SeedEmployee & QcIntegrationSelectTest::employee(long long id)
{
    auto it = std::find_if(s_employees.begin(), s_employees.end(), [id](const SeedEmployee & e) { return e.id == id; });
    return *it;
}

const SeedDepartment & QcIntegrationSelectTest::departmentById(long long id)
{
    auto it = std::find_if(s_departments.begin(), s_departments.end(), [id](const SeedDepartment & d) { return d.id == id; });
    return *it;
}

std::vector<SeedEmployee> QcIntegrationSelectTest::employeesIn(long long departmentId)
{
    std::vector<SeedEmployee> result;
    std::copy_if(s_employees.begin(), s_employees.end(), std::back_inserter(result),
                 [departmentId](const SeedEmployee & e) { return e.departmentId == departmentId; });
    return result;
}

std::optional<QcResultSet> QcIntegrationSelectTest::run(const QcSqlQuery & query)
{
    // primaryTestDriver(), not toSql()'s PostgreSQL default -- s_conn was
    // opened against primaryTestDriver() (see testDbConfigOrDefault() in
    // SetUpTestSuite()), and every driver but that one renders incompatible
    // SQL (wrong placeholder syntax, wrong quoting, ...).
    const QcSqlStatement statement = query.toSql(primaryTestDriver());
    return s_conn->execute(statement.sql, statement.params);
}

std::optional<QcNamedResultSet> QcIntegrationSelectTest::runNamed(const QcSqlQuery & query)
{
    const QcSqlStatement statement = query.toSql(primaryTestDriver());
    return s_conn->executeNamed(statement.sql, statement.params);
}

// =====================================================================
// Basic projection
// =====================================================================

TEST_F(QcIntegrationSelectTest, SelectAllColumnsFromDepartmentsMatchesSeedCount)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_departments");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), s_departments.size());
}

TEST_F(QcIntegrationSelectTest, SelectSpecificColumnsWithAliasReturnsOnlyRequestedColumns)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id <emp_id>", "full_name <name>"});
    query.where("id").isEqualTo(1LL);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    ASSERT_EQ((*result)[0].size(), 2u);
    EXPECT_EQ(asInt64((*result)[0][0]), 1);
    EXPECT_EQ(std::get<std::string>((*result)[0][1]), "Alice Johnson");
}

// =====================================================================
// Named result rows -- QcNativeConnection::executeNamed()
// =====================================================================

TEST_F(QcIntegrationSelectTest, NamedSelectKeysRowsByPlainColumnNameWhenNoAliasGiven)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id", "full_name"});
    query.where("id").isEqualTo(1LL);

    auto result = runNamed(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    const QcNamedRow & row = (*result)[0];
    ASSERT_EQ(row.size(), 2u);
    EXPECT_EQ(asInt64(row.at("id")), 1);
    EXPECT_EQ(std::get<std::string>(row.at("full_name")), "Alice Johnson");
}

TEST_F(QcIntegrationSelectTest, NamedSelectKeysRowsByAliasWhenOneIsGiven)
{
    // Mirrors SelectSpecificColumnsWithAliasReturnsOnlyRequestedColumns above
    // -- same query, but the named row's keys must be the aliases
    // ("emp_id"/"name"), not the underlying column names ("id"/"full_name"),
    // proving the column name comes from driver metadata on the *rendered*
    // SQL (which already carries the alias, see QcSqlDialect), not just
    // echoed back from the caller's own addReturnValues() argument text.
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id <emp_id>", "full_name <name>"});
    query.where("id").isEqualTo(1LL);

    auto result = runNamed(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    const QcNamedRow & row = (*result)[0];
    ASSERT_EQ(row.size(), 2u);
    EXPECT_EQ(asInt64(row.at("emp_id")), 1);
    EXPECT_EQ(std::get<std::string>(row.at("name")), "Alice Johnson");
    EXPECT_EQ(row.count("id"), 0u);
    EXPECT_EQ(row.count("full_name"), 0u);
}

TEST_F(QcIntegrationSelectTest, NamedSelectPreservesNullAsMonostate)
{
    // employee 2 (Bob Martinez) has no nickname -- see buildSeedData().
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id", "nickname"});
    query.where("id").isEqualTo(2LL);

    auto result = runNamed(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_TRUE(std::holds_alternative<std::monostate>((*result)[0].at("nickname")));
}

TEST_F(QcIntegrationSelectTest, NamedSelectRowCountAndValuesMatchThePositionalEquivalent)
{
    // Cross-checks executeNamed() against execute() over the full
    // departments table rather than one hand-picked row -- same rows, same
    // values, just addressed by name instead of position.
    QcSqlQuery positionalQuery;
    positionalQuery.fromTable("qc_bt_departments");
    positionalQuery.addReturnValues({"id", "name", "region"});
    positionalQuery.orderAsc({"id"});
    auto positional = run(positionalQuery);
    ASSERT_TRUE(positional.has_value());

    QcSqlQuery namedQuery;
    namedQuery.fromTable("qc_bt_departments");
    namedQuery.addReturnValues({"id", "name", "region"});
    namedQuery.orderAsc({"id"});
    auto named = runNamed(namedQuery);
    ASSERT_TRUE(named.has_value());

    ASSERT_EQ(named->size(), positional->size());
    for (std::size_t i = 0; i < positional->size(); ++i) {
        EXPECT_EQ(asInt64((*named)[i].at("id")), asInt64((*positional)[i][0]));
        EXPECT_EQ(std::get<std::string>((*named)[i].at("name")), std::get<std::string>((*positional)[i][1]));
        EXPECT_EQ(std::get<std::string>((*named)[i].at("region")), std::get<std::string>((*positional)[i][2]));
    }
}

TEST_F(QcIntegrationSelectTest, NamedSelectWithDuplicateColumnNameKeepsOnlyTheLastOne)
{
    // Documented, deliberate limitation (see QcNamedRow's doc comment in
    // qcnativeconnection.h): a self-join selecting "id" from both sides
    // without aliasing either one is genuinely ambiguous by column name --
    // std::map keeps only the last value written, silently, rather than
    // erroring. Here that's employee 2's own id (the second "id" in the
    // SELECT list), not employee 2's manager's id (the first).
    QcSqlQuery managerRef;
    managerRef.fromTable("qc_bt_employees");

    QcSqlQuery query;
    query.fromTable("qc_bt_employees e");
    query.addLeftJoin("m", managerRef, qi("m") + "." + qi("id") + " = " + qi("e") + "." + qi("manager_id"));
    query.addReturnValues({"m.id", "e.id"}); // both columns are named plain "id"
    query.where("e.id").isEqualTo(2LL); // Bob Martinez, manager_id = 1 (Alice Johnson)

    auto result = runNamed(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    const QcNamedRow & row = (*result)[0];
    ASSERT_EQ(row.size(), 1u); // one surviving key, not two
    EXPECT_EQ(asInt64(row.at("id")), 2); // e.id (last), not m.id (1, discarded)
}

TEST_F(QcIntegrationSelectTest, NamedSelectOnUnaliasedExpressionColumnKeyIsDriverDependent)
{
    // addReturnValue()/addReturnValues() accept raw SQL text (the escape
    // hatch this builder gives for expressions it has no dedicated method
    // for, e.g. "COUNT(*) <cnt>") and the "<alias>" suffix is optional --
    // nothing in the builder enforces one. When it's omitted, the key this
    // column lands under in a named row is whatever *the driver itself*
    // names an unaliased expression column, which is a real, live-verified,
    // per-driver-different answer, not something this project's code
    // computes or controls:
    //   - PostgreSQL derives a short name from the outer function/keyword
    //     ("count" for COUNT(*)) -- NOT the full expression text.
    //   - SQLite/MySQL report the full expression text verbatim
    //     ("COUNT(*)").
    //   - Oracle reports the expression text too (already uppercase here,
    //     since this builder emits "COUNT(*)" uppercase itself).
    //   - MSSQL's SQLDescribeCol reports an EMPTY column name ("") for
    //     *any* unaliased expression -- confirmed directly via isql against
    //     a live SQL Server, not assumed.
    // None of this crashes or corrupts anything -- executeNamed() still
    // returns a well-formed QcNamedResultSet, an empty string is a
    // perfectly legal std::map key -- but the key is unpredictable across
    // drivers for any unaliased expression, and on MSSQL
    // specifically, two or more unaliased expressions in the same SELECT
    // list would collide on the same "" key under the documented
    // duplicate-name rule (see NamedSelectWithDuplicateColumnNameKeepsOnlyTheLastOne
    // above) -- not just a self-join edge case, but any two raw expressions
    // without aliases. Always alias raw/free-text return values
    // ("COUNT(*) <cnt>") when the result will be read through
    // executeNamed()/executeReturningNamed().
    QcSqlQuery query;
    query.fromTable("qc_bt_departments");
    query.addReturnValue("*").count(); // deliberately no "<alias>"

    auto result = runNamed(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    const QcNamedRow & row = (*result)[0];
    ASSERT_EQ(row.size(), 1u);
    switch (primaryTestDriver()) {
        case QcDbDriver::PostgreSQL:
            EXPECT_EQ(asInt64(row.at("count")), static_cast<long long>(s_departments.size()));
            break;
        case QcDbDriver::SQLite:
        case QcDbDriver::MySQL:
        case QcDbDriver::Oracle:
            EXPECT_EQ(asInt64(row.at("COUNT(*)")), static_cast<long long>(s_departments.size()));
            break;
        case QcDbDriver::MSSQL:
            EXPECT_EQ(asInt64(row.at("")), static_cast<long long>(s_departments.size()));
            break;
    }
}

// =====================================================================
// WHERE - value comparators
// =====================================================================

TEST_F(QcIntegrationSelectTest, WhereEqualToFiltersToMatchingRows)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("department_id").isEqualTo(3LL);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.departmentId == 3) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

TEST_F(QcIntegrationSelectTest, WhereNotEqualToExcludesMatchingRows)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("department_id").isNotEqualTo(3LL);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::size_t expectedCount = 0;
    for (const SeedEmployee & e : s_employees) {
        if (e.departmentId != 3) ++expectedCount;
    }
    EXPECT_EQ(result->size(), expectedCount);
}

TEST_F(QcIntegrationSelectTest, WhereGreaterThanAndLessThanBoundSalaryRange)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("salary").isGreaterThan(70000.0);
    query.and_("salary").isLessThan(130000.0);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.salary > 70000.0 && e.salary < 130000.0) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

TEST_F(QcIntegrationSelectTest, WhereGreaterThanOrEqualToAndLessThanOrEqualToInclusiveBounds)
{
    // Bounds are exactly two anchor employees' salaries -- proves the
    // comparators are inclusive, not just "roughly the right range".
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("salary").isGreaterThanOrEqualTo(76000.0);
    query.and_("salary").isLessThanOrEqualTo(145000.0);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.salary >= 76000.0 && e.salary <= 145000.0) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_TRUE(expected.count(1)); // salary exactly 145000.00
    EXPECT_TRUE(expected.count(5)); // salary exactly 76000.00
}

TEST_F(QcIntegrationSelectTest, WhereLikeMatchesPattern)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("full_name").isLike("%Martinez%");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.fullName.find("Martinez") != std::string::npos) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_TRUE(expected.count(2)); // "Bob Martinez"
}

TEST_F(QcIntegrationSelectTest, WhereILikeIsCaseInsensitive)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("full_name").isIlike("%JOHNSON%");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        std::string lower = e.fullName;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.find("johnson") != std::string::npos) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_TRUE(expected.count(1)); // "Alice Johnson"
}

TEST_F(QcIntegrationSelectTest, WhereIsNullFindsMissingNicknames)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("nickname").isNull();

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (!e.nickname) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

TEST_F(QcIntegrationSelectTest, WhereIsNotNullExcludesMissingNicknames)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("nickname").isNotNull();

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::size_t expectedCount = 0;
    for (const SeedEmployee & e : s_employees) {
        if (e.nickname) ++expectedCount;
    }
    EXPECT_EQ(result->size(), expectedCount);
}

TEST_F(QcIntegrationSelectTest, WhereInWithExplicitValueList)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("id").isIn(QcSqlBase::QcVariantList{1LL, 4LL, 9LL});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, (std::set<long long>{1, 4, 9}));
}

TEST_F(QcIntegrationSelectTest, WhereInWithEmptyListMatchesNoRows)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("id").isIn(QcSqlBase::QcVariantList{});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST_F(QcIntegrationSelectTest, WhereNotInWithEmptyListMatchesAllRows)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("id").isNotIn(QcSqlBase::QcVariantList{});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), s_employees.size());
}

TEST_F(QcIntegrationSelectTest, WhereBetweenSalaryRangeMatchesInclusiveBounds)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("salary").isBetween(76000.0, 145000.0);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.salary >= 76000.0 && e.salary <= 145000.0) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

TEST_F(QcIntegrationSelectTest, WhereNotBetweenExcludesRange)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("salary").isNotBetween(76000.0, 145000.0);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (!(e.salary >= 76000.0 && e.salary <= 145000.0)) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

TEST_F(QcIntegrationSelectTest, WhereIsDistinctFromTreatsNullAsDistinctFromValue)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("nickname").isDistinctFrom(std::string("Iv"));

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    // Every row whose nickname isn't exactly "Iv" -- including NULLs, which
    // plain `!=` would have silently dropped.
    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (!e.nickname || *e.nickname != "Iv") expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_TRUE(expected.count(2)); // nickname is NULL
    EXPECT_FALSE(expected.count(10)); // nickname is exactly "Iv"
}

TEST_F(QcIntegrationSelectTest, WhereIsNotDistinctFromTreatsNullAsNotDistinctFromNull)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("nickname").isNotDistinctFrom(QcSqlBase::QcVariant{});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (!e.nickname) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

// =====================================================================
// WHERE - logical combinations
// =====================================================================

TEST_F(QcIntegrationSelectTest, AndChainNarrowsResultsProgressively)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("department_id").isEqualTo(1LL);
    query.and_("is_active").isEqualTo(1LL);
    query.and_("salary").isGreaterThan(100000.0);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.departmentId == 1 && e.isActive && e.salary > 100000.0) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

TEST_F(QcIntegrationSelectTest, OrChainWidensResults)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("department_id").isEqualTo(3LL);
    query.or_("department_id").isEqualTo(4LL);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.departmentId == 3 || e.departmentId == 4) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

TEST_F(QcIntegrationSelectTest, ParenthesizedOrWithinAndIsolatesGrouping)
{
    // department_id = 1 AND (salary > 140000 OR nickname IS NULL)
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("department_id").isEqualTo(1LL);
    query.and_OpenParenthesis("salary").isGreaterThan(140000.0);
    query.or_("nickname").isNull();
    query.closeParenthesis();

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.departmentId == 1 && (e.salary > 140000.0 || !e.nickname)) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

TEST_F(QcIntegrationSelectTest, DeeplyNestedParenthesesGroupCorrectly)
{
    // (department_id = 1 OR department_id = 2)
    // AND (salary > 100000 OR (is_active = 0 AND nickname IS NULL))
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where_OpenParenthesis("department_id").isEqualTo(1LL);
    query.or_("department_id").isEqualTo(2LL);
    query.closeParenthesis();
    query.and_OpenParenthesis("salary").isGreaterThan(100000.0);
    query.or_OpenParenthesis("is_active").isEqualTo(0LL);
    query.and_("nickname").isNull();
    query.closeParenthesis();
    query.closeParenthesis();

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        const bool deptMatch = (e.departmentId == 1 || e.departmentId == 2);
        const bool salaryOrIdleMatch = (e.salary > 100000.0) || (!e.isActive && !e.nickname);
        if (deptMatch && salaryOrIdleMatch) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

// =====================================================================
// Subqueries / EXISTS
// =====================================================================

TEST_F(QcIntegrationSelectTest, WhereInWithSubqueryMatchesDepartmentFilter)
{
    QcSqlQuery inner;
    inner.fromTable("qc_bt_departments");
    inner.addReturnValues({"id"});
    inner.where("region").isEqualTo(std::string("EU"));

    QcSqlQuery outer;
    outer.fromTable("qc_bt_employees");
    outer.addReturnValues({"id"});
    outer.where("department_id").isIn(inner);

    auto result = run(outer);
    ASSERT_TRUE(result.has_value());

    std::set<long long> euDepartments;
    for (const SeedDepartment & d : s_departments) {
        if (d.region == "EU") euDepartments.insert(d.id);
    }
    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (euDepartments.count(e.departmentId)) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

TEST_F(QcIntegrationSelectTest, WhereExistsCorrelatedSubqueryFindsDepartmentsWithHighEarners)
{
    QcSqlQuery inner;
    inner.fromTable("qc_bt_employees");
    inner.addFreeText(qi("department_id") + " = " + qi("qc_bt_departments") + "." + qi("id"), {});
    inner.where("salary").isGreaterThan(140000.0);

    QcSqlQuery outer;
    outer.fromTable("qc_bt_departments");
    outer.addReturnValues({"id"});
    outer.where("").exists(inner);

    auto result = run(outer);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedDepartment & d : s_departments) {
        const std::vector<SeedEmployee> members = employeesIn(d.id);
        if (std::any_of(members.begin(), members.end(), [](const SeedEmployee & e) { return e.salary > 140000.0; })) {
            expected.insert(d.id);
        }
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereNotExistsCorrelatedSubqueryFindsDepartmentsWithoutHighEarners)
{
    QcSqlQuery inner;
    inner.fromTable("qc_bt_employees");
    inner.addFreeText(qi("department_id") + " = " + qi("qc_bt_departments") + "." + qi("id"), {});
    inner.where("salary").isGreaterThan(140000.0);

    QcSqlQuery outer;
    outer.fromTable("qc_bt_departments");
    outer.addReturnValues({"id"});
    outer.where("").notExists(inner);

    auto result = run(outer);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedDepartment & d : s_departments) {
        const std::vector<SeedEmployee> members = employeesIn(d.id);
        if (!std::any_of(members.begin(), members.end(), [](const SeedEmployee & e) { return e.salary > 140000.0; })) {
            expected.insert(d.id);
        }
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_TRUE(expected.count(7)); // "Legal" has no employees at all
}

TEST_F(QcIntegrationSelectTest, FromSubQueryWrapsAggregateSelect)
{
    QcSqlQuery agg;
    agg.fromTable("qc_bt_employees");
    agg.addReturnValue("department_id");
    agg.addReturnValue("salary <avg_salary>").avg();
    agg.groupBy({"department_id"});

    QcSqlQuery outer;
    outer.fromSubQuery("dept_stats", agg);
    outer.addReturnValues({"department_id", "avg_salary"});
    outer.where("avg_salary").isGreaterThan(90000.0);
    outer.orderAsc({"department_id"});

    auto result = run(outer);
    ASSERT_TRUE(result.has_value());

    std::map<long long, double> expectedAverages;
    std::map<long long, int> counts;
    for (const SeedEmployee & e : s_employees) {
        expectedAverages[e.departmentId] += e.salary;
        counts[e.departmentId] += 1;
    }
    std::set<long long> expectedDepartments;
    for (auto & [deptId, total] : expectedAverages) {
        const double avg = total / counts[deptId];
        if (avg > 90000.0) expectedDepartments.insert(deptId);
    }

    std::set<long long> actualDepartments;
    for (auto & row : *result) {
        const long long deptId = asInt64(row[0]);
        actualDepartments.insert(deptId);
        const double avg = expectedAverages[deptId] / counts[deptId];
        EXPECT_NEAR(asDouble(row[1]), avg, 0.5);
    }
    EXPECT_EQ(actualDepartments, expectedDepartments);
}

// =====================================================================
// Joins
// =====================================================================

TEST_F(QcIntegrationSelectTest, InnerJoinAttachesDepartmentToEmployee)
{
    QcSqlQuery deptRef;
    deptRef.fromTable("qc_bt_departments");

    QcSqlQuery query;
    query.fromTable("qc_bt_employees e");
    query.addJoin("d", deptRef, qi("d") + "." + qi("id") + " = " + qi("e") + "." + qi("department_id"));
    query.addReturnValues({"e.id <emp_id>", "d.name <dept_name>"});
    query.where("e.id").isEqualTo(1LL);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0][0]), 1);
    EXPECT_EQ(std::get<std::string>((*result)[0][1]), "Engineering");
}

TEST_F(QcIntegrationSelectTest, LeftJoinKeepsEmployeesWithoutNotes)
{
    QcSqlQuery notesRef;
    notesRef.fromTable("qc_bt_notes");

    QcSqlQuery query;
    query.fromTable("qc_bt_employees e");
    query.addLeftJoin("n", notesRef, qi("n") + "." + qi("employee_id") + " = " + qi("e") + "." + qi("id"));
    query.addReturnValues({"e.id"});
    query.where("e.id").isGreaterThan(10LL); // filler range, includes employees with zero notes

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> notedEmployees;
    for (const SeedNote & n : s_notes) notedEmployees.insert(n.employeeId);

    std::size_t expectedRows = 0;
    for (const SeedEmployee & e : s_employees) {
        if (e.id <= 10) continue;
        const std::size_t noteCount = static_cast<std::size_t>(std::count_if(
            s_notes.begin(), s_notes.end(), [&](const SeedNote & n) { return n.employeeId == e.id; }));
        expectedRows += std::max<std::size_t>(noteCount, 1); // LEFT JOIN: at least one row even with zero notes
    }
    EXPECT_EQ(result->size(), expectedRows);

    std::set<long long> actualIds;
    for (auto & row : *result) actualIds.insert(asInt64(row[0]));
    std::set<long long> expectedIds;
    for (const SeedEmployee & e : s_employees) {
        if (e.id > 10) expectedIds.insert(e.id);
    }
    EXPECT_EQ(actualIds, expectedIds); // every filler employee shows up at least once, matched or not
}

TEST_F(QcIntegrationSelectTest, RightJoinKeepsDepartmentsWithoutEmployees)
{
    QcSqlQuery deptRef;
    deptRef.fromTable("qc_bt_departments");

    QcSqlQuery query;
    query.fromTable("qc_bt_employees e");
    query.addRightJoin("d", deptRef, qi("d") + "." + qi("id") + " = " + qi("e") + "." + qi("department_id"));
    query.addReturnValues({"d.id <dept_id>", "e.id <emp_id>"});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> departmentsSeen;
    bool legalHasNullEmployee = false;
    for (auto & row : *result) {
        const long long deptId = asInt64(row[0]);
        departmentsSeen.insert(deptId);
        if (deptId == 7) {
            EXPECT_TRUE(std::holds_alternative<std::monostate>(row[1]));
            legalHasNullEmployee = true;
        }
    }

    std::set<long long> expectedDepartments;
    for (const SeedDepartment & d : s_departments) expectedDepartments.insert(d.id);
    EXPECT_EQ(departmentsSeen, expectedDepartments); // every department appears, even "Legal" with zero employees
    EXPECT_TRUE(legalHasNullEmployee);
}

// MySQL has no FULL JOIN/FULL OUTER JOIN syntax at all (verified directly:
// ERROR 1064, "You have an error in your SQL syntax ... near 'FULL JOIN'")
// -- unlike the other dialect differences this project papers over
// (placeholders, CONCAT, LIMIT/OFFSET, RETURNING, ...), there is no
// same-shape rewrite: emulating it needs a structurally different query
// (LEFT JOIN UNION RIGHT JOIN, filtering out the side already covered by
// the LEFT JOIN half), which QcSqlQuery::addFullJoin() doesn't attempt --
// same "left to fail at execution time, no silent equivalent to degrade to"
// choice as WITH TIES on MSSQL/Oracle (see QcSqlDialect::limitOffsetClause()).
TEST_F(QcIntegrationSelectTest, FullJoinKeepsBothUnmatchedSides)
{
    if (primaryTestDriver() == QcDbDriver::MySQL) {
        GTEST_SKIP() << "MySQL has no FULL JOIN/FULL OUTER JOIN syntax";
    }
    QcSqlQuery leftEmployees;
    leftEmployees.fromTable("qc_bt_employees");
    leftEmployees.addReturnValues({"id", "department_id"});
    leftEmployees.where("department_id").isEqualTo(1LL); // only Engineering employees on the left

    QcSqlQuery rightDepartments;
    rightDepartments.fromTable("qc_bt_departments");
    rightDepartments.addReturnValues({"id"});
    rightDepartments.where("id").isIn(QcSqlBase::QcVariantList{2LL, 7LL}); // Sales + Legal on the right, neither is Engineering

    QcSqlQuery outer;
    outer.fromSubQuery("e", leftEmployees);
    outer.addFullJoin("d", rightDepartments, qi("d") + "." + qi("id") + " = " + qi("e") + "." + qi("department_id"), true);
    outer.addReturnValues({"e.id <emp_id>", "d.id <dept_id>"});

    auto result = run(outer);
    ASSERT_TRUE(result.has_value());

    bool sawUnmatchedLeft = false; // an Engineering employee with no matching right-side department
    bool sawUnmatchedRight = false; // Sales or Legal with no matching left-side employee
    for (auto & row : *result) {
        const bool empIsNull = std::holds_alternative<std::monostate>(row[0]);
        const bool deptIsNull = std::holds_alternative<std::monostate>(row[1]);
        EXPECT_FALSE(empIsNull && deptIsNull);
        if (!empIsNull && deptIsNull) sawUnmatchedLeft = true;
        if (empIsNull && !deptIsNull) sawUnmatchedRight = true;
    }
    EXPECT_TRUE(sawUnmatchedLeft);
    EXPECT_TRUE(sawUnmatchedRight);

    const std::size_t engineeringCount = employeesIn(1).size();
    EXPECT_EQ(result->size(), engineeringCount + 2); // every Engineering employee unmatched, plus Sales and Legal unmatched
}

TEST_F(QcIntegrationSelectTest, CrossJoinProducesFullCartesianProduct)
{
    QcSqlQuery deptsSubset;
    deptsSubset.fromTable("qc_bt_departments");
    deptsSubset.addReturnValues({"id"});
    deptsSubset.where("id").isIn(QcSqlBase::QcVariantList{1LL, 2LL});

    QcSqlQuery empsSubset;
    empsSubset.fromTable("qc_bt_employees");
    empsSubset.addReturnValues({"id"});
    empsSubset.where("id").isIn(QcSqlBase::QcVariantList{1LL, 4LL, 7LL});

    QcSqlQuery outer;
    outer.fromSubQuery("d", deptsSubset);
    outer.addCrossJoin("e", empsSubset, true);
    outer.addReturnValues({"d.id <dept_id>", "e.id <emp_id>"});

    auto result = run(outer);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 6u); // 2 departments x 3 employees

    std::set<std::pair<long long, long long>> actualPairs;
    for (auto & row : *result) {
        actualPairs.insert({asInt64(row[0]), asInt64(row[1])});
    }
    std::set<std::pair<long long, long long>> expectedPairs;
    for (long long d : {1LL, 2LL}) {
        for (long long e : {1LL, 4LL, 7LL}) {
            expectedPairs.insert({d, e});
        }
    }
    EXPECT_EQ(actualPairs, expectedPairs);
}

TEST_F(QcIntegrationSelectTest, SelfJoinResolvesManagerFullName)
{
    QcSqlQuery managerRef;
    managerRef.fromTable("qc_bt_employees");

    QcSqlQuery query;
    query.fromTable("qc_bt_employees e");
    query.addLeftJoin("m", managerRef, qi("m") + "." + qi("id") + " = " + qi("e") + "." + qi("manager_id"));
    query.addReturnValues({"e.id <emp_id>", "m.full_name <manager_name>"});
    query.where("e.id").isEqualTo(2LL); // Bob Martinez, manager = Alice Johnson

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*result)[0][1]), "Alice Johnson");
}

TEST_F(QcIntegrationSelectTest, JoinAsSubQueryAggregatesPerDepartment)
{
    QcSqlQuery deptAvg;
    deptAvg.fromTable("qc_bt_employees");
    deptAvg.addReturnValue("department_id");
    deptAvg.addReturnValue("salary <avg_salary>").avg();
    deptAvg.groupBy({"department_id"});

    QcSqlQuery query;
    query.fromTable("qc_bt_employees e");
    query.addJoin("da", deptAvg, qi("da") + "." + qi("department_id") + " = " + qi("e") + "." + qi("department_id"), /*asSubQuery=*/true);
    query.addReturnValues({"e.id <emp_id>", "da.avg_salary <dept_avg>"});
    query.where("e.department_id").isEqualTo(1LL);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    const std::vector<SeedEmployee> engineering = employeesIn(1);
    double total = 0;
    for (const SeedEmployee & e : engineering) total += e.salary;
    const double expectedAvg = total / static_cast<double>(engineering.size());

    ASSERT_EQ(result->size(), engineering.size());
    for (auto & row : *result) {
        EXPECT_NEAR(asDouble(row[1]), expectedAvg, 0.01);
    }
}

// =====================================================================
// CTE / set operations
// =====================================================================

TEST_F(QcIntegrationSelectTest, CommonTableExpressionIsReferencedByOuterQuery)
{
    QcSqlQuery cte;
    cte.fromTable("qc_bt_employees");
    cte.addReturnValues({"id", "department_id"});
    cte.where("is_active").isEqualTo(1LL);

    QcSqlQuery outer;
    outer.with_("active_emps", cte);
    outer.fromTable("active_emps");
    outer.addReturnValue("* <cnt>").count();

    auto result = run(outer);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);

    const std::size_t expected = static_cast<std::size_t>(
        std::count_if(s_employees.begin(), s_employees.end(), [](const SeedEmployee & e) { return e.isActive; }));
    EXPECT_EQ(asInt64((*result)[0][0]), static_cast<long long>(expected));
}

TEST_F(QcIntegrationSelectTest, UnionCombinesDistinctRowsAcrossTwoFilters)
{
    QcSqlQuery a;
    a.fromTable("qc_bt_employees");
    a.addReturnValues({"id"});
    a.where("department_id").isEqualTo(1LL);

    QcSqlQuery b;
    b.fromTable("qc_bt_employees");
    b.addReturnValues({"id"});
    b.where("salary").isGreaterThan(120000.0);

    a.unionWith(b);

    auto result = run(a);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.departmentId == 1 || e.salary > 120000.0) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_EQ(result->size(), expected.size()); // no duplicates despite overlap between the two filters
}

TEST_F(QcIntegrationSelectTest, UnionAllPreservesDuplicateRows)
{
    QcSqlQuery a;
    a.fromTable("qc_bt_employees");
    a.addReturnValues({"id"});
    a.where("department_id").isEqualTo(1LL);

    QcSqlQuery b;
    b.fromTable("qc_bt_employees");
    b.addReturnValues({"id"});
    b.where("salary").isGreaterThan(120000.0);

    a.unionAllWith(b);

    auto result = run(a);
    ASSERT_TRUE(result.has_value());

    const std::size_t countA = employeesIn(1).size();
    const std::size_t countB = static_cast<std::size_t>(
        std::count_if(s_employees.begin(), s_employees.end(), [](const SeedEmployee & e) { return e.salary > 120000.0; }));
    EXPECT_EQ(result->size(), countA + countB);
}

TEST_F(QcIntegrationSelectTest, IntersectReturnsRowsPresentInBothSides)
{
    QcSqlQuery a;
    a.fromTable("qc_bt_employees");
    a.addReturnValues({"id"});
    a.where("department_id").isEqualTo(1LL);

    QcSqlQuery b;
    b.fromTable("qc_bt_employees");
    b.addReturnValues({"id"});
    b.where("salary").isGreaterThan(120000.0);

    a.intersectWith(b);

    auto result = run(a);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.departmentId == 1 && e.salary > 120000.0) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, ExceptReturnsRowsOnlyInFirstSide)
{
    QcSqlQuery a;
    a.fromTable("qc_bt_employees");
    a.addReturnValues({"id"});
    a.where("department_id").isEqualTo(1LL);

    QcSqlQuery b;
    b.fromTable("qc_bt_employees");
    b.addReturnValues({"id"});
    b.where("salary").isGreaterThan(120000.0);

    a.exceptWith(b);

    auto result = run(a);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.departmentId == 1 && !(e.salary > 120000.0)) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

// =====================================================================
// Ordering / pagination / distinct
// =====================================================================

TEST_F(QcIntegrationSelectTest, OrderByAscendingSortsSalaryLowToHigh)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"salary"});
    query.where("department_id").isEqualTo(1LL);
    query.orderAsc({"salary"});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::vector<double> values;
    for (auto & row : *result) values.push_back(asDouble(row[0]));
    EXPECT_TRUE(std::is_sorted(values.begin(), values.end()));
    EXPECT_EQ(values.size(), employeesIn(1).size());
}

TEST_F(QcIntegrationSelectTest, OrderByDescendingSortsSalaryHighToLow)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"salary"});
    query.where("department_id").isEqualTo(1LL);
    query.orderDesc({"salary"});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::vector<double> values;
    for (auto & row : *result) values.push_back(asDouble(row[0]));
    EXPECT_TRUE(std::is_sorted(values.begin(), values.end(), std::greater<double>()));
}

TEST_F(QcIntegrationSelectTest, OrderByMultipleColumnsBreaksTiesByCorrectColumn)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"department_id", "id"});
    query.orderAsc({"department_id"});
    query.orderDesc({"id"});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), s_employees.size());

    long long previousDept = std::numeric_limits<long long>::min();
    long long previousId = std::numeric_limits<long long>::max();
    for (auto & row : *result) {
        const long long dept = asInt64(row[0]);
        const long long id = asInt64(row[1]);
        if (dept == previousDept) {
            EXPECT_LT(id, previousId); // within a department, ids descend
        } else {
            EXPECT_GT(dept, previousDept); // departments ascend
            previousId = std::numeric_limits<long long>::max();
        }
        previousDept = dept;
        previousId = id;
    }
}

TEST_F(QcIntegrationSelectTest, OrderByNullsFirstPlacesNullRowsBeforeNonNullRows)
{
    // `manager_id` is nullable, and both several anchor rows (see
    // buildSeedData()) and about half of the randomly-generated filler rows
    // have a NULL manager_id -- exercising the emulated NULLS FIRST path
    // (MySQL/MSSQL, see QcSqlDialect::orderByEntry()) as well as the native
    // one on a data set where NULLs actually interleave with non-NULL values
    // in the driver's own default ordering, not just happen to already sort
    // first/last on their own. A numeric column is used (rather than the
    // also-nullable `nickname` text column) so the non-NULL ordering check
    // below can compare byte/numeric order directly -- PostgreSQL's default
    // en_US.UTF-8 collation orders text differently from plain C++
    // std::string comparison, which isn't what this test is about.
    std::size_t expectedNullCount = 0;
    for (const SeedEmployee & e : s_employees) {
        if (!e.managerId) {
            ++expectedNullCount;
        }
    }
    ASSERT_GT(expectedNullCount, 0u);

    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"manager_id"});
    query.orderAsc({"manager_id"}, QcSqlQuery::_nullsFirst_);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), s_employees.size());

    std::vector<long long> nonNullValues;
    for (std::size_t i = 0; i < result->size(); ++i) {
        const bool isNull = std::holds_alternative<std::monostate>((*result)[i][0]);
        if (i < expectedNullCount) {
            EXPECT_TRUE(isNull) << "row " << i << " expected NULL";
        } else {
            ASSERT_FALSE(isNull) << "row " << i << " expected non-NULL";
            nonNullValues.push_back(asInt64((*result)[i][0]));
        }
    }
    EXPECT_TRUE(std::is_sorted(nonNullValues.begin(), nonNullValues.end()));
}

TEST_F(QcIntegrationSelectTest, OrderByNullsLastPlacesNullRowsAfterNonNullRows)
{
    std::size_t expectedNullCount = 0;
    for (const SeedEmployee & e : s_employees) {
        if (!e.managerId) {
            ++expectedNullCount;
        }
    }
    ASSERT_GT(expectedNullCount, 0u);

    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"manager_id"});
    query.orderDesc({"manager_id"}, QcSqlQuery::_nullsLast_);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), s_employees.size());

    const std::size_t nonNullCount = result->size() - expectedNullCount;
    std::vector<long long> nonNullValues;
    for (std::size_t i = 0; i < result->size(); ++i) {
        const bool isNull = std::holds_alternative<std::monostate>((*result)[i][0]);
        if (i < nonNullCount) {
            ASSERT_FALSE(isNull) << "row " << i << " expected non-NULL";
            nonNullValues.push_back(asInt64((*result)[i][0]));
        } else {
            EXPECT_TRUE(isNull) << "row " << i << " expected NULL";
        }
    }
    EXPECT_TRUE(std::is_sorted(nonNullValues.begin(), nonNullValues.end(), std::greater<long long>()));
}

TEST_F(QcIntegrationSelectTest, OrderByNullsPositionComposesCorrectlyWithMultipleSortColumns)
{
    // NULLS FIRST/LAST on MySQL/MSSQL is emulated as an extra leading
    // CASE-tiebreaker key ahead of the real column (see
    // QcSqlDialect::orderByEntry()) -- this only actually renders valid,
    // correctly-composing SQL when it's exercised alongside *other* sort
    // columns, not just in isolation. Orders by department_id (plain, no
    // NULLS clause) first, then the nullable manager_id (NULLS FIRST)
    // second, and checks: department_id groups stay contiguous and
    // non-decreasing, and *within* each department's group every NULL
    // manager_id row precedes every non-NULL one, with the non-NULL values
    // themselves ascending.
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"department_id", "manager_id"});
    query.orderAsc({"department_id"});
    query.orderAsc({"manager_id"}, QcSqlQuery::_nullsFirst_);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), s_employees.size());

    long long previousDept = std::numeric_limits<long long>::min();
    bool sawNonNullManagerInDept = false;
    long long previousManagerId = std::numeric_limits<long long>::min();
    std::size_t nullManagerRowsSeen = 0;
    std::size_t nonNullAdjacentPairsChecked = 0;

    for (auto & row : *result) {
        const long long dept = asInt64(row[0]);
        ASSERT_GE(dept, previousDept); // department_id never goes backwards
        if (dept != previousDept) {
            previousDept = dept;
            sawNonNullManagerInDept = false;
            previousManagerId = std::numeric_limits<long long>::min();
        }

        if (std::holds_alternative<std::monostate>(row[1])) {
            EXPECT_FALSE(sawNonNullManagerInDept) << "NULL manager_id found after a non-NULL one within department " << dept;
            ++nullManagerRowsSeen;
        } else {
            const long long managerId = asInt64(row[1]);
            if (sawNonNullManagerInDept) {
                EXPECT_GE(managerId, previousManagerId) << "department " << dept;
                ++nonNullAdjacentPairsChecked;
            }
            sawNonNullManagerInDept = true;
            previousManagerId = managerId;
        }
    }

    // Sanity: the seed data actually exercises both assertions above (some
    // NULL manager_id rows, and at least one non-NULL/non-NULL adjacency to
    // compare) -- otherwise this test could pass vacuously.
    EXPECT_GT(nullManagerRowsSeen, 0u);
    EXPECT_GT(nonNullAdjacentPairsChecked, 0u);
}

TEST_F(QcIntegrationSelectTest, LimitReturnsExactlyRequestedRowCount)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.orderAsc({"id"});
    query.limit(15);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 15u);
}

TEST_F(QcIntegrationSelectTest, LimitWithOffsetSkipsLeadingRows)
{
    QcSqlQuery firstPage;
    firstPage.fromTable("qc_bt_employees");
    firstPage.addReturnValues({"id"});
    firstPage.orderAsc({"id"});
    firstPage.limit(5, 0);

    QcSqlQuery secondPage;
    secondPage.fromTable("qc_bt_employees");
    secondPage.addReturnValues({"id"});
    secondPage.orderAsc({"id"});
    secondPage.limit(5, 5);

    auto first = run(firstPage);
    auto second = run(secondPage);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(first->size(), 5u);
    ASSERT_EQ(second->size(), 5u);

    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(asInt64((*second)[i][0]), asInt64((*first)[i][0]) + 5);
    }
}

TEST_F(QcIntegrationSelectTest, OffsetWithoutLimitReturnsAllRemainingRows)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.orderAsc({"id"});
    query.limit(0, 10); // rowsCount <= 0: no cap, just skip the first 10

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), s_employees.size() - 10);
}

TEST_F(QcIntegrationSelectTest, PaginationAcrossAllPagesCoversEveryRowExactlyOnce)
{
    constexpr int pageSize = 20;
    std::set<long long> seenIds;
    int page = 0;
    while (true) {
        QcSqlQuery query;
        query.fromTable("qc_bt_employees");
        query.addReturnValues({"id"});
        query.orderAsc({"id"});
        query.limit(pageSize, page * pageSize);

        auto result = run(query);
        ASSERT_TRUE(result.has_value());
        if (result->empty()) break;

        for (auto & row : *result) {
            const long long id = asInt64(row[0]);
            EXPECT_TRUE(seenIds.insert(id).second) << "duplicate id " << id << " across pages";
        }
        ++page;
        ASSERT_LT(page, 100); // safety valve against an infinite loop on a real bug
    }
    EXPECT_EQ(seenIds.size(), s_employees.size());
}

TEST_F(QcIntegrationSelectTest, DistinctRemovesDuplicateDepartmentIds)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.distinct();
    query.addReturnValues({"department_id"});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) expected.insert(e.departmentId);

    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_EQ(result->size(), expected.size());
}

TEST_F(QcIntegrationSelectTest, DistinctOnMatchesActiveDriverShape)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.distinctOn({"department_id"});
    query.addReturnValues({"department_id", "id"});
    query.orderAsc({"department_id", "id"});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    if (primaryTestDriver() == QcDbDriver::PostgreSQL) {
        // DISTINCT ON (department_id) keeps exactly one row per department_id --
        // one per department that actually has employees.
        std::set<long long> seenDepartments;
        for (auto & row : *result) {
            EXPECT_TRUE(seenDepartments.insert(asInt64(row[0])).second);
        }
        std::set<long long> expectedDepartments;
        for (const SeedEmployee & e : s_employees) expectedDepartments.insert(e.departmentId);
        EXPECT_EQ(seenDepartments, expectedDepartments);
    } else {
        // distinctOn() degrades to plain DISTINCT outside PostgreSQL (see
        // qcsqlquery.cpp) -- (department_id, id) is already unique per row
        // (id is a primary key), so DISTINCT changes nothing.
        EXPECT_EQ(result->size(), s_employees.size());
    }
}

// =====================================================================
// GROUP BY / HAVING / aggregates
// =====================================================================

TEST_F(QcIntegrationSelectTest, GroupByDepartmentCountsMatchGeneratedData)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("department_id");
    query.addReturnValue("* <emp_count>").count();
    query.groupBy({"department_id"});
    query.orderAsc({"department_id"});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::map<long long, long long> expected;
    for (const SeedEmployee & e : s_employees) expected[e.departmentId] += 1;

    ASSERT_EQ(result->size(), expected.size());
    for (auto & row : *result) {
        const long long deptId = asInt64(row[0]);
        EXPECT_EQ(asInt64(row[1]), expected.at(deptId));
    }
}

TEST_F(QcIntegrationSelectTest, GroupByHavingFiltersAggregatedGroups)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("department_id");
    query.addReturnValue("* <emp_count>").count();
    query.groupBy({"department_id"});
    query.having("COUNT(*)").isGreaterThan(5LL);
    query.orderAsc({"department_id"});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::map<long long, long long> counts;
    for (const SeedEmployee & e : s_employees) counts[e.departmentId] += 1;

    std::set<long long> expectedDepartments;
    for (auto & [deptId, count] : counts) {
        if (count > 5) expectedDepartments.insert(deptId);
    }

    std::set<long long> actualDepartments;
    for (auto & row : *result) {
        const long long deptId = asInt64(row[0]);
        actualDepartments.insert(deptId);
        EXPECT_GT(asInt64(row[1]), 5);
    }
    EXPECT_EQ(actualDepartments, expectedDepartments);
    EXPECT_FALSE(expectedDepartments.empty());
}

TEST_F(QcIntegrationSelectTest, GroupByWithSumAvgMinMaxMatchesComputedExpectations)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("department_id");
    query.addReturnValue("salary <total_salary>").sum();
    query.addReturnValue("salary <avg_salary>").avg();
    query.addReturnValue("salary <min_salary>").min();
    query.addReturnValue("salary <max_salary>").max();
    query.groupBy({"department_id"});
    query.orderAsc({"department_id"});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    for (auto & row : *result) {
        const long long deptId = asInt64(row[0]);
        const std::vector<SeedEmployee> members = employeesIn(deptId);
        ASSERT_FALSE(members.empty());

        double total = 0;
        double minSalary = members.front().salary;
        double maxSalary = members.front().salary;
        for (const SeedEmployee & e : members) {
            total += e.salary;
            minSalary = std::min(minSalary, e.salary);
            maxSalary = std::max(maxSalary, e.salary);
        }
        const double avg = total / static_cast<double>(members.size());

        EXPECT_NEAR(asDouble(row[1]), total, 0.5);
        EXPECT_NEAR(asDouble(row[2]), avg, 0.5);
        EXPECT_NEAR(asDouble(row[3]), minSalary, 0.01);
        EXPECT_NEAR(asDouble(row[4]), maxSalary, 0.01);
    }
}

// =====================================================================
// Functions: CAST, CONCAT, ROUND, UPPER/LOWER
// =====================================================================

TEST_F(QcIntegrationSelectTest, CastElementComparesConvertedColumnToTextValue)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    QcSqlQueryElement & el = query.where("department_id");
    el.cast(QcSqlBase::_int_, QcSqlBase::_string_, std::string("1"));

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.departmentId == 1) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

TEST_F(QcIntegrationSelectTest, CastValueRendersConvertedColumnInSelectList)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("id");
    query.addReturnValue("department_id <dept_as_text>").cast(QcSqlBase::_int_, QcSqlBase::_string_);
    query.where("id").isEqualTo(1LL);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*result)[0][1]), "1");
}

TEST_F(QcIntegrationSelectTest, ConcatBuildsExpectedCompositeString)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("full_name <combo>").concat({"' <'", "email", "'>'"});
    query.where("id").isEqualTo(1LL);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*result)[0][0]), "Alice Johnson <alice.johnson@example.com>");
}

TEST_F(QcIntegrationSelectTest, RoundFunctionRoundsToRequestedPrecision)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("salary <rounded>").round(0);
    query.where("id").isEqualTo(9LL); // Hiro Tanaka, salary 142999.60

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_NEAR(asDouble((*result)[0][0]), 143000.0, 0.5);
}

TEST_F(QcIntegrationSelectTest, UpperCaseAndLowerCaseTransformSelectedColumn)
{
    QcSqlQuery upperQuery;
    upperQuery.fromTable("qc_bt_employees");
    upperQuery.addReturnValue("full_name <upper_name>").upperCase();
    upperQuery.where("id").isEqualTo(3LL); // "Carol O'Brien"

    auto upperResult = run(upperQuery);
    ASSERT_TRUE(upperResult.has_value());
    ASSERT_EQ(upperResult->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*upperResult)[0][0]), "CAROL O'BRIEN");

    QcSqlQuery lowerQuery;
    lowerQuery.fromTable("qc_bt_employees");
    lowerQuery.addReturnValue("full_name <lower_name>").lowerCase();
    lowerQuery.where("id").isEqualTo(3LL);

    auto lowerResult = run(lowerQuery);
    ASSERT_TRUE(lowerResult.has_value());
    ASSERT_EQ(lowerResult->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*lowerResult)[0][0]), "carol o'brien");
}

TEST_F(QcIntegrationSelectTest, ExtractFunctionPullsYearMonthDayFromHireDate)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("hire_date <yr>").extract(QcSqlBase::_year_);
    query.addReturnValue("hire_date <mo>").extract(QcSqlBase::_month_);
    query.addReturnValue("hire_date <dy>").extract(QcSqlBase::_day_);
    query.where("id").isEqualTo(1LL); // Alice Johnson, hire_date "2015-03-10"

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0][0]), 2015);
    EXPECT_EQ(asInt64((*result)[0][1]), 3);
    EXPECT_EQ(asInt64((*result)[0][2]), 10);
}

TEST_F(QcIntegrationSelectTest, DateAddFunctionShiftsHireDateForwardVerifiedViaExtract)
{
    // Composing dateAdd().extract() (rather than comparing the shifted date
    // as text) sidesteps the fact that each driver's DATE/TEXT storage
    // formats a shifted timestamp differently (see
    // QcSqlDialect::dateAddExpr() -- e.g. SQLite's datetime() always adds a
    // "00:00:00" time-of-day) -- extracting a single plain integer back out
    // is directly comparable everywhere.
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("hire_date <shifted_day>").dateAdd(QcSqlBase::_day_, 5).extract(QcSqlBase::_day_);
    query.where("id").isEqualTo(1LL); // hire_date "2015-03-10" + 5 days = "2015-03-15", no month rollover

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0][0]), 15);
}

TEST_F(QcIntegrationSelectTest, SubstringAndLengthFunctionsOperateOnFullName)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("full_name <first_five>").substring(1, 5);
    query.addReturnValue("full_name <name_len>").length();
    query.where("id").isEqualTo(1LL); // "Alice Johnson" -- 13 characters

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*result)[0][0]), "Alice");
    EXPECT_EQ(asInt64((*result)[0][1]), 13);
}

TEST_F(QcIntegrationSelectTest, CoalesceFunctionFallsBackToLiteralWhenNicknameIsNull)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("nickname <resolved>").coalesce({"'(none)'"});
    query.where("id").isEqualTo(2LL); // Bob Martinez, nickname NULL in seed data

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*result)[0][0]), "(none)");
}

TEST_F(QcIntegrationSelectTest, SearchedCaseClassifiesSalaryIntoBands)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("id");
    query.addReturnValue("<salary_band>").case_({
        {qi("salary") + " >= 120000", "'high'"},
        {qi("salary") + " >= 90000", "'mid'"},
    }, std::string("'low'"));
    query.where("id").isIn(QcSqlBase::QcVariantList{1LL, 4LL, 7LL});
    query.orderAsc({"id"});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 3u);
    // 1 Alice Johnson: 145000.00 -> high; 4 Dmitri Volkov: 98000.00 -> mid;
    // 7 Frank Ibrahim: 61000.00 -> low.
    EXPECT_EQ(std::get<std::string>((*result)[0][1]), "high");
    EXPECT_EQ(std::get<std::string>((*result)[1][1]), "mid");
    EXPECT_EQ(std::get<std::string>((*result)[2][1]), "low");
}

TEST_F(QcIntegrationSelectTest, CountDistinctFunctionCountsUniqueDepartmentsAmongActiveEmployees)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("department_id <distinct_depts>").count(true);
    query.where("is_active").isEqualTo(1LL);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);

    std::set<long long> expectedDepts;
    for (const SeedEmployee & e : s_employees) {
        if (e.isActive) expectedDepts.insert(e.departmentId);
    }
    EXPECT_EQ(asInt64((*result)[0][0]), static_cast<long long>(expectedDepts.size()));
}

// =====================================================================
// JSON
// =====================================================================

TEST_F(QcIntegrationSelectTest, JsonFieldExtractionReturnsSeededValue)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    switch (primaryTestDriver()) {
        case QcDbDriver::PostgreSQL:
            query.addReturnValue("metadata->>'level' <level_text>");
            break;
        case QcDbDriver::SQLite:
            query.addReturnValue("json_extract(metadata, '$.level') <level_text>");
            break;
        case QcDbDriver::Oracle:
            // "metadata" is quoted here (unlike the identical-looking MSSQL/MySQL
            // branch below) because this suite's Oracle schema itself is created
            // with quoted, case-preserved DDL (see createSchema()) -- an unquoted
            // reference here would fold to uppercase and miss the column entirely.
            query.addReturnValue("JSON_VALUE(" + qi("metadata") + ", '$.level') <level_text>");
            break;
        case QcDbDriver::MSSQL:
        case QcDbDriver::MySQL:
            query.addReturnValue("JSON_VALUE(metadata, '$.level') <level_text>");
            break;
    }
    query.where("id").isEqualTo(1LL); // Alice Johnson, metaLevel = 5

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0][0]), 5);
}

TEST_F(QcIntegrationSelectTest, JsonArrayElementExtractionReturnsSeededSkill)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    switch (primaryTestDriver()) {
        case QcDbDriver::PostgreSQL:
            query.addReturnValue("metadata->'skills'->>0 <first_skill>");
            break;
        case QcDbDriver::SQLite:
            query.addReturnValue("json_extract(metadata, '$.skills[0]') <first_skill>");
            break;
        case QcDbDriver::Oracle:
            query.addReturnValue("JSON_VALUE(" + qi("metadata") + ", '$.skills[0]') <first_skill>");
            break;
        case QcDbDriver::MSSQL:
        case QcDbDriver::MySQL:
            query.addReturnValue("JSON_VALUE(metadata, '$.skills[0]') <first_skill>");
            break;
    }
    query.where("id").isEqualTo(1LL); // skills[0] == "c++"

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*result)[0][0]), "c++");
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByExtractedJsonFieldValue)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    switch (primaryTestDriver()) {
        case QcDbDriver::PostgreSQL:
            query.addFreeText("(metadata->>'remote')::boolean = true", {});
            break;
        case QcDbDriver::SQLite:
            query.addFreeText("json_extract(metadata, '$.remote') = 1", {});
            break;
        case QcDbDriver::Oracle:
            query.addFreeText("JSON_VALUE(" + qi("metadata") + ", '$.remote') = 'true'", {});
            break;
        case QcDbDriver::MSSQL:
        case QcDbDriver::MySQL:
            query.addFreeText("JSON_VALUE(metadata, '$.remote') = 'true'", {});
            break;
    }

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.metaRemote) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

// =====================================================================
// JSON via the query-builder JSON API
// (QcSqlQueryElement::isXxxJson...(), QcSqlQueryValue::jsonExtract...())
// =====================================================================
//
// Unlike the three driver-switch tests just above (hand-written raw SQL,
// one branch per driver), every test below runs *identical* C++ against
// whichever driver this suite is built for -- QcSqlDialect::jsonExtractExpr()
// (qcsqldialect.cpp) is now the one place the per-driver JSON syntax lives,
// so the call site needs no switch at all. Flat-schema tests below reuse
// qc_bt_employees.metadata (level: int, remote: bool, skills: string array,
// rating: double -- see metadataJson()); nested-path tests further down use
// a JSON *literal* instead of a column, to exercise multi-level
// object/array traversal ("a.b[1].c") that the flat schema has no room for.

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonNumberEquality)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isEqualToJsonNumber(3LL, "level");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.metaLevel == 3) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonNumberInequality)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isNotEqualToJsonNumber(5LL, "level");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.metaLevel != 5) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonNumberGreaterThan)
{
    // ".55" thresholds sit strictly between the ".1"-rounded rating grid
    // metadataJson() seeds -- no data point can land exactly on the
    // boundary, so this doesn't depend on double-vs-numeric round-trip
    // precision agreeing on a tie.
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isGreaterThanJsonNumber(4.55, "rating");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.metaRating > 4.55) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonNumberGreaterThanOrEqualTo)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isGreaterThanOrEqualToJsonNumber(4LL, "level");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.metaLevel >= 4) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonNumberLessThan)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isLessThanJsonNumber(3.45, "rating");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.metaRating < 3.45) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonNumberLessThanOrEqualTo)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isLessThanOrEqualToJsonNumber(2LL, "level");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.metaLevel <= 2) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonTextEqualityOnFirstSkill)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isEqualToJsonText(std::string("c++"), "skills[0]");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (!e.metaSkills.empty() && e.metaSkills[0] == "c++") expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonTextInequalityOnFirstSkill)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isNotEqualToJsonText(std::string("c++"), "skills[0]");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (!e.metaSkills.empty() && e.metaSkills[0] != "c++") expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonTextGreaterThanOnFirstSkill)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isGreaterThanJsonText(std::string("m"), "skills[0]");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (!e.metaSkills.empty() && e.metaSkills[0] > "m") expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonTextGreaterThanOrEqualToOnFirstSkill)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isGreaterThanOrEqualToJsonText(std::string("s"), "skills[0]");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (!e.metaSkills.empty() && e.metaSkills[0] >= "s") expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonTextLessThanOnFirstSkill)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isLessThanJsonText(std::string("m"), "skills[0]");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (!e.metaSkills.empty() && e.metaSkills[0] < "m") expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonTextLessThanOrEqualToOnFirstSkill)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isLessThanOrEqualToJsonText(std::string("d"), "skills[0]");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (!e.metaSkills.empty() && e.metaSkills[0] <= "d") expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonTextLikeOnFirstSkill)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isLikeJsonText("c%", "skills[0]");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (!e.metaSkills.empty() && e.metaSkills[0].rfind("c", 0) == 0) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonTextNotLikeOnFirstSkill)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isNotLikeJsonText("c%", "skills[0]");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (!e.metaSkills.empty() && e.metaSkills[0].rfind("c", 0) != 0) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonTextIlikeOnFirstSkillIsCaseInsensitive)
{
    // Every seeded skill happens to already be lowercase -- "C%" (uppercase)
    // only matches anything at all *because* isIlikeJsonText() is
    // case-insensitive; a plain isLikeJsonText("C%", ...) would match
    // nothing here.
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isIlikeJsonText("C%", "skills[0]");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (!e.metaSkills.empty() && e.metaSkills[0].rfind("c", 0) == 0) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonTextNotIlikeOnFirstSkill)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isNotILikeJsonText("C%", "skills[0]");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (!e.metaSkills.empty() && e.metaSkills[0].rfind("c", 0) != 0) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonArrayAsTextMatchesAnyElementNotJustFirst)
{
    // isLikeJsonArrayAsText() searches the *whole* serialized skills array,
    // unlike isEqualToJsonText()/isLikeJsonText() above (which only ever
    // look at one indexed element, skills[0]) -- "python" can be anywhere in
    // the array.
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isLikeJsonArrayAsText("%\"python\"%", "skills");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (std::find(e.metaSkills.begin(), e.metaSkills.end(), "python") != e.metaSkills.end()) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());

    // The "any position" claim above would still hold trivially if every
    // match happened to be skills[0] too -- confirm at least one expected
    // match has "python" at some other index, so this genuinely exercises
    // array-wide search rather than degenerating to the skills[0] case.
    bool sawNonFirstMatch = false;
    for (const SeedEmployee & e : s_employees) {
        const auto it = std::find(e.metaSkills.begin(), e.metaSkills.end(), "python");
        if (it != e.metaSkills.end() && it != e.metaSkills.begin()) {
            sawNonFirstMatch = true;
            break;
        }
    }
    EXPECT_TRUE(sawNonFirstMatch);
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByJsonArrayAsTextIlikeIsCaseInsensitive)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("metadata").isIlikeJsonArrayAsText("%\"PYTHON\"%", "skills");

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (std::find(e.metaSkills.begin(), e.metaSkills.end(), "python") != e.metaSkills.end()) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(expected.empty());
}

TEST_F(QcIntegrationSelectTest, SelectExtractsJsonScalarValuesViaBuilder)
{
    // Same underlying values as JsonFieldExtractionReturnsSeededValue/
    // JsonArrayElementExtractionReturnsSeededSkill above, but reached through
    // jsonExtract() instead of a hand-written per-driver SELECT expression.
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("metadata <level_text>").jsonExtract("level");
    query.addReturnValue("metadata <first_skill>").jsonExtract("skills[0]");
    query.where("id").isEqualTo(1LL); // Alice Johnson, metaLevel = 5, skills[0] = "c++"

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0][0]), 5);
    EXPECT_EQ(std::get<std::string>((*result)[0][1]), "c++");
}

TEST_F(QcIntegrationSelectTest, SelectExtractsJsonNumberViaBuilder)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("metadata <rating>").jsonExtractNumber("rating");
    query.where("id").isEqualTo(1LL); // Alice Johnson, metaRating = 4.8

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_NEAR(asDouble((*result)[0][0]), 4.8, 0.001);
}

TEST_F(QcIntegrationSelectTest, SelectExtractsRawJsonArrayFragmentViaBuilder)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("metadata <skills_raw>").jsonExtractRaw("skills");
    query.where("id").isEqualTo(1LL); // skills = ["c++", "postgres", "distributed-systems"]

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);

    const std::string skillsRaw = std::get<std::string>((*result)[0][0]);
    // A raw JSON fragment, not one unwrapped scalar -- still bracketed, with
    // every element's own quoting intact (contrast SelectExtractsJsonScalarValuesViaBuilder's
    // jsonExtract("skills[0]") above, which unwraps a single element).
    EXPECT_NE(skillsRaw.find('['), std::string::npos) << skillsRaw;
    EXPECT_NE(skillsRaw.find(']'), std::string::npos) << skillsRaw;
    EXPECT_NE(skillsRaw.find("\"c++\""), std::string::npos) << skillsRaw;
    EXPECT_NE(skillsRaw.find("\"postgres\""), std::string::npos) << skillsRaw;
    EXPECT_NE(skillsRaw.find("\"distributed-systems\""), std::string::npos) << skillsRaw;
}

// ---- Nested object/array paths ("a.b[1].c") via a JSON literal ----
//
// qc_bt_employees.metadata is a flat document (level/remote/skills/rating,
// one array of strings) -- these tests use a JSON literal instead of a
// stored column (still selected FROM qc_bt_employees, filtered down to
// exactly one row by id -- not limit(), which on MSSQL requires an ORDER BY
// alongside it that these single-literal-row queries have no natural
// column to sort by) to prove jsonSearchPath's dot/bracket convention
// actually walks multiple nesting levels end to end against a real engine,
// not just that qcsqldialect.cpp renders the right text (already pinned
// directly in test_qcsqldialect.cpp).

TEST_F(QcIntegrationSelectTest, SelectExtractsNumberFromObjectInsideArrayInsideObject)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("'{\"a\":{\"b\":[{\"c\":10},{\"c\":20},{\"c\":30}]}}' <nested>").jsonExtractNumber("a.b[1].c");
    query.where("id").isEqualTo(1LL);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_NEAR(asDouble((*result)[0][0]), 20.0, 0.001);
}

TEST_F(QcIntegrationSelectTest, SelectExtractsTextFromScalarArrayInsideNestedObject)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("'{\"tags\":{\"list\":[\"x\",\"y\",\"z\"]}}' <item>").jsonExtract("tags.list[2]");
    query.where("id").isEqualTo(1LL);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(std::get<std::string>((*result)[0][0]), "z");
}

TEST_F(QcIntegrationSelectTest, SelectExtractsRawArrayFragmentFromNestedObject)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValue("'{\"tags\":{\"list\":[\"x\",\"y\",\"z\"]}}' <list_raw>").jsonExtractRaw("tags.list");
    query.where("id").isEqualTo(1LL);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);

    const std::string listRaw = std::get<std::string>((*result)[0][0]);
    EXPECT_NE(listRaw.find('['), std::string::npos) << listRaw;
    EXPECT_NE(listRaw.find(']'), std::string::npos) << listRaw;
    EXPECT_NE(listRaw.find("\"x\""), std::string::npos) << listRaw;
    EXPECT_NE(listRaw.find("\"y\""), std::string::npos) << listRaw;
    EXPECT_NE(listRaw.find("\"z\""), std::string::npos) << listRaw;
}

TEST_F(QcIntegrationSelectTest, WhereFiltersByNestedObjectInsideArrayInsideObjectViaJsonLiteral)
{
    // The WHERE-side isXxxJson...() family walks the same nested paths as
    // the SELECT-side jsonExtract...() family above -- proven here the same
    // way, through a literal (via where() on the literal text, exactly like
    // where()/addReturnValue() already accept any raw expression -- see
    // quoteRef()'s doc comment in qcsqldialect.h) rather than a stored
    // column. The literal's truth value doesn't depend on which row it's
    // evaluated against, so and_("id") pins the match down to exactly one
    // (id=1) instead of asserting over however many rows qc_bt_employees
    // happens to have.
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("'{\"a\":{\"b\":[{\"c\":10},{\"c\":20},{\"c\":30}]}}'").isEqualToJsonNumber(20LL, "a.b[1].c");
    query.and_("id").isEqualTo(1LL);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1u);
}

// =====================================================================
// addFreeText / injection safety / large IN-lists / kitchen sink
// =====================================================================

TEST_F(QcIntegrationSelectTest, AddFreeTextFragmentCombinesWithBuilderWhereConditions)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("department_id").isEqualTo(1LL);
    query.addFreeText(qi("salary") + " > ?", {100000.0});

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> expected;
    for (const SeedEmployee & e : s_employees) {
        if (e.departmentId == 1 && e.salary > 100000.0) expected.insert(e.id);
    }
    std::set<long long> actual;
    for (auto & row : *result) actual.insert(asInt64(row[0]));
    EXPECT_EQ(actual, expected);
}

TEST_F(QcIntegrationSelectTest, ApostropheInFilterValueIsSafelyBound)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("full_name").isEqualTo(std::string("Carol O'Brien"));

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0][0]), 3);
}

TEST_F(QcIntegrationSelectTest, UnicodeFilterValueMatchesExactRow)
{
    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("full_name").isEqualTo(std::string("José Ñandú García"));

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ(asInt64((*result)[0][0]), 5);
}

TEST_F(QcIntegrationSelectTest, LargeInListMatchesAllProvidedIds)
{
    QcSqlBase::QcVariantList ids;
    std::set<long long> expectedIds;
    for (long long id = 1; id <= 150; id += 2) {
        ids.push_back(id);
        expectedIds.insert(id);
    }

    QcSqlQuery query;
    query.fromTable("qc_bt_employees");
    query.addReturnValues({"id"});
    query.where("id").isIn(ids);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());

    std::set<long long> actualIds;
    for (auto & row : *result) actualIds.insert(asInt64(row[0]));
    EXPECT_EQ(actualIds, expectedIds);
}

TEST_F(QcIntegrationSelectTest, KitchenSinkQueryCombinesJoinWhereGroupByHavingOrderByLimit)
{
    QcSqlQuery deptRef;
    deptRef.fromTable("qc_bt_departments");

    QcSqlQuery query;
    query.fromTable("qc_bt_employees e");
    query.addJoin("d", deptRef, qi("d") + "." + qi("id") + " = " + qi("e") + "." + qi("department_id"));
    query.where("e.is_active").isEqualTo(1LL);
    query.and_("d.region").isIn(QcSqlBase::QcVariantList{std::string("EU"), std::string("APAC")});
    query.addReturnValue("d.id");
    query.addReturnValue("* <active_count>").count();
    query.addReturnValue("e.salary <avg_salary>").avg();
    query.groupBy({"d.id"});
    query.having("COUNT(*)").isGreaterThan(0LL);
    query.orderDesc({"avg_salary"});
    query.limit(3);

    auto result = run(query);
    ASSERT_TRUE(result.has_value());
    EXPECT_LE(result->size(), 3u);

    std::map<long long, std::pair<long long, double>> expected; // dept -> (count, totalSalary)
    for (const SeedEmployee & e : s_employees) {
        if (!e.isActive) continue;
        const SeedDepartment & dept = departmentById(e.departmentId);
        if (dept.region != "EU" && dept.region != "APAC") continue;
        auto & agg = expected[e.departmentId];
        agg.first += 1;
        agg.second += e.salary;
    }

    double previousAvg = std::numeric_limits<double>::max();
    for (auto & row : *result) {
        const long long deptId = asInt64(row[0]);
        const long long count = asInt64(row[1]);
        const double avgSalary = asDouble(row[2]);

        ASSERT_TRUE(expected.count(deptId));
        EXPECT_EQ(count, expected[deptId].first);
        EXPECT_NEAR(avgSalary, expected[deptId].second / static_cast<double>(expected[deptId].first), 0.5);
        EXPECT_LE(avgSalary, previousAvg);
        previousAvg = avgSalary;
    }
}
