#include <catch2/catch.hpp>
#include "core/table/TableParser.hpp"

using namespace LinguaAlpaca;

TEST_CASE("TableParser - User OTSL Table Format", "[core][table]") {
    std::string otsl = 
        "<fcel>商品名称<fcel>数量<nl>"
        "<fcel>黄豆芽丝<fcel>6590<nl>"
        "<fcel>海带片<fcel>50<nl>"
        "<fcel>广车菜心<fcel>90+50<nl>"
        "<fcel>冬瓜<fcel>40<nl>"
        "<fcel>小黄鱼<fcel>70<nl>"
        "<fcel>南瓜<fcel>80<nl>"
        "<fcel>玉米<fcel>5<nl>"
        "<fcel>小青椒<fcel>50<nl>"
        "<fcel>大红椒<fcel>30<nl>"
        "<fcel>螺丝椒<fcel>50<nl>"
        "<fcel>大蒜子<fcel>2<nl>"
        "<fcel>葱<fcel>3<nl>"
        "<fcel>毛豆子<fcel>6<nl>"
        "<fcel>牛肉<fcel>4<nl>"
        "<fcel>小河鱼<fcel>5<nl>"
        "<fcel>芹菜<fcel>20+30<nl>"
        "<ecel><ecel><nl>";

    SECTION("IsTableFormat correctly detects OTSL tags") {
        REQUIRE(TableParser::IsTableFormat(otsl) == true);
        REQUIRE(TableParser::IsTableFormat("普通的一段文本没有表格") == false);
    }

    SECTION("Parse OTSL table structure") {
        TableData data = TableParser::Parse(otsl);
        REQUIRE_FALSE(data.IsEmpty());
        REQUIRE(data.hasHeader == true);
        REQUIRE(data.headers.size() == 2);
        REQUIRE(data.headers[0] == "商品名称");
        REQUIRE(data.headers[1] == "数量");

        REQUIRE(data.rows.size() == 16);
        REQUIRE(data.rows[0][0] == "黄豆芽丝");
        REQUIRE(data.rows[0][1] == "6590");
        REQUIRE(data.rows[1][0] == "海带片");
        REQUIRE(data.rows[1][1] == "50");
        REQUIRE(data.rows[2][0] == "广车菜心");
        REQUIRE(data.rows[2][1] == "90+50");
        REQUIRE(data.rows[15][0] == "芹菜");
        REQUIRE(data.rows[15][1] == "20+30");
    }

    SECTION("ToMarkdown conversion") {
        TableData data = TableParser::Parse(otsl);
        std::string md = TableParser::ToMarkdown(data);
        REQUIRE(md.find("| 商品名称 | 数量 |") != std::string::npos);
        REQUIRE(md.find("| :--- | :--- |") != std::string::npos);
        REQUIRE(md.find("| 黄豆芽丝 | 6590 |") != std::string::npos);
        REQUIRE(md.find("| 芹菜 | 20+30 |") != std::string::npos);
    }

    SECTION("ToTsv conversion for Excel paste") {
        TableData data = TableParser::Parse(otsl);
        std::string tsv = TableParser::ToTsv(data);
        REQUIRE(tsv.find("商品名称\t数量\n") != std::string::npos);
        REQUIRE(tsv.find("黄豆芽丝\t6590\n") != std::string::npos);
        REQUIRE(tsv.find("芹菜\t20+30\n") != std::string::npos);
    }

    SECTION("ToCsv conversion") {
        TableData data = TableParser::Parse(otsl);
        std::string csv = TableParser::ToCsv(data);
        REQUIRE(csv.find("商品名称,数量\n") != std::string::npos);
        REQUIRE(csv.find("黄豆芽丝,6590\n") != std::string::npos);
    }

    SECTION("ToSpeechText conversion") {
        TableData data = TableParser::Parse(otsl);
        std::string speech = TableParser::ToSpeechText(data);
        REQUIRE(speech.find("识别到表格，共 16 行数据。") != std::string::npos);
        REQUIRE(speech.find("第 1 行：商品名称 黄豆芽丝，数量 6590。") != std::string::npos);
    }
}

TEST_CASE("TableParser - Empty and Merge Cells in OTSL", "[core][table]") {
    std::string otsl = 
        "<fcel>姓名<fcel>科目<fcel>分数<nl>"
        "<fcel>张三<fcel>语文<fcel>95<nl>"
        "<fcel>李四<ecel><fcel>缺考<nl>"
        "<fcel>王五<lcel><fcel>100<nl>";

    TableData data = TableParser::Parse(otsl);
    REQUIRE(data.headers.size() == 3);
    REQUIRE(data.rows.size() == 3);
    REQUIRE(data.rows[1][0] == "李四");
    REQUIRE(data.rows[1][1] == "");
    REQUIRE(data.rows[1][2] == "缺考");
}

TEST_CASE("TableParser - Markdown Table Parsing", "[core][table]") {
    std::string md = 
        "| 城市 | 人口 (万) | 省份 |\n"
        "| :--- | :---: | ---: |\n"
        "| 杭州 | 1200 | 浙江 |\n"
        "| 南京 | 940 | 江苏 |\n";

    REQUIRE(TableParser::IsTableFormat(md) == true);

    TableData data = TableParser::Parse(md);
    REQUIRE(data.headers.size() == 3);
    REQUIRE(data.headers[0] == "城市");
    REQUIRE(data.headers[1] == "人口 (万)");
    REQUIRE(data.headers[2] == "省份");

    REQUIRE(data.rows.size() == 2);
    REQUIRE(data.rows[0][0] == "杭州");
    REQUIRE(data.rows[0][1] == "1200");
    REQUIRE(data.rows[0][2] == "浙江");
    REQUIRE(data.rows[1][0] == "南京");
}
