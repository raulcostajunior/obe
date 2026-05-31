#ifndef OBE_SCANNER_HPP
#define OBE_SCANNER_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "ErrorInfo.hpp"
#include "Token.hpp"

namespace obe::scanner {

    struct ScanResults {
        std::vector<Token> tokens;
        std::vector<ErrorInfo> errors;
    };

    /**
     * @brief Scans a given source file, returning the list of tokens found in it.
     *
     * @param srcFilePath the path of the source file to be scanned.
     * @param lowerCaseKeywords use lowercase keywords?
     * @param spacesPerTab to how many spaces does a tab correspond? (zero means unknown)
     *
     * @return list of tokens (and the lexical errors) in the file.
     *
     * @note Lower case keywords mode has been introduced because of the high number of
     * opinions against all upper case keywords.
     */
    ScanResults scanSrcFile(const std::string& srcFilePath, bool lowerCaseKeywords = false,
                            uint8_t spacesPerTab = 0);

    /**
     * @brief Scans a string with the contents of a source file.
     *
     * @param src the contents of a source file.
     * @param lowerCaseKeywords use lowercase keywords?
     * @param spacesPerTab how many spaces correspond to each tab? (zero means unknown)
     *
     * @return list of tokens (and the lexical errors) in the contents.
     *
     * @note Lower case keywords mode has been introduced because of the high number of
     * opinions against all upper case keywords.
     */
    ScanResults scan(const std::string& src, bool lowerCaseKeywords = false,
                     uint8_t spacesPerTab = 0);

} // namespace obe::scanner
#endif
