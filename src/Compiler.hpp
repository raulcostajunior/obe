#ifndef OBE_COMPILER_HPP
#define OBE_COMPILER_HPP

#include <vector>

#include "ErrorInfo.hpp"

namespace obe {
    struct CompilationResults {
        std::vector<ErrorInfo> errors;
        // TODO: Add data structure representing the generated WASM module
    };

    // TODO: the compiler is the "driver". It should be able to compile from file or from string
    // (
    //       remove the scan from file from the scanner; only the compiler takes care of reading
    //       from the FS.
    class Compiler {
       public:
       private:
    };

} // namespace obe

#endif // OBE_COMPILER_HPP
