#ifndef OBE_ERRORINFO_HPP
#define OBE_ERRORINFO_HPP

#include <string>

namespace obe {

    struct ErrorInfo {
        int line = -1;   // -1 flags for a non-locatable error
        int column = -1; // -1 flags for a non-locatable error
        std::string msg;
    };

    std::ostream& operator<<(std::ostream& ostr, const ErrorInfo& errInf);

} // namespace obe

#endif // OBE_ERRORINFO_HPP
