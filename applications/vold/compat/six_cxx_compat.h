/*
 * applications/vold/compat/six_cxx_compat.h
 *
 * Minimal freestanding C++17 STL & POSIX runtime definitions for building
 * AOSP system/vold C++ sources inside SIX guest userland (-nostdinc -nostdinc++).
 */

#ifndef _SIX_VOLD_CXX_COMPAT_H
#define _SIX_VOLD_CXX_COMPAT_H

#include <stddef.h>
#include <stdarg.h>

extern "C" {
typedef int pid_t;
typedef unsigned short uid_t;
typedef unsigned short gid_t;
typedef unsigned short mode_t;
typedef unsigned short dev_t;
typedef long off_t;
typedef int ssize_t;
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;

extern int errno;

int printf(const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

void *malloc(unsigned int size);
void *calloc(unsigned int nmemb, unsigned int size);
void *realloc(void *ptr, unsigned int size);
void free(void *ptr);
void exit(int status);
void _exit(int status);
int atoi(const char *s);

char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
size_t strlen(const char *s);
char *strerror(int errnum);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

int open(const char *pathname, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int ioctl(int fd, int request, ...);
int access(const char *pathname, int mode);
int unlink(const char *pathname);
int rmdir(const char *pathname);
int mkdir(const char *pathname, int mode);
int mknod(const char *pathname, int mode, int dev);
int symlink(const char *target, const char *linkpath);
int chmod(const char *pathname, int mode);
int mount(const char *specialfile, const char *dir, const char *filesystemtype,
          unsigned long rwflag, const void *data);
int umount(const char *specialfile);
int sync(void);
pid_t fork(void);
pid_t getpid(void);
int execv(const char *path, char *const argv[]);
pid_t waitpid(pid_t pid, int *status, int options);
int usleep(unsigned long usec);

#include <fcntl.h>
#include <sys/socket.h>
#include <linux/binder.h>
#include <linux/dm.h>

#ifndef AF_NETLINK
#define AF_NETLINK 16
#define PF_NETLINK AF_NETLINK
#endif
#ifndef NETLINK_KOBJECT_UEVENT
#define NETLINK_KOBJECT_UEVENT 15
#endif

struct sockaddr_nl {
    unsigned short nl_family;
    unsigned short nl_pad;
    unsigned int   nl_pid;
    unsigned int   nl_groups;
};
}

#ifndef EIO
#define EIO 5
#endif

#ifndef S_IFBLK
#define S_IFBLK 0060000
#endif

#ifndef F_OK
#define F_OK 0
#define X_OK 1
#endif

static inline dev_t makedev(unsigned int maj, unsigned int min) {
    return (dev_t)(((maj & 0xff) << 8) | (min & 0xff));
}
static inline unsigned int major(dev_t dev) {
    return (unsigned int)((dev >> 8) & 0xff);
}
static inline unsigned int minor(dev_t dev) {
    return (unsigned int)(dev & 0xff);
}

/* Placement new */
inline void* operator new(size_t, void* p) noexcept { return p; }
inline void* operator new[](size_t, void* p) noexcept { return p; }

namespace std {

using ::size_t;

class string {
public:
    static const size_t npos = (size_t)-1;

    string() : mData(nullptr), mLen(0), mCap(0) {
        ensure(16);
        mData[0] = '\0';
    }

    string(const char* s) : mData(nullptr), mLen(0), mCap(0) {
        if (!s) s = "";
        mLen = strlen(s);
        ensure(mLen + 1);
        memcpy(mData, s, mLen + 1);
    }

    string(const char* s, size_t n) : mData(nullptr), mLen(0), mCap(0) {
        if (!s) { n = 0; }
        mLen = n;
        ensure(mLen + 1);
        if (n > 0) memcpy(mData, s, n);
        mData[mLen] = '\0';
    }

    string(const string& other) : mData(nullptr), mLen(0), mCap(0) {
        mLen = other.mLen;
        ensure(mLen + 1);
        memcpy(mData, other.c_str(), mLen + 1);
    }

    string(string&& other) noexcept : mData(other.mData), mLen(other.mLen), mCap(other.mCap) {
        other.mData = nullptr;
        other.mLen = 0;
        other.mCap = 0;
    }

    ~string() {
        if (mData) free(mData);
    }

    string& operator=(const string& other) {
        if (this != &other) {
            mLen = other.mLen;
            ensure(mLen + 1);
            memcpy(mData, other.c_str(), mLen + 1);
        }
        return *this;
    }

    string& operator=(const char* s) {
        if (!s) s = "";
        mLen = strlen(s);
        ensure(mLen + 1);
        memcpy(mData, s, mLen + 1);
        return *this;
    }

    const char* c_str() const { return mData ? mData : ""; }
    const char* data() const { return c_str(); }
    size_t size() const { return mLen; }
    size_t length() const { return mLen; }
    bool empty() const { return mLen == 0; }

    void clear() {
        mLen = 0;
        if (mData) mData[0] = '\0';
    }

    char operator[](size_t idx) const { return c_str()[idx]; }
    char& operator[](size_t idx) { return mData[idx]; }

    string& append(const char* s, size_t n) {
        if (!s || n == 0) return *this;
        ensure(mLen + n + 1);
        memcpy(mData + mLen, s, n);
        mLen += n;
        mData[mLen] = '\0';
        return *this;
    }

    string& append(const char* s) {
        if (!s) return *this;
        return append(s, strlen(s));
    }

    string& append(const string& s) {
        return append(s.c_str(), s.size());
    }

    string& operator+=(const string& s) { return append(s); }
    string& operator+=(const char* s) { return append(s); }
    string& operator+=(char c) {
        char buf[2] = {c, '\0'};
        return append(buf, 1);
    }

    size_t find(char c, size_t pos = 0) const {
        if (pos >= mLen) return npos;
        const char* p = strchr(c_str() + pos, c);
        return p ? (size_t)(p - c_str()) : npos;
    }

    size_t find(const char* sub, size_t pos = 0) const {
        if (!sub || pos >= mLen) return npos;
        const char* p = strstr(c_str() + pos, sub);
        return p ? (size_t)(p - c_str()) : npos;
    }

    string substr(size_t pos, size_t count = npos) const {
        if (pos >= mLen) return string("");
        size_t avail = mLen - pos;
        if (count > avail) count = avail;
        return string(c_str() + pos, count);
    }

    bool operator==(const string& rhs) const { return strcmp(c_str(), rhs.c_str()) == 0; }
    bool operator!=(const string& rhs) const { return strcmp(c_str(), rhs.c_str()) != 0; }
    bool operator==(const char* rhs) const { return strcmp(c_str(), rhs ? rhs : "") == 0; }
    bool operator!=(const char* rhs) const { return strcmp(c_str(), rhs ? rhs : "") != 0; }

private:
    char* mData;
    size_t mLen;
    size_t mCap;

    void ensure(size_t needed) {
        if (needed <= mCap && mData) return;
        size_t newCap = mCap ? mCap * 2 : 32;
        while (newCap < needed) newCap *= 2;
        char* newBuf = (char*)malloc(newCap);
        if (mData) {
            memcpy(newBuf, mData, mLen + 1);
            free(mData);
        } else {
            newBuf[0] = '\0';
        }
        mData = newBuf;
        mCap = newCap;
    }
};

inline string operator+(const string& a, const string& b) {
    string r(a);
    r += b;
    return r;
}
inline string operator+(const string& a, const char* b) {
    string r(a);
    r += b;
    return r;
}
inline string operator+(const char* a, const string& b) {
    string r(a);
    r += b;
    return r;
}

inline string to_string(int v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", v);
    return string(buf);
}
inline string to_string(unsigned int v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", v);
    return string(buf);
}

template <typename T>
class vector {
public:
    vector() : mItems(nullptr), mSize(0), mCap(0) {}
    vector(const vector<T>& other) : mItems(nullptr), mSize(0), mCap(0) {
        for (size_t i = 0; i < other.mSize; i++) push_back(other.mItems[i]);
    }
    ~vector() {
        clear();
        if (mItems) free(mItems);
    }
    vector<T>& operator=(const vector<T>& other) {
        if (this != &other) {
            clear();
            for (size_t i = 0; i < other.mSize; i++) push_back(other.mItems[i]);
        }
        return *this;
    }
    void push_back(const T& val) {
        if (mSize >= mCap) {
            size_t newCap = mCap ? mCap * 2 : 8;
            T* newBuf = (T*)malloc(sizeof(T) * newCap);
            for (size_t i = 0; i < mSize; i++) {
                new (&newBuf[i]) T(mItems[i]);
                mItems[i].~T();
            }
            if (mItems) free(mItems);
            mItems = newBuf;
            mCap = newCap;
        }
        new (&mItems[mSize]) T(val);
        mSize++;
    }
    void clear() {
        for (size_t i = 0; i < mSize; i++) mItems[i].~T();
        mSize = 0;
    }
    size_t size() const { return mSize; }
    bool empty() const { return mSize == 0; }
    T& operator[](size_t idx) { return mItems[idx]; }
    const T& operator[](size_t idx) const { return mItems[idx]; }
    T* begin() { return mItems; }
    T* end() { return mItems + mSize; }
    const T* begin() const { return mItems; }
    const T* end() const { return mItems + mSize; }
private:
    T* mItems;
    size_t mSize;
    size_t mCap;
};

template <typename T>
class list {
private:
    struct Node {
        T val;
        Node* prev;
        Node* next;
        Node(const T& v) : val(v), prev(nullptr), next(nullptr) {}
    };
    Node* mHead;
    Node* mTail;
    size_t mSize;
public:
    class iterator {
    public:
        Node* cur;
        iterator(Node* n) : cur(n) {}
        T& operator*() const { return cur->val; }
        T* operator->() const { return &cur->val; }
        iterator& operator++() { if (cur) cur = cur->next; return *this; }
        iterator operator++(int) { iterator tmp = *this; if (cur) cur = cur->next; return tmp; }
        bool operator==(const iterator& rhs) const { return cur == rhs.cur; }
        bool operator!=(const iterator& rhs) const { return cur != rhs.cur; }
    };
    list() : mHead(nullptr), mTail(nullptr), mSize(0) {}
    ~list() { clear(); }
    void push_back(const T& v) {
        Node* n = new Node(v);
        n->prev = mTail;
        if (mTail) mTail->next = n;
        else mHead = n;
        mTail = n;
        mSize++;
    }
    iterator erase(iterator it) {
        Node* n = it.cur;
        if (!n) return iterator(nullptr);
        Node* nxt = n->next;
        if (n->prev) n->prev->next = n->next;
        else mHead = n->next;
        if (n->next) n->next->prev = n->prev;
        else mTail = n->prev;
        delete n;
        mSize--;
        return iterator(nxt);
    }
    void clear() {
        Node* c = mHead;
        while (c) {
            Node* nxt = c->next;
            delete c;
            c = nxt;
        }
        mHead = mTail = nullptr;
        mSize = 0;
    }
    bool empty() const { return mSize == 0; }
    size_t size() const { return mSize; }
    iterator begin() { return iterator(mHead); }
    iterator end() { return iterator(nullptr); }
    iterator begin() const { return iterator(mHead); }
    iterator end() const { return iterator(nullptr); }
};

struct RefCountBlock {
    int count;
    void (*deleter)(void*);
    void* raw;
};

template <typename T>
class shared_ptr {
public:
    T* mPtr;
    RefCountBlock* mCtrl;

    shared_ptr() : mPtr(nullptr), mCtrl(nullptr) {}
    shared_ptr(decltype(nullptr)) : mPtr(nullptr), mCtrl(nullptr) {}

    explicit shared_ptr(T* p) : mPtr(p), mCtrl(nullptr) {
        if (p) {
            mCtrl = new RefCountBlock{1, [](void* raw) { delete static_cast<T*>(raw); }, p};
        }
    }

    shared_ptr(const shared_ptr<T>& other) : mPtr(other.mPtr), mCtrl(other.mCtrl) {
        if (mCtrl) mCtrl->count++;
    }

    template <typename U>
    shared_ptr(const shared_ptr<U>& other) : mPtr(other.mPtr), mCtrl(other.mCtrl) {
        if (mCtrl) mCtrl->count++;
    }

    ~shared_ptr() { release(); }

    shared_ptr<T>& operator=(const shared_ptr<T>& other) {
        if (this != &other) {
            release();
            mPtr = other.mPtr;
            mCtrl = other.mCtrl;
            if (mCtrl) mCtrl->count++;
        }
        return *this;
    }

    void reset(T* p = nullptr) {
        release();
        if (p) {
            mPtr = p;
            mCtrl = new RefCountBlock{1, [](void* raw) { delete static_cast<T*>(raw); }, p};
        }
    }

    T* get() const { return mPtr; }
    T* operator->() const { return mPtr; }
    T& operator*() const { return *mPtr; }
    explicit operator bool() const { return mPtr != nullptr; }

    bool operator==(const shared_ptr<T>& rhs) const { return mPtr == rhs.mPtr; }
    bool operator!=(const shared_ptr<T>& rhs) const { return mPtr != rhs.mPtr; }

private:
    void release() {
        if (mCtrl) {
            mCtrl->count--;
            if (mCtrl->count <= 0) {
                if (mCtrl->deleter && mCtrl->raw) mCtrl->deleter(mCtrl->raw);
                delete mCtrl;
            }
            mCtrl = nullptr;
            mPtr = nullptr;
        }
    }
};

template <typename T, typename... Args>
shared_ptr<T> make_shared(Args&&... args) {
    return shared_ptr<T>(new T(args...));
}

} // namespace std

namespace android {
typedef int32_t status_t;
enum {
    OK = 0,
    NO_ERROR = 0,
    UNKNOWN_ERROR = (-2147483647 - 1),
    NO_MEMORY = -12,
    INVALID_OPERATION = -38,
    BAD_VALUE = -22,
    BAD_TYPE = -2147483647,
    NAME_NOT_FOUND = -2,
    PERMISSION_DENIED = -1,
    NO_INIT = -19,
    ALREADY_EXISTS = -17,
    DEAD_OBJECT = -32,
};
} // namespace android

#endif /* _SIX_VOLD_CXX_COMPAT_H */
