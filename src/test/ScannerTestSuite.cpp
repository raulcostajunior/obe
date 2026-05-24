#include <gtest/gtest.h>

#include <filesystem>

#include "Scanner.hpp"

using namespace obc;

TEST(ScannerTests, TestEmptyFile) {
    // An empty file must have the EOM token and no errors.
    const auto [tokens, errors] = Scanner::scan("");
    EXPECT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens.at(tokens.size() - 1).type, TokenType::EOM);
    EXPECT_EQ(errors.size(), 0);
}

TEST(ScannerTests, TestLowerCaseKeywords) {
    const std::string lowerCaseSrc{
          R"(
module LowerCaseModule;

procedure WriteCounters;
    var i: integer;
    i := 0;
    while i < 4 do
      WriteInt(i);
      i := i + 1;
    end
end WriteCounters;

begin
  WriteCounters;
end LowerCaseModule.
)"};

    const std::string upperCaseSrc{
          R"(
MODULE UpperCaseModule;

PROCEDURE WriteCounters;
    VAR i: integer;
    i := 0;
    WHILE i < 4 DO
      WriteInt(i);
      i := i + 1;
    END
END WriteCounters;

BEGIN
  WriteCounters;
END LowerCaseModule.
)"};

    constexpr int expectTokens =
          42; // Expected number of tokens in src files used in this test.

    // A lexically valid file with lowerCase keywords should be successfully parsed with all
    // the keywords identified with the scanner in lowerCaseKeywords mode.
    auto res = Scanner::scan(lowerCaseSrc, true);
    ASSERT_EQ(res.errors.size(), 0);
    ASSERT_EQ(res.tokens.size(), expectTokens);
    EXPECT_EQ(res.tokens.at(0).type, TokenType::MODULE);
    EXPECT_EQ(res.tokens.at(0).line, 2);
    EXPECT_EQ(res.tokens.at(0).column, 1);
    EXPECT_EQ(res.tokens.at(1).type, TokenType::IDENT);
    EXPECT_EQ(res.tokens.at(38).type, TokenType::END);
    EXPECT_EQ(res.tokens.at(39).type, TokenType::IDENT);
    EXPECT_EQ(res.tokens.at(39).lexeme, "LowerCaseModule");
    EXPECT_EQ(res.tokens.at(39).line, 15);
    EXPECT_EQ(res.tokens.at(39).column, 5);
    EXPECT_EQ(res.tokens.at(40).type, TokenType::DOT);
    EXPECT_EQ(res.tokens.at(res.tokens.size() - 1).type, TokenType::EOM);

    // A lexically valid file with lowerCaseKeywords should be successfully parsed with none
    // of the keywords identified when the scanner is in the default uppercase-keywords mode.
    // All keywords should be identified as token identifiers.
    res = Scanner::scan(lowerCaseSrc);
    ASSERT_EQ(res.errors.size(), 0);
    ASSERT_EQ(res.tokens.size(), expectTokens);
    EXPECT_EQ(res.tokens.at(0).type, TokenType::IDENT);
    EXPECT_EQ(res.tokens.at(0).lexeme, "module");
    EXPECT_EQ(res.tokens.at(1).type, TokenType::IDENT);
    EXPECT_EQ(res.tokens.at(38).type, TokenType::IDENT);
    EXPECT_EQ(res.tokens.at(38).lexeme, "end");
    EXPECT_EQ(res.tokens.at(39).type, TokenType::IDENT);
    EXPECT_EQ(res.tokens.at(39).lexeme, "LowerCaseModule");
    EXPECT_EQ(res.tokens.at(39).line, 15);
    EXPECT_EQ(res.tokens.at(39).column, 5);
    EXPECT_EQ(res.tokens.at(40).type, TokenType::DOT);
    EXPECT_EQ(res.tokens.at(41).type, TokenType::EOM);

    // A lexically valid file with uppercase keywords should be successfully parsed with all
    // the keywords identified with the scanner in the default uppercase-keywords mode.
    res = Scanner::scan(upperCaseSrc);
    ASSERT_EQ(res.errors.size(), 0);
    ASSERT_EQ(res.tokens.size(), expectTokens);
    EXPECT_EQ(res.tokens.at(0).type, TokenType::MODULE);
    EXPECT_EQ(res.tokens.at(1).type, TokenType::IDENT);
    EXPECT_EQ(res.tokens.at(38).type, TokenType::END);
    EXPECT_EQ(res.tokens.at(39).type, TokenType::IDENT);
    EXPECT_EQ(res.tokens.at(39).lexeme, "LowerCaseModule");
    EXPECT_EQ(res.tokens.at(40).type, TokenType::DOT);
    EXPECT_EQ(res.tokens.at(41).type, TokenType::EOM);

    // A lexically valid file with uppercase keywords should be successfully parsed with none
    // of the keywords identified when the scanner is in lowercase-keywords mode.
    // All keywords should be identified as token identifiers.
    res = Scanner::scan(upperCaseSrc, true);
    ASSERT_EQ(res.errors.size(), 0);
    ASSERT_EQ(res.tokens.size(), expectTokens);
    EXPECT_EQ(res.tokens.at(0).type, TokenType::IDENT);
    EXPECT_EQ(res.tokens.at(0).lexeme, "MODULE");
    EXPECT_EQ(res.tokens.at(1).type, TokenType::IDENT);
    EXPECT_EQ(res.tokens.at(38).type, TokenType::IDENT);
    EXPECT_EQ(res.tokens.at(38).lexeme, "END");
    EXPECT_EQ(res.tokens.at(39).type, TokenType::IDENT);
    EXPECT_EQ(res.tokens.at(39).lexeme, "LowerCaseModule");
    EXPECT_EQ(res.tokens.at(40).type, TokenType::DOT);
    EXPECT_EQ(res.tokens.at(41).type, TokenType::EOM);
}

