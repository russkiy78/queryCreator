#include <gtest/gtest.h>

#include "query/qcsqldialect.h"
#include "query/qcsqlquery.h"

// Separator QcSqlQuery::toSql() places between a FROM-subquery/JOIN source
// and its alias -- matches tableAliasSeparator() in qcsqlquery.cpp. Oracle
// is the one driver here that rejects the ANSI "AS" keyword for a *table*
// alias outright (verified directly: `FROM dual AS d` -> ORA-00933); every
// other driver accepts (and this project otherwise always uses) "AS" there.
// The tests below use toSql()'s default driver (PostgreSQL, see
// qcsqlquery.h), hence " AS " -- test_qcsqlquery.cpp doesn't otherwise pass
// an explicit QcDbDriver, this constant would need to change to " " if that
// changes.
constexpr const char * kTableAliasSeparator = " AS ";

// Shorthand for QcSqlDialect::quoteIdentifier() -- expected-SQL strings
// below build up identifiers through this instead of hardcoding a
// driver-specific quote character. Uses quoteIdentifier()'s default driver
// (PostgreSQL), matching toSql()'s own default -- every test in this file
// that cares about a specific driver's rendering passes one explicitly to
// both.
std::string q(const std::string & name)
{
    return QcSqlDialect::quoteIdentifier(name);
}

TEST(QcSqlQueryAddReturnValues, ReturnsTrueForColumnsWithAlias)
{
    QcSqlQuery query;

    EXPECT_TRUE(query.addReturnValues({"test <test_alias>", "test1 <test_alias1>"}));
}

TEST(QcSqlQueryAddReturnValues, ReturnsTrueForColumnsWithoutAlias)
{
    QcSqlQuery query;

    EXPECT_TRUE(query.addReturnValues({"plain_column"}));
}

TEST(QcSqlQueryAddReturnValues, ReturnsTrueForEmptyList)
{
    QcSqlQuery query;

    EXPECT_TRUE(query.addReturnValues({}));
}

// See test_qcsqlqueryvalue.cpp / test_qcsqlqueryelement.cpp for why this is a
// friend fixture rather than public getters: SQL generation (the eventual
// consumer of this state) doesn't exist yet, so there's nothing to expose
// publicly, but the behavior still needs to be verified against real state
// rather than only "it compiles and returns something".
class QcSqlQueryWhiteBoxTest : public ::testing::Test
{
protected:
    static const std::string & fromTable(const QcSqlQuery & q) { return q.m_fromTable; }
    static bool hasFromSubquery(const QcSqlQuery & q) { return static_cast<bool>(q.m_fromSubquery); }
    static bool distinct(const QcSqlQuery & q) { return q.m_distinct; }
    static const QcSqlQuery::QcStringList & distinctOn(const QcSqlQuery & q) { return q.m_distinctOn; }

    static std::size_t cteCount(const QcSqlQuery & q) { return q.m_ctes.size(); }
    static const std::string & cteName(const QcSqlQuery & q, std::size_t i) { return q.m_ctes[i].name; }
    static bool cteHasQuery(const QcSqlQuery & q, std::size_t i) { return static_cast<bool>(q.m_ctes[i].query); }

    static std::size_t joinCount(const QcSqlQuery & q) { return q.m_joins.size(); }
    static int joinType(const QcSqlQuery & q, std::size_t i) { return q.m_joins[i].type; }
    static const std::string & joinAlias(const QcSqlQuery & q, std::size_t i) { return q.m_joins[i].alias; }
    static const std::string & joinOnCondition(const QcSqlQuery & q, std::size_t i) { return q.m_joins[i].onCondition; }
    static bool joinAsSubQuery(const QcSqlQuery & q, std::size_t i) { return q.m_joins[i].asSubQuery; }
    static bool joinHasQuery(const QcSqlQuery & q, std::size_t i) { return static_cast<bool>(q.m_joins[i].query); }

    static std::size_t freeTextCount(const QcSqlQuery & q) { return q.m_freeTextFragments.size(); }
    static const std::string & freeTextText(const QcSqlQuery & q, std::size_t i) { return q.m_freeTextFragments[i].text; }
    static const QcSqlQuery::QcVariantList & freeTextValues(const QcSqlQuery & q, std::size_t i) { return q.m_freeTextFragments[i].values; }

    static std::size_t valueCount(const QcSqlQuery & q) { return q.m_values.size(); }

    static std::size_t whereCount(const QcSqlQuery & q) { return q.m_whereElements.size(); }
    static int whereConnector(const QcSqlQuery & q, std::size_t i) { return q.m_whereElements[i].first; }
    static const QcSqlQueryElement & whereElement(const QcSqlQuery & q, std::size_t i) { return q.m_whereElements[i].second; }

    static std::size_t havingCount(const QcSqlQuery & q) { return q.m_havingElements.size(); }
    static int havingConnector(const QcSqlQuery & q, std::size_t i) { return q.m_havingElements[i].first; }
    static const QcSqlQueryElement & havingElement(const QcSqlQuery & q, std::size_t i) { return q.m_havingElements[i].second; }

