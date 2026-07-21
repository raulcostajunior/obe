#include "scanner.hpp"

#include <array>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>
#include <fstream>
#include <string_view>

#include "ErrorInfo.hpp"
#include "TokenUtils.hpp"

namespace obe::scanner {
    // Size of the buffer for storing an errno corresponding message.
    constexpr size_t ERR_MSG_BUFF_SIZE = 256U;

    namespace {
        
        /**
         * Context of an ongoing scan operation. Each scan operation creates an
         * instance of ScanContext at its start. The context stores bookkeeping data for the
         * scan process and is passed around (and modified) by the different methods of the
         * Scanner class.
         */
        struct ScanContext {
            // The source input being scanned.
            // A string_view here is safe - the lifetime of a ScanContext is limited to
            // a single scan operation. The string_view allows avoiding both copying strings and
            // having to declare a const data member.
            std::string_view srcInput;
            // Use lowercase keyword?
            bool lowerCaseKeywords;
            // Number of spaces per tab - zero means it is not known and lines with tabs cannot
            // have the currColumn information updated after the first line tab is found.
            uint8_t spacesPerTab;
            // Should currColumn be ignored? (currColumn parameter description below for more
            // details)
            bool ignoreCurrColumn{false};
            // Index, in the src input, of the character being scanned.
            unsigned long lexPos{0};
            // Number of the line from the src input currently being scanned.
            int currLine{1};
            // Number of the column (of the current line) from the src input currently
            // being scanned. Column information should be ignored if at least one '\t' has been
            // found in the current line and the number of spaces per tab is not set.
            int currColumn{1};
            // Starting column of the current token being scanned - initialized to a sentinel
            // value
            int currTokenColumn{-1};
            // The tokens (and errors) found by the ongoing scan operation.
            ScanResults results;

            ScanContext(const std::string& srcInput, const bool lowerKey,
                        const uint8_t spacesPerTab)
                : srcInput{srcInput}, lowerCaseKeywords{lowerKey}, spacesPerTab{spacesPerTab} {}

            int getCurrColumn() const {
                if (ignoreCurrColumn) {
                    return -1;
                }
                return currColumn;
            }
        };

        /**
         * @brief Returns whether the whole src input has been already scanned or not.
         *
         * @param ctx  the context of the ongoing scan operation.
         * @return true if all the characters from the src input have already been scanned.
         * @return false there is at least one more character from the src input to be scanned.
         */
        bool _allScanned(const ScanContext& ctx) {
            return ctx.lexPos >= ctx.srcInput.length();
        }

        /**
         * @brief Returns the next character in the source being scanned and advances the scan
         * by one character.
         * @param ctx the context of the ongoing scan operation.
         * @return the next character in the source being scanned.
         */
        char _nextChr(ScanContext& ctx) {
            const char chr = ctx.srcInput.at(ctx.lexPos);
            ctx.lexPos++;
            return chr;
        }

        /**
         * @brief Returns the next character in the source being scanned but doesn't advance
         * the scan.
         *
         * @param ctx the context of the ongoing scan operation.
         * @return The next character in the source being scanned. If the end of the input has
         * been reached, it returns '\0'.
         */
        char _nextChrNoAdvance(const ScanContext& ctx) {
            if (_allScanned(ctx)) {
                return '\0';
            }
            return ctx.srcInput.at(ctx.lexPos);
        }

        /**
         * @brief Returns whether the next character matches an expected character or not.
         *
         * If the next character matches the expected character, it is also "consumed" by
         * advancing the scan by one character.
         *
         * @param ctx the context of the ongoing scan operation.
         * @param expChr the expected character after the current one.
         * @return true if the next character matches the expected character.
         */
        bool _nextChrMatch(ScanContext& ctx, const char expChr) {
            if (_allScanned(ctx) || ctx.srcInput.at(ctx.lexPos) != expChr) {
                return false;
            }
            ctx.lexPos++;
            return true;
        }

