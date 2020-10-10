TODO:
1. buffer manager should release lock while I/O
2. use my own buffer manager for slotted page, when the first point is done

---

# Slotted Pages

In this exercise you implement the **Record Access** functionality of our database system.
We provide you with a basic record interface in `src/database.cc`, which you can access using the `database_wrapper`.
The basic functionality allows to insert new tuples and read existing tuples.

Your task is to implement the necessary components for the underlying storage engine:
* A free-space inventory (FSI)
* Slotted pages (SP)

In our system, each table has its own FSI and SP segment, both operating on the BufferManger.
This meta information is managed by the Schema in `include/schema.h`.

## Implementation

The `SPSegment` should store slotted pages as discussed in the lecture.
Your implementation should support allocation, reading, writing, resizing, and erasing of records in a page.
You should also implement **TID redirects** and use a **free-space inventory** to find suitable pages more efficiently.
For TID redirects, you should additionally make sure to bound the number of redirects to one:  
If a TID is already redirected, do not redirect the redirection target, but create a new redirection target instead.  

For both the slotted pages `sp_segment` and the free-space inventory `fsi_segment`, you can refer to 
the lecture on Access Paths.
The `sp_segment` should use the free-space inventory to efficiently find space for new pages.

## Testing
**IMPORTANT**: You can find tests in `test/segment_test.cc` and `test/slotted_page_test.cc`.
Note, however, that the tests are only meant to help YOU find the most obvious bugs in your
code. This time, the complexity of the assigment does not only come from algorithmic
problems but also from a higher amount of C++ code that you should write. 
We tried to keep the interface generic which gives you a high degree of freedom
during the implementation at the cost of more fine-granular tests.
Therefore, DO NOT blindly assume that your implementation is correct & complete
once your tests turn green.
Instead, we encourage you to add/modify/delete the tests throughout the
assignments.

Additionally, your code will be checked for code quality. You can run the
checks for that by using the `lint` CMake target (e.g. by running `make lint`).
When your code does not pass those checks, we may deduct (a small amount of)
points.