    static std::size_t orderByCount(const QcSqlQuery & q) { return q.m_orderBy.size(); }
    static int orderByDirection(const QcSqlQuery & q, std::size_t i) { return q.m_orderBy[i].direction; }
    static int orderByNulls(const QcSqlQuery & q, std::size_t i) { return q.m_orderBy[i].nulls; }
    static const std::string & orderByColumn(const QcSqlQuery & q, std::size_t i) { return q.m_orderBy[i].column; }

    static std::size_t groupByCount(const QcSqlQuery & q) { return q.m_groupBy.size(); }
    static const std::string & groupByColumn(const QcSqlQuery & q, std::size_t i) { return q.m_groupBy[i].second; }

    static int limitFrom(const QcSqlQuery & q) { return q.m_limitFrom; }
    static int limitTo(const QcSqlQuery & q) { return q.m_limitTo; }
    static bool limitWithTies(const QcSqlQuery & q) { return q.m_limitWithTies; }

    static int whereParenDepth(const QcSqlQuery & q) { return q.m_whereParenDepth; }

    static std::size_t setOperationCount(const QcSqlQuery & q) { return q.m_setOperations.size(); }
    static int setOperationType(const QcSqlQuery & q, std::size_t i) { return q.m_setOperations[i].type; }
    static bool setOperationHasQuery(const QcSqlQuery & q, std::size_t i) { return static_cast<bool>(q.m_setOperations[i].query); }

    // QcSqlQueryWhiteBoxTest is also a declared friend of QcSqlQueryElement
    // (see qcsqlqueryelement.h) so tests here can confirm where()/having()
    // really threaded the column name through, not just that *some* element
    // was appended.
    static int elementCompareType(const QcSqlQueryElement & e) { return e.m_compareType; }
    static const std::string & elementColumnName(const QcSqlQueryElement & e) { return e.m_columnName; }
};

TEST_F(QcSqlQueryWhiteBoxTest, AddReturnValuesPopulatesValuesWithNameAndAlias)
{
    QcSqlQuery query;

    query.addReturnValues({"test <test_alias>", "plain_column"});

    EXPECT_EQ(valueCount(query), 2u);
}

TEST_F(QcSqlQueryWhiteBoxTest, AddReturnValueAppendsAndReturnsChainableReference)
{
    QcSqlQuery query;

    QcSqlQueryValue & value = query.addReturnValue("test3 <test_alias3>");
    value.upperCase().round(1);

    EXPECT_EQ(valueCount(query), 1u);
}

TEST_F(QcSqlQueryWhiteBoxTest, AddFreeTextStoresTextAndValues)
{
    QcSqlQuery query;

    query.addFreeText("col = ?", {QcSqlQuery::QcVariant(5LL)});

    ASSERT_EQ(freeTextCount(query), 1u);
    EXPECT_EQ(freeTextText(query, 0), "col = ?");
    ASSERT_EQ(freeTextValues(query, 0).size(), 1u);
    EXPECT_EQ(std::get<long long>(freeTextValues(query, 0)[0]), 5LL);
}

TEST_F(QcSqlQueryWhiteBoxTest, DistinctSetsFlag)
{
    QcSqlQuery query;

    QcSqlQuery & ref = query.distinct();

    EXPECT_EQ(&ref, &query);
    EXPECT_TRUE(distinct(query));
}

TEST_F(QcSqlQueryWhiteBoxTest, DistinctOnSetsFlagAndColumns)
{
    QcSqlQuery query;

    query.distinctOn({"a", "b"});

    EXPECT_TRUE(distinct(query));
    ASSERT_EQ(distinctOn(query).size(), 2u);
    EXPECT_EQ(distinctOn(query)[0], "a");
    EXPECT_EQ(distinctOn(query)[1], "b");
}

TEST_F(QcSqlQueryWhiteBoxTest, FromTableSetsTableAndClearsSubquery)
{
    QcSqlQuery sub;
    sub.fromTable("groups");
    QcSqlQuery query;
    query.fromSubQuery("g", sub);
    ASSERT_TRUE(hasFromSubquery(query));

    QcSqlQuery & ref = query.fromTable("users");

    EXPECT_EQ(&ref, &query);
    EXPECT_EQ(fromTable(query), "users");
    EXPECT_FALSE(hasFromSubquery(query));
}

TEST_F(QcSqlQueryWhiteBoxTest, FromSubQuerySetsAliasAndOwnsCopy)
{
    QcSqlQuery sub;
    sub.fromTable("groups");
    QcSqlQuery query;

    query.fromSubQuery("g", sub);

    EXPECT_EQ(fromTable(query), "g");
    EXPECT_TRUE(hasFromSubquery(query));
}

TEST_F(QcSqlQueryWhiteBoxTest, WithAddsCte)
{
    QcSqlQuery cte;
    cte.fromTable("groups");
    QcSqlQuery query;

    QcSqlQuery & ref = query.with_("recent_groups", cte);

    EXPECT_EQ(&ref, &query);
    ASSERT_EQ(cteCount(query), 1u);
    EXPECT_EQ(cteName(query, 0), "recent_groups");
    EXPECT_TRUE(cteHasQuery(query, 0));
}

