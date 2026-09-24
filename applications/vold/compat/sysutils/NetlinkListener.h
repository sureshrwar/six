/*
 * applications/vold/compat/sysutils/NetlinkListener.h
 *
 * AOSP libsysutils <sysutils/NetlinkListener.h> base class for NetlinkHandler.
 */

#ifndef _SYSUTILS_NETLINKLISTENER_H
#define _SYSUTILS_NETLINKLISTENER_H

#include "NetlinkEvent.h"

class NetlinkListener {
public:
    NetlinkListener(int socket, int format = NETLINK_FORMAT_ASCII)
        : mSocket(socket), mFormat(format), mRunning(false) {}

    virtual ~NetlinkListener() {}

    int startListener() {
        mRunning = true;
        return 0;
    }

    int stopListener() {
        mRunning = false;
        return 0;
    }

    int getSocket() const { return mSocket; }

    bool dispatchUevent(const struct binder_uevent_msg* uev) {
        if (!mRunning || !uev) return false;
        char buf[512];
        int len = snprintf(buf, sizeof(buf),
                           "%s@%s\nACTION=%s\nDEVPATH=%s\nSUBSYSTEM=%s\nDEVNAME=%s\n"
                           "DEVTYPE=disk\nMAJOR=%d\nMINOR=%d\nID_FS_TYPE=%s\n"
                           "ID_FS_UUID=%s\nID_FS_LABEL=%s\nSEQNUM=%u\n",
                           uev->action, uev->devpath, uev->action, uev->devpath,
                           uev->subsystem, uev->devname, uev->major, uev->minor,
                           uev->fstype, uev->uuid, uev->label, uev->seqnum);
        NetlinkEvent evt;
        if (evt.decode(buf, len, mFormat)) {
            onEvent(&evt);
            return true;
        }
        return false;
    }

    bool pollOnce() {
        if (!mRunning || mSocket < 0) return false;
        char buf[1024];
        int n = recv(mSocket, buf, sizeof(buf) - 1, 0);
        if (n <= 0) return false;

        NetlinkEvent evt;
        if (evt.decode(buf, n, mFormat)) {
            onEvent(&evt);
            return true;
        }
        return false;
    }

protected:
    virtual void onEvent(NetlinkEvent* evt) = 0;

private:
    int mSocket;
    int mFormat;
    bool mRunning;
};

#endif /* _SYSUTILS_NETLINKLISTENER_H */
