/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * applications/vold/model/VolumeBase.cpp — AOSP system/vold/model/VolumeBase.cpp
 */

#include "VolumeBase.h"
#include "../VolumeManager.h"
#include <android-base/logging.h>
#include <android-base/stringprintf.h>

using android::base::StringPrintf;

namespace android {
namespace vold {

VolumeBase::VolumeBase(Type type)
    : mType(type),
      mMountFlags(0),
      mMountUserId(-1),
      mCreated(false),
      mState(State::kUnmounted),
      mSilent(false) {}

VolumeBase::~VolumeBase() {}

std::shared_ptr<android::os::IVoldListener> VolumeBase::getListener() const {
    return VolumeManager::Instance()->getListener();
}

void VolumeBase::setState(State state) {
    mState = state;
    auto listener = getListener();
    if (listener && !mSilent) {
        listener->onVolumeStateChanged(getId(), static_cast<int32_t>(mState));
    }
}

status_t VolumeBase::setDiskId(const std::string& diskId) {
    mDiskId = diskId;
    return OK;
}

status_t VolumeBase::setPartGuid(const std::string& partGuid) {
    mPartGuid = partGuid;
    return OK;
}

status_t VolumeBase::setMountFlags(int mountFlags) {
    mMountFlags = mountFlags;
    return OK;
}

status_t VolumeBase::setMountUserId(int mountUserId) {
    mMountUserId = mountUserId;
    return OK;
}

status_t VolumeBase::setSilent(bool silent) {
    mSilent = silent;
    return OK;
}

status_t VolumeBase::setId(const std::string& id) {
    mId = id;
    return OK;
}

status_t VolumeBase::setPath(const std::string& path) {
    mPath = path;
    auto listener = getListener();
    if (listener && !mSilent) {
        listener->onVolumePathChanged(getId(), mPath);
    }
    return OK;
}

status_t VolumeBase::setInternalPath(const std::string& internalPath) {
    mInternalPath = internalPath;
    auto listener = getListener();
    if (listener && !mSilent) {
        listener->onVolumeInternalPathChanged(getId(), mInternalPath);
    }
    return OK;
}

void VolumeBase::addVolume(const std::shared_ptr<VolumeBase>& volume) {
    mVolumes.push_back(volume);
}

void VolumeBase::removeVolume(const std::shared_ptr<VolumeBase>& volume) {
    for (auto it = mVolumes.begin(); it != mVolumes.end(); ++it) {
        if (*it == volume) {
            mVolumes.erase(it);
            break;
        }
    }
}

std::shared_ptr<VolumeBase> VolumeBase::findVolume(const std::string& id) {
    for (auto it = mVolumes.begin(); it != mVolumes.end(); ++it) {
        auto vol = *it;
        if (vol->getId() == id) {
            return vol;
        }
    }
    return nullptr;
}

status_t VolumeBase::create() {
    if (mCreated) return BAD_VALUE;

    mCreated = true;
    status_t res = doCreate();
    auto listener = getListener();
    if (listener && !mSilent) {
        listener->onVolumeCreated(getId(), static_cast<int32_t>(mType),
                                  mDiskId, mPartGuid, mMountUserId);
    }
    setState(State::kUnmounted);
    return res;
}

status_t VolumeBase::doCreate() {
    return OK;
}

status_t VolumeBase::destroy() {
    if (!mCreated) return BAD_VALUE;

    if (mState == State::kMounted) {
        unmount();
        setState(State::kBadRemoval);
    } else {
        setState(State::kRemoved);
    }

    auto listener = getListener();
    if (listener && !mSilent) {
        listener->onVolumeDestroyed(getId());
    }
    status_t res = doDestroy();
    mCreated = false;
    return res;
}

status_t VolumeBase::doDestroy() {
    return OK;
}

status_t VolumeBase::mount() {
    if ((mState != State::kUnmounted) && (mState != State::kUnmountable)) {
        LOG(WARNING) << getId() << " mount requires state unmounted or unmountable";
        return BAD_VALUE;
    }
    setState(State::kChecking);
    status_t res = doMount();
    if (res == OK) {
        setState(State::kMounted);
        doPostMount();
    } else {
        setState(State::kUnmountable);
    }
    return res;
}

void VolumeBase::doPostMount() {}

status_t VolumeBase::unmount() {
    if (mState != State::kMounted && mState != State::kMountedReadOnly) {
        return BAD_VALUE;
    }
    setState(State::kEjecting);

    for (auto it = mVolumes.begin(); it != mVolumes.end(); ++it) {
        auto vol = *it;
        vol->destroy();
    }
    mVolumes.clear();

    status_t res = doUnmount();
    setState(State::kUnmounted);
    return res;
}

status_t VolumeBase::format(const std::string& fsType) {
    if (mState == State::kMounted) {
        unmount();
    }
    if ((mState != State::kUnmounted) && (mState != State::kUnmountable)) {
        return BAD_VALUE;
    }
    setState(State::kFormatting);
    status_t res = doFormat(fsType);
    setState(State::kUnmounted);
    return res;
}

status_t VolumeBase::doFormat(const std::string& /*fsType*/) {
    return INVALID_OPERATION;
}

}  // namespace vold
}  // namespace android