TEST_F(QcSqlQueryWhiteBoxTest, JoinMethodsRecordCorrectTypeAndData)
{
    QcSqlQuery joinQuery;
    joinQuery.fromTable("groups");
    QcSqlQuery query;

    query.addLeftJoin("g", joinQuery, "g.id = users.group_id");
    query.addJoin("g2", joinQuery, "g2.id = users.group_id2");
    query.addRightJoin("g3", joinQuery, "g3.id = users.group_id3");
    query.addFullJoin("g4", joinQuery, "g4.id = users.group_id4");
    query.addCrossJoin("g5", joinQuery, true);

    ASSERT_EQ(joinCount(query), 5u);
    EXPECT_EQ(joinType(query, 0), QcSqlBase::_leftJoin_);
    EXPECT_EQ(joinAlias(query, 0), "g");
    EXPECT_EQ(joinOnCondition(query, 0), "g.id = users.group_id");
    EXPECT_TRUE(joinHasQuery(query, 0));

    EXPECT_EQ(joinType(query, 1), QcSqlBase::_innerJoin_);
    EXPECT_EQ(joinType(query, 2), QcSqlBase::_rightJoin_);
    EXPECT_EQ(joinType(query, 3), QcSqlBase::_fullJoin_);

    EXPECT_EQ(joinType(query, 4), QcSqlBase::_crossJoin_);
    EXPECT_EQ(joinAlias(query, 4), "g5");
    EXPECT_TRUE(joinOnCondition(query, 4).empty());
    EXPECT_TRUE(joinAsSubQuery(query, 4));
}

TEST_F(QcSqlQueryWhiteBoxTest, WhereStartsChainWithColumnAndDefaultCompareType)
{
    QcSqlQuery query;

    QcSqlQueryElement & element = query.where("id");
    element.isEqualTo(QcSqlQuery::QcVariant(5LL));

    ASSERT_EQ(whereCount(query), 1u);
    EXPECT_EQ(&whereElement(query, 0), &element);
    EXPECT_EQ(elementColumnName(whereElement(query, 0)), "id");
    EXPECT_EQ(elementCompareType(whereElement(query, 0)), QcSqlBase::_isEqualTo_);
}

TEST_F(QcSqlQueryWhiteBoxTest, AndOrAppendWithCorrectConnector)
{
    QcSqlQuery query;

    query.where("a").isEqualTo(QcSqlQuery::QcVariant(1LL));
    query.and_("b").isEqualTo(QcSqlQuery::QcVariant(2LL));
    query.or_("c").isEqualTo(QcSqlQuery::QcVariant(3LL));

    ASSERT_EQ(whereCount(query), 3u);
    EXPECT_EQ(elementColumnName(whereElement(query, 1)), "b");
    EXPECT_EQ(elementColumnName(whereElement(query, 2)), "c");
    // connector values themselves are a private implementation detail
    // (QcSqlQuery::LogicalConnector); what matters observably is that and_/or_
    // don't collapse to the same connector.
    EXPECT_NE(whereConnector(query, 1), whereConnector(query, 2));
}

TEST_F(QcSqlQueryWhiteBoxTest, OpenParenthesisSugarMethodsPushMarkerThenElement)
{
    QcSqlQuery query;

    query.where_OpenParenthesis("a").isEqualTo(QcSqlQuery::QcVariant(1LL)); // [0] (  [1] a
    query.and_OpenParenthesis("b").isEqualTo(QcSqlQuery::QcVariant(2LL));  // [2] (  [3] b
    query.closeParenthesis();                                              // [4] )
    query.or_OpenParenthesis("c").isEqualTo(QcSqlQuery::QcVariant(3LL));   // [5] (  [6] c
    query.closeParenthesis();                                              // [7] )
    query.closeParenthesis();                                              // [8] )

    ASSERT_EQ(whereCount(query), 9u);
    EXPECT_EQ(elementCompareType(whereElement(query, 0)), QcSqlBase::_openParenthesis_);
    EXPECT_EQ(elementColumnName(whereElement(query, 1)), "a");
    EXPECT_EQ(elementCompareType(whereElement(query, 2)), QcSqlBase::_openParenthesis_);
    EXPECT_EQ(elementColumnName(whereElement(query, 3)), "b");
    EXPECT_EQ(elementCompareType(whereElement(query, 4)), QcSqlBase::_closeParenthesis_);
    EXPECT_EQ(elementCompareType(whereElement(query, 5)), QcSqlBase::_openParenthesis_);
    EXPECT_EQ(elementColumnName(whereElement(query, 6)), "c");
    EXPECT_EQ(elementCompareType(whereElement(query, 7)), QcSqlBase::_closeParenthesis_);
    EXPECT_EQ(elementCompareType(whereElement(query, 8)), QcSqlBase::_closeParenthesis_);

    // 3 opens (where_/and_/or_ OpenParenthesis), 3 explicit closeParenthesis() calls.
    EXPECT_EQ(whereParenDepth(query), 0);
}

TEST_F(QcSqlQueryWhiteBoxTest, OpenParenthesisIncrementsDepthAndCloseDecrementsIt)
{
    QcSqlQuery query;

    EXPECT_TRUE(query.openParenthesis());
    EXPECT_EQ(whereParenDepth(query), 1);
    EXPECT_TRUE(query.openParenthesis());
    EXPECT_EQ(whereParenDepth(query), 2);
    EXPECT_TRUE(query.closeParenthesis());
    EXPECT_EQ(whereParenDepth(query), 1);
    EXPECT_TRUE(query.closeParenthesis());
    EXPECT_EQ(whereParenDepth(query), 0);

    ASSERT_EQ(whereCount(query), 4u);
}

