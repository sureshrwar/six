/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * applications/vold/NetlinkManager.h — AOSP system/vold/NetlinkManager.h
 */

#ifndef ANDROID_VOLD_NETLINK_MANAGER_H
#define ANDROID_VOLD_NETLINK_MANAGER_H

#include "NetlinkHandler.h"

namespace android {
namespace vold {

class NetlinkManager {
public:
    virtual ~NetlinkManager();

    static NetlinkManager* Instance();

    int start();
    int stop();

    NetlinkHandler* getHandler() const { return mHandler; }

private:
    NetlinkManager();

    static NetlinkManager* sInstance;

    NetlinkHandler* mHandler;
    int mSock;
};

}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_NETLINK_MANAGER_H
