#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "external/doctest.h"

#include "ropts.h"

#include <string>

using namespace ropts;

TEST_CASE("cow_str") {
    CowStr s;
    CHECK(s.view() == "");
    CHECK(s.view() == ropts::string_view());
    CHECK(s.type() == CowStr::Type::Borrowed);
    s = "literal";
    CHECK(s.view() == "literal");
    CHECK(s.type() == CowStr::Type::Borrowed);
    s = std::string("rvalue std::string");
    CHECK(s.view() == "rvalue std::string");
    CHECK(s.type() == CowStr::Type::Owned);
    std::string lvalue_string{"lvalue std::string"};
    s = CowStr::borrowed(lvalue_string);
    CHECK(s.view() == "lvalue std::string");
    CHECK(s.type() == CowStr::Type::Borrowed);
}

TEST_CASE("temporary") {
    Option<int> factor;
    factor.short_name = "f";
    factor.help_text = "integer factor";
    factor.value = 42;
    factor.value_name = "i";
    factor.from_text = [](std::optional<int> &, std::string_view) {
        // Do nothing
    };
}