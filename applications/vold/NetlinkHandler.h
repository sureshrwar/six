/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * applications/vold/NetlinkHandler.h — AOSP system/vold/NetlinkHandler.h
 */

#ifndef ANDROID_VOLD_NETLINK_HANDLER_H
#define ANDROID_VOLD_NETLINK_HANDLER_H

#include <sysutils/NetlinkListener.h>

namespace android {
namespace vold {

class NetlinkHandler : public NetlinkListener {
public:
    explicit NetlinkHandler(int listenerSocket);
    virtual ~NetlinkHandler();

    int start();
    int stop();

protected:
    void onEvent(NetlinkEvent* evt) override;
};

}  // namespace vold
}  // namespace android

#endif  // ANDROID_VOLD_NETLINK_HANDLER_H
