#ifndef WATCHOS_SECURITY_ACCESS_POLICY_HPP
#define WATCHOS_SECURITY_ACCESS_POLICY_HPP

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace watchos {
namespace security {

inline std::string normalizeAccessOperation(std::string operation) {
    const auto notSpace = [](unsigned char character) {
        return std::isspace(character) == 0;
    };
    const auto first = std::find_if(operation.begin(), operation.end(), notSpace);
    const auto last = std::find_if(operation.rbegin(), operation.rend(), notSpace).base();
    operation = first < last ? std::string(first, last) : std::string();
    std::transform(operation.begin(), operation.end(), operation.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    if (operation == "EXEC") {
        operation = "EXECUTE";
    }
    return operation;
}

inline bool isSupportedAccessOperation(const std::string& operation) {
    return operation == "READ" || operation == "WRITE" || operation == "EXECUTE";
}

inline std::string normalizeAccessPolicy(std::string policy) {
    const auto notSpace = [](unsigned char character) {
        return std::isspace(character) == 0;
    };
    const auto first = std::find_if(policy.begin(), policy.end(), notSpace);
    const auto last = std::find_if(policy.rbegin(), policy.rend(), notSpace).base();
    policy = first < last ? std::string(first, last) : std::string();
    std::transform(policy.begin(), policy.end(), policy.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    if (policy != "ALLOW" && policy != "DENY") {
        throw std::invalid_argument("Access policy must be ALLOW or DENY");
    }
    return policy;
}

} // namespace security
} // namespace watchos

#endif // WATCHOS_SECURITY_ACCESS_POLICY_HPP
