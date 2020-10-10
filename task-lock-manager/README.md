# Transaction-Aware Lock Management

Implement a Lock Manager, as discussed in the lecture.
Your implementation should allow to concurrently lock and unlock items and block the execution thread until the lock 
could be acquired.
However, you should detect and handle deadlocks using the wait-for graph technique discussed in the lecture.
To block the thread, you can use a `std::shared_mutex`, and to abort a transaction, just throw an exception.

Your `WaitsForGraph` should be able to add new waits-for dependencies, but abort the transaction as soon as the graph
becomes cyclic.
When a transaction finishes, you should also remove all of its waits-for dependants.
Keep in mind that the graph will need separate synchronization to the Lock Manager.

The `LockManager` should use a fixed-size hashtable with a lock for each bucket and chain.
To manage the lifetime of the lock objects themselves, we recommend to use a [`std::shared_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr/shared_ptr)
with a custom deleter that deletes the lock in the LockManager.
The `Lock` structure is already set to have its lifetime managed by a `std::shared_ptr<Lock>`.
You can read more about this [here](https://en.cppreference.com/w/cpp/memory/enable_shared_from_this).

## Testing

You can test your implementation using the tests in `test/lock_manager_test.cc`.
They are designed to separately test the `WaitsForGraph` and the `LockManager` logic.
To run them, compile the project and then execute the `tester` binary.

**Passing the tests is a requirement but does not automatically mean that you will get full points.**

Additionally, your code will be checked for code quality. You can run the
checks for that by using the `lint` CMake target (e.g. by running `make lint`).
When your code does not pass those checks, we may deduct (a small amount of)
points.
