#include <gtest/gtest.h>

#include "query/qcsqldialect.h"
#include "query/qcsqlquery.h"
#include "query/qcsqlqueryelement.h"

// Shorthand for QcSqlDialect::quoteIdentifier() -- see the identical helper
// (and its rationale) in test_qcsqlquery.cpp.
namespace {
std::string q(const std::string & name)
{
    return QcSqlDialect::quoteIdentifier(name);
}
} // namespace

// See test_qcsqlqueryvalue.cpp for why this is a friend fixture rather than
// public getters: toSql() (exercised separately below) is the public way to
// observe rendering, but constructors/comparators still need to be verified
// against real state, not just a bool return value.
class QcSqlQueryElementWhiteBoxTest : public ::testing::Test
{
protected:
    static int compareType(const QcSqlQueryElement & e) { return e.m_compareType; }
    static const std::string & columnName(const QcSqlQueryElement & e) { return e.m_columnName; }
    static const QcSqlQueryElement::QcVariant & value(const QcSqlQueryElement & e) { return e.m_value; }
    static const QcSqlQueryElement::QcVariantList & values(const QcSqlQueryElement & e) { return e.m_values; }
    static bool hasSubQuery(const QcSqlQueryElement & e) { return static_cast<bool>(e.m_subQuery); }
    static int functionType(const QcSqlQueryElement & e) { return e.m_functionType; }
    static const std::vector<QcSqlQueryElement::QcVariant> & functionParams(const QcSqlQueryElement & e) { return e.m_functionParams; }
    static bool isJsonComparison(const QcSqlQueryElement & e) { return e.m_isJsonComparison; }
    static int jsonValueKind(const QcSqlQueryElement & e) { return e.m_jsonValueKind; }
    static const std::string & jsonSearchPath(const QcSqlQueryElement & e) { return e.m_jsonSearchPath; }
};

TEST_F(QcSqlQueryElementWhiteBoxTest, DefaultConstructorHasEqualToAndNoColumn)
{
    QcSqlQueryElement element;

    EXPECT_EQ(compareType(element), QcSqlBase::_isEqualTo_);
    EXPECT_TRUE(columnName(element).empty());
}

TEST_F(QcSqlQueryElementWhiteBoxTest, ColumnConstructorSetsColumnKeepsDefaultCompareType)
{
    QcSqlQueryElement element("id");

    EXPECT_EQ(columnName(element), "id");
    EXPECT_EQ(compareType(element), QcSqlBase::_isEqualTo_);
}

TEST_F(QcSqlQueryElementWhiteBoxTest, MarkerConstructorSetsCompareTypeAndNoColumn)
{
    QcSqlQueryElement open(QcSqlBase::_openParenthesis_);
    QcSqlQueryElement close(QcSqlBase::_closeParenthesis_);

    EXPECT_EQ(compareType(open), QcSqlBase::_openParenthesis_);
    EXPECT_TRUE(columnName(open).empty());
    EXPECT_EQ(compareType(close), QcSqlBase::_closeParenthesis_);
}

TEST_F(QcSqlQueryElementWhiteBoxTest, ValueConstructorSetsCompareTypeColumnAndValue)
{
    QcSqlQueryElement element(QcSqlBase::_isGreaterThan_, "age", QcSqlQueryElement::QcVariant(18LL));

    EXPECT_EQ(compareType(element), QcSqlBase::_isGreaterThan_);
    EXPECT_EQ(columnName(element), "age");
    EXPECT_EQ(std::get<long long>(value(element)), 18LL);
}

TEST_F(QcSqlQueryElementWhiteBoxTest, SubQueryConstructorSetsCompareTypeColumnAndOwnsSubQuery)
{
    QcSqlQuery sub;
    sub.fromTable("groups");

    QcSqlQueryElement element(QcSqlBase::_isIn_, "group_id", sub);

    EXPECT_EQ(compareType(element), QcSqlBase::_isIn_);
    EXPECT_EQ(columnName(element), "group_id");
    EXPECT_TRUE(hasSubQuery(element));
}

TEST_F(QcSqlQueryElementWhiteBoxTest, IsLikeFamilySetsCompareTypeAndValue)
{
    QcSqlQueryElement likeEl("name");
    EXPECT_TRUE(likeEl.isLike("%a%"));
    EXPECT_EQ(compareType(likeEl), QcSqlBase::_isLike_);
    EXPECT_EQ(std::get<std::string>(value(likeEl)), "%a%");

    QcSqlQueryElement ilikeEl("name");
    EXPECT_TRUE(ilikeEl.isIlike("%a%"));
    EXPECT_EQ(compareType(ilikeEl), QcSqlBase::_isILike_);

    QcSqlQueryElement notLikeEl("name");
    EXPECT_TRUE(notLikeEl.isNotLike("%a%"));
    EXPECT_EQ(compareType(notLikeEl), QcSqlBase::_isNotLike_);

    QcSqlQueryElement notIlikeEl("name");
    EXPECT_TRUE(notIlikeEl.isNotILike("%a%"));
    EXPECT_EQ(compareType(notIlikeEl), QcSqlBase::_isNotILike_);
}

TEST_F(QcSqlQueryElementWhiteBoxTest, IsEqualToWithValueSetsValueNotSubQuery)
{
    QcSqlQueryElement element("id");

    EXPECT_TRUE(element.isEqualTo(QcSqlQueryElement::QcVariant(5LL)));

    EXPECT_EQ(compareType(element), QcSqlBase::_isEqualTo_);
    EXPECT_EQ(std::get<long long>(value(element)), 5LL);
    EXPECT_FALSE(hasSubQuery(element));
}

TEST_F(QcSqlQueryElementWhiteBoxTest, IsEqualToWithSubQuerySetsSubQueryNotValue)
{
    QcSqlQuery sub;
    sub.fromTable("groups");
    QcSqlQueryElement element("group_id");

    EXPECT_TRUE(element.isEqualTo(sub));

    EXPECT_EQ(compareType(element), QcSqlBase::_isEqualTo_);
    EXPECT_TRUE(hasSubQuery(element));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(value(element)));
}

