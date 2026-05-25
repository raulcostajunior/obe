#include "Parser.hpp"

namespace obe {

    Parser::Parser(std::vector<Token>&& tokens) : m_tokens(std::move(tokens)) {}

} // namespace obe