TEST(ScannerTests, TestModuleWithUnfinishedComment) {
    // A source file that finishes with an unfinished comment must trigger an error.
    const std::string unfinishedCommentMsg{"Source module ends in an unfinished comment."};

    const std::string moduleSrc{R"(
MODULE UnfinishedComment;

(* This is a multiline, open ended
comment and should be rejected.
)"};
    auto [tokens, errors] = Scanner::scan(moduleSrc);
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens.at(0).type, TokenType::MODULE);
    EXPECT_EQ(tokens.at(1).type, TokenType::IDENT);
    EXPECT_EQ(tokens.at(1).lexeme, "UnfinishedComment");
    EXPECT_EQ(tokens.at(2).type, TokenType::SEMICOLON);
    EXPECT_EQ(tokens.at(tokens.size() - 1).type, TokenType::EOM);

    ASSERT_EQ(errors.size(), 1);
    EXPECT_EQ(errors.at(0).line, 6);
    EXPECT_EQ(errors.at(0).column, 1);
    EXPECT_EQ(errors.at(0).msg, unfinishedCommentMsg);
}

TEST(ScannerTests, TestModuleWithInvalidSymbol) {
    // A source file with an invalid terminal symbol must trigger a lexical error.
    // The error must not stop the scanner, which must continue finding tokens
    // until the end of the module.
    const std::string invalidSymbolSrcPrefix{R"(
MODULE WithInvalidSymbol;

(* ? is not a valid terminal in the language; it should be accepted in a 
comment, but trigger an error when outside a comment; tabs used for indentation *)

BEGIN
)"};
    // Note: no raw strings to specify the tabs as the source editor can be configured to
    // convert tabs to spaces on save. Use explicit "\t" instead.
    const std::string invalidSymbolSrc{invalidSymbolSrcPrefix +
                                       "\tVAR i: INTEGER?;\n\tWriteInt(i)\nEND.\n"};
    auto [tokens, errors] = Scanner::scan(invalidSymbolSrc, false, 4);
    ASSERT_EQ(tokens.size(), 17);
    EXPECT_EQ(tokens.at(0).type, TokenType::MODULE);
    EXPECT_EQ(tokens.at(1).type, TokenType::IDENT);
    EXPECT_EQ(tokens.at(1).lexeme, "WithInvalidSymbol");
    EXPECT_EQ(tokens.at(3).type, TokenType::COMMENT);
    // Tokens 8 and 9 are the ones around the invalid symbol in the source.
    // We verify if they have been properly scanned.
    EXPECT_EQ(tokens.at(8).type, TokenType::IDENT);
    EXPECT_EQ(tokens.at(8).lexeme, "INTEGER");
    EXPECT_EQ(tokens.at(9).type, TokenType::SEMICOLON);
    EXPECT_EQ(tokens.at(15).type, TokenType::DOT);
    EXPECT_EQ(tokens.at(tokens.size() - 1).type, TokenType::EOM);

    ASSERT_EQ(errors.size(), 1);
    // The R-String for the source starts with a new line, so the MODULE line
    // is already line 2 in the source - that's the reason for line with the
    // invalid symbol being line 8, not 7.
    EXPECT_EQ(errors.at(0).line, 8);
    // Since the number of spaces per tab has been specified, the column position for the
    // error must be defined
    EXPECT_EQ(errors.at(0).column, 19);
    EXPECT_EQ(errors.at(0).msg, std::string{"Unexpected character, '?' found."});

    auto [tokensUnkSpaces, errorsUnkSpaces] = Scanner::scan(invalidSymbolSrc);
    ASSERT_EQ(tokensUnkSpaces.size(), 17);
    ASSERT_EQ(errorsUnkSpaces.size(), 1);
    EXPECT_EQ(errorsUnkSpaces.at(0).line, 8);
    // With the number of spaces per tab unspecified (its default value of 0), the column
    // position for the error must be undefined (numerical value -1).
    EXPECT_EQ(errorsUnkSpaces.at(0).column, -1);
}