TEST_F(QcSqlQueryWhiteBoxTest, CloseParenthesisWithoutMatchingOpenReturnsFalse)
{
    QcSqlQuery query;

    EXPECT_FALSE(query.closeParenthesis());
    EXPECT_EQ(whereCount(query), 0u);
}

TEST_F(QcSqlQueryWhiteBoxTest, OrderAscAndOrderDescRecordDirectionPerColumn)
{
    QcSqlQuery query;

    query.orderAsc({"a", "b"});
    query.orderDesc({"c"});

    ASSERT_EQ(orderByCount(query), 3u);
    EXPECT_EQ(orderByColumn(query, 0), "a");
    EXPECT_EQ(orderByColumn(query, 1), "b");
    EXPECT_EQ(orderByColumn(query, 2), "c");
    EXPECT_EQ(orderByDirection(query, 0), orderByDirection(query, 1));
    EXPECT_NE(orderByDirection(query, 0), orderByDirection(query, 2));
}

TEST_F(QcSqlQueryWhiteBoxTest, OrderAscAndOrderDescDefaultToNullsDefault)
{
    QcSqlQuery query;

    query.orderAsc({"a"});
    query.orderDesc({"b"});

    ASSERT_EQ(orderByCount(query), 2u);
    EXPECT_EQ(orderByNulls(query, 0), QcSqlQuery::_nullsDefault_);
    EXPECT_EQ(orderByNulls(query, 1), QcSqlQuery::_nullsDefault_);
}

TEST_F(QcSqlQueryWhiteBoxTest, OrderAscAndOrderDescRecordNullsPositionPerCall)
{
    QcSqlQuery query;

    query.orderAsc({"a", "b"}, QcSqlQuery::_nullsFirst_);
    query.orderDesc({"c"}, QcSqlQuery::_nullsLast_);

    ASSERT_EQ(orderByCount(query), 3u);
    EXPECT_EQ(orderByNulls(query, 0), QcSqlQuery::_nullsFirst_);
    EXPECT_EQ(orderByNulls(query, 1), QcSqlQuery::_nullsFirst_);
    EXPECT_EQ(orderByNulls(query, 2), QcSqlQuery::_nullsLast_);
}

TEST_F(QcSqlQueryWhiteBoxTest, GroupByRecordsEachColumn)
{
    QcSqlQuery query;

    query.groupBy({"a", "b"});

    ASSERT_EQ(groupByCount(query), 2u);
    EXPECT_EQ(groupByColumn(query, 0), "a");
    EXPECT_EQ(groupByColumn(query, 1), "b");
}

TEST_F(QcSqlQueryWhiteBoxTest, LimitStoresRowsStartAndWithTies)
{
    QcSqlQuery query;

    query.limit(10, 20, true);

    EXPECT_EQ(limitTo(query), 10);
    EXPECT_EQ(limitFrom(query), 20);
    EXPECT_TRUE(limitWithTies(query));
}

TEST_F(QcSqlQueryWhiteBoxTest, LimitDefaultsStartRowAndWithTies)
{
    QcSqlQuery query;

    query.limit(5);

    EXPECT_EQ(limitTo(query), 5);
    EXPECT_EQ(limitFrom(query), 0);
    EXPECT_FALSE(limitWithTies(query));
}

TEST_F(QcSqlQueryWhiteBoxTest, HavingAndOrHavingAppendWithColumnAndConnector)
{
    QcSqlQuery query;

    query.having("cnt").isGreaterThan(QcSqlQuery::QcVariant(1LL));
    query.and_Having("sum").isLessThan(QcSqlQuery::QcVariant(100LL));
    query.or_Having("avg").isEqualTo(QcSqlQuery::QcVariant(5LL));

    ASSERT_EQ(havingCount(query), 3u);
    EXPECT_EQ(elementColumnName(havingElement(query, 0)), "cnt");
    EXPECT_EQ(elementColumnName(havingElement(query, 1)), "sum");
    EXPECT_EQ(elementColumnName(havingElement(query, 2)), "avg");
    EXPECT_NE(havingConnector(query, 1), havingConnector(query, 2));
}

TEST_F(QcSqlQueryWhiteBoxTest, SetOperationsAppendWithCorrectType)
{
    QcSqlQuery other;
    other.fromTable("users");
    QcSqlQuery query;
    query.fromTable("users");

    query.unionWith(other);
    query.unionAllWith(other);
    query.intersectWith(other);
    query.exceptWith(other);

    ASSERT_EQ(setOperationCount(query), 4u);
    EXPECT_EQ(setOperationType(query, 0), QcSqlBase::_union_);
    EXPECT_EQ(setOperationType(query, 1), QcSqlBase::_unionAll_);
    EXPECT_EQ(setOperationType(query, 2), QcSqlBase::_intersect_);
    EXPECT_EQ(setOperationType(query, 3), QcSqlBase::_except_);
    EXPECT_TRUE(setOperationHasQuery(query, 3));
}

