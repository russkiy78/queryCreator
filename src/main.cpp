#include <iostream>

#include "query/qcnativeconnection.h"
#include "query/qcsqlquery.h"

int main(){
    // Driver is picked once, at runtime -- must have been compiled into
    // this build (QC_DB_DRIVERS, see CMakeLists.txt) or nativeDriverInfo()/
    // QcNativeConnection's constructor throws/reports accordingly.
    const QcDbDriver driver = QcDbDriver::PostgreSQL;
    std::cout << QcNativeConnection::nativeDriverInfo(driver) << std::endl;

    QcSqlQuery query;
    // Set once, up front -- every toSql() call below renders for this
    // driver without repeating it.
    query.useDriver(driver);

    query.addReturnValues({"test <test_alias>", "test1 <test_alias1>"});
    query.addReturnValue("test3 <test_alias3>").upperCase().cast(QcSqlBase::dataTypes::_string_, QcSqlBase::dataTypes::_float_).round(1);

    query.fromTable("users");
    // isEqualTo()/isLike()/... return bool (success of setting the
    // condition), not QcSqlQueryElement&, so unlike addReturnValue() above
    // these can't be chained back onto the query in a single expression.
    query.where("id").isEqualTo(5LL);
    query.and_("name").isLike("%test%");

    const QcSqlStatement statement = query.toSql();
    std::cout << statement.sql << std::endl;
}
