#include "moderndbs/lock_manager.h"
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <random>
#include <unordered_set>

using DataItem = moderndbs::DataItem;
using DeadLockError = moderndbs::DeadLockError;
using Lock = moderndbs::Lock;
using LockManager = moderndbs::LockManager;
using LockMode = moderndbs::LockMode;
using Transaction = moderndbs::Transaction;
using WaitsForGraph = moderndbs::WaitsForGraph;

namespace {

// NOLINTNEXTLINE
TEST(WaitsForTest, CycleOfTwo) {
  Transaction t1, t2;
  auto l1 = Lock(1), l2 = Lock(2);
  l1.ownership = l2.ownership = LockMode::Exclusive;
  l1.owners.push_back(&t1);  // T1 has L1
  l2.owners.push_back(&t2);  // T2 has L2

  auto wfg = WaitsForGraph();
  EXPECT_NO_THROW(wfg.addWaitsFor(t1, l2));  // T1 waits for L2
  EXPECT_THROW(wfg.addWaitsFor(t2, l1), DeadLockError);  // T2 waits for L1
  // Wait-for graph: T1 <-> T2
}

// NOLINTNEXTLINE
TEST(WaitsForTest, CycleOfThree) {
  Transaction t1, t2, t3;
  auto l1 = Lock(1), l2 = Lock(2), l3 = Lock(3);
  l1.ownership = l2.ownership = l3.ownership = LockMode::Exclusive;
  l1.owners.push_back(&t1);  // T1 has L1
  l2.owners.push_back(&t2);  // T2 has L2
  l3.owners.push_back(&t3);  // T3 has L3

  auto wfg = WaitsForGraph();
  EXPECT_NO_THROW(wfg.addWaitsFor(t1, l2));  // T1 waits for L2
  EXPECT_NO_THROW(wfg.addWaitsFor(t2, l3));  // T2 waits for L3
  // Wait-for graph: T1 -> T2 -> T3

  EXPECT_THROW(wfg.addWaitsFor(t3, l1), DeadLockError);  // T3 waits for L1
  // Wait-for graph: T1 -> T2 -> T3
  //                 ^-----------|
}

// NOLINTNEXTLINE
TEST(WaitsForTest, NoDeadLock) {
  Transaction t0, t1, t2, t3;
  auto l0 = Lock(0), l1 = Lock(1), l2 = Lock(2), l3 = Lock(3);
  l0.ownership = l1.ownership = l2.ownership = l3.ownership = LockMode::Exclusive;
  l0.owners.push_back(&t0);  // T0 has L0
  l1.owners.push_back(&t1);  // T1 has L1
  l2.owners.push_back(&t2);  // T2 has L2
  l3.owners.push_back(&t3);  // T3 has L4

  auto wfg = WaitsForGraph();
  EXPECT_NO_THROW(wfg.addWaitsFor(t0, l1));
  EXPECT_NO_THROW(wfg.addWaitsFor(t1, l2));
  EXPECT_NO_THROW(wfg.addWaitsFor(t2, l3));
  EXPECT_NO_THROW(wfg.addWaitsFor(t1, l3));
  // Wait-for graph: T0 -> T1 -> T2 -> T3
  //                        |----------^
}

// NOLINTNEXTLINE
TEST(WaitsForTest, DeadLockFailsGracefully) {
  Transaction t1, t2, t3;
  auto l1 = Lock(1), l2 = Lock(2), l3 = Lock(3);
  l1.ownership = l2.ownership = l3.ownership = LockMode::Exclusive;
  l1.owners.push_back(&t1);  // T1 has L1
  l2.owners.push_back(&t2);  // T2 has L2
  l3.owners.push_back(&t3);  // T3 has L3

  auto wfg = WaitsForGraph();
  EXPECT_NO_THROW(wfg.addWaitsFor(t1, l2));
  EXPECT_THROW(wfg.addWaitsFor(t2, l1), DeadLockError);
  // Wait-for graph: T1 <-> T2

  // If this fails, you keep the deadlock cycle of the previous waits-for
  // in the graph. If you detect a deadlock, you should resolve it immediately
  // by removing that transaction
  EXPECT_NO_THROW(wfg.addWaitsFor(t3, l1));
  // Wait-for graph: T3 -> T1
  // T2 is aborted
}

// NOLINTNEXTLINE
TEST(LockManagerTest, SharedAcquire) {
  auto lockManager = LockManager(1024);
  auto t0 = Transaction(lockManager), t1 = Transaction(lockManager);
  t0.addLock(0, LockMode::Shared);
  t1.addLock(1024, LockMode::Shared);
  EXPECT_EQ(t0.getLocks().size(), 1);
  EXPECT_EQ(t1.getLocks().size(), 1);
  EXPECT_EQ(lockManager.getLockMode(0), LockMode::Shared);
  EXPECT_EQ(lockManager.getLockMode(1024), LockMode::Shared);
}

// NOLINTNEXTLINE
TEST(LockManagerTest, UnlockAtEndOfTransaction) {
  auto lockManager = LockManager(1024);
  {
    auto t = Transaction(lockManager);
    t.addLock(0, LockMode::Exclusive);
    EXPECT_EQ(t.getLocks().size(), 1);
    EXPECT_EQ(lockManager.getLockMode(0), LockMode::Exclusive);
  }
  EXPECT_EQ(lockManager.getLockMode(0), LockMode::Unlocked);
  auto t = Transaction(lockManager);
  t.addLock(0, LockMode::Exclusive);
  EXPECT_EQ(t.getLocks().size(), 1);
  EXPECT_EQ(lockManager.getLockMode(0), LockMode::Exclusive);
}

// NOLINTNEXTLINE
TEST(LockManagerTest, IncompatibleLocksBlock) {
  using namespace std::chrono_literals;
  auto lockManager = LockManager(1024);
  auto t0 = std::optional<Transaction>(lockManager);
  t0->addLock(0, LockMode::Exclusive);

  auto shouldBlock = std::async(std::launch::async, [&] {
    auto t1 = Transaction(lockManager);
    t1.addLock(0, LockMode::Exclusive);
  });
  auto status = shouldBlock.wait_for(5ms);
  EXPECT_EQ(status, std::future_status::timeout);
  t0.reset();
}

// NOLINTNEXTLINE
TEST(LockManagerTest, DeadLockThrows) {
  auto lockManager = LockManager(1024);
  auto t0 = std::optional<Transaction>(lockManager);
  t0->addLock(0, LockMode::Exclusive);

  auto thrown = std::atomic<size_t>(0);
  auto lockAcquired = std::promise<void>();
  auto shouldBlock = std::async([&] {
    auto t1 = Transaction(lockManager);
    t1.addLock(1, LockMode::Exclusive);
    lockAcquired.set_value();
    try {
      t1.addLock(0, LockMode::Exclusive);
    } catch (const DeadLockError &) {
      ++thrown;
    }
  });
  lockAcquired.get_future().wait();
  try {
    t0->addLock(1, LockMode::Exclusive);
  } catch (const DeadLockError &) {
    ++thrown;
  }
  t0.reset();

  // One transaction should be aborted, but one should succeed
  EXPECT_EQ(thrown, 1);
}

// Added by Jigao
// NOLINTNEXTLINE
TEST(LockManagerTest, MultithreadSharedLocking) {
  auto lm = LockManager(1024);
  std::atomic<size_t> aborts = 0;
  std::vector<std::thread> threads;

  /// Using 20 Threads. They are just using SHARED LOCKS anyway.  :D
  for (size_t i = 0; i < 20; ++i) {
    threads.emplace_back([i, &lm, &aborts] {
      auto engine = std::mt19937_64(i);
      // Accesses on DataItems are geometrically distributed
      auto dataItemDistr = std::geometric_distribution<DataItem>(.1);
      // Number of locks is also geometrically distributed
      auto numLocksDistr = std::geometric_distribution<size_t>(.2);

      for (size_t j = 0; j < 20; ++j) {
        try {
          /// All txns locally in each thread.
          auto transaction = Transaction(lm);
          auto numLocks = numLocksDistr(engine);
          auto alreadyLocked = std::unordered_set<DataItem>();
          for (size_t lock = 0; lock < numLocks; ++lock) {
            DataItem dataItem;
            do {
              dataItem = dataItemDistr(engine);
            } while (alreadyLocked.count(dataItem));
            transaction.addLock(dataItem, LockMode::Shared);
            alreadyLocked.insert(dataItem);
          }
        } catch (const DeadLockError &) {
          ++aborts;
        }
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }
  auto foo = aborts.load();
  EXPECT_EQ(foo, 0);
}

// Added by Jigao
// NOLINTNEXTLINE
TEST(LockManagerTest, MultithreadExclusiveLocking) {
  auto lm = LockManager(1024);
  std::atomic<size_t> aborts = 0;
  std::vector<std::thread> threads;

  for (size_t i = 0; i < 4; ++i) {
    threads.emplace_back([i, &lm, &aborts] {
      auto engine = std::mt19937_64(i);
      // Accesses on DataItems are geometrically distributed
      auto dataItemDistr = std::geometric_distribution<DataItem>(.1);
      // Number of locks is also geometrically distributed
      auto numLocksDistr = std::geometric_distribution<size_t>(.2);

      for (size_t j = 0; j < 20; ++j) {
        try {
          /// All txns locally in each thread.
          auto transaction = Transaction(lm);
          auto numLocks = numLocksDistr(engine);
          auto alreadyLocked = std::unordered_set<DataItem>();
          for (size_t lock = 0; lock < numLocks; ++lock) {
            DataItem dataItem;
            do {
              dataItem = dataItemDistr(engine);
            } while (alreadyLocked.count(dataItem));
            transaction.addLock(dataItem, LockMode::Exclusive);
            alreadyLocked.insert(dataItem);
          }
        } catch (const DeadLockError &) {
          ++aborts;
        }
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }
  auto foo = aborts.load();
  EXPECT_LT(foo, 20);
}

// NOLINTNEXTLINE
TEST(LockManagerTest, MultithreadLocking) {
  auto lm = LockManager(1024);
  std::atomic<size_t> aborts = 0;
  std::vector<std::thread> threads;

  for (size_t i = 0; i < 4; ++i) {
    threads.emplace_back([i, &lm, &aborts] {
      auto engine = std::mt19937_64(i);
      // Accesses on DataItems are geometrically distributed
      auto dataItemDistr = std::geometric_distribution<DataItem>(.1);
      // 80% are shared accesses
      auto sharedDistr = std::bernoulli_distribution(0.8);
      // Number of locks is also geometrically distributed
      auto numLocksDistr = std::geometric_distribution<size_t>(.2);

      for (size_t j = 0; j < 20; ++j) {
        try {
          /// All txns locally in each thread.
          auto transaction = Transaction(lm);
          auto numLocks = numLocksDistr(engine);
          auto alreadyLocked = std::unordered_set<DataItem>();
          for (size_t lock = 0; lock < numLocks; ++lock) {
            DataItem dataItem;
            do {
              dataItem = dataItemDistr(engine);
            } while (alreadyLocked.count(dataItem));
            auto mode = sharedDistr(engine) ? LockMode::Shared : LockMode::Exclusive;
            transaction.addLock(dataItem, mode);
            alreadyLocked.insert(dataItem);
          }
        } catch (const DeadLockError &) {
          ++aborts;
        }
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }
  auto foo = aborts.load();
  EXPECT_LT(foo, 20);
}

} // namespace