TEST_F(QcSqlQueryElementWhiteBoxTest, IsNotEqualToFamily)
{
    QcSqlQuery sub;
    sub.fromTable("groups");

    QcSqlQueryElement byValue("id");
    EXPECT_TRUE(byValue.isNotEqualTo(QcSqlQueryElement::QcVariant(5LL)));
    EXPECT_EQ(compareType(byValue), QcSqlBase::_isNotEqualTo_);

    QcSqlQueryElement bySubQuery("id");
    EXPECT_TRUE(bySubQuery.isNotEqualTo(sub));
    EXPECT_EQ(compareType(bySubQuery), QcSqlBase::_isNotEqualTo_);
    EXPECT_TRUE(hasSubQuery(bySubQuery));
}

TEST_F(QcSqlQueryElementWhiteBoxTest, OrderingComparators)
{
    QcSqlQueryElement gt("age");
    EXPECT_TRUE(gt.isGreaterThan(QcSqlQueryElement::QcVariant(1LL)));
    EXPECT_EQ(compareType(gt), QcSqlBase::_isGreaterThan_);

    QcSqlQueryElement gte("age");
    EXPECT_TRUE(gte.isGreaterThanOrEqualTo(QcSqlQueryElement::QcVariant(1LL)));
    EXPECT_EQ(compareType(gte), QcSqlBase::_isGreaterThanOrEqualTo_);

    QcSqlQueryElement lt("age");
    EXPECT_TRUE(lt.isLessThan(QcSqlQueryElement::QcVariant(1LL)));
    EXPECT_EQ(compareType(lt), QcSqlBase::_isLessThan_);

    QcSqlQueryElement lte("age");
    EXPECT_TRUE(lte.isLessThanOrEqualTo(QcSqlQueryElement::QcVariant(1LL)));
    EXPECT_EQ(compareType(lte), QcSqlBase::_isLessThanOrEqualTo_);
}

TEST_F(QcSqlQueryElementWhiteBoxTest, IsNullFamilyClearsValue)
{
    QcSqlQueryElement isNullEl("deleted_at");
    isNullEl.isEqualTo(QcSqlQueryElement::QcVariant(1LL)); // pre-seed a value
    EXPECT_TRUE(isNullEl.isNull());
    EXPECT_EQ(compareType(isNullEl), QcSqlBase::_isNull_);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(value(isNullEl)));

    QcSqlQueryElement isNotNullEl("deleted_at");
    EXPECT_TRUE(isNotNullEl.isNotNull());
    EXPECT_EQ(compareType(isNotNullEl), QcSqlBase::_isNotNull_);
}

TEST_F(QcSqlQueryElementWhiteBoxTest, IsInFamily)
{
    QcSqlQuery sub;
    sub.fromTable("groups");
    QcSqlQueryElement::QcVariantList list{QcSqlQueryElement::QcVariant(1LL), QcSqlQueryElement::QcVariant(2LL)};

    QcSqlQueryElement inList("id");
    EXPECT_TRUE(inList.isIn(list));
    EXPECT_EQ(compareType(inList), QcSqlBase::_isIn_);
    EXPECT_EQ(values(inList).size(), 2u);

    QcSqlQueryElement inSub("id");
    EXPECT_TRUE(inSub.isIn(sub));
    EXPECT_EQ(compareType(inSub), QcSqlBase::_isIn_);
    EXPECT_TRUE(hasSubQuery(inSub));

    QcSqlQueryElement notInList("id");
    EXPECT_TRUE(notInList.isNotIn(list));
    EXPECT_EQ(compareType(notInList), QcSqlBase::_isNotIn_);
    EXPECT_EQ(values(notInList).size(), 2u);

    QcSqlQueryElement notInSub("id");
    EXPECT_TRUE(notInSub.isNotIn(sub));
    EXPECT_EQ(compareType(notInSub), QcSqlBase::_isNotIn_);
    EXPECT_TRUE(hasSubQuery(notInSub));
}

TEST_F(QcSqlQueryElementWhiteBoxTest, IsBetweenFamilyStoresBothOperands)
{
    QcSqlQueryElement between("age");
    EXPECT_TRUE(between.isBetween(QcSqlQueryElement::QcVariant(1LL), QcSqlQueryElement::QcVariant(10LL)));
    EXPECT_EQ(compareType(between), QcSqlBase::_isBetween_);
    ASSERT_EQ(values(between).size(), 2u);
    EXPECT_EQ(std::get<long long>(values(between)[0]), 1LL);
    EXPECT_EQ(std::get<long long>(values(between)[1]), 10LL);

    QcSqlQueryElement notBetween("age");
    EXPECT_TRUE(notBetween.isNotBetween(QcSqlQueryElement::QcVariant(1LL), QcSqlQueryElement::QcVariant(10LL)));
    EXPECT_EQ(compareType(notBetween), QcSqlBase::_isNotBetween_);
    EXPECT_EQ(values(notBetween).size(), 2u);
}

TEST_F(QcSqlQueryElementWhiteBoxTest, IsDistinctFromFamily)
{
    QcSqlQuery sub;
    sub.fromTable("groups");

    QcSqlQueryElement distinctVal("id");
    EXPECT_TRUE(distinctVal.isDistinctFrom(QcSqlQueryElement::QcVariant(1LL)));
    EXPECT_EQ(compareType(distinctVal), QcSqlBase::_isDistinctFrom_);

    QcSqlQueryElement distinctSub("id");
    EXPECT_TRUE(distinctSub.isDistinctFrom(sub));
    EXPECT_TRUE(hasSubQuery(distinctSub));

    QcSqlQueryElement notDistinctVal("id");
    EXPECT_TRUE(notDistinctVal.isNotDistinctFrom(QcSqlQueryElement::QcVariant(1LL)));
    EXPECT_EQ(compareType(notDistinctVal), QcSqlBase::_isNotDistinctFrom_);

    QcSqlQueryElement notDistinctSub("id");
    EXPECT_TRUE(notDistinctSub.isNotDistinctFrom(sub));
    EXPECT_TRUE(hasSubQuery(notDistinctSub));
}

TEST_F(QcSqlQueryElementWhiteBoxTest, ExistsFamilySetsCompareTypeAndSubQuery)
{
    QcSqlQuery sub;
    sub.fromTable("groups");

    QcSqlQueryElement existsEl("");
    EXPECT_TRUE(existsEl.exists(sub));
    EXPECT_EQ(compareType(existsEl), QcSqlBase::_exists_);
    EXPECT_TRUE(hasSubQuery(existsEl));

    QcSqlQueryElement notExistsEl("");
    EXPECT_TRUE(notExistsEl.notExists(sub));
    EXPECT_EQ(compareType(notExistsEl), QcSqlBase::_notExists_);
    EXPECT_TRUE(hasSubQuery(notExistsEl));
}