TEST(ScannerTests, TestModuleWithStringLiteral) {
    namespace fs = std::filesystem;
    const std::string srcFilePath{
          fs::path(__FILE__).parent_path().append("oberon_src").append("Hello.Mod").string()};
    auto [tokens, errors] = Scanner::scanSrcFile(srcFilePath);
    ASSERT_EQ(tokens.size(), 18);
    EXPECT_EQ(tokens.at(0).type, TokenType::MODULE);
    EXPECT_EQ(tokens.at(1).type, TokenType::IDENT);
    EXPECT_EQ(tokens.at(1).lexeme, "Hello");
    EXPECT_EQ(tokens.at(3).type, TokenType::BEGIN);
    EXPECT_EQ(tokens.at(4).type, TokenType::IDENT);
    EXPECT_EQ(tokens.at(4).lexeme, "WriteLn");
    EXPECT_EQ(tokens.at(5).type, TokenType::LEFT_PAREN);
    EXPECT_EQ(tokens.at(6).type, TokenType::STRING);
    EXPECT_EQ(tokens.at(6).lexeme, "Hello world!");
    EXPECT_EQ(tokens.at(6).line, 3);
    EXPECT_EQ(tokens.at(6).column, 24);
    EXPECT_EQ(tokens.at(tokens.size() - 1).type, TokenType::EOM);
}