        /**
         * @brief Scans the optional scale factor at the end of a real number literal.
         *
         * @param ctx the context of the ongoing scan operation.
         * @param realBasePart the integer and decimal part (without any exponent) information
         * of the real number literal - the scanner can only know that the literal has a scale
         * factor when it finds the introducing 'E' of the exponential notation.
         */
        void _scanRealScaleFactor(ScanContext& ctx, const std::string& realBasePart) {
            std::string lex{realBasePart};
            char nextCh = _nextChr(ctx);
            ctx.currColumn++;
            if (nextCh != '+' && nextCh != '-') {
                ctx.results.errors.push_back(ErrorInfo{
                      .line = ctx.currLine,
                      .column = ctx.getCurrColumn(),
                      .msg = "Real number scale factor must start with an 'E' followed by "
                             "either a '+' or '-' signal."});
            } else {
                lex.push_back(nextCh);
                nextCh = _nextChr(ctx);
                ctx.currColumn++;
                if (isdigit(nextCh) == 0) {
                    ctx.results.errors.push_back(ErrorInfo{
                          .line = ctx.currLine,
                          .column = ctx.getCurrColumn(),
                          .msg = "Scale factor of a real number must have at least one digit "
                                 "after the '+' or '-' signal."});
                } else {
                    lex.push_back(nextCh);
                    nextCh = _nextChrNoAdvance(ctx);
                    while (isdigit(nextCh) != 0) {
                        lex.push_back(nextCh);
                        ctx.lexPos++;
                        ctx.currColumn++;
                        nextCh = _nextChrNoAdvance(ctx);
                    }
                    ctx.results.tokens.push_back(Token{.type = TokenType::REAL,
                                                       .lexeme = lex,
                                                       .line = ctx.currLine,
                                                       .column = ctx.currTokenColumn});
                }
            }
        }

        /**
         * @brief Scans a real number - sequence of digits in base 10 with a decimal separator.
         * A real number literal can have an optional scale factor at its end.
         *
         * @note: As the scanner can only know that it is handling a real number once it finds
         * the decimal separator, this method actually scans the decimal and the optional scale
         * factor at the end of the real number.
         *
         * @param ctx the context of the ongoing scan operation.
         * @param integerPart the integer part of the real number (includes the decimal
         * separator).
         */
        void _scanRealNumber(ScanContext& ctx, const std::string& integerPart) {
            std::string lex{integerPart};
            char nextChr = _nextChrNoAdvance(ctx);
            while (isdigit(nextChr) != 0) {
                lex.push_back(nextChr);
                ctx.lexPos++;
                ctx.currColumn++;
                nextChr = _nextChrNoAdvance(ctx);
            }
            if (nextChr == 'E') {
                // Found the optional scale factor at the end.
                lex.push_back(nextChr);
                ctx.lexPos++;
                ctx.currColumn++;
                _scanRealScaleFactor(ctx, lex);
            } else {
                ctx.results.tokens.emplace_back(Token{.type = TokenType::REAL,
                                                      .lexeme = lex,
                                                      .line = ctx.currLine,
                                                      .column = ctx.currTokenColumn});
            }
        }

