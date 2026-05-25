#ifndef OBE_PARSER_HPP
#define OBE_PARSER_HPP

#include <vector>

#include "Token.hpp"

namespace obe {

    class Parser {
       public:
        Parser(std::vector<Token> &&tokens);

       private:
        std::vector<Token> m_tokens;
    };

} // namespace obe

#endif // OBE_PARSER_HPP
