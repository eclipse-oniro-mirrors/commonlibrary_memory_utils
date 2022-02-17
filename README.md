# 内存系统库

-   [简介](#section_introduction)
-   [目录](#section_catalogue)
-   [内存系统库](#section_libraries)
    -   [libdmabufheap系统库](#section_libdmabufheap)
-   [使用说明](#section_usage)

## 简介<a name="section_introduction"></a>

内存系统库部件位于公共基础库子系统中，为上层业务提供对应的操作内存的系统库，保证上层业务的稳定性。

## 目录<a name="section_catalogue"></a>

```
/utils/memory
└── libdmabufheap           # DMA内存分配链接库
    ├── BUILD.gn
    ├── include             # DMA内存分配链接库头文件目录
    │   └── dmabuf_alloc.h
    ├── src                 # DMA内存分配链接库源代码目录
    │   └── dmabuf_alloc.c
    └── test                # DMA内存分配链接库自测用例目录
```
## 内存系统库<a name="section_libraries"></a>

内存系统库是集成内存操作的系统库的部件，对内存操作的系统库进行统一管理。

### libdmabufheap系统库<a name="section_libdmabufheap"></a>

为业务提供分配共享内存的接口，通过在硬件设备和用户空间之间分配和共享内存，实现
设备、进程间零拷贝内存，提升执行效率。

## 使用说明<a name="section_usage"></a>

系统开发者可以通过配置productdefine/common/products下的产品定义json文件，增加或移除本部件，来启用或停用本部件。

` "utils:utils_memory":{} `