        /**
         * @brief Scans a number - sequence of digits optionally in hexadecimal form - or a
         * single char string - sequence of digits or hexadecimal digits followed by an "X". A
         * hexadecimal number literal must end with an "H" to be valid.
         *
         * @param ctx the context of the ongoing scan operation.
         * @param firstDigit the first digit of the number.
         */
        void _scanNumberOrSingleCharString(ScanContext& ctx, const char firstDigit) {
            std::string lex{firstDigit};
            char nextChr = _nextChrNoAdvance(ctx);
            while (isHexDigit(nextChr)) {
                lex.push_back(nextChr);
                ctx.lexPos++;
                ctx.currColumn++;
                nextChr = _nextChrNoAdvance(ctx);
            }
            if (nextChr == 'X') {
                // The end of a single character string has been found. The character must be
                // evaluated from the hexadecimal value given by the lexeme.
                if (lex.size() > 2) {
                    ctx.results.errors.push_back(ErrorInfo{
                          .line = ctx.currLine,
                          .column = ctx.getCurrColumn(),
                          .msg =
                                "Single character strings must have values between 0 and FF."});
                } else {
                    const int charCode = std::stoi(
                          lex, nullptr,
                          16); // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
                    const std::string strLex{static_cast<char>(charCode)};
                    ctx.results.tokens.emplace_back(Token{.type = TokenType::STRING,
                                                          .lexeme = strLex,
                                                          .line = ctx.currLine,
                                                          .column = ctx.currTokenColumn});
                }
                // Consume the 'X' - it is not part of the string
                ctx.lexPos++;
                ctx.currColumn++;
            } else if (nextChr == 'H') {
                // The end of an integer literal in hexadecimal form has been found - the H is
                // part of the integer literal and must be included in its lexeme.
                lex.push_back(nextChr);
                ctx.lexPos++;
                ctx.currColumn++;
                ctx.results.tokens.emplace_back(Token{.type = TokenType::INTEGER,
                                                      .lexeme = lex,
                                                      .line = ctx.currLine,
                                                      .column = ctx.currTokenColumn});
            } else if (nextChr == '.') {
                // A decimal separator indicates that a REAL literal is being scanned.
                if (!allBase10Digits(lex)) {
                    // Oberon only allows integer numbers to be represented in hex. Real numbers
                    // must always be expressed in base 10.
                    ctx.results.errors.emplace_back(ErrorInfo{
                          .line = ctx.currLine,
                          .column = ctx.getCurrColumn(),
                          .msg = "Real numbers must use only digits between 0 and 9."});
                }
                lex.push_back(nextChr);
                ctx.lexPos++;
                ctx.currColumn++;
                _scanRealNumber(ctx, lex);
            } else {
                // The lexeme found so far can be an integer literal in decimal form - unless it
                // contains any hexadecimal digit that is not a base 10 digit.
                if (allBase10Digits(lex)) {
                    // The lexeme is a valid integer literal in decimal form.
                    ctx.results.tokens.emplace_back(Token{.type = TokenType::INTEGER,
                                                          .lexeme = lex,
                                                          .line = ctx.currLine,
                                                          .column = ctx.currTokenColumn});
                } else {
                    // A hexadecimal digit that is not a base 10 digit has been found; report
                    // the error.
                    ctx.results.errors.emplace_back(ErrorInfo{
                          .line = ctx.currLine,
                          .column = ctx.getCurrColumn(),
                          .msg = "Hexadecimal number must be terminated with an 'H'."});
                }
            }
        }

        /**
         * @brief Scans an identifier - sequence of letters and digits initiated by a letter.
         *
         * An identifier can be a keyword of the language or can be a simple identifier (e.g., a
         * variable or constant name).
         *
         * @param ctx the context of the ongoing scan operation.
         * @param firstLetter the first letter of the identifier.
         */
        void _scanIdentifier(ScanContext& ctx, const char firstLetter) {
            std::string identLex{firstLetter};
            char nextChr = _nextChrNoAdvance(ctx);
            while (isalpha(nextChr) != 0 || isdigit(nextChr) != 0) {
                identLex.push_back(nextChr);
                ctx.lexPos++;
                ctx.currColumn++;
                nextChr = _nextChrNoAdvance(ctx);
            }
            const TokenType tkType =
                  Token::typeFromIdentifierLexeme(ctx.lowerCaseKeywords, identLex);
            ctx.results.tokens.emplace_back(Token{.type = tkType,
                                                  .lexeme = identLex,
                                                  .line = ctx.currLine,
                                                  .column = ctx.currTokenColumn});
        }

