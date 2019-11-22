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

TEST_CASE("text_conversion") {
    {
        // Text formatting
        std::string buf;
        CHECK(write_text(buf, "blah ") == 5);
        CHECK(write_text(buf, '!') == 1);
        CHECK(buf == "blah !");
    }
    {
        // Num to str
        std::string buf;
        CHECK(write_value(buf, int(42)) == 2);
        CHECK(buf == "42");
        CHECK(write_value(buf, int(-4000)) == 5);
        CHECK(buf == "42-4000");
    }
    {
        // Str to num
        constexpr int argc = 6;
        char const * argv[argc] = {"ignored", "42", "-4000", "007", "45.67", "azerty"};
        CommandLine state{argc, argv};
        CHECK(ValueTrait<int>::parse(state, "a") == 42);
        CHECK(ValueTrait<int>::parse(state, "a") == -4000);
        CHECK(ValueTrait<int>::parse(state, "a") == 7);
        CHECK_THROWS_AS_MESSAGE(
            ValueTrait<int>::parse(state, "a"),
            Exception,
            "value 'a' is not a valid integer (int): '45.67'");
        CHECK_THROWS_AS_MESSAGE(
            ValueTrait<int>::parse(state, "a"),
            Exception,
            "value 'a' is not a valid integer (int): 'azerty'");
        CHECK_THROWS_AS_MESSAGE(ValueTrait<int>::parse(state, "a"), Exception, "missing value 'a'");
    }
}

TEST_CASE("temporary") {
    Application app{"test"};

    OptionSingle<int> factor{'f'};
    factor.help_text = "Integer factor";
    factor.value_name = "N";
    factor.value = 42;
    app.add(factor);

    OptionSingle<std::tuple<int, int, int>> triple{'t', "triple"};
    triple.help_text = "Make a tuple with 3 elements";
    triple.value_name = {"A", "B", "C"};
    app.add(triple);

    app.write_usage(stderr);

    // TODO add tests for parsing system wide
    try {
        char const * argv[] = {"", "-t", "42", "-a"};
        app.parse({4, argv});
    } catch(Exception const & e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
    }
}