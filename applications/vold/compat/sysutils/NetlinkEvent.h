/*
 * applications/vold/compat/sysutils/NetlinkEvent.h
 *
 * AOSP libsysutils <sysutils/NetlinkEvent.h> implementation for SIX.
 * Decodes kernel NETLINK_KOBJECT_UEVENT payloads into Action, Subsystem,
 * and KEY=VALUE parameter table queried via findParam().
 */

#ifndef _SYSUTILS_NETLINKEVENT_H
#define _SYSUTILS_NETLINKEVENT_H

#include "../six_cxx_compat.h"

#define NETLINK_FORMAT_ASCII 0

class NetlinkEvent {
public:
    enum class Action {
        kUnknown = 0,
        kAdd = 1,
        kRemove = 2,
        kChange = 3,
        kLinkUp = 4,
        kLinkDown = 5,
        kAddressUpdated = 6,
        kAddressRemoved = 7,
        kRdnss = 8,
        kRouteUpdated = 9,
        kRouteRemoved = 10,
        kBind = 11,
        kUnbind = 12,
    };

    NetlinkEvent() : mAction(Action::kUnknown), mParamCount(0) {
        mSubsystem[0] = '\0';
    }

    ~NetlinkEvent() {}

    bool decode(char* buffer, int size, int /*format*/ = NETLINK_FORMAT_ASCII) {
        mAction = Action::kUnknown;
        mSubsystem[0] = '\0';
        mParamCount = 0;

        if (!buffer || size <= 0) return false;
        buffer[size] = '\0';

        int i = 0;
        while (i < size) {
            /* Advance to end of token (NUL or newline) */
            char* line = &buffer[i];
            int len = 0;
            while (i + len < size && buffer[i + len] != '\0' && buffer[i + len] != '\n') {
                len++;
            }
            buffer[i + len] = '\0';
            i += len + 1;

            if (len == 0) continue;

            char* eq = strchr(line, '=');
            if (!eq) {
                /* Header line e.g. "add@/devices/..." */
                if (strncmp(line, "add@", 4) == 0) mAction = Action::kAdd;
                else if (strncmp(line, "remove@", 7) == 0) mAction = Action::kRemove;
                else if (strncmp(line, "change@", 7) == 0) mAction = Action::kChange;
                continue;
            }

            *eq = '\0';
            const char* key = line;
            const char* val = eq + 1;

            if (strcmp(key, "ACTION") == 0) {
                if (strcmp(val, "add") == 0) mAction = Action::kAdd;
                else if (strcmp(val, "remove") == 0) mAction = Action::kRemove;
                else if (strcmp(val, "change") == 0) mAction = Action::kChange;
            } else if (strcmp(key, "SUBSYSTEM") == 0) {
                strncpy(mSubsystem, val, sizeof(mSubsystem) - 1);
                mSubsystem[sizeof(mSubsystem) - 1] = '\0';
            }

            if (mParamCount < 24) {
                strncpy(mKeys[mParamCount], key, sizeof(mKeys[0]) - 1);
                mKeys[mParamCount][sizeof(mKeys[0]) - 1] = '\0';
                strncpy(mVals[mParamCount], val, sizeof(mVals[0]) - 1);
                mVals[mParamCount][sizeof(mVals[0]) - 1] = '\0';
                mParamCount++;
            }
        }
        return mAction != Action::kUnknown || mParamCount > 0;
    }

    Action getAction() const { return mAction; }
    const char* getSubsystem() const { return mSubsystem; }

    const char* findParam(const char* paramName) const {
        if (!paramName) return nullptr;
        for (int i = 0; i < mParamCount; i++) {
            if (strcmp(mKeys[i], paramName) == 0) {
                return mVals[i];
            }
        }
        return nullptr;
    }

private:
    Action mAction;
    char mSubsystem[64];
    int mParamCount;
    char mKeys[24][32];
    char mVals[24][128];
};

#endif /* _SYSUTILS_NETLINKEVENT_H */