TEST_F(QcSqlQueryElementWhiteBoxTest, CastWithValueSetsFunctionAndEqualToComparison)
{
    QcSqlQueryElement element("amount");

    EXPECT_TRUE(element.cast(QcSqlBase::_string_, QcSqlBase::_float_, QcSqlQueryElement::QcVariant(std::string("1.5"))));

    EXPECT_EQ(functionType(element), QcSqlBase::_cast_);
    ASSERT_EQ(functionParams(element).size(), 2u);
    EXPECT_EQ(std::get<long long>(functionParams(element)[0]), QcSqlBase::_string_);
    EXPECT_EQ(std::get<long long>(functionParams(element)[1]), QcSqlBase::_float_);
    EXPECT_EQ(compareType(element), QcSqlBase::_isEqualTo_);
    EXPECT_EQ(std::get<std::string>(value(element)), "1.5");
}

TEST_F(QcSqlQueryElementWhiteBoxTest, CastWithSubQuerySetsFunctionAndSubQuery)
{
    QcSqlQuery sub;
    sub.fromTable("groups");
    QcSqlQueryElement element("amount");

    EXPECT_TRUE(element.cast(QcSqlBase::_string_, QcSqlBase::_float_, sub));

    EXPECT_EQ(functionType(element), QcSqlBase::_cast_);
    EXPECT_EQ(compareType(element), QcSqlBase::_isEqualTo_);
    EXPECT_TRUE(hasSubQuery(element));
}

TEST_F(QcSqlQueryElementWhiteBoxTest, JsonNumberFamilySetsCompareTypeValueAndJsonState)
{
    QcSqlQueryElement eq("metadata");
    EXPECT_TRUE(eq.isEqualToJsonNumber(QcSqlQueryElement::QcVariant(5LL), "level"));
    EXPECT_EQ(compareType(eq), QcSqlBase::_isEqualTo_);
    EXPECT_EQ(std::get<long long>(value(eq)), 5LL);
    EXPECT_TRUE(isJsonComparison(eq));
    EXPECT_EQ(jsonValueKind(eq), QcSqlBase::_jsonAsNumber_);
    EXPECT_EQ(jsonSearchPath(eq), "level");

    QcSqlQueryElement ne("metadata");
    EXPECT_TRUE(ne.isNotEqualToJsonNumber(QcSqlQueryElement::QcVariant(5LL), "level"));
    EXPECT_EQ(compareType(ne), QcSqlBase::_isNotEqualTo_);
    EXPECT_TRUE(isJsonComparison(ne));

    QcSqlQueryElement gt("metadata");
    EXPECT_TRUE(gt.isGreaterThanJsonNumber(QcSqlQueryElement::QcVariant(5LL), "rating"));
    EXPECT_EQ(compareType(gt), QcSqlBase::_isGreaterThan_);
    EXPECT_EQ(jsonSearchPath(gt), "rating");

    QcSqlQueryElement gte("metadata");
    EXPECT_TRUE(gte.isGreaterThanOrEqualToJsonNumber(QcSqlQueryElement::QcVariant(5LL), "rating"));
    EXPECT_EQ(compareType(gte), QcSqlBase::_isGreaterThanOrEqualTo_);

    QcSqlQueryElement lt("metadata");
    EXPECT_TRUE(lt.isLessThanJsonNumber(QcSqlQueryElement::QcVariant(5LL), "rating"));
    EXPECT_EQ(compareType(lt), QcSqlBase::_isLessThan_);

    QcSqlQueryElement lte("metadata");
    EXPECT_TRUE(lte.isLessThanOrEqualToJsonNumber(QcSqlQueryElement::QcVariant(5LL), "rating"));
    EXPECT_EQ(compareType(lte), QcSqlBase::_isLessThanOrEqualTo_);
    EXPECT_EQ(jsonValueKind(lte), QcSqlBase::_jsonAsNumber_);
}

TEST_F(QcSqlQueryElementWhiteBoxTest, JsonTextFamilySetsCompareTypeValueAndJsonState)
{
    QcSqlQueryElement eq("metadata");
    EXPECT_TRUE(eq.isEqualToJsonText(QcSqlQueryElement::QcVariant(std::string("c++")), "skills[0]"));
    EXPECT_EQ(compareType(eq), QcSqlBase::_isEqualTo_);
    EXPECT_EQ(std::get<std::string>(value(eq)), "c++");
    EXPECT_TRUE(isJsonComparison(eq));
    EXPECT_EQ(jsonValueKind(eq), QcSqlBase::_jsonAsText_);
    EXPECT_EQ(jsonSearchPath(eq), "skills[0]");

    QcSqlQueryElement ne("metadata");
    EXPECT_TRUE(ne.isNotEqualToJsonText(QcSqlQueryElement::QcVariant(std::string("c++")), "skills[0]"));
    EXPECT_EQ(compareType(ne), QcSqlBase::_isNotEqualTo_);

    QcSqlQueryElement gt("metadata");
    EXPECT_TRUE(gt.isGreaterThanJsonText(QcSqlQueryElement::QcVariant(std::string("a")), "skills[0]"));
    EXPECT_EQ(compareType(gt), QcSqlBase::_isGreaterThan_);

    QcSqlQueryElement gte("metadata");
    EXPECT_TRUE(gte.isGreaterThanOrEqualToJsonText(QcSqlQueryElement::QcVariant(std::string("a")), "skills[0]"));
    EXPECT_EQ(compareType(gte), QcSqlBase::_isGreaterThanOrEqualTo_);

    QcSqlQueryElement lt("metadata");
    EXPECT_TRUE(lt.isLessThanJsonText(QcSqlQueryElement::QcVariant(std::string("z")), "skills[0]"));
    EXPECT_EQ(compareType(lt), QcSqlBase::_isLessThan_);

    QcSqlQueryElement lte("metadata");
    EXPECT_TRUE(lte.isLessThanOrEqualToJsonText(QcSqlQueryElement::QcVariant(std::string("z")), "skills[0]"));
    EXPECT_EQ(compareType(lte), QcSqlBase::_isLessThanOrEqualTo_);
    EXPECT_EQ(jsonValueKind(lte), QcSqlBase::_jsonAsText_);
}