TEST(ScannerTests, TestModuleWithNumericLiterals) {
    namespace fs = std::filesystem;
    const std::string srcFilePath{fs::path(__FILE__)
                                          .parent_path()
                                          .append("oberon_src")
                                          .append("NumLiterals.Mod")
                                          .string()};
    auto [tokens, errors] = Scanner::scanSrcFile(srcFilePath);
    ASSERT_EQ(tokens.size(), 74);
    // InvalidRealNoIntPart = .2E+4 must be scanned as a dot, followed by an invalid hex int
    // (2E), a plus, and a 4 integer.
    EXPECT_EQ(tokens.at(4).type, TokenType::IDENT);
    EXPECT_EQ(tokens.at(4).lexeme, "InvalidRealNoIntPart");
    EXPECT_EQ(tokens.at(5).type, TokenType::EQUAL);
    EXPECT_EQ(tokens.at(6).type, TokenType::DOT);
    EXPECT_EQ(tokens.at(7).type, TokenType::PLUS);
    EXPECT_EQ(tokens.at(8).type, TokenType::INTEGER);
    EXPECT_EQ(tokens.at(8).lexeme, "4");
    // ValidRealNoDecimalScale must be scanned as a REAL with the appropriate lexeme.
    EXPECT_EQ(tokens.at(15).type, TokenType::IDENT);
    EXPECT_EQ(tokens.at(15).lexeme, "ValidRealNoDecimalScale");
    EXPECT_EQ(tokens.at(16).type, TokenType::EQUAL);
    EXPECT_EQ(tokens.at(17).type, TokenType::REAL);
    EXPECT_EQ(tokens.at(17).lexeme, "23.E+2");
    EXPECT_EQ(tokens.at(19).type, TokenType::COMMENT);
    // ValidRealNoDecimalNScale must be scanned as a REAL with the appropriate lexeme.
    EXPECT_EQ(tokens.at(21).type, TokenType::IDENT);
    EXPECT_EQ(tokens.at(21).lexeme, "ValidRealNoDecimalNoScale");
    EXPECT_EQ(tokens.at(22).type, TokenType::EQUAL);
    EXPECT_EQ(tokens.at(23).type, TokenType::REAL);
    EXPECT_EQ(tokens.at(23).lexeme, "23.");
    // ValidHexInt must be scanned as an INTEGER with the appropriate lexeme.
    EXPECT_EQ(tokens.at(26).type, TokenType::IDENT);
    EXPECT_EQ(tokens.at(26).lexeme, "ValidHexInt");
    EXPECT_EQ(tokens.at(27).type, TokenType::EQUAL);
    EXPECT_EQ(tokens.at(28).type, TokenType::INTEGER);
    EXPECT_EQ(tokens.at(28).lexeme, "87AH");
    // 2AX must be recognized as a valid one-char string. 2A is 42 in base 10 and is the code
    // for the '*'.
    EXPECT_EQ(tokens.at(37).type, TokenType::STRING);
    EXPECT_EQ(tokens.at(37).lexeme, "*");
    EXPECT_EQ(tokens.at(37).line, 9);
    EXPECT_EQ(tokens.at(37).column, 36);

    EXPECT_EQ(tokens.at(tokens.size() - 1).type, TokenType::EOM);

    ASSERT_EQ(errors.size(), 4);
    EXPECT_EQ(errors.at(0).line, 3);
    EXPECT_EQ(errors.at(0).column, 41);
    EXPECT_EQ(errors.at(0).msg, "Hexadecimal number must be terminated with an 'H'.");
    EXPECT_EQ(errors.at(1).line, 4);
    EXPECT_EQ(errors.at(1).column, 45);
    EXPECT_EQ(errors.at(1).msg,
              "Real number scale factor must start with an 'E' followed by either a '+' or '-' "
              "signal.");
    EXPECT_EQ(errors.at(2).line, 8);
    EXPECT_EQ(errors.at(2).column, 31);
    EXPECT_EQ(errors.at(2).msg, "Hexadecimal number must be terminated with an 'H'.");
    EXPECT_EQ(errors.at(3).line, 12);
    EXPECT_EQ(errors.at(3).column, 41);
    EXPECT_EQ(errors.at(3).msg, "Real numbers must use only digits between 0 and 9.");
}
