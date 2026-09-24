/*
 * applications/vold/compat/android-base/logging.h
 *
 * AOSP <android-base/logging.h> stream logger shim (LOG(INFO), PLOG(ERROR), etc.)
 */

#ifndef _ANDROID_BASE_LOGGING_H
#define _ANDROID_BASE_LOGGING_H

#include "../six_cxx_compat.h"

#define DEBUG   0
#define INFO    1
#define WARNING 2
#define ERROR   3
#define FATAL   4

namespace android {
namespace base {

class LogStream {
public:
    LogStream(int level, bool appendErrno = false)
        : mLevel(level), mAppendErrno(appendErrno), mSavedErrno(errno) {
        const char* tag = "I";
        if (level == DEBUG) tag = "D";
        else if (level == WARNING) tag = "W";
        else if (level == ERROR || level == FATAL) tag = "E";
        char prefix[32];
        snprintf(prefix, sizeof(prefix), "vold %s: ", tag);
        mBuf += prefix;
    }

    ~LogStream() {
        if (mAppendErrno) {
            char ebuf[64];
            snprintf(ebuf, sizeof(ebuf), ": %s (errno=%d)", strerror(mSavedErrno), mSavedErrno);
            mBuf += ebuf;
        }
        /* Write to /tmp/vold.log so daemon logs can be inspected anytime */
        mkdir("/tmp", 0777);
        int fd = open("/tmp/vold.log", O_CREAT | O_WRONLY, 0644);
        if (fd >= 0) {
            lseek(fd, 0, 2);
            write(fd, mBuf.c_str(), mBuf.size());
            write(fd, "\n", 1);
            close(fd);
        }
    }

    LogStream& operator<<(const char* s) {
        if (s) mBuf += s;
        return *this;
    }
    LogStream& operator<<(const std::string& s) {
        mBuf += s;
        return *this;
    }
    LogStream& operator<<(int v) {
        mBuf += std::to_string(v);
        return *this;
    }
    LogStream& operator<<(unsigned int v) {
        mBuf += std::to_string(v);
        return *this;
    }
    LogStream& operator<<(long v) {
        mBuf += std::to_string((int)v);
        return *this;
    }
    LogStream& operator<<(unsigned long v) {
        mBuf += std::to_string((unsigned int)v);
        return *this;
    }

private:
    int mLevel;
    bool mAppendErrno;
    int mSavedErrno;
    std::string mBuf;
};

} // namespace base
} // namespace android

#define LOG(severity) ::android::base::LogStream(severity, false)
#define PLOG(severity) ::android::base::LogStream(severity, true)

#endif /* _ANDROID_BASE_LOGGING_H */
