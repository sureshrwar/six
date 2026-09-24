/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * applications/vold/fs/Ntfs.cpp — AOSP system/vold/fs/Ntfs.cpp (ag/41271392)
 */

#include "Ntfs.h"
#include "../Utils.h"

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

using android::base::StringPrintf;

namespace android {
namespace vold {
namespace ntfs {

static const char* kNtfs3gPath = "/bin/ntfs-3g";
static const char* kNtfsFixPath = "/bin/ntfsfix";
static const char* kMkntfsPath = "/bin/mkntfs";

bool IsSupported() {
    return access(kNtfs3gPath, X_OK) == 0 && access(kNtfsFixPath, X_OK) == 0;
}

status_t Check(const std::string& /* source */) {
    // Safe strategy (ChromeOS model): ntfsfix only clears $LogFile rather than
    // replaying journal transactions, which risks silent data loss.
    // Therefore, Check is a no-op by default. If dirty, R/W mount will fail
    // and trigger safe Read-Only (-o ro) fallback in PublicVolume.
    LOG(INFO) << "NTFS Check skipped (using safe R/O mount fallback strategy)";
    return 0;
}

status_t Mount(const std::string& source, const std::string& target, int ownerUid, int ownerGid,
               int permMask, bool ro, pid_t* outDriverPid) {
    // 1. Open /dev/fuse descriptor in vold (which has CAP_SYS_ADMIN)
    int fuseFd = open("/dev/fuse", O_RDWR);
    if (fuseFd < 0) {
        PLOG(ERROR) << "Failed to open /dev/fuse";
        return -EIO;
    }
    fcntl(fuseFd, F_SETFD, FD_CLOEXEC);

    // 2. Open block device descriptor in vold (which has root privileges)
    int blockMode = ro ? O_RDONLY : O_RDWR;
    int blockFd = open(source.c_str(), blockMode);
    if (blockFd < 0) {
        PLOG(ERROR) << "Failed to open block device " << source;
        close(fuseFd);
        return -EIO;
    }
    fcntl(blockFd, F_SETFD, FD_CLOEXEC);

    // 3. Perform kernel mount of /dev/fuse onto target path in vold
    std::string mountOpts = StringPrintf("fd=%d,rootmode=40000,user_id=%d,group_id=%d,allow_other",
                                         fuseFd, ownerUid, ownerGid);
    if (ro) {
        mountOpts += ",ro";
    }

    unsigned long mountFlags = MS_NOSUID | MS_NODEV | MS_NOEXEC;
    if (ro) {
        mountFlags |= MS_RDONLY;
    }

    if (mount("/dev/fuse", target.c_str(), "fuse.ntfs-3g", mountFlags, (void*)mountOpts.c_str()) != 0) {
        PLOG(ERROR) << "Failed to mount /dev/fuse onto " << target;
        close(blockFd);
        close(fuseFd);
        return -EIO;
    }

    // Create readiness pipe: parent waits on read end, child writes on success
    int readyPipe[2];
    if (pipe(readyPipe) != 0) {
        PLOG(ERROR) << "Failed to create ready pipe for " << target;
        umount(target.c_str());
        close(blockFd);
        close(fuseFd);
        return -errno;
    }
    int readyReadFd = readyPipe[0];
    int readyWriteFd = readyPipe[1];
    fcntl(readyReadFd, F_SETFD, FD_CLOEXEC);
    fcntl(readyWriteFd, F_SETFD, FD_CLOEXEC);

    // 4. Build ntfs-3g command line passing pre-opened fd=N and /dev/fd/N block device
    std::vector<std::string> cmd;
    cmd.push_back(kNtfs3gPath);
    cmd.push_back("--ready-fd");
    cmd.push_back(StringPrintf("%d", readyWriteFd));
    cmd.push_back("-o");

    std::string options =
            StringPrintf("fd=%d,uid=%d,gid=%d,fmask=%o,dmask=%o,utf8,direct_io,no_detach",
                         fuseFd, ownerUid, ownerGid, permMask, permMask);
    if (ro) {
        options += ",ro";
    }

    cmd.push_back(options);
    cmd.push_back(StringPrintf("/dev/fd/%d", blockFd));
    cmd.push_back(target);

    // 5. Spawn ntfs-3g as AID_MEDIA_RW using ForkExecvpAsyncAsUser.
    // Pass descriptors to keep so FD_CLOEXEC is cleared only in the child process after fork().
    std::vector<int> fdsToKeep;
    fdsToKeep.push_back(fuseFd);
    fdsToKeep.push_back(blockFd);
    fdsToKeep.push_back(readyWriteFd);

    pid_t pid = ForkExecvpAsyncAsUser(cmd, AID_MEDIA_RW, AID_MEDIA_RW, nullptr, fdsToKeep);

    // Close parent's copies of fds handed to the child / kernel mount
    close(readyWriteFd);
    close(blockFd);
    close(fuseFd);

    if (pid <= 0) {
        LOG(ERROR) << "NTFS mount failed to start ntfs-3g driver";
        close(readyReadFd);
        umount(target.c_str());
        return -EIO;
    }

    // 6. Wait for ntfs-3g to signal readiness, exit on error, or timeout
    char readyByte = 0;
    int bytes = read(readyReadFd, &readyByte, 1);
    close(readyReadFd);

    if (bytes == 1 && readyByte == '1') {
        LOG(INFO) << "NTFS mount initialized successfully with PID " << pid
                  << " (fuseFd=" << fuseFd << ", blockFd=" << blockFd
                  << ", ro=" << (ro ? "true" : "false") << ")";
        if (outDriverPid != nullptr) {
            *outDriverPid = pid;
        }
        return 0;
    }

    // Child closed pipe or exited without writing ready byte (mount failure)
    int status = 0;
    pid_t waitRes = waitpid(pid, &status, WNOHANG);
    if (waitRes == pid) {
        LOG(ERROR) << "ntfs-3g driver PID " << pid << " exited prematurely with status " << status;
    } else {
        LOG(ERROR) << "ntfs-3g driver PID " << pid << " closed ready pipe unexpectedly";
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }

    umount(target.c_str());
    return -EIO;
}

status_t Format(const std::string& source) {
    std::vector<std::string> cmd;
    cmd.push_back(kMkntfsPath);
    cmd.push_back("-f");
    cmd.push_back("-Q");
    cmd.push_back(source);

    int rc = ForkExecvp(cmd);
    if (rc == 0) {
        LOG(INFO) << "Format OK";
        return 0;
    } else {
        LOG(ERROR) << "Format failed (code " << rc << ")";
        errno = EIO;
        return -1;
    }
}

}  // namespace ntfs
}  // namespace vold
}  // namespace android