TEST_F(QcSqlQueryElementWhiteBoxTest, JsonLikeTextFamilySetsCompareTypeAndJsonState)
{
    QcSqlQueryElement likeEl("metadata");
    EXPECT_TRUE(likeEl.isLikeJsonText("c%", "skills[0]"));
    EXPECT_EQ(compareType(likeEl), QcSqlBase::_isLike_);
    EXPECT_EQ(std::get<std::string>(value(likeEl)), "c%");
    EXPECT_TRUE(isJsonComparison(likeEl));
    EXPECT_EQ(jsonValueKind(likeEl), QcSqlBase::_jsonAsText_);
    EXPECT_EQ(jsonSearchPath(likeEl), "skills[0]");

    QcSqlQueryElement ilikeEl("metadata");
    EXPECT_TRUE(ilikeEl.isIlikeJsonText("c%", "skills[0]"));
    EXPECT_EQ(compareType(ilikeEl), QcSqlBase::_isILike_);

    QcSqlQueryElement notLikeEl("metadata");
    EXPECT_TRUE(notLikeEl.isNotLikeJsonText("c%", "skills[0]"));
    EXPECT_EQ(compareType(notLikeEl), QcSqlBase::_isNotLike_);

    QcSqlQueryElement notIlikeEl("metadata");
    EXPECT_TRUE(notIlikeEl.isNotILikeJsonText("c%", "skills[0]"));
    EXPECT_EQ(compareType(notIlikeEl), QcSqlBase::_isNotILike_);
}

TEST_F(QcSqlQueryElementWhiteBoxTest, JsonLikeArrayAsTextFamilySetsCompareTypeAndRawKind)
{
    QcSqlQueryElement likeEl("metadata");
    EXPECT_TRUE(likeEl.isLikeJsonArrayAsText("%\"python\"%", "skills"));
    EXPECT_EQ(compareType(likeEl), QcSqlBase::_isLike_);
    EXPECT_EQ(std::get<std::string>(value(likeEl)), "%\"python\"%");
    EXPECT_TRUE(isJsonComparison(likeEl));
    EXPECT_EQ(jsonValueKind(likeEl), QcSqlBase::_jsonAsRaw_);
    EXPECT_EQ(jsonSearchPath(likeEl), "skills");

    QcSqlQueryElement ilikeEl("metadata");
    EXPECT_TRUE(ilikeEl.isIlikeJsonArrayAsText("%\"python\"%", "skills"));
    EXPECT_EQ(compareType(ilikeEl), QcSqlBase::_isILike_);
    EXPECT_EQ(jsonValueKind(ilikeEl), QcSqlBase::_jsonAsRaw_);
}

TEST_F(QcSqlQueryElementWhiteBoxTest, LaterOrdinaryComparatorClearsJsonState)
{
    QcSqlQueryElement element("metadata");
    element.isEqualToJsonNumber(QcSqlQueryElement::QcVariant(5LL), "level");
    ASSERT_TRUE(isJsonComparison(element));

    element.isEqualTo(QcSqlQueryElement::QcVariant(1LL));
    EXPECT_FALSE(isJsonComparison(element));
    EXPECT_TRUE(jsonSearchPath(element).empty());
}

TEST_F(QcSqlQueryElementWhiteBoxTest, LaterJsonComparatorOverwritesPreviousJsonState)
{
    QcSqlQueryElement element("metadata");
    element.isEqualToJsonNumber(QcSqlQueryElement::QcVariant(5LL), "level");
    element.isLikeJsonText("c%", "skills[0]");

    EXPECT_EQ(compareType(element), QcSqlBase::_isLike_);
    EXPECT_TRUE(isJsonComparison(element));
    EXPECT_EQ(jsonValueKind(element), QcSqlBase::_jsonAsText_);
    EXPECT_EQ(jsonSearchPath(element), "skills[0]");
}

TEST_F(QcSqlQueryElementWhiteBoxTest, LaterComparatorClearsEarlierOperandState)
{
    QcSqlQuery sub;
    sub.fromTable("groups");
    QcSqlQueryElement element("id");

    element.isIn(QcSqlQueryElement::QcVariantList{QcSqlQueryElement::QcVariant(1LL)});
    ASSERT_FALSE(values(element).empty());

    element.isEqualTo(sub);
    EXPECT_TRUE(values(element).empty());
    EXPECT_TRUE(hasSubQuery(element));

    element.isEqualTo(QcSqlQueryElement::QcVariant(2LL));
    EXPECT_FALSE(hasSubQuery(element));
    EXPECT_EQ(std::get<long long>(value(element)), 2LL);
}

TEST_F(QcSqlQueryElementWhiteBoxTest, CopyingElementWithSubQueryDeepCopies)
{
    QcSqlQuery sub;
    sub.fromTable("groups");
    QcSqlQueryElement original("group_id");
    original.isEqualTo(sub);

    QcSqlQueryElement copy(original);

    EXPECT_TRUE(hasSubQuery(original));
    EXPECT_TRUE(hasSubQuery(copy));
}

TEST(QcSqlQueryElementToSql, ParenthesisMarkersRenderBare)
{
    QcSqlQueryElement open(QcSqlBase::_openParenthesis_);
    QcSqlQueryElement close(QcSqlBase::_closeParenthesis_);
    QcSqlQuery::QcVariantList params;

    EXPECT_EQ(open.toSql(params), "(");
    EXPECT_EQ(close.toSql(params), ")");
    EXPECT_TRUE(params.empty());
}

TEST(QcSqlQueryElementToSql, IsEqualToBindsValueAndUsesPlaceholderOne)
{
    QcSqlQueryElement element("id");
    element.isEqualTo(QcSqlQueryElement::QcVariant(5LL));
    QcSqlQuery::QcVariantList params;

    EXPECT_EQ(element.toSql(params), q("id") + " = " + QcSqlDialect::placeholder(1));
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(std::get<long long>(params[0]), 5LL);
}