        /**
         * @brief Scans a comment - all the characters between a "(*" and a "*)"
         * *
         * @attention this internal method must be called only when the scanner knows that is in
         * a comment - after a "(*", but before a "*)".
         *
         * @param ctx the context of the ongoing scan operation.
         */
        void _scanComment(ScanContext& ctx) {
            bool endOfCommentFound = false;
            std::string strLex{};
            // As a comment can be multiline, the scan line and column at the start of its
            // lexeme extraction are captured to be used later when the comment token is
            // constructed
            const int initialLine = ctx.currLine;
            // const int initialColumn = ctx.getCurrColumn();
            while (!_allScanned(ctx) && !endOfCommentFound) {
                switch (const char nextChr = _nextChrNoAdvance(ctx)) {
                    case '\n':
                        strLex.push_back(nextChr);
                        ctx.currLine++;
                        ctx.currColumn = 1;
                        ctx.ignoreCurrColumn = false;
                        break;
                    case '\t':
                        if (ctx.spacesPerTab > 0) {
                            ctx.currColumn += ctx.spacesPerTab;
                            for (uint8_t i = 0; i < ctx.spacesPerTab; i++) {
                                strLex.push_back(' ');
                            }
                        } else {
                            ctx.ignoreCurrColumn = true;
                            strLex.push_back(' ');
                            ctx.currColumn++;
                        }
                        break;
                    case '*':
                        // There's a chance that the end of comment has been reached;
                        // Advances the scan and checks if the next character is ")"
                        ctx.lexPos++;
                        if (_nextChrNoAdvance(ctx) == ')') {
                            // The end of the comment has indeed been reached.
                            ctx.currColumn++;
                            ctx.results.tokens.emplace_back(
                                  Token{.type = TokenType::COMMENT,
                                        .lexeme = strLex,
                                        .line = initialLine,
                                        .column = ctx.currTokenColumn});
                            endOfCommentFound = true;
                        }
                        break;
                    default:
                        strLex.push_back(nextChr);
                        ctx.currColumn++;
                }
                ctx.lexPos++;
            }
            if (!endOfCommentFound) {
                // If the end of the comment has not been found at this point, it means we
                // have an unfinished comment.
                ctx.results.errors.emplace_back(
                      ErrorInfo{.line = ctx.currLine,
                                .column = ctx.getCurrColumn(),
                                .msg = "Source module ends in an unfinished comment."});
            }
        }

        /**
         * @brief Consumes the scanning input until an end of string character (the double
         * quotes) is found.
         *
         * @attention This internal method must be called only when the scanner knows that is in
         * a string literal - after the initial double quotes, but before the final double
         * quotes. A string literal in Oberon must be on a single line - an end of line before
         * the string's closing double quotes triggers an error.
         *
         * @param ctx the context of the ongoing scan operation.
         *
         */
        void _scanString(ScanContext& ctx) {
            std::string strLex{};
            while (!_allScanned(ctx)) {
                if (const char nextChr = _nextChrNoAdvance(ctx);
                    nextChr != '\n' && nextChr != '"') {
                    // In the middle of the string literal - just keep on acquiring the lexeme
                    strLex.push_back(_nextChrNoAdvance(ctx));
                    ctx.lexPos++;
                    ctx.currColumn++;
                } else {
                    if (nextChr == '\n') {
                        ctx.results.errors.emplace_back(ErrorInfo{
                              .line = ctx.currLine,
                              .column = ctx.getCurrColumn(),
                              .msg =
                                    "Unterminated string - strings must be on a single line."});
                    } else {
                        // Double-quotes (End of string literal) found
                        ctx.lexPos++;
                        ctx.currColumn++;
                        ctx.results.tokens.emplace_back(Token{.type = TokenType::STRING,
                                                              .lexeme = strLex,
                                                              .line = ctx.currLine,
                                                              .column = ctx.currTokenColumn});
                    }
                    break;
                }
            }
        }

