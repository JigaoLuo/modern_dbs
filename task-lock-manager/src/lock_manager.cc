#include "moderndbs/lock_manager.h"

#include <cassert>
#include <algorithm>
#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include <unordered_set>

namespace moderndbs {

void Transaction::addLock(DataItem dataItem, LockMode mode) {
    assert(lockManager);
    auto lock = lockManager->acquireLock(*this, dataItem, mode);
    locks.push_back(std::move(lock));
}

Transaction::~Transaction() {
    if (lockManager) {
        lockManager->wfg.removeTransaction(*this);
    }

    for (auto& lock : locks) {
        {
            auto guard = std::unique_lock(lock->metadata_latch);
            lock->owners.erase(std::remove(lock->owners.begin(), lock->owners.end(), this), lock->owners.end());
        }
        if (lock->ownership == LockMode::Shared) {
            lock->lock.unlock_shared();
        } else {
            lock->lock.unlock();
        }
    }
}

using Node = std::pair<const Transaction* const, std::vector<const Transaction*>>;
using Graph = const std::unordered_map<const Transaction*, std::vector<const Transaction*>>;
using VisitedSet = std::unordered_set<const Transaction*>;

static bool findDuplicate(Graph& graph, const Node* node, const VisitedSet& visited) {
    if (visited.find(node->first) != visited.end()) {
        return true;
    }

    auto current_visited = visited;  /// A copy.
    current_visited.insert(node->first);
    for (const auto& transcation : node->second) {
        auto it = graph.find(transcation);
        if (it == graph.end()) {
            continue;
        }
        auto& n = *it;
        /// DFS.
        if (findDuplicate(graph, &n, current_visited)) {
            return true;
        }
    }
    return false;
}

static bool findDuplicate(Graph& graph, const Node* node) {
    /// DFS.
    auto visited = VisitedSet();
    return findDuplicate(graph, node, visited);
}

void WaitsForGraph::addWaitsFor(const Transaction &transaction, const Lock &lock) {
    auto gurad = std::unique_lock(mutex);

    /// Add edges.
    auto& waits_for = graph[&transaction];
    for (auto* other_tx : lock.owners) {
        if (std::find(waits_for.begin(), waits_for.end(), other_tx) == waits_for.end()) {
            waits_for.push_back(other_tx);
        }
    }
    /// The check for cycles.
    for (const auto* other_tx : lock.owners) {
        auto it = graph.find(other_tx);
        if (it == graph.end()) {
            continue;
        }
        auto node = *it;
        if (findDuplicate(graph, &node)) {
            graph.erase(&transaction);
            throw DeadLockError();
        }
    }
}

void WaitsForGraph::addWaiters(const Transaction& owner, const std::vector<const Transaction*>& waiters) {
    auto guard = std::unique_lock(mutex);

    /// Add edges.
    /// A edge: waiting -> owner.
    for (auto* waiter : waiters) {
        auto& already_waits_for = graph[waiter];
        if (std::find(already_waits_for.begin(), already_waits_for.end(), &owner) == already_waits_for.end()) {
            already_waits_for.push_back(&owner);
        }
    }
    /// The check for cycles.
    auto outgoing = graph.find(&owner);
    if (outgoing == graph.end()) {
        return;
    }
    auto node = *outgoing;
    if (findDuplicate(graph, &node)) {
        throw DeadLockError();
    }
}

void WaitsForGraph::removeTransaction(const Transaction &transaction) {
    auto guard = std::unique_lock(mutex);

    graph.erase(&transaction);
    for (auto& list : graph) {
        auto& v = list.second;
        v.erase(std::remove(v.begin(), v.end(), &transaction), v.end());
    }
}

LockManager::~LockManager() {
    for (auto& chain : table) {
        if (chain.first != nullptr) {
            Lock* prev_lock(nullptr);
            Lock* lock_ptr = chain.first;
            while (lock_ptr) {
                prev_lock = lock_ptr;
                lock_ptr = lock_ptr->next;
                delete(prev_lock);
            }
        }
    }
}

std::shared_ptr<Lock> LockManager::acquireLock(Transaction &transaction, DataItem dataItem, LockMode mode) {
    static constexpr auto hash = Hash();
    auto& chain = table[hash(dataItem) % table.size()];
    auto guard = std::unique_lock(chain.latch);

    auto lock = [&]() -> std::shared_ptr<Lock> {
        auto** list_head = &chain.first;
        while (*list_head) {
            /// Clean up expired locks.
            /// Lazy Deletion.
            if ((*list_head)->weak_from_this().expired()) {
                auto* expired_lock = *list_head;
                *list_head = expired_lock->next;
                delete expired_lock;
                continue;
            }

            /// If found the correct lock.
            if (**list_head == dataItem) {
                auto res = (*list_head)->weak_from_this().lock();  /// Get the shared_ptr.

                /// Possible, the shared pointer may expire concurrently.
                if (!res) {
                    assert((*list_head)->weak_from_this().expired());
                    auto* expired_lock = *list_head;
                    *list_head = expired_lock->next;
                    delete expired_lock;
                }

                return res;
            }
        list_head = &(*list_head)->next;
        }
        return nullptr;
    }();

    /// Lock not found OR lock expired.
    if (!lock) {
        lock = Lock::construct(dataItem);
        lock->next = chain.first;
        chain.first = lock.get();
    }

    /// Then we can release the lock on the chain and only lock the meta-latch.
    guard.unlock();
    auto lock_guard= std::unique_lock(lock->metadata_latch);

    /// Can we directly acquire the lock?
    if (mode == LockMode::Shared ? lock->lock.try_lock_shared() : lock->lock.try_lock()) {
        lock->ownership = mode;
        lock->owners.push_back(&transaction);
        wfg.addWaiters(transaction, lock->waiters);
        return lock;
    }

    /// If no, then we have to wait.
    wfg.addWaitsFor(transaction, *lock);
    lock->waiters.push_back(&transaction);
    lock_guard.unlock();

    if (mode == LockMode::Shared) {
        lock->lock.lock_shared();
    } else {
        lock->lock.lock();
    }

    /// Remove edge in wait-for graph.
    lock_guard.lock();
    lock->waiters.erase(std::remove(lock->waiters.begin(), lock->waiters.end(), &transaction), lock->waiters.end());

    lock->ownership = mode;
    lock->owners.push_back(&transaction);

    /// I get the lock, all the waits have to wait for me.
    wfg.addWaiters(transaction, lock->waiters);
    return lock;
}

LockMode LockManager::getLockMode(DataItem dataItem) {
    static constexpr auto hash = Hash();
    auto& chain = table[hash(dataItem) % table.size()];
    auto guard = std::unique_lock(chain.latch);

    auto* lock = [&]() -> Lock* {
        for (auto* it = chain.first; it; it = it->next) {
            if (*it == dataItem) {
                return it;
            }
        }
        return nullptr;
    }();
    if (!lock || lock->weak_from_this().expired()) {
        return LockMode::Unlocked;
    }
    return lock->ownership;
}

void LockManager::deleteLock(Lock *) {
    /// Intentionally left blank.
}

} // namespace moderndbs
