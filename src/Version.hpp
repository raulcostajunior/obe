
#ifndef OBE_VERSION_HPP
#define OBE_VERSION_HPP

#include <sstream>
#include <string>

namespace obe {

    constexpr int OBC_VERSION_MAJOR{0};
    constexpr int OBC_VERSION_MINOR{1};
    constexpr int OBC_VERSION_PATCH{0};

    std::string inline obcVersion() {
        std::ostringstream ostr;
        ostr << OBC_VERSION_MAJOR << "." << OBC_VERSION_MINOR << "." << OBC_VERSION_PATCH;
        return ostr.str();
    }

} // namespace obe


#endif