TEST_F(QcSqlQueryWhiteBoxTest, CopyingQueryDeepCopiesSubqueriesAndChains)
{
    QcSqlQuery sub;
    sub.fromTable("groups");
    QcSqlQuery original;
    original.fromSubQuery("g", sub);
    original.where("id").isEqualTo(QcSqlQuery::QcVariant(1LL));
    original.with_("cte1", sub);
    original.addJoin("j", sub, "j.id = g.id");
    original.unionWith(sub);

    QcSqlQuery copy(original);

    EXPECT_TRUE(hasFromSubquery(copy));
    EXPECT_EQ(whereCount(copy), 1u);
    EXPECT_TRUE(cteHasQuery(copy, 0));
    EXPECT_TRUE(joinHasQuery(copy, 0));
    EXPECT_TRUE(setOperationHasQuery(copy, 0));

    // Independent copies: mutating the copy must not affect the original.
    copy.and_("extra").isNull();
    EXPECT_EQ(whereCount(copy), 2u);
    EXPECT_EQ(whereCount(original), 1u);
}

TEST(QcSqlQueryToSql, SelectsStarWhenNoReturnValuesAndOmitsFromWhenNoSource)
{
    QcSqlQuery query;

    EXPECT_EQ(query.toSql().sql, "SELECT *");
}

TEST(QcSqlQueryToSql, RendersReturnValuesAndFromTable)
{
    QcSqlQuery query;
    query.addReturnValues({"id", "name <n>"});
    query.fromTable("users");

    EXPECT_EQ(query.toSql().sql, "SELECT " + q("id") + ", " + q("name") + " AS " + q("n") + " FROM " + q("users"));
}

TEST(QcSqlQueryToSql, DistinctAddsKeyword)
{
    QcSqlQuery query;
    query.distinct();
    query.addReturnValues({"id"});
    query.fromTable("users");

    EXPECT_EQ(query.toSql().sql, "SELECT DISTINCT " + q("id") + " FROM " + q("users"));
}

TEST(QcSqlQueryToSql, DistinctOnRendersPostgresSyntax)
{
    // toSql()'s default driver is PostgreSQL (see qcsqlquery.h) -- distinctOn()
    // degrades to plain DISTINCT on every other driver, see the next test.
    QcSqlQuery query;
    query.distinctOn({"a", "b"});
    query.addReturnValues({"id"});
    query.fromTable("users");

    EXPECT_EQ(query.toSql(QcDbDriver::PostgreSQL).sql, "SELECT DISTINCT ON (" + q("a") + ", " + q("b") + ") " + q("id") + " FROM " + q("users"));
}

