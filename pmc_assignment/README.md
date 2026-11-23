# PostgreSQL 16 性能计数器观测

本作业在 PostgreSQL 16 的私有 Hash Join 建表路径 `MultiExecPrivateHash` 中加入 CPU 性能计数器（PMC）观测。通过会话参数 `halo_pmc_list` 从 `public.pmc_name` 表选择计数器；表结构和事件配置参考课程材料。参数默认为空，即默认关闭观测。

## 编译安装

```sh
./configure --prefix=/path/to/isolated/pg-pmc --enable-cassert
make -j4
make install
```

请使用独立的安装目录和数据库集群进行测试，不要覆盖正在使用的 PostgreSQL 实例。上述路径只是示例，请根据实际环境替换。

## 使用方法

在测试数据库中执行一次 `pmc_assignment/setup.sql`，创建与课程示例兼容的事件定义表。如果数据库中已有 `public.pmc_name` 表，不要重复执行建表语句。当前数据库角色需要有读取该表的权限。

```sql
SET max_parallel_workers_per_gather = 0;
SET halo_pmc_list = 'cycles,Instructions';
SELECT count(*) FROM table_a a JOIN table_b b ON a.id = b.id;
SET halo_pmc_list = '';
```

其中 `table_a`、`table_b` 和 `id` 是示例，使用时应替换为实际表和连接列。选择执行计划中包含 `Hash Join` 和私有 `Hash` 节点的查询。每个成功打开的计数器会输出一条 INFO 信息：

- `whole`：从启用计数器到 Hash 建表结束的原始计数，包含逐行采样产生的开销。
- `enabled`、`running`：内核报告的事件启用时间和实际运行时间，单位为纳秒。
- `loop`：每行数据进行哈希计算和插入前后两次读取的差值之和，不包括获取下一行输入的调用。
- `samples`、`missed`：逐行读取成功和未能读取的次数。

计数器在建表期间持续运行。x86 环境下，快速路径读取 perf 元数据共享页，仅在内核允许时执行 `RDPMC`；循环内不反复启停计数器。输出为未经多路复用比例校正的原始计数，比较结果前应检查 `enabled`、`running` 和 `missed`。如果快速读取不可用，逐行采样会记为未命中；最终仍会尝试通过 `read()` 报告整个建表阶段的计数。事件不支持、表不存在或 perf 权限不足时会输出警告，正常查询仍可继续执行。

当前实现只覆盖私有 Hash 建表，不覆盖并行共享 Hash 建表。事件定义与 CPU 和运行环境有关，特别是原始硬件事件不能保证跨机器通用。计数为零不一定意味着没有执行工作：虚拟机即使允许打开事件，也可能没有暴露可用的虚拟 PMU。

## 测试

```sh
sh pmc_assignment/test.sh /path/to/isolated/pg-pmc/bin
```

测试脚本会创建独立的临时数据库集群和 Unix socket，退出时停止测试服务器，并输出临时日志目录。测试内容包括：确认使用 Hash Join、检查查询结果不变，以及验证缺表、未知事件、无效输入和计数器可用或被拒绝时的行为。

如果 PostgreSQL 测试进程有权打开性能事件，且系统允许 `RDPMC`，可要求测试必须经过 x86 快速读取路径：

```sh
PMC_REQUIRE_FAST=1 sh pmc_assignment/test.sh /path/to/isolated/pg-pmc/bin
```

## 已验证结果与限制

开发环境中分别使用普通 `postgres` 用户，以及只向测试进程授予 `CAP_PERFMON` 的方式运行了测试。普通用户因权限不足无法打开事件；临时授予权限后，快速路径完成了 1000 次读取，未出现未命中。

该 VMware 虚拟机的用户态 `cycles` 和 `instructions` 读数为零；使用系统命令 `perf stat -e cycles:u,instructions:u` 也观察到了相同情况。因此，这次测试验证了读取流程，但不能证明计数数值准确。要进行有意义的性能验证，还需要具有可用 PMU 和相应权限的主机或虚拟机。

## 参考资料

- Linux `perf_event_open(2)` 手册，以及 `linux/perf_event.h` 中 `perf_event_mmap_page` 的读取协议。