TEST(QcSqlQueryElementToSql, PlaceholderNumberingContinuesFromExistingParams)
{
    QcSqlQueryElement element("id");
    element.isEqualTo(QcSqlQueryElement::QcVariant(5LL));
    QcSqlQuery::QcVariantList params{QcSqlQueryElement::QcVariant(1LL), QcSqlQueryElement::QcVariant(2LL)};

    EXPECT_EQ(element.toSql(params), q("id") + " = " + QcSqlDialect::placeholder(3));
    ASSERT_EQ(params.size(), 3u);
}

TEST(QcSqlQueryElementToSql, IsEqualToWithSubQueryEmbedsRenderedSubquery)
{
    QcSqlQuery sub;
    sub.fromTable("groups");
    QcSqlQueryElement element("group_id");
    element.isEqualTo(sub);
    QcSqlQuery::QcVariantList params;

    EXPECT_EQ(element.toSql(params), q("group_id") + " = (" + sub.toSql().sql + ")");
}

TEST(QcSqlQueryElementToSql, ComparatorFamilyRendersExpectedOperators)
{
    QcSqlQuery::QcVariantList params;
    auto render = [&params](auto & element) { return element.toSql(params); };

    QcSqlQueryElement notEq("a");
    notEq.isNotEqualTo(QcSqlQueryElement::QcVariant(1LL));
    EXPECT_EQ(render(notEq), q("a") + " != " + QcSqlDialect::placeholder(1));

    QcSqlQueryElement gt("a");
    gt.isGreaterThan(QcSqlQueryElement::QcVariant(1LL));
    EXPECT_EQ(render(gt), q("a") + " > " + QcSqlDialect::placeholder(2));

    QcSqlQueryElement gte("a");
    gte.isGreaterThanOrEqualTo(QcSqlQueryElement::QcVariant(1LL));
    EXPECT_EQ(render(gte), q("a") + " >= " + QcSqlDialect::placeholder(3));

    QcSqlQueryElement lt("a");
    lt.isLessThan(QcSqlQueryElement::QcVariant(1LL));
    EXPECT_EQ(render(lt), q("a") + " < " + QcSqlDialect::placeholder(4));

    QcSqlQueryElement lte("a");
    lte.isLessThanOrEqualTo(QcSqlQueryElement::QcVariant(1LL));
    EXPECT_EQ(render(lte), q("a") + " <= " + QcSqlDialect::placeholder(5));
}

