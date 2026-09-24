/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * applications/vold/NetlinkHandler.cpp — AOSP system/vold/NetlinkHandler.cpp
 */

#include "NetlinkHandler.h"
#include "VolumeManager.h"

#include <android-base/logging.h>

namespace android {
namespace vold {

NetlinkHandler::NetlinkHandler(int listenerSocket)
    : NetlinkListener(listenerSocket, NETLINK_FORMAT_ASCII) {}

NetlinkHandler::~NetlinkHandler() {}

int NetlinkHandler::start() {
    return this->startListener();
}

int NetlinkHandler::stop() {
    return this->stopListener();
}

void NetlinkHandler::onEvent(NetlinkEvent* evt) {
    VolumeManager* vm = VolumeManager::Instance();
    const char* subsys = evt->getSubsystem();

    if (!subsys) {
        LOG(WARNING) << "No subsystem found in netlink event";
        return;
    }

    if (strcmp(subsys, "block") == 0) {
        vm->handleBlockEvent(evt);
    }
}

}  // namespace vold
}  // namespace android
