# IPC Zero-Copy vs Copy Benchmark (Linux)

这个仓库提供两个小程序，用于在 Linux 上模拟“复制式通信”和“零拷贝-ish 共享内存通信”的性能差异。

- `socket` 模式：发送端通过 TCP 发送数据，接收端 `recv` 到用户态缓冲区后做简单累加运算。
- `shm` 模式：发送端将数据写入 POSIX 共享内存，接收端直接读取共享内存并做同样的累加运算。

## 构建

```bash
make
```

## 运行示例

### 1) 复制式（TCP Socket）

终端 A：
```bash
./ipc_receiver socket 9090
```

终端 B：
```bash
./ipc_sender socket 127.0.0.1 9090 512 64
```

上面示例发送 512MB，总块大小 64KB。

### 2) 共享内存（零拷贝-ish）

终端 A：
```bash
./ipc_receiver shm
```

终端 B：
```bash
./ipc_sender shm 512 64
```

## 参数说明

- `total_mb`：总发送大小（MB），默认 256。
- `chunk_kb`：每次发送块大小（KB），默认 64。
- `port`：socket 监听端口，默认 9090。

## 注意事项

- `shm` 模式使用一个共享内存区和两个 POSIX 信号量进行同步，主要用于简单性能对比。
- 若重复运行 `shm` 模式，发送端会先 `shm_unlink` 清理旧的共享内存。
