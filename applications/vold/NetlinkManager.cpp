/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * applications/vold/NetlinkManager.cpp — AOSP system/vold/NetlinkManager.cpp
 */

#include "NetlinkManager.h"
#include <android-base/logging.h>

namespace android {
namespace vold {

NetlinkManager* NetlinkManager::sInstance = nullptr;

NetlinkManager* NetlinkManager::Instance() {
    if (!sInstance) {
        sInstance = new NetlinkManager();
    }
    return sInstance;
}

NetlinkManager::NetlinkManager() : mHandler(nullptr), mSock(-1) {}

NetlinkManager::~NetlinkManager() {}

int NetlinkManager::start() {
    struct sockaddr_nl nladdr;
    memset(&nladdr, 0, sizeof(nladdr));
    nladdr.nl_family = AF_NETLINK;
    nladdr.nl_pid = getpid();
    nladdr.nl_groups = 1;

    mSock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (mSock >= 0) {
        if (bind(mSock, (struct sockaddr*)&nladdr, sizeof(nladdr)) < 0) {
            close(mSock);
            mSock = -1;
        }
    }
    if (mSock < 0) {
        /* Linux 2.0.11 delivers kernel NETLINK_KOBJECT_UEVENT via /dev/binder */
        mSock = open("/dev/binder", O_RDONLY);
    }

    mHandler = new NetlinkHandler(mSock);
    if (mHandler->start() != 0) {
        PLOG(ERROR) << "Unable to start NetlinkHandler";
        if (mSock >= 0) close(mSock);
        mSock = -1;
        return -1;
    }

    LOG(INFO) << "NetlinkManager started (PF_NETLINK socket fd=" << mSock << ")";
    return 0;
}

int NetlinkManager::stop() {
    if (mHandler) {
        mHandler->stop();
        delete mHandler;
        mHandler = nullptr;
    }
    if (mSock >= 0) {
        close(mSock);
        mSock = -1;
    }
    return 0;
}

}  // namespace vold
}  // namespace android