        /**
         * Handles potential two-char tokens by looking ahead for the next character in the code
         * and either consuming it as part of a two-char token if it matches the expected token
         * or returns immediately after registering the one-time token found.
         *
         * @param firstChr first character of the potentially two-character's token.
         * @param expectTokenType token type to be added if the second character matches what is
         * expected.
         * @param expectSecondChr the expected look-ahead character needed to compose a
         * two-character token.
         * @param ctx the context of the ongoing scan operation.
         */
        void _handleTwoCharTokens(const char firstChr, const TokenType expectTokenType,
                                  const char expectSecondChr, ScanContext& ctx) {
            std::string strLex{firstChr};
            TokenType tokenType = expectTokenType;
            if (_nextChrMatch(ctx, expectSecondChr)) {
                strLex += expectSecondChr;
                ctx.currColumn += 2;
            } else {
                ctx.currColumn++;
                tokenType = Token::typeFromChar(firstChr);
            }
            ctx.results.tokens.emplace_back(Token{.type = tokenType,
                                                  .lexeme = strLex,
                                                  .line = ctx.currLine,
                                                  .column = ctx.currTokenColumn});
        }

        /**
         * @brief Scans the next token from the src input.
         *
         * Keeps advancing the src input until the next token (or an error) is found.
         *
         * @param ctx the context of the ongoing scan operation.
         */
        void _scanNextToken(ScanContext& ctx) {
            ctx.currTokenColumn = ctx.getCurrColumn();
            switch (const char chr = _nextChr(ctx)) {
                // Handling of single-char tokens
                case '&':
                case ',':
                case '=':
                case '#':
                case '[':
                case '-':
                case '+':
                case ']':
                case ')':
                // A close parenthesis matched in this context won't be a comment-terminating
                // character. Such a right parenthesis will be consumed by the comment-consuming
                // loop.
                case ';':
                case '*':
                // A star matched in this context won't be one of the comments terminating
                // characters. Such stars will be consumed by the comment-consuming loop.
                case '~':
                case '{':
                case '}':
                case '^':
                    try {
                        const std::string strLex{chr};
                        ctx.results.tokens.emplace_back(Token{.type = Token::typeFromChar(chr),
                                                              .lexeme = strLex,
                                                              .line = ctx.currLine,
                                                              .column = ctx.currTokenColumn});
                    } catch (std::invalid_argument const& ex) {
                        ctx.results.errors.emplace_back(ErrorInfo{.line = ctx.currLine,
                                                                  .column = ctx.getCurrColumn(),
                                                                  .msg = ex.what()});
                    }
                    ctx.currColumn++;
                    break;

                // Handling of (potentially) two-char tokens
                case '<':
                    _handleTwoCharTokens(chr, TokenType::LESS_EQUAL, '=', ctx);
                    break;
                case '>':
                    _handleTwoCharTokens(chr, TokenType::GREATER_EQUAL, '=', ctx);
                    break;
                case ':':
                    _handleTwoCharTokens(chr, TokenType::ASSIGN, '=', ctx);
                    break;
                case '.':
                    _handleTwoCharTokens(chr, TokenType::LABEL_RANGE, '.', ctx);
                    break;

                // Handling of whitespace characters (except newlines): simply consumed. Blanks
                // are not ignored when inside strings or comments.
                case ' ':
                case '\r':
                    ctx.currColumn++;
                    break;
                case '\t':
                    if (ctx.spacesPerTab > 0) {
                        ctx.currColumn += ctx.spacesPerTab;
                    } else {
                        // Since spacesPerTab is not set, column number information will be
                        // ignored until the end of the current line.
                        ctx.ignoreCurrColumn = true;
                    }
                    break;
                // Handling of new lines outside comments - the comment handler takes care of
                // new lines in the middle of comments
                case '\n':
                    ctx.currLine++;
                    // Column number information taken into account again - at least until a
                    // '\t' is found in the line whose scan is starting if the number of
                    // spacesPerTab is not set.
                    ctx.ignoreCurrColumn = false;
                    ctx.currColumn = 1;
                    break;

                // Handling of (potential) comments. If the "(" is followed by an asterisk "*"
                // and indeed starts a comment, the scanning process will be captured by the
                // comment-consuming loop.
                case '(': {
                    if (_nextChrMatch(ctx, '*')) {
                        // Found start of comment - "consume" it.
                        ctx.currColumn++;
                        ctx.currTokenColumn = ctx.currColumn;
                        _scanComment(ctx);
                        break;
                    } // Found a single-character open parenthesis token.
                    const std::string strLex{chr};
                    ctx.results.tokens.emplace_back(Token{.type = Token::typeFromChar(chr),
                                                          .lexeme = strLex,
                                                          .line = ctx.currLine,
                                                          .column = ctx.currTokenColumn});
                    ctx.currColumn++;
                    break;
                }
                // Handling of string literals. String literals cannot contain internal double
                // quotes and cannot span across multiple lines.
                case '"':
                    ctx.currColumn++;
                    ctx.currTokenColumn = ctx.currColumn;
                    _scanString(ctx);
                    break;

                default:
                    if (std::isalpha(chr) != 0) {
                        ctx.currColumn++;
                        // Don't update currTokenColumn since the previous character will be the
                        // first character of the yet to be scanned identifier
                        _scanIdentifier(ctx, chr);
                    } else if (std::isdigit(chr) != 0) {
                        ctx.currColumn++;
                        // Don't update currTokenColumn since the previous character will be the
                        // first character of the yet to be scanned number or single char string
                        _scanNumberOrSingleCharString(ctx, chr);
                    } else {
                        ctx.results.errors.emplace_back(ErrorInfo{
                              .line = ctx.currLine,
                              .column = ctx.getCurrColumn(),
                              .msg =
                                    std::string{"Unexpected character, '"} + chr + "' found."});
                        ctx.currColumn++;
                    }
            }
        }
    } // namespace