TEST(QcSqlQueryElementToSql, LikeFamilyRendersExpectedSyntax)
{
    QcSqlQuery::QcVariantList params;

    QcSqlQueryElement like("name");
    like.isLike("%a%");
    EXPECT_EQ(like.toSql(params), q("name") + " LIKE " + QcSqlDialect::placeholder(1));

    QcSqlQueryElement notLike("name");
    notLike.isNotLike("%a%");
    EXPECT_EQ(notLike.toSql(params), q("name") + " NOT LIKE " + QcSqlDialect::placeholder(2));

    QcSqlQueryElement ilike("name");
    ilike.isIlike("%a%");
    EXPECT_EQ(ilike.toSql(params, QcDbDriver::PostgreSQL), q("name") + " ILIKE " + QcSqlDialect::placeholder(3));

    // Only PostgreSQL has ILIKE -- every other driver degrades to
    // LOWER()/LOWER() (see qcsqlqueryelement.cpp).
    for (QcDbDriver driver : {QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlQuery::QcVariantList degradedParams;
        QcSqlQueryElement ilikeOther("name");
        ilikeOther.isIlike("%a%");
        EXPECT_EQ(ilikeOther.toSql(degradedParams, driver),
                  "LOWER(" + QcSqlDialect::quoteIdentifier("name", driver) + ") LIKE LOWER(" + QcSqlDialect::placeholder(1, driver) + ")")
            << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlQueryElementToSql, IsNullFamilyRendersWithoutBindingAnything)
{
    QcSqlQuery::QcVariantList params;

    QcSqlQueryElement isNullEl("deleted_at");
    isNullEl.isNull();
    EXPECT_EQ(isNullEl.toSql(params), q("deleted_at") + " IS NULL");

    QcSqlQueryElement isNotNullEl("deleted_at");
    isNotNullEl.isNotNull();
    EXPECT_EQ(isNotNullEl.toSql(params), q("deleted_at") + " IS NOT NULL");

    EXPECT_TRUE(params.empty());
}

TEST(QcSqlQueryElementToSql, IsInWithListBindsEachValue)
{
    QcSqlQueryElement element("id");
    element.isIn(QcSqlQueryElement::QcVariantList{QcSqlQueryElement::QcVariant(1LL), QcSqlQueryElement::QcVariant(2LL)});
    QcSqlQuery::QcVariantList params;

    EXPECT_EQ(element.toSql(params), q("id") + " IN (" + QcSqlDialect::placeholder(1) + ", " + QcSqlDialect::placeholder(2) + ")");
    ASSERT_EQ(params.size(), 2u);
}

TEST(QcSqlQueryElementToSql, IsInWithSubQueryEmbedsRenderedSubquery)
{
    QcSqlQuery sub;
    sub.fromTable("groups");
    QcSqlQueryElement element("group_id");
    element.isIn(sub);
    QcSqlQuery::QcVariantList params;

    EXPECT_EQ(element.toSql(params), q("group_id") + " IN (" + sub.toSql().sql + ")");
}

TEST(QcSqlQueryElementToSql, IsInWithEmptyListRendersTautologicalFalse)
{
    QcSqlQueryElement isIn("id");
    isIn.isIn(QcSqlQueryElement::QcVariantList{});
    QcSqlQueryElement isNotIn("id");
    isNotIn.isNotIn(QcSqlQueryElement::QcVariantList{});
    QcSqlQuery::QcVariantList params;

    EXPECT_EQ(isIn.toSql(params), "1=0");
    EXPECT_EQ(isNotIn.toSql(params), "1=1");
    EXPECT_TRUE(params.empty());
}

TEST(QcSqlQueryElementToSql, IsBetweenBindsBothOperandsInOrder)
{
    QcSqlQueryElement element("age");
    element.isBetween(QcSqlQueryElement::QcVariant(1LL), QcSqlQueryElement::QcVariant(10LL));
    QcSqlQuery::QcVariantList params;

    EXPECT_EQ(element.toSql(params), q("age") + " BETWEEN " + QcSqlDialect::placeholder(1) + " AND " + QcSqlDialect::placeholder(2));
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(std::get<long long>(params[0]), 1LL);
    EXPECT_EQ(std::get<long long>(params[1]), 10LL);
}

TEST(QcSqlQueryElementToSql, IsNotBetweenRendersNotBetween)
{
    QcSqlQueryElement element("age");
    element.isNotBetween(QcSqlQueryElement::QcVariant(1LL), QcSqlQueryElement::QcVariant(10LL));
    QcSqlQuery::QcVariantList params;

    EXPECT_EQ(element.toSql(params), q("age") + " NOT BETWEEN " + QcSqlDialect::placeholder(1) + " AND " + QcSqlDialect::placeholder(2));
}

// No two of PostgreSQL/SQLite/MySQL/Oracle/MSSQL agree on how to spell a
// NULL-safe "not equal" -- unlike ILIKE (just a different keyword), the
// *shape* of the rendered expression differs per driver (see
// qcsqlqueryelement.cpp). Each branch below matches that driver's actual
// implementation, including how many parameters it ends up binding for the
// same logical value (Oracle/Postgres/SQLite/MySQL bind once and can reuse
// the placeholder text; MSSQL's "?" is positional, so a value operand used
// twice in the expression has to be bound twice).
TEST(QcSqlQueryElementToSql, IsDistinctFromFamilyMatchesActiveDriverShape)
{
    // Runs once per driver in a single build/binary now (previously this was
    // an #if/#elif chain, only one branch of which ever compiled/ran per
    // build).
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        QcSqlQueryElement distinctEl("a");
        distinctEl.isDistinctFrom(QcSqlQueryElement::QcVariant(1LL));
        QcSqlQuery::QcVariantList distinctParams;
        const std::string distinctSql = distinctEl.toSql(distinctParams, driver);

        QcSqlQueryElement notDistinctEl("a");
        notDistinctEl.isNotDistinctFrom(QcSqlQueryElement::QcVariant(1LL));
        QcSqlQuery::QcVariantList notDistinctParams;
        const std::string notDistinctSql = notDistinctEl.toSql(notDistinctParams, driver);
        const std::string a = QcSqlDialect::quoteIdentifier("a", driver);

        switch (driver) {
            case QcDbDriver::PostgreSQL:
                EXPECT_EQ(distinctSql, a + " IS DISTINCT FROM " + QcSqlDialect::placeholder(1, driver));
                EXPECT_EQ(notDistinctSql, a + " IS NOT DISTINCT FROM " + QcSqlDialect::placeholder(1, driver));
                EXPECT_EQ(distinctParams.size(), 1u);
                break;
            case QcDbDriver::SQLite:
                EXPECT_EQ(distinctSql, a + " IS NOT " + QcSqlDialect::placeholder(1, driver));
                EXPECT_EQ(notDistinctSql, a + " IS " + QcSqlDialect::placeholder(1, driver));
                EXPECT_EQ(distinctParams.size(), 1u);
                break;
            case QcDbDriver::MySQL:
                EXPECT_EQ(distinctSql, "NOT (" + a + " <=> " + QcSqlDialect::placeholder(1, driver) + ")");
                EXPECT_EQ(notDistinctSql, a + " <=> " + QcSqlDialect::placeholder(1, driver));
                EXPECT_EQ(distinctParams.size(), 1u);
                break;
            case QcDbDriver::Oracle: {
                // Bound once, ":1" referenced three times -- named placeholders can be
                // repeated for one bind (see qcsqlqueryelement.cpp). The `=` only ever
                // runs guarded by "IS NOT NULL" on both sides (verified directly
                // against real Oracle: the naive "a = b OR (a IS NULL AND b IS NULL)"
                // form silently drops NULL-vs-value rows from IS DISTINCT FROM results
                // -- three-valued logic, see the production comment).
                const std::string expectedEqualOrBothNull = "((" + a + " IS NULL AND :1 IS NULL) OR (" + a + " IS NOT NULL AND :1 IS NOT NULL AND " + a + " = :1))";
                EXPECT_EQ(distinctSql, "NOT " + expectedEqualOrBothNull);
                EXPECT_EQ(notDistinctSql, expectedEqualOrBothNull);
                EXPECT_EQ(distinctParams.size(), 1u);
                break;
            }
            case QcDbDriver::MSSQL: {
                // Positional "?" -- bound three times, once per textual occurrence
                // (each IS NULL check plus the equality), unlike Oracle's single named
                // placeholder reused above.
                const std::string expectedEqualOrBothNull = "((" + a + " IS NULL AND ? IS NULL) OR (" + a + " IS NOT NULL AND ? IS NOT NULL AND " + a + " = ?))";
                EXPECT_EQ(distinctSql, "NOT " + expectedEqualOrBothNull);
                EXPECT_EQ(notDistinctSql, expectedEqualOrBothNull);
                EXPECT_EQ(distinctParams.size(), 3u);
                break;
            }
        }
    }
}

TEST(QcSqlQueryElementToSql, ExistsFamilyRendersWithoutColumn)
{
    QcSqlQuery sub;
    sub.fromTable("groups");
    QcSqlQueryElement existsEl("ignored_column");
    existsEl.exists(sub);
    QcSqlQueryElement notExistsEl("ignored_column");
    notExistsEl.notExists(sub);
    QcSqlQuery::QcVariantList params;

    EXPECT_EQ(existsEl.toSql(params), "EXISTS (" + sub.toSql().sql + ")");
    EXPECT_EQ(notExistsEl.toSql(params), "NOT EXISTS (" + sub.toSql().sql + ")");
}

TEST(QcSqlQueryElementToSql, CastWrapsColumnBeforeComparison)
{
    QcSqlQueryElement element("amount");
    element.cast(QcSqlBase::_string_, QcSqlBase::_float_, QcSqlQueryElement::QcVariant(std::string("1.5")));
    QcSqlQuery::QcVariantList params;

    const std::string expected = "CAST(" + q("amount") + " AS " + QcSqlDialect::dataTypeName(QcSqlBase::_float_) + ") = " + QcSqlDialect::placeholder(1);
    EXPECT_EQ(element.toSql(params), expected);
}

// The isXxxJson...() family (qcsqlqueryelement.h) reuses every ordinary
// comparator's exact toSql() switch branch (">"/"="/"LIKE"/...) -- only the
// left-hand side changes, from a bare quoted column to
// QcSqlDialect::jsonExtractExpr(column, path, kind, driver). These tests
// build the expected left-hand side through that same dialect function
// (already pinned directly in test_qcsqldialect.cpp) rather than
// hand-spelling per-driver JSON syntax a second time here -- what's under
// test at this layer is the wiring (which kind, which path, which
// comparator operator), not jsonExtractExpr()'s own per-driver rendering.
TEST(QcSqlQueryElementToSql, JsonNumberComparatorsCompareExtractedNumberOnEveryDriver)
{
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        const std::string lhs = QcSqlDialect::jsonExtractExpr(QcSqlDialect::quoteIdentifier("metadata", driver), "rating", QcSqlBase::_jsonAsNumber_, driver);

        QcSqlQueryElement gt("metadata");
        gt.isGreaterThanJsonNumber(QcSqlQueryElement::QcVariant(4.5), "rating");
        QcSqlQuery::QcVariantList params;
        EXPECT_EQ(gt.toSql(params, driver), lhs + " > " + QcSqlDialect::placeholder(1, driver)) << "driver=" << static_cast<int>(driver);
        ASSERT_EQ(params.size(), 1u) << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(std::get<double>(params[0]), 4.5) << "driver=" << static_cast<int>(driver);

        QcSqlQueryElement eq("metadata");
        eq.isEqualToJsonNumber(QcSqlQueryElement::QcVariant(5LL), "level");
        QcSqlQuery::QcVariantList eqParams;
        EXPECT_EQ(eq.toSql(eqParams, driver), QcSqlDialect::jsonExtractExpr(QcSqlDialect::quoteIdentifier("metadata", driver), "level", QcSqlBase::_jsonAsNumber_, driver)
                + " = " + QcSqlDialect::placeholder(1, driver))
            << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlQueryElementToSql, JsonTextComparatorsCompareExtractedTextOnEveryDriver)
{
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        const std::string lhs = QcSqlDialect::jsonExtractExpr(QcSqlDialect::quoteIdentifier("metadata", driver), "skills[0]", QcSqlBase::_jsonAsText_, driver);

        QcSqlQueryElement eq("metadata");
        eq.isEqualToJsonText(QcSqlQueryElement::QcVariant(std::string("c++")), "skills[0]");
        QcSqlQuery::QcVariantList params;
        EXPECT_EQ(eq.toSql(params, driver), lhs + " = " + QcSqlDialect::placeholder(1, driver)) << "driver=" << static_cast<int>(driver);
        ASSERT_EQ(params.size(), 1u) << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(std::get<std::string>(params[0]), "c++") << "driver=" << static_cast<int>(driver);

        QcSqlQueryElement ne("metadata");
        ne.isNotEqualToJsonText(QcSqlQueryElement::QcVariant(std::string("c++")), "skills[0]");
        QcSqlQuery::QcVariantList neParams;
        EXPECT_EQ(ne.toSql(neParams, driver), lhs + " != " + QcSqlDialect::placeholder(1, driver)) << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlQueryElementToSql, LikeJsonTextFamilyRendersLikeOperatorsOnEveryDriver)
{
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        const std::string lhs = QcSqlDialect::jsonExtractExpr(QcSqlDialect::quoteIdentifier("metadata", driver), "skills[0]", QcSqlBase::_jsonAsText_, driver);

        QcSqlQueryElement likeEl("metadata");
        likeEl.isLikeJsonText("c%", "skills[0]");
        QcSqlQuery::QcVariantList likeParams;
        EXPECT_EQ(likeEl.toSql(likeParams, driver), lhs + " LIKE " + QcSqlDialect::placeholder(1, driver)) << "driver=" << static_cast<int>(driver);

        QcSqlQueryElement notLikeEl("metadata");
        notLikeEl.isNotLikeJsonText("c%", "skills[0]");
        QcSqlQuery::QcVariantList notLikeParams;
        EXPECT_EQ(notLikeEl.toSql(notLikeParams, driver), lhs + " NOT LIKE " + QcSqlDialect::placeholder(1, driver)) << "driver=" << static_cast<int>(driver);
    }
}

