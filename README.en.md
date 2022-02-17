# The Part of Memory System Library

-   [Introduction](#section_introduction)
-   [Directory Structure](#section_catalogue)
-   [Memory System Library](#section_libraries)
    -   [libdmabufheap system library](#section_libdmabufheap)
-   [Usage Guidelines](#section_usage)

## Introduction<a name="section_introduction"></a>

The part of *Memory system Library* belongs to the subsystem named *Utils Subsystem*. It provides the system library for upper-layer services to operate memory, ensuring the stability of upper-layer services.

## Directory Structure<a name="section_catalogue"></a>

```
/utils/memory
└── libdmabufheap           # DMA memory allocation system library
    ├── BUILD.gn
    ├── include             # DMA memory allocation system library header directory
    │   └── dmabuf_alloc.h
    ├── src                 # DMA memory allocation system library source directory
    │   └── dmabuf_alloc.c
    └── test                # DMA memory allocation system library usecase directory
```
## Memory System Libraries<a name="section_libraries"></a>

The memory system library is a component of the system library that integrates memory operations and manages them in a unified manner.

### libdmabufheap system library<a name="section_libdmabufheap"></a>

The memory system library provides interfaces for services to allocate and share shared memory. By allocating and sharing memory between hardware devices and user space, zero-copy memory between devices and processes is implemented to improve execution efficiency.

## Usage Guidelines<a name="section_usage"></a>

System developers can add or remove this part by configuring the product definition JSON file under **/productdefine/common/products** to enable or disable this part:

` "utils:utils_memory":{} `