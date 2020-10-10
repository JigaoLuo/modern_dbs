# B+-Tree

Implement a B+-Tree index for your database using the header `include/moderndbs/btree.h`.
Use page ids and your buffer manager instead of pointers to resolve child nodes in the tree.

The B+-Tree must support the following operations:
- `insert` Inserts a new key-value pair into the tree.
- `erase` Deletes a specified key. You may simplify the logic by accepting under full pages.
- `lookup` Returns a value or indicates that the key was not found.
- You *may* omit the linking of leaf nodes.

Use the concurrency control techniques from the slides **Concurrent Access (2)** and **Concurrent Access (3)**.
You can assume, that the `buffer_manager` methods `fix_page` and `unfix_page` work as described in the lecture.
(We added a fake buffer manager that allows you to test your code without I/O)


The index should be implemented as C++ template that accepts the parameters **key type**, **value type**, **comparator** and **page size**.
Note that this task does no longer use a dynamic **page size** but compile-time constants.
You therefore have to implement the B+-Tree primarily in the header `include/moderndbs/btree.h` but you are able to compute a more convenient node layout at compile-time.

You can test your implementation using the tests in `test/btree_test.cc`.
The tests will instantiate your B+-Tree as `BTree<uint64_t, uint64_t, std::less<uint64_t>, 1024>`.
To run them, compile the project and then execute the `tester` binary.

**Passing the tests is a requirement but does not automatically mean that you will get full points.**

Additionally, your code will be checked for code quality. You can run the
checks for that by using the `lint` CMake target (e.g. by running `make lint`).
When your code does not pass those checks, we may deduct (a small amount of)
points.