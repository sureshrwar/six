/*
 * applications/vold/compat/binder/Status.h
 *
 * AOSP <binder/Status.h> implementation for VoldNativeService.
 */

#ifndef _BINDER_STATUS_H
#define _BINDER_STATUS_H

#include "../six_cxx_compat.h"

namespace android {
namespace binder {

class Status {
public:
    static Status ok() { return Status(0, ""); }

    static Status fromServiceSpecificError(int32_t err, const char* msg = "") {
        return Status(err ? err : -1, msg ? msg : "ServiceSpecificError");
    }

    static Status fromStatusT(status_t status) {
        if (status == OK) return ok();
        return Status(status, "status_t error");
    }

    Status() : mCode(0), mMessage("") {}
    Status(int32_t code, const std::string& msg) : mCode(code), mMessage(msg) {}

    bool isOk() const { return mCode == 0; }
    int32_t exceptionCode() const { return mCode; }
    int32_t serviceSpecificErrorCode() const { return mCode; }
    const std::string& exceptionMessage() const { return mMessage; }

private:
    int32_t mCode;
    std::string mMessage;
};

} // namespace binder
} // namespace android

#endif /* _BINDER_STATUS_H */
