# Operators

Implement the following simplified physical operators by using the iterator model:

* Print: Prints tuples separated by newlines. Attributes should be separated
  with a comma without any extra spaces.
* Projection: Generates tuples with a subset of attributes.
* Select: Filters tuples by a given predicate. Each predicate consists of one
  relational operator (`l == r`, `l != r`, `l < r`, `l <= r`, `l > r`, `l >= r`)
  where `l` stands for an attribute and `r` stands for an attribute or a
  constant.
* Sort: Sorts tuples by the given attributes and direction (ascending or
  descending).
* HashJoin: Computes the inner equi-join of two inputs on one attribute.
* HashAggregation: Groups and calculates (potentially multiple) aggregates on
  the input.
* Union: Computes the union of two inputs with set semantics.
* UnionAll: Computes the union of two inputs with bag semantics.
* Intersect: Computes the intersection of two inputs with set semantics.
* IntersectAll: Computes the intersection of two inputs with bag semantics.
* Except: Computes the difference of two inputs with set semantics.
* ExceptAll: Computes the difference of two inputs with bag semantics.

To pass tuples between the operators, implement a `Register` class that
represents a single attribute. It should support storing 64 bit signed integers
and fixed size strings of length 16.

Add your implementation to the files `include/moderndbs/algebra.h` and
`src/algebra.cc`. There you can find the `Register` class and one class for
each operator. All definitions for the methods that you should implement are
provided. You can add new member functions and member variables to all classes
as needed.

Your implementation should pass all tests starting with `IteratorModelTest`.
All other tests are optional and not required to pass to get full points for
this task. We will of course check your code for correctness.

**Passing the tests is a requirement but does not automatically mean that you will get full points.**

Additionally, your code will be checked for code quality. You can run the
checks for that by using the `lint` CMake target (e.g. by running `make lint`).
When your code does not pass those checks, we may deduct (a small amount of)
points.
