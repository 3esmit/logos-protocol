#ifndef LOGOS_CALL_ERROR_H
#define LOGOS_CALL_ERROR_H

// The canonical cross-module call error — the C++ face of the protocol's
// {code, message, origin} error JSON (see makeErrorJson / lp_invoke's
// out_error_json). Deliberately Qt-free: it crosses into Qt-free module
// code (universal C++ impls catch LogosCallError from generated typed
// wrappers), so only std types appear here.

#include <stdexcept>
#include <string>

namespace logos {

// Error codes are lowercase snake_case strings, mirroring the C ABI's JSON
// contract rather than an enum so the set can grow (transport-level codes,
// provider dispatch errors) without an ABI break.
//
// Currently produced:
//   "object_unavailable" — the target module/object could not be acquired
//                          (not loaded, not published, or transport failure).
struct CallError {
    std::string code;     // empty = no error
    std::string message;
    std::string origin;   // module the error originated from / was detected for

    bool ok() const { return code.empty(); }
    void clear() { code.clear(); message.clear(); origin.clear(); }
};

// Thrown by generated typed client wrappers when the underlying remote call
// fails — so a caller can distinguish "the call failed" from a legitimate
// default-valued result. Generated provider dispatch catches anything that
// escapes the module author's code and converts it into an ordinary method
// failure, so an unhandled LogosCallError never kills the module process.
class LogosCallError : public std::runtime_error {
public:
    explicit LogosCallError(CallError err)
        : std::runtime_error(err.code + ": " + err.message +
                             (err.origin.empty() ? "" : " (origin: " + err.origin + ")")),
          error(std::move(err)) {}

    CallError error;
};

} // namespace logos

#endif // LOGOS_CALL_ERROR_H
