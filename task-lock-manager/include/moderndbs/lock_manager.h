#ifndef INCLUDE_MODERNDBS_LOCK_MANAGER_H
#define INCLUDE_MODERNDBS_LOCK_MANAGER_H

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <mutex>
#include <vector>
#include <unordered_map>

namespace moderndbs {

/// A DataItem, usually a TID.
using DataItem = uint64_t;
/// The locking mode
enum class LockMode { Unlocked, Shared, Exclusive };

/// A Transaction Lock.
struct Lock;

/// A Lock Manager.
class LockManager;

/// Transaction ID: start with 1. 0 is the invalid transaction id.
using TXN_ID = uint64_t;

/// Lock ID: start with 1. 0 is the invalid lock id.
using LOCK_ID = uint64_t;

/// A transaction.
/// Can create a lock on a DataItem, if the DataItem has nothing.
/// Can't free a lock, which is done by Lock Manager.
class Transaction {
    friend class WaitsForGraph;

    private:
    /// Transaction ID: start with 1. 0 is the invalid transaction id.
    inline static std::atomic<TXN_ID> TXN_IDs = 1;
    /// Txn Id.
    const TXN_ID txn_id;
    /// The lock manager.
    LockManager *const lockManager = nullptr;
    /// The acquired locks.
    std::vector<std::shared_ptr<Lock>> locks;

    public:
    /// Constructor
    Transaction() : txn_id(TXN_IDs++) {}
    /// Constructor
    explicit Transaction(LockManager &lockManager) : txn_id(TXN_IDs++), lockManager(&lockManager) {}
    /// Destructor.
    /// Strict 2Pl, only releases the locks, when txn ends.
    ~Transaction();

    /// Get txn Id
    TXN_ID getTxnId() const { return txn_id; }
    /// Add a lock for the data item
    void addLock(DataItem dataItem, LockMode mode);
    /// Get the locks
    const auto &getLocks() { return locks; }
};

/// A lock on a DataItem
/// std::enable_shared_from_this to manage a resource using reference counting.
struct Lock : std::enable_shared_from_this<Lock> {
    /// Lock ID: start with 1. 0 is the invalid lock id.
    inline static std::atomic<LOCK_ID> LOCK_IDs = 1;
    /// Lock Id.
    const LOCK_ID lock_id;
    /// The next lock in the chain.
    Lock *next = nullptr;
    /// The data item to check.
    const DataItem item;
    /// The actual lock.
    std::shared_mutex lock;
    /// The latch for metadata. To update owners and waiters.
    std::mutex metadata_latch;
    /// The owners of the lock: One in case of Exclusive, multiple when Shared.
    std::vector<const Transaction*> owners;
    /// The waiters of the lock, currently blocked by the owners.
    std::vector<const Transaction*> waiters;
    /// The current locked state.
    LockMode ownership = LockMode::Unlocked;

    /// Constructor
    explicit Lock(DataItem item) : lock_id(LOCK_IDs++), item(item) {}
    /// Destructor
    ~Lock() = default;

    /// Comparison operator to check if the lock is for the DataItem.
    bool operator!=(DataItem other) const { return this->item != other; }
    /// Comparison operator to check if the lock is for the DataItem.
    bool operator==(DataItem other) const { return this->item == other; }

    /// Construct a shared pointer of lock without deleter.
    /// So the destructor of transaction DO NOT free this Lock.
    /// To free this lock: we should do it explicitly in LockManager::deleteLock.
    /// In this way: the locks on the chain do not bring memory-free overhead on the chain.
    /// The expired locks are freed, when other process the chain and find locks expired.
    /// This piggyback lazy memory-free puts less memory stress on the chain.
    /// More Detail and Reason: in Lecture's Slide.
    static std::shared_ptr<Lock> construct(DataItem item) {
        return std::shared_ptr<Lock>(new Lock(item),
                                     [](Lock*){} /*an empty custom deleter. Intended not to delete, otherwise dangling pointer in the chain*/);
    }

    /// Take a shared ownership of this lock.
    /// weak_from_this().lock(): always constructs a shared_ptr.
    std::shared_ptr<Lock> getAsSharedPtr() { return weak_from_this().lock(); }

    /// Check if the lock is expired.
    /// weak_from_this().expired() == reference counter is 0.
    /// Nobody has this lock. + The empty custom deleter := the lock is still here but has no owner. So marked.
    bool IsLockExpired() { return weak_from_this().expired(); }
};

/// An exception that signals an avoided deadlock
class DeadLockError : std::exception {
    const char *what() const noexcept override { return "deadlock detected"; }
};

/// A wait-for graph structure to detect deadlocks when transactions need to
/// wait on another
class WaitsForGraph {
    private:
    /// Mutex
    std::mutex mutex;
    /// The wait graph: the values wait for the key.
    std::unordered_map<const Transaction*, std::vector<const Transaction*>> graph;

    public:
    friend LockManager;
    /// Add a wait-for relationship of the specified transaction on the specified lock, respectively the owners of the lock.
    /// Throws a DeadLockError in case waiting for the lock would result in a deadlock.
    ///
    /// The task in one sentence: transaction waits for owners of lock.
    void addWaitsFor(const Transaction& transaction, const Lock &lock);

    /// Remove the waits-for dependencies *to and from* this transaction from the waits-for graph.
    void removeTransaction(const Transaction &transaction);

    /// Add waiters for a current owner in the graph.
    void addWaiters(const Transaction& owner, const std::vector<const Transaction*>& waiters);
};

/// A lock manager for concurrency-safe acquiring and releasing locks.
/// A lock manager is owned by the main-thread.
class LockManager {
    friend class Transaction;

    private:
    /// A hash function to map a DataItem to a position in the table.
    using Hash = std::hash<DataItem>;

    /// A Chain of locks for each bucket of the hash table.
    struct Chain {
        /// The latch to modify the chain.
        std::mutex latch;
        /// The chain.
        Lock *first = nullptr;

        /// Constructor.
        Chain() = default;
        /// Move-Constructor to store in standard containers.
        Chain(Chain&& other) noexcept : first(other.first) {}
        /// Move-Assignment to store in standard containers.
        Chain &operator=(Chain&& other) noexcept {
            first = other.first;
            return *this;
        }
    };

    /// The hashtable.
    /// Key: DataItem.
    /// Value: Lock or Chain.
    std::vector<Chain> table;
    /// The wait-for graph to check for deadlocks
    WaitsForGraph wfg;

    public:
    /// Constructor
    /// @param bucketCount: The size of the table. Needs to be fixed to avoid rehashing.
    explicit LockManager(size_t bucketCount) { table.resize(bucketCount); }

    /// Destructor: clean up all lock, regardless they are expired or not.
    ~LockManager();

    /// Acquire a lock for the transaction on the specified dataItem, in requested mode.
    /// In case of a deadlock, throws an DeadLockError.
    std::shared_ptr<Lock> acquireLock(Transaction &transaction, DataItem dataItem, LockMode mode);

    /// Get the lock mode of the dataItem.
    LockMode getLockMode(DataItem dataItem);

    /// Delete a lock.
    void deleteLock(Lock *lock);

    /// Delete a txn from wfg. TODO: delte
    void removeWFGTransaction(const Transaction &transaction) {
        std::scoped_lock l(wfg.mutex);
        wfg.removeTransaction(transaction);
    }
};
} // namespace moderndbs

#endif
