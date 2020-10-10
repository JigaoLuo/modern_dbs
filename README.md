# Databases Implementation on Modern CPU Architectures's Lab

## Introduction

Databases Implementation on Modern CPU Architectures, SS 2020, TUM

Website: https://db.in.tum.de/teaching/ss20/moderndbs/index.shtml?lang=en

## Dependency

- [CMake](https://cmake.org/)
- [Clang-tidy](https://clang.llvm.org/extra/clang-tidy/)
- [LLVM Package](https://llvm.org/): used only in `Task 07`.

## Tasks

### [Task 01: External Sorting](/task-external-sort/)

Build with:
```bash
$ mkdir -p build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=Debug ..
$ make
```

Run with:
```bash
$ ./tester
$ ./external_sort 
```

### [Task 02: Buffer Manager](/task-buffer-manager/)

Build with:
```bash
$ mkdir -p build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=Debug ..
$ make
```

Run with:
```bash
$ ./tester
```

### [Task 03: Slotted Pages](/task-slotted-pages/)

Build with:
```bash
$ mkdir -p build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=Debug ..
$ make
```

Run with:
```bash
$ ./tester
$ ./database_wrapper
```

### [Task 04: B+ Tree](/task-btree/)

Build with:
```bash
$ mkdir -p build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=Debug ..
$ make
```

Run with:
```bash
$ ./tester
```

### [Task 05: Lock Manager](/task-lock-manager/)

Build with:
```bash
$ mkdir -p build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=Debug ..
$ make
```

Run with:
```bash
$ ./tester
```

### [Task 06: Operators](task-operators/)

Build with:
```bash
$ mkdir -p build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=Debug ..
$ make
```

Run with:
```bash
$ ./tester
```

### [Task 07: Expression with LLVM Code Generation](task-expressions/)

Build with:
```bash
$ mkdir -p build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=Debug ..
$ make
```

Run with:
```bash
$ ./tester
$ ./bm_expression
```