// isIlikeJsonText()/isNotILikeJsonText() compose two dialect-branched
// behaviors at once (see qcsqlqueryelement.cpp's _isILike_/_isNotILike_
// cases) -- only PostgreSQL has native ILIKE, every other driver degrades to
// LOWER(...)/LOWER(...) -- verified separately here because that degradation
// wraps LOWER() around the *JSON-extracted* expression, not just a bare
// column, which is a real interaction between the two features, not just a
// restatement of either one alone.
TEST(QcSqlQueryElementToSql, IlikeJsonTextDegradesToLowerOnNonPostgresDrivers)
{
    QcSqlQueryElement pgEl("metadata");
    pgEl.isIlikeJsonText("%c%", "skills[0]");
    QcSqlQuery::QcVariantList pgParams;
    const std::string pgLhs = QcSqlDialect::jsonExtractExpr(q("metadata"), "skills[0]", QcSqlBase::_jsonAsText_, QcDbDriver::PostgreSQL);
    EXPECT_EQ(pgEl.toSql(pgParams, QcDbDriver::PostgreSQL), pgLhs + " ILIKE " + QcSqlDialect::placeholder(1, QcDbDriver::PostgreSQL));

    for (QcDbDriver driver : {QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        const std::string lhs = QcSqlDialect::jsonExtractExpr(QcSqlDialect::quoteIdentifier("metadata", driver), "skills[0]", QcSqlBase::_jsonAsText_, driver);

        QcSqlQueryElement el("metadata");
        el.isIlikeJsonText("%c%", "skills[0]");
        QcSqlQuery::QcVariantList params;
        EXPECT_EQ(el.toSql(params, driver), "LOWER(" + lhs + ") LIKE LOWER(" + QcSqlDialect::placeholder(1, driver) + ")") << "driver=" << static_cast<int>(driver);

        QcSqlQueryElement notEl("metadata");
        notEl.isNotILikeJsonText("%c%", "skills[0]");
        QcSqlQuery::QcVariantList notParams;
        EXPECT_EQ(notEl.toSql(notParams, driver), "LOWER(" + lhs + ") NOT LIKE LOWER(" + QcSqlDialect::placeholder(1, driver) + ")") << "driver=" << static_cast<int>(driver);
    }
}

TEST(QcSqlQueryElementToSql, LikeJsonArrayAsTextUsesRawKindExtractionOnEveryDriver)
{
    for (QcDbDriver driver : {QcDbDriver::PostgreSQL, QcDbDriver::Oracle, QcDbDriver::MySQL, QcDbDriver::SQLite, QcDbDriver::MSSQL}) {
        const std::string lhs = QcSqlDialect::jsonExtractExpr(QcSqlDialect::quoteIdentifier("metadata", driver), "skills", QcSqlBase::_jsonAsRaw_, driver);

        QcSqlQueryElement likeEl("metadata");
        likeEl.isLikeJsonArrayAsText("%\"python\"%", "skills");
        QcSqlQuery::QcVariantList params;
        EXPECT_EQ(likeEl.toSql(params, driver), lhs + " LIKE " + QcSqlDialect::placeholder(1, driver)) << "driver=" << static_cast<int>(driver);
        ASSERT_EQ(params.size(), 1u) << "driver=" << static_cast<int>(driver);
        EXPECT_EQ(std::get<std::string>(params[0]), "%\"python\"%") << "driver=" << static_cast<int>(driver);
    }
}

// renderChain() is QcSqlQuery's WHERE/HAVING-chain renderer pulled out to be
// shared with QcSqlUpdate/QcSqlDelete's WHERE clauses (see qcsqlupdate.h/
// qcsqldelete.h) -- QcSqlQuery's own toSql() tests (test_qcsqlquery.cpp,
// "WhereChain..."/"GroupByAndHaving...") already exercise it end to end
// through that caller; these tests call it directly to document/pin its
// contract at its own class boundary.
TEST(QcSqlQueryElementRenderChain, GluesAndOrCorrectlyAndOmitsConnectorAfterOpenParen)
{
    QcSqlQuery::QcVariantList params;
    std::vector<std::pair<int, QcSqlQueryElement>> chain;
    chain.push_back({0, QcSqlQueryElement("a")});
    chain.back().second.isEqualTo(QcSqlQueryElement::QcVariant(1LL));
    chain.push_back({0, QcSqlQueryElement(QcSqlBase::_openParenthesis_)});
    chain.push_back({0, QcSqlQueryElement("b")});
    chain.back().second.isEqualTo(QcSqlQueryElement::QcVariant(2LL));
    chain.push_back({1, QcSqlQueryElement("c")});
    chain.back().second.isEqualTo(QcSqlQueryElement::QcVariant(3LL));
    chain.push_back({0, QcSqlQueryElement(QcSqlBase::_closeParenthesis_)});

    const std::string expected = q("a") + " = " + QcSqlDialect::placeholder(1)
        + " AND (" + q("b") + " = " + QcSqlDialect::placeholder(2) + " OR " + q("c") + " = " + QcSqlDialect::placeholder(3) + ")";
    EXPECT_EQ(QcSqlQueryElement::renderChain(chain, params), expected);
    ASSERT_EQ(params.size(), 3u);
}

TEST(QcSqlQueryElementRenderChain, EmptyChainRendersEmptyString)
{
    QcSqlQuery::QcVariantList params;
    std::vector<std::pair<int, QcSqlQueryElement>> chain;

    EXPECT_TRUE(QcSqlQueryElement::renderChain(chain, params).empty());
    EXPECT_TRUE(params.empty());
}

// =====================================================================
// isIncomplete() / validateChain() -- feeds QcSqlQuery/QcSqlUpdate/
// QcSqlDelete::validate() (see qcsqlbase.h's QcQueryBuildError)
// =====================================================================

TEST(QcSqlQueryElementIsIncomplete, TrueForFreshlyConstructedColumnElement)
{
    QcSqlQueryElement element("col");

    EXPECT_TRUE(element.isIncomplete());
}

TEST(QcSqlQueryElementIsIncomplete, FalseAfterComparatorChained)
{
    QcSqlQueryElement element("col");
    element.isEqualTo(QcSqlQueryElement::QcVariant(1LL));

    EXPECT_FALSE(element.isIncomplete());
}

TEST(QcSqlQueryElementIsIncomplete, FalseAfterIsNull)
{
    // isNull() leaves m_value at monostate too -- isIncomplete() must key
    // off more than "value is empty" or this would be a false positive.
    QcSqlQueryElement element("col");
    element.isNull();

    EXPECT_FALSE(element.isIncomplete());
}

TEST(QcSqlQueryElementIsIncomplete, FalseForParenthesisMarkers)
{
    QcSqlQueryElement openMarker(QcSqlBase::_openParenthesis_);
    QcSqlQueryElement closeMarker(QcSqlBase::_closeParenthesis_);

    EXPECT_FALSE(openMarker.isIncomplete());
    EXPECT_FALSE(closeMarker.isIncomplete());
}

TEST(QcSqlQueryElementValidateChain, ReportsEachIncompleteElementWithColumnAndLabel)
{
    std::vector<std::pair<int, QcSqlQueryElement>> chain;
    chain.push_back({0, QcSqlQueryElement("a")});
    chain.back().second.isEqualTo(QcSqlQueryElement::QcVariant(1LL));
    chain.push_back({0, QcSqlQueryElement("b")});

    const QcSqlBase::QcStringList problems = QcSqlQueryElement::validateChain(chain, "WHERE");

    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems[0].find("WHERE"), std::string::npos);
    EXPECT_NE(problems[0].find("b"), std::string::npos);
}

TEST(QcSqlQueryElementValidateChain, EmptyChainHasNoProblems)
{
    std::vector<std::pair<int, QcSqlQueryElement>> chain;

    EXPECT_TRUE(QcSqlQueryElement::validateChain(chain, "WHERE").empty());
}

TEST(QcSqlQueryElementValidateChain, IgnoresParenthesisMarkers)
{
    std::vector<std::pair<int, QcSqlQueryElement>> chain;
    chain.push_back({0, QcSqlQueryElement(QcSqlBase::_openParenthesis_)});
    chain.push_back({0, QcSqlQueryElement("a")});
    chain.back().second.isEqualTo(QcSqlQueryElement::QcVariant(1LL));
    chain.push_back({0, QcSqlQueryElement(QcSqlBase::_closeParenthesis_)});

    EXPECT_TRUE(QcSqlQueryElement::validateChain(chain, "WHERE").empty());
}
