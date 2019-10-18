#ifndef ROPTS_INCLUDE_GUARD
#define ROPTS_INCLUDE_GUARD

#include <string_view>
#include <type_traits>
#include <vector>

namespace ropts {

struct OptionBase {
  virtual ~OptionBase() = default;
  virtual void reset() = 0;
  virtual void parse(std::string_view sv) = 0; // FIXME interface for multi args ?
  // Check (required, ...)
  // Name
  // Doc
};

struct Parser {
  std::vector<OptionBase *> options_;

  std::vector<OptionBase *> positionals_;
  // OR subcommands
};

// Template versions of Optionbase interface will register in a parser.
// They must outlive the parser itself.
// The parser will fill them with values from the parsing step

} // namespace ropts

#endif