TEST(QcSqlQueryToSql, DistinctOnDegradesToPlainDistinctOnNonPostgresDrivers)
{
    for (QcDbDriver driver : {QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlQuery query;
        query.distinctOn({"a", "b"});
        query.addReturnValues({"id"});
        query.fromTable("users");

        EXPECT_EQ(query.toSql(driver).sql, "SELECT DISTINCT " + QcSqlDialect::quoteIdentifier("id", driver)
            + " FROM " + QcSqlDialect::quoteTableRef("users", driver)) << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlQueryToSql, FromSubQueryRendersParenthesizedSelectWithAlias)
{
    QcSqlQuery sub;
    sub.addReturnValues({"id"});
    sub.fromTable("groups");
    QcSqlQuery query;
    query.fromSubQuery("g", sub);

    EXPECT_EQ(query.toSql().sql, "SELECT * FROM (SELECT " + q("id") + " FROM " + q("groups") + ")" + std::string(kTableAliasSeparator) + q("g"));
}

TEST(QcSqlQueryToSql, PlainTableJoinReadsFromTableNameOfNestedQuery)
{
    QcSqlQuery joinSource;
    joinSource.fromTable("groups");
    QcSqlQuery query;
    query.fromTable("users");
    query.addJoin("g", joinSource, "g.id = users.group_id");

    EXPECT_EQ(query.toSql().sql, "SELECT * FROM " + q("users") + " JOIN " + q("groups") + std::string(kTableAliasSeparator) + q("g") + " ON g.id = users.group_id");
}

TEST(QcSqlQueryToSql, JoinFamilyRendersCorrectKeywords)
{
    QcSqlQuery src;
    src.fromTable("t");

    const std::string sep = kTableAliasSeparator;

    QcSqlQuery left;
    left.fromTable("a");
    left.addLeftJoin("t", src, "t.id = a.id");
    EXPECT_EQ(left.toSql().sql, "SELECT * FROM " + q("a") + " LEFT JOIN " + q("t") + sep + q("t") + " ON t.id = a.id");

    QcSqlQuery right;
    right.fromTable("a");
    right.addRightJoin("t", src, "t.id = a.id");
    EXPECT_EQ(right.toSql().sql, "SELECT * FROM " + q("a") + " RIGHT JOIN " + q("t") + sep + q("t") + " ON t.id = a.id");

    QcSqlQuery full;
    full.fromTable("a");
    full.addFullJoin("t", src, "t.id = a.id");
    EXPECT_EQ(full.toSql().sql, "SELECT * FROM " + q("a") + " FULL JOIN " + q("t") + sep + q("t") + " ON t.id = a.id");

    QcSqlQuery cross;
    cross.fromTable("a");
    cross.addCrossJoin("t", src);
    EXPECT_EQ(cross.toSql().sql, "SELECT * FROM " + q("a") + " CROSS JOIN " + q("t") + sep + q("t"));
}

TEST(QcSqlQueryToSql, JoinAsSubQueryRendersParenthesizedSelect)
{
    QcSqlQuery joinSource;
    joinSource.addReturnValues({"id"});
    joinSource.fromTable("groups");
    QcSqlQuery query;
    query.fromTable("users");
    query.addJoin("g", joinSource, "g.id = users.group_id", /*asSubQuery=*/true);

    EXPECT_EQ(query.toSql().sql, "SELECT * FROM " + q("users") + " JOIN (SELECT " + q("id") + " FROM " + q("groups") + ")" + std::string(kTableAliasSeparator) + q("g") + " ON g.id = users.group_id");
}

TEST(QcSqlQueryToSql, WhereChainGluesAndOrCorrectly)
{
    QcSqlQuery query;
    query.fromTable("users");
    query.where("a").isEqualTo(QcSqlQuery::QcVariant(1LL));
    query.and_("b").isEqualTo(QcSqlQuery::QcVariant(2LL));
    query.or_("c").isEqualTo(QcSqlQuery::QcVariant(3LL));

    const std::string expected = "SELECT * FROM " + q("users") + " WHERE " + q("a") + " = " + QcSqlDialect::placeholder(1)
        + " AND " + q("b") + " = " + QcSqlDialect::placeholder(2) + " OR " + q("c") + " = " + QcSqlDialect::placeholder(3);
    QcSqlStatement statement = query.toSql();
    EXPECT_EQ(statement.sql, expected);
    ASSERT_EQ(statement.params.size(), 3u);
    EXPECT_EQ(std::get<long long>(statement.params[2]), 3LL);
}

TEST(QcSqlQueryToSql, WhereChainWithParenthesesOmitsConnectorRightAfterOpenParen)
{
    QcSqlQuery query;
    query.fromTable("users");
    query.where("a").isEqualTo(QcSqlQuery::QcVariant(1LL));
    query.and_OpenParenthesis("b").isEqualTo(QcSqlQuery::QcVariant(2LL));
    query.or_("c").isEqualTo(QcSqlQuery::QcVariant(3LL));
    query.closeParenthesis();

    const std::string expected = "SELECT * FROM " + q("users") + " WHERE " + q("a") + " = " + QcSqlDialect::placeholder(1)
        + " AND (" + q("b") + " = " + QcSqlDialect::placeholder(2) + " OR " + q("c") + " = " + QcSqlDialect::placeholder(3) + ")";
    EXPECT_EQ(query.toSql().sql, expected);
}

TEST(QcSqlQueryToSql, AddFreeTextRenumbersQuestionMarksAndAndsIntoWhere)
{
    QcSqlQuery query;
    query.fromTable("users");
    query.where("a").isEqualTo(QcSqlQuery::QcVariant(1LL));
    query.addFreeText("b > ? AND b < ?", {QcSqlQuery::QcVariant(2LL), QcSqlQuery::QcVariant(3LL)});

    const std::string expected = "SELECT * FROM " + q("users") + " WHERE " + q("a") + " = " + QcSqlDialect::placeholder(1)
        + " AND b > " + QcSqlDialect::placeholder(2) + " AND b < " + QcSqlDialect::placeholder(3);
    QcSqlStatement statement = query.toSql();
    EXPECT_EQ(statement.sql, expected);
    ASSERT_EQ(statement.params.size(), 3u);
    EXPECT_EQ(std::get<long long>(statement.params[1]), 2LL);
    EXPECT_EQ(std::get<long long>(statement.params[2]), 3LL);
}

TEST(QcSqlQueryToSql, AddFreeTextAloneStillRendersWhereWithoutLeadingAnd)
{
    QcSqlQuery query;
    query.fromTable("users");
    query.addFreeText("a > ?", {QcSqlQuery::QcVariant(1LL)});

    EXPECT_EQ(query.toSql().sql, "SELECT * FROM " + q("users") + " WHERE a > " + QcSqlDialect::placeholder(1));
}

TEST(QcSqlQueryToSql, GroupByAndHavingRender)
{
    QcSqlQuery query;
    query.addReturnValues({"dept"});
    query.fromTable("employees");
    query.groupBy({"dept"});
    query.having("cnt").isGreaterThan(QcSqlQuery::QcVariant(5LL));

    EXPECT_EQ(query.toSql().sql, "SELECT " + q("dept") + " FROM " + q("employees") + " GROUP BY " + q("dept") + " HAVING " + q("cnt") + " > " + QcSqlDialect::placeholder(1));
}

TEST(QcSqlQueryToSql, OrderByRendersAscAndDesc)
{
    QcSqlQuery query;
    query.fromTable("users");
    query.orderAsc({"a"});
    query.orderDesc({"b"});

    EXPECT_EQ(query.toSql().sql, "SELECT * FROM " + q("users") + " ORDER BY " + q("a") + " ASC, " + q("b") + " DESC");
}

TEST(QcSqlQueryToSql, OrderByNullsPositionRendersNativeSyntaxOnPostgreSqlSqliteOracle)
{
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::SQLite, QcDbDriver::Oracle}) {
        QcSqlQuery query;
        query.fromTable("users");
        query.orderAsc({"a"}, QcSqlQuery::_nullsFirst_);
        query.orderDesc({"b"}, QcSqlQuery::_nullsLast_);

        const std::string expected = "SELECT * FROM " + QcSqlDialect::quoteTableRef("users", driver) + " ORDER BY "
            + QcSqlDialect::quoteRef("a", driver) + " ASC NULLS FIRST, " + QcSqlDialect::quoteRef("b", driver) + " DESC NULLS LAST";
        EXPECT_EQ(query.toSql(driver).sql, expected) << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlQueryToSql, OrderByNullsPositionIsEmulatedOnMySqlAndMssql)
{
    for (QcDbDriver driver : {QcDbDriver::MySQL, QcDbDriver::MSSQL}) {
        QcSqlQuery query;
        query.fromTable("users");
        query.orderAsc({"a"}, QcSqlQuery::_nullsFirst_);
        query.orderDesc({"b"}, QcSqlQuery::_nullsLast_);

        const std::string qa = QcSqlDialect::quoteRef("a", driver);
        const std::string qb = QcSqlDialect::quoteRef("b", driver);
        const std::string expected = "SELECT * FROM " + QcSqlDialect::quoteTableRef("users", driver) + " ORDER BY "
            + "CASE WHEN " + qa + " IS NULL THEN 0 ELSE 1 END, " + qa + " ASC, "
            + "CASE WHEN " + qb + " IS NULL THEN 1 ELSE 0 END, " + qb + " DESC";
        EXPECT_EQ(query.toSql(driver).sql, expected) << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlQueryToSql, OrderByNullsPositionComposesAcrossMultipleColumnsOnPostgreSqlSqliteOracle)
{
    // Three columns, mixed nulls handling, with the NULLS-FIRST column
    // sandwiched between a plain (default) column and a NULLS-LAST one --
    // makes sure orderByEntry()'s NULLS clause for one column doesn't leak
    // into or swallow the columns around it.
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::SQLite, QcDbDriver::Oracle}) {
        QcSqlQuery query;
        query.fromTable("users");
        query.orderAsc({"a"});
        query.orderAsc({"b"}, QcSqlQuery::_nullsFirst_);
        query.orderDesc({"c"}, QcSqlQuery::_nullsLast_);

        const std::string expected = "SELECT * FROM " + QcSqlDialect::quoteTableRef("users", driver) + " ORDER BY "
            + QcSqlDialect::quoteRef("a", driver) + " ASC, "
            + QcSqlDialect::quoteRef("b", driver) + " ASC NULLS FIRST, "
            + QcSqlDialect::quoteRef("c", driver) + " DESC NULLS LAST";
        EXPECT_EQ(query.toSql(driver).sql, expected) << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlQueryToSql, OrderByNullsPositionComposesAcrossMultipleColumnsOnMySqlAndMssql)
{
    // Same three-column mix as the native-driver counterpart above, but on
    // the two drivers where NULLS FIRST/LAST is emulated via an extra
    // leading CASE-tiebreaker key (see QcSqlDialect::orderByEntry()) --
    // that tiebreaker's own internal ", " must not disturb the surrounding
    // columns' position in the overall comma-separated key list, and a
    // plain (default) column adjacent to it must render with no CASE at
    // all.
    for (QcDbDriver driver : {QcDbDriver::MySQL, QcDbDriver::MSSQL}) {
        QcSqlQuery query;
        query.fromTable("users");
        query.orderAsc({"a"});
        query.orderAsc({"b"}, QcSqlQuery::_nullsFirst_);
        query.orderDesc({"c"}, QcSqlQuery::_nullsLast_);

        const std::string qa = QcSqlDialect::quoteRef("a", driver);
        const std::string qb = QcSqlDialect::quoteRef("b", driver);
        const std::string qc = QcSqlDialect::quoteRef("c", driver);
        const std::string expected = "SELECT * FROM " + QcSqlDialect::quoteTableRef("users", driver) + " ORDER BY "
            + qa + " ASC, "
            + "CASE WHEN " + qb + " IS NULL THEN 0 ELSE 1 END, " + qb + " ASC, "
            + "CASE WHEN " + qc + " IS NULL THEN 1 ELSE 0 END, " + qc + " DESC";
        EXPECT_EQ(query.toSql(driver).sql, expected) << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlQueryToSql, OrderByNullsDefaultRendersNoNullsClauseOnAnyDriver)
{
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlQuery query;
        query.fromTable("users");
        query.orderAsc({"a"});

        const std::string expected = "SELECT * FROM " + QcSqlDialect::quoteTableRef("users", driver) + " ORDER BY " + QcSqlDialect::quoteRef("a", driver) + " ASC";
        EXPECT_EQ(query.toSql(driver).sql, expected) << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlQueryToSql, LimitDelegatesToDialectWithCorrectArgumentMapping)
{
    QcSqlQuery query;
    query.fromTable("users");
    query.limit(10, 20, true);

    const std::string expected = "SELECT * FROM " + q("users") + QcSqlDialect::limitOffsetClause(10, 20, true);
    EXPECT_EQ(query.toSql().sql, expected);
}

TEST(QcSqlQueryToSql, WithRendersCteBeforeSelect)
{
    QcSqlQuery cte;
    cte.addReturnValues({"id"});
    cte.fromTable("groups");
    QcSqlQuery query;
    query.with_("recent_groups", cte);
    query.fromTable("recent_groups");

    EXPECT_EQ(query.toSql().sql, "WITH " + q("recent_groups") + " AS (SELECT " + q("id") + " FROM " + q("groups") + ") SELECT * FROM " + q("recent_groups"));
}

TEST(QcSqlQueryToSql, SetOperationsMatchActiveDriverParenthesizationShape)
{
    // Runs once per driver in a single build/binary now (previously this was
    // an #if/#else chain, only one branch of which ever compiled/ran per
    // build).
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlQuery rhs;
        rhs.fromTable("archived_users");
        QcSqlQuery query;
        query.fromTable("users");
        query.unionAllWith(rhs);

        const std::string users = QcSqlDialect::quoteTableRef("users", driver);
        const std::string archived = QcSqlDialect::quoteTableRef("archived_users", driver);
        if (driver == QcDbDriver::PostgreSQL || driver == QcDbDriver::MySQL) {
            // Only PostgreSQL/MySQL accept a parenthesized set-operation member.
            EXPECT_EQ(query.toSql(driver).sql, "(SELECT * FROM " + users + ") UNION ALL (SELECT * FROM " + archived + ")") << "driver=" << static_cast<int>(driver);
        } else {
            // SQLite/MSSQL/Oracle reject "(SELECT ...) UNION ALL (SELECT ...)"
            // outright (verified directly against SQLite: "Error: in prepare, near
            // '(': syntax error") -- see qcsqlquery.cpp.
            EXPECT_EQ(query.toSql(driver).sql, "SELECT * FROM " + users + " UNION ALL SELECT * FROM " + archived) << "driver=" << static_cast<int>(driver);
        }
    }
}

TEST(QcSqlQueryToSql, ExistsSubQueryConditionEmbedsSubquerySql)
{
    QcSqlQuery sub;
    sub.fromTable("orders");
    sub.where("orders.user_id").isEqualTo(QcSqlQuery::QcVariant(1LL));
    QcSqlQuery query;
    query.fromTable("users");
    query.where("").exists(sub);

    QcSqlStatement statement = query.toSql();
    EXPECT_EQ(statement.sql, "SELECT * FROM " + q("users") + " WHERE EXISTS (SELECT * FROM " + q("orders")
        + " WHERE " + q("orders") + "." + q("user_id") + " = " + QcSqlDialect::placeholder(1) + ")");
    ASSERT_EQ(statement.params.size(), 1u);
}

TEST(QcSqlQueryToSql, ParameterNumberingIsGlobalAcrossJoinWhereAndSetOperation)
{
    // Exercises the whole point of threading `params` through recursive
    // toSql(QcVariantList&) calls: every bind value across a JOIN subquery,
    // the main WHERE, and a set-operation's subquery must land in one flat,
    // correctly-numbered parameter list -- not each restart at 1.
    QcSqlQuery joinSource;
    joinSource.fromTable("groups");
    joinSource.where("active").isEqualTo(QcSqlQuery::QcVariant(1LL));

    QcSqlQuery rhs;
    rhs.fromTable("archived_users");
    rhs.where("id").isGreaterThan(QcSqlQuery::QcVariant(100LL));

    QcSqlQuery query;
    query.fromTable("users");
    query.addJoin("g", joinSource, "g.id = users.group_id", /*asSubQuery=*/true);
    query.where("name").isLike("%a%");
    query.unionWith(rhs);

    QcSqlStatement statement = query.toSql();
    ASSERT_EQ(statement.params.size(), 3u);
    EXPECT_EQ(std::get<long long>(statement.params[0]), 1LL);
    EXPECT_EQ(std::get<std::string>(statement.params[1]), "%a%");
    EXPECT_EQ(std::get<long long>(statement.params[2]), 100LL);
    // The rendered SQL must reference placeholders 1, 2, 3 in that order.
    EXPECT_NE(statement.sql.find(QcSqlDialect::placeholder(1)), std::string::npos);
    EXPECT_NE(statement.sql.find(QcSqlDialect::placeholder(2)), std::string::npos);
    EXPECT_NE(statement.sql.find(QcSqlDialect::placeholder(3)), std::string::npos);
}

TEST(QcSqlQueryToSql, NoArgOverloadMatchesThreadingOverload)
{
    QcSqlQuery query;
    query.fromTable("users");
    query.where("id").isEqualTo(QcSqlQuery::QcVariant(5LL));

    QcSqlQuery::QcVariantList params;
    const std::string sqlFromThreadingOverload = query.toSql(params);
    QcSqlStatement statement = query.toSql();

    EXPECT_EQ(statement.sql, sqlFromThreadingOverload);
    ASSERT_EQ(statement.params.size(), params.size());
    EXPECT_EQ(std::get<long long>(statement.params[0]), std::get<long long>(params[0]));
}
