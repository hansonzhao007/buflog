#ifndef SPOTON_RETRY_LOCK_H
#define SPOTON_RETRY_LOCK_H

#include <atomic>

namespace spoton {

class RetryVersionLock {
private:
    // 2b type 60b version 1b lock 1b obsolete
    std::atomic<uint64_t> typeVersionLockObsolete{0b100};

public:
    static bool isLocked (uint64_t version) { return ((version & 0b10) == 0b10); }
    static bool isObsolete (uint64_t version) { return (version & 1) == 1; }
    static bool isLockedOrObsolete (uint64_t version) { return version & 0b11; }

    uint64_t readLockOrRestart (bool& needRestart) const {
        uint64_t version;
        version = typeVersionLockObsolete.load ();
        if (isLocked (version)) {
            needRestart = true;
        }
        return version;
    }
    void checkOrRestart (uint64_t startRead, bool& needRestart) const {
        readUnlockOrRestart (startRead, needRestart);
    }
    void readUnlockOrRestart (uint64_t startRead, bool& needRestart) const {
        needRestart = (startRead != typeVersionLockObsolete.load ());
    }

    void writeLockOrRestart (bool& needRestart) {
        uint64_t version;
        version = readLockOrRestart (needRestart);
        if (needRestart) return;

        upgradeToWriteLockOrRestart (version, needRestart);
        if (needRestart) return;
    }
    void upgradeToWriteLockOrRestart (uint64_t& version, bool& needRestart) {
        if (typeVersionLockObsolete.compare_exchange_strong (version, version + 0b10)) {
            version = version + 0b10;
        } else {
            needRestart = true;
        }
    }
    void writeUnlock () { typeVersionLockObsolete.fetch_add (0b10); }
    void writeUnlockObsolete () { typeVersionLockObsolete.fetch_add (0b11); }
};

};  // namespace spoton
#endif