    ScanResults scanSrcFile(const std::string& srcFilePath, bool lowerCaseKeywords,
                            uint8_t spacesPerTab) {
        std::string src;
        {
            // Scope for the srcFile ifstream - allows its destruction before lexing
            // work actually takes place.
            std::ifstream srcFile(srcFilePath);
            if (!srcFile.is_open()) {
                // Some error happened during file opening.
                ScanResults res;
                res.errors.emplace_back(
                      ErrorInfo{.msg = std::string{"File '"} + srcFilePath +
                                       "' not found or not available for reading."});
                return res;
            }
            while (srcFile) {
                // Reads the source file line by line and stores its contents in
                // primary memory.
                std::string nextLine;
                std::getline(srcFile, nextLine);
                src.append(nextLine);
                // As std::getline consumes the delimiter - in this case the default
                // "\n" - we add it to the string if the end of the file hasn't still been
                // reached.
                if (srcFile) {
                    src.append("\n");
                }
            }
            srcFile.close();
            if (srcFile.bad()) {
                // Some error happened during the file read operation.
                // NOTE: fail() should not be used for detecting read errors in this
                //       context: a file whose last line consists solely of the
                //       new line character would cause getline to return no
                //       character and set both eofbit and failbit.
                ScanResults res;
                std::array<char, ERR_MSG_BUFF_SIZE> errBuf{};

#if defined(__MSVCRT__) || defined(_MSC_VER)
                // On Windows, using the Microsoft supplied runtime, the safe
                // version of strerror is not strerror_r, but strerror_s.
                strerror_s(errBuf.data(), errBuf.size(), errno);
#else
                strerror_r(errno, errBuf.data(), errBuf.size());
#endif
                res.errors.emplace_back(
                      ErrorInfo{.msg = std::string{"Error while reading '" + srcFilePath +
                                                   "': " + errBuf.data()}});
                return res;
            }
        }
        // Scans the source file from its in-memory storage.
        return scan(src, lowerCaseKeywords, spacesPerTab);
    }


    ScanResults scan(const std::string& src, bool lowerCaseKeywords, uint8_t spacesPerTab) {
        ScanContext ctx(src, lowerCaseKeywords, spacesPerTab);

        while (!_allScanned(ctx)) {
            _scanNextToken(ctx);
        }

        // An End-of-Module is always inserted to provide a clear indicator for the parser.
        ctx.results.tokens.emplace_back(Token{.type = TokenType::EOM,
                                              .lexeme = "",
                                              .line = ctx.currLine,
                                              .column = ctx.currColumn});

        return ctx.results;
    }

} // namespace obe::scanner
