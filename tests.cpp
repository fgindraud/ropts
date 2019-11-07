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
    Parser parser;

    Option<int> factor;
    factor.short_name = 'f';
    factor.help_text = "Integer factor";
    factor.value = 42;
    factor.value_name = "N";
    factor.from_text = [](std::optional<int> &, std::string_view) {
        // Do nothing
    };
    parser.add(factor);

    Option<int, Fixed<3>> triple;
    triple.short_name = 't';
    triple.long_name = "triple";
    triple.help_text = "Make a tuple with 3 elements";
    triple.value_name = {"A", "B", "C"};
    parser.add(triple);

    parser.print_usage(stderr);
}