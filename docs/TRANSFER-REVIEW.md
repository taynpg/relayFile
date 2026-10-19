# relayFile 传输逻辑评审

> 本文位于 `docs/`，文中所有源码路径与行号均**相对仓库根目录**。
> 评审范围：`Core/Protocol`、`Core/Net`、`Core/File`、`Gui/Form/RelayTask.cpp`、`Console/TransTest.cpp`
> 构建：MSYS2 UCRT64 + Qt6，`-DRF_USE_GUI=OFF`，Debug
> 实测：评审时 200MB / 1GB 单文件经本机环回中继，SHA-256 与 `cmp` 均一致，吞吐约 **41MB/s**（Debug）；
> 三批改造后同机为 **77.9MB/s（200MB）/ 82.2MB/s（1GB）**，详见文末各批验证表。

## 结论

整体分层是对的：控制连接与数据连接分离、帧格式有明确边界、滑动窗口 + 重传、分包/归档两条路径都跑得通，工程可读性也在平均水准之上。**问题不在"能不能传"，而在"传错了会不会知道"和"失败后会不会毁数据"。**

当时最需要修的是四个"静默错误"（均已修复，见下方状态总览）：

1. 接收端**先回 ACK 再写盘**，写盘失败只打日志 → 发送方认为已送达，双方都报成功，文件已损坏。
2. 接收端**直接写目标路径**（`WriteOnly` 即截断），中断/失败后目标文件是半截垃圾，原文件已被毁。
3. **完成语义没有握手**，`Answer_Complete` 定义了却从没用过：发送方一发完 `Request_Complete` 就宣布成功，接收方若没收到就超时失败。归档传输下发送方还会立刻删掉临时归档。
4. 帧**没有校验和、没有长度上限**，一个假帧头配一个合法正长度就能让解析器永久等待并吃光内存。

下面按严重级别列，每条都标了位置。不用考虑兼容性，所以给了"应该改成什么样"，而不是"怎么打补丁"。

---

## 改造状态总览

> 图例：**✅ 已修复** = 已落地并通过编译 + 端到端验证；**⏳ 未处理** = 仍然存在，逐条原因见下方对应条目；**⚠️ 部分** = 只修了一部分。
> 最近一次核对：2026-10-19，对照当前 HEAD 源码逐条 grep 确认，不是照抄改造记录。

| 级别 | 条目 | 状态 | 落点 / 原因 |
|---|---|---|---|
| P0-1 | ACK 早于落盘 | ✅ 第一批 | `OneFileTrans::handleRecvChuck` 改为先写盘后 ACK |
| P0-2 | 无 `.part` + rename | ✅ 第一批 | `.relaypart` + 长度校验 + 原子 rename |
| P0-3 | 完成语义无确认 | ✅ 第一批 | `Request_Complete` → `Answer_Complete` 握手 |
| P0-4 | 帧无 CRC/无上限 | ✅ 第二批 | 新帧格式 + headerCrc + `kMaxFrameSize` |
| P0-5 | 前导垃圾不清除 | ✅ 第二批 | 只保留末尾 3 字节，逐字节重同步 |
| P1-1 | 数据通路在 GUI 线程 | ✅ 第三批 | 新增 `FileSessionIO`，独立 IO 线程 |
| P1-2 | 一帧一 ACK / 按帧窗口 | ✅ 第二批 | 字节窗口 4MB + 累积 ACK + SACK |
| P1-3 | 串行执行、失败即中断整批 | ⚠️ 部分 | 元信息检查已改为"单文件缺失跳过"；**传输循环仍是失败即 `break`**（`RelayTask.cpp:426-429`），且仍为单任务串行 |
| P1-4 | `RunTaskItem` 忙等无超时 | ✅ 第三批 | 条件变量 + 两级超时（30s / 6h） |
| P1-5 | O(N) 串行 RTT | ✅ 第三批 | `AskFileMetaBatch` / `AskFileSamplesBatch` |
| P1-6 | 每块 4~5 次拷贝 | ⏳ 未处理 | 实测非瓶颈；`miniBuffer::Append` 仍无 `reserve` |
| P1-7 | 读锁下 `transMap_[key]` | ⏳ 未处理 | `ServerCore.cpp:286-287` 仍持 `QReadLocker` 调 `operator[]` |
| P2-8 | `Message` 拷贝漏字段 | ✅ 第一批 | 四个特殊成员 `= default` |
| P2-9 | `onCancelWaitMsg` 迭代中 erase | ✅ 第一批 | 先移出容器 + 锁外回调 + `done` 去重 |
| P2-10 | `FileSession` 析构不等线程 | ✅ 第一批 | `Quit()` 幂等，`ClientCore` 回 worker 线程销毁 |
| P2-11 | 客户端收帧无异常边界 | ✅ 第一批 | `onReadyRead` / `handleFrame` 全部 try/catch |
| P2-12 | 零字节文件进度除零 | ⏳ 未处理 | `RelayTask.cpp:907` 仍是 `transed * 1.0 / total` |
| P2-13 | 解包路径校验可绕过 | ⏳ 未处理 | `LocalHandle.cpp:257` 仍是无分隔符的 `startsWith` |
| P2-14 | 心跳判活窗口偏松 | ⏳ 未处理 | `ServerCore.cpp:11` 巡检仍为 15000ms |
| P2-15 | IPv6 客户端 ID 塌成 0.0.0.0 | ⏳ 未处理 | `ServerCore.cpp:50-53` 仍用 `toIPv4Address()` |
| P2-16 | 帧类型按数值区间判定 | ⏳ 未处理 | `Protocol.h:138-167` 仍是 `>=` / `<` 比大小 |
| P2-17 | `mark` 一名三用 | ✅ 第二批 | 拆成 `OneFrame::flags` / `Message::flags` |
| P2-18 | 死代码与未用协议类型 | ⚠️ 部分 | `FileSession::getFileMeta`、`bRequest/vRequest` 已处理；**`Answer_Start/Ack/Chuck/Cancel` 四个死类型与 `CmdExecutor` 仍在** |
| P2-19 | 头部尺寸注释与常量不符 | ✅ 第二批 | 新协议 `kFixedHeaderSize = 36`，注释与常量一致 |
| P2-20 | native-endian + 定长字符串 | ✅ 第二批 | 显式小端 + 变长路由字段（u8 长度） |
| 安全-1 | 明文帧绕过加密（降级） | ⏳ 未处理 | `CryptoHelper.cpp:79-81` 无加密位仍直接放行 |
| 安全-2 | 服务器零认证、`from` 可伪造 | ⏳ 未处理 | 未做身份校验 |
| 安全-3 | 口令直 Hash 当密钥 | ⏳ 未处理 | 无 salt / 迭代 / 密钥确认 |
| 安全-4 | 控制帧全程明文 | ⏳ 未处理 | 服务器仍需解析部分控制帧 |

**一句话现状**：会导致数据损坏的四条 P0 已全部关闭；剩下的是**一个"失败即中断整批"的行为问题**、**六条健壮性细节**（除零、路径校验、心跳、IPv6、类型判定、读锁写容器）和**整块安全边界**。

---

## P0 — 会造成数据损坏 / 永久卡死

### 1. ACK 早于落盘，写失败被吞掉  ✅ 已修复（第一批）
`Core/Net/OneFileTrans.cpp:200-216`

```cpp
auto f = CreateFrame(kFileType_Request_Ack);   // 先 ACK
emit signalRequestSend(f);
...
qint64 written = recvFile_.write(...);
if (written != size) { qWarning() << "写入文件失败"; }   // 只 warning
sendOrRecvTimeout_->start(defRecvTimeout);
```

头文件注释里写明这是为了修"写盘慢 → ACK 迟到 → 超时重传死循环"（`OneFileTrans.h:16-20`）。但用正确性换延迟是错的方向：磁盘满、网络盘掉线、权限错时，接收方照常 ACK，发送方推进窗口，最后双方都报"成功"，文件是坏的。

**改法**：写盘必须成功才 ACK；失败走 `Cancel` 让发送方立刻停。延迟问题用线程解决（见 P1-1），不要用 ACK 时序解决。

### 2. 没有 `.part` + rename，失败即毁原文件  ✅ 已修复（第一批）
`Core/Net/OneFileTrans.cpp:96-101`

```cpp
recvFile_.setFileName(filePath_);              // 就是最终目标路径
recvFile_.open(QIODevice::WriteOnly);          // 默认 Truncate
```

传输失败 / 用户取消 / 进程崩溃，目标路径上留下的是一个不定长残片，而原来的好文件已经没了。这个工具是拿来覆盖同步的，破坏性最大。

**改法**：
- 写入 `目标目录/<name>.relaypart`（或 `.part-<uuid>`）
- `Complete` 且字节数校验通过 → `flush()` → 原子 `rename`
- 失败/取消 → 删除 `.part`
- 顺带获得断点续传的基础（`.part` 长度 + 文件指纹 = 续传偏移）

### 3. 完成语义没有端到端确认  ✅ 已修复（第一批）
`Core/Net/OneFileTrans.cpp:145-156`、`238-285`

发送方在 `nextSend()` 里发完 `Request_Complete` 就 `state=Finished`、`emit signalFinished()`；`signalRequestSend` 的返回值也没人看（连接断了也照样"成功"）。接收方只有在收到 `Request_Complete` 时才收尾，收不到就 15s 超时判失败。

结果：**一侧成功、一侧失败**，GUI 上两边状态不一致。归档场景更糟：`RelayTask.cpp:276` 在 `RunTaskItem` 返回 true 后立刻 `QFile::remove(archivePath)`，此时对端可能还在等 Complete。

协议里 `kFileType_Answer_Complete` / `Answer_Cancel` / `Answer_Ack` / `Answer_Chuck` / `Answer_Start` 五个类型**定义了从未使用**（全局 grep 只有定义处）。

**改法**：`Request_Complete` → 接收方校验 `transSize_ == totalSize_`（可选再比 SHA-256）→ 回 `Answer_Complete{ok, err}` → 发送方才置 Finished。Answer 丢失就重发 Complete（幂等）。

### 4. 帧无校验、无长度上限 → 假帧头可永久卡死  ✅ 已修复（第二批）
`Core/Protocol/Protocol.cpp:41-113`

- 只有 `FF FE` 帧头 + `FF FF` 帧尾做定界，**没有 CRC**。数据体里出现 `FF FE` 就会被当候选帧头扫；如果后续 `len` 恰好合理、对应位置又恰好是 `FF FF`，就误判成真帧 → 整条流永久错位，且无法检测。
- `len < 0` 被当成假帧头跳过（第 72 行），但 `len` 是一个巨大的**正数**时，第 78 行直接 `return nullptr` 等更多数据 —— 永远等不到，缓冲区只涨不消。注释里说"常出现巨大 len"，但只挡了负数。
- 没有 `kMaxBodySize`，对端可以一个帧要求你分配 2GB。

**改法**：改成长度前缀 + 头部 CRC：

```
magic(4) | ver(1) | type(2) | flags(2) | sessionId(8) | index(8) | bodyLen(4) | headerCrc(4) | from/to/uuid(变长)
body: bodyLen 字节 | bodyCrc(4, 可选，大块建议关掉)
```

`bodyLen > kMaxBody` 直接判定协议错误并断开（不是跳过），比现在"猜哪里是下一帧"可靠一个量级。头部 CRC 让"部分收到/错位"能被明确识别。

### 5. 前导垃圾不清除  ✅ 已修复（第二批）
`Core/Protocol/Protocol.cpp:50-57`、`110-112`

`offset` 是局部变量，扫完没找到帧就 `return nullptr`，一个字节都不消费。如果流里有一段永远匹配不上的垃圾，每次 `readyRead` 都从 0 重新扫，缓冲区只增不减（O(n²) + 内存泄漏式增长）。

**改法**：无帧时保留最后一个 `HEADER_SIZE-1` 字节的尾部（可能被截断的帧头），其余丢弃。

---

## P1 — 架构与性能

### 1. 整条数据通路都跑在 GUI 线程  ✅ 已修复（第三批）
`Gui/relayFile.cpp:26-36` 在 GUI 线程创建 `FileSession`/`OneFileTrans`；`ClientCore` 才被 move 到 worker 线程。`signalDeliverFrame` 是跨线程 `QueuedConnection`，所以：

```
socket(worker) --排队--> GUI 线程: UnPack 前后处理 / 写盘 / 组 ACK --排队--> worker: write()
```

每 256KB 就要在 GUI 事件循环里跑两趟，**磁盘 IO 直接在 GUI 线程做**。UI 一忙（大表格重绘、弹窗、`processEvents` 嵌套）ACK 就晚，发送方 5s 超时重传 —— 这正是注释里那套"ACK 优先 / 把超时从 15s 降到 5s"补丁的根因。补丁在治症状。

**改法**：把 `OneFileTrans` 整体挪进一个 IO 线程（或直接让 `FileSession` 在 worker 线程里处理），GUI 只接收进度/完成信号。文件写和 ACK 出现在同一个线程里，时序问题从根上消失，`handleRecvChuck` 就能老老实实"先写后 ACK"。

### 2. 一帧一 ACK + 按帧计窗口  ✅ 已修复（第二批）
`Core/Net/OneFileTrans.cpp:123`、`163-217`；`CoreDefine.hpp:15-16`

256KB 一块、每块一个 ACK、窗口 32 帧。ACK 帧本身 126 字节不算大，但每块一次事件循环往返、一次 `RemoveOf`、一次 `CreateFrame`，开销是常数级叠加。窗口按"帧数"计，改块大小就改吞吐特性，不可预测。

**建议**：
- 窗口按**字节**计（如 4MB 在途），块大小与窗口解耦
- 累积 ACK：收到 N 字节或每 20ms 回一次 `ackIndex`（单调），而不是每块一个
- 重传用 SACK 位图，避免现在"只重传 ackIndex_ 那一块"的保守策略
- 握手时协商块大小/窗口（大文件大块，小文件小块）

### 3. 串行执行、失败即中断整批  ⚠️ 部分修复
`Gui/Form/RelayTask.cpp:412-425`

`for (id : todo) { ... if (!ok) { allSuccess=false; break; } }` —— 一个文件失败，后面全部不传，且没有"跳过继续"。同时全进程只有一个传输在跑（`DoubleLinker` 的 `isRunTaskItem_`/`curTaskUUID_` 是单槽状态，两个传输窗口同时开会互相踩）。

多小文件场景代价最大：每个文件都要 `Request_Send → Answer → Request_Start → echo → Complete` 至少 4 个控制往返，几百个小文件时间全耗在握手上。

**建议**：失败只标记该项并继续；批量握手（一次请求带一批文件）；或对小文件自动走归档路径（已有 `TarXzPacker`，直接复用）。

### 4. `RunTaskItem` 忙等且无总超时  ✅ 已修复（第三批）
`Core/Net/ClientHelper.cpp:131-145`

```cpp
while (isRunTaskItem_) { QThread::msleep(1); QCoreApplication::processEvents(); ... }
```

1ms 轮询 + 在非 GUI 线程调 `processEvents()`，CPU 白烧；更关键的是**没有超时**：对端不回应（服务器丢帧、对端崩溃、Answer 被防重逻辑丢掉）时永远转圈，UI 永远停在"传输中"，只能关窗口。

**改法**：`OneFileTrans` 的完成/失败信号 + `QWaitCondition` 驱动；外层加整体超时（例如 30s 无进度即失败）。

### 5. 存在性/采样检查是 O(N) 次串行 RTT  ✅ 已修复（第三批）
`Gui/Form/RelayTask.cpp:596-631`

每个文件一次 `AskFileMeta`；目标已存在时，再两侧各一次 `AskFileSamples`（各 10×4KB）。1000 个文件 = 至少 2000 次往返，而且是串行的，每次都挂着 30s 超时上限。慢网络下这一步比传文件还久。

**改法**：一次 `AskFileMetaBatch`（带 path 列表，回 `vector<FileMeta>`）；采样也批量；结果按批流水，不要一个文件一个来回。

### 6. 每块 4~5 次拷贝  ⏳ 未改（已评估为非瓶颈）
链路：`QTcpSocket::readAll` → `miniBuffer::Append`（`back_inserter`，不预留）→ `Protocol::UnPack` 拷进 `frame->data` → 加密再拷一份 → `RemoveOf(0,n)`（`vector::erase`，memmove 整个剩余缓冲）→ `QFile::write`。

1GB 实测没出现吞吐退化（41MB/s 稳定），所以**这不是当前瓶颈**，但纯属白烧内存带宽。改成"读偏移 + 环形缓冲 + `std::move` 取 body"，再顺手把 `Append` 的 `reserve` 补上，收益是免费拿的。

### 7. 服务器转发时的读锁写容器  ⏳ 未修复
`Core/Net/ServerCore.cpp:276-287`

```cpp
QReadLocker locker(&transLock_);
o = transMap_[otherId];      // operator[] 是非 const，key 不存在会插入空项
```

读锁下做可写操作，语义上就是数据竞争；还会在表里遗留空 `shared_ptr` 项。改 `value()` / `constFind`。单线程事件循环下侥幸没炸，一旦改成多线程服务器（`ServerAlone` 正在做）就是真 bug。

---

## P2 — 健壮性与细节

### 8. `Message` 拷贝构造 / 赋值漏字段  ✅ 已修复（第一批）
`Core/Protocol/Message.cpp:3-37`：拷贝构造漏了 `transId`、`uuid`。

现在没炸是因为 `FileSession::pushTask` 手动补了 `resp.uuid = msg.uuid`（`FileSession.cpp:50`）。任何人以后依赖拷贝都会拿到空 uuid，而且很难查。**这两个函数应该直接 `= default`**（`FileMeta` 的手写拷贝/移动同理）。

### 9. `onCancelWaitMsg` 迭代中 erase  ✅ 已修复（第一批）
`Core/Net/ControlSession.cpp:134-156`

```cpp
for (auto it = requestWaitFrame_.begin(); it != requestWaitFrame_.end(); it++) {
    ...
    requestWaitFrame_.erase(it);      // 之后 it++ 是对失效迭代器自增
}
```

而且是在**持锁状态下调用用户回调**（`promise->set_value`），可重入。改成 `it = requestWaitFrame_.erase(it)`，回调移到锁外，并先判重（`set_value` 二次调用会抛 `future_error`）。

### 10. `FileSession` 析构不等线程  ✅ 已修复（第一批）
`Core/Net/FileSession.cpp:16-20`：直接 `delete clientCore_; delete clientWorker_;`，没有 `quit()/wait()`。socket 在 worker 线程创建却被主线程删除。

实测每次退出都能复现：

```
QSocketNotifier: Socket notifiers cannot be enabled or disabled from another thread
QThread: Destroyed while thread '' is still running
```

把 `Quit()` 放进析构，或用 `deleteLater` + `QThread::finished` 串联。

### 11. 客户端收帧没有异常边界  ✅ 已修复（第一批）
`Core/Net/ClientCore.cpp:89-104` 没有 `try/catch`，而 `ServerCore::onRead` 有。`cereal` 反序列化遇到截断/畸形数据会抛，异常会穿透 Qt 事件循环（未定义行为 / `terminate`）。`deserializeStruct` 本身也没有长度/版本校验。

服务端在 `ControlSession::handleFrame` 开头就无条件 `deserializeStruct(frame->data, *answerMsg)`，任何非 Message 的帧都会抛。

### 12. 零字节文件进度除零  ⏳ 未修复
`Gui/Form/RelayTask.cpp:847-853`：`transed * 1.0 / total`，`total == 0` 时得到 NaN，再 `int(NaN)` 是 UB。空文件走 `nextSend()` 时确实会 `emit signalProcess(0, 0)`。

### 13. 解包路径校验可绕过  ⏳ 未修复
`Core/File/LocalHandle.cpp:257`

```cpp
if (!fullPath.startsWith(QDir::cleanPath(extractPath)))
```

没有分隔符。`extractPath = C:\out` 时，`C:\out-evil\x` 也通过。补一个 `+ QDir::separator()` 或比较 `relativeFilePath` 不以 `..` 开头。

### 14. 心跳判活窗口偏松  ⏳ 未修复
`Core/Net/ServerCore.cpp:10-13, 121-149`：客户端 1.5s 一次心跳，服务端 15s 才巡检一次，判活阈值 5s → 实际最长 20s 才能发现掉线，期间文件帧会一直被转发进死连接。巡检周期改成 1~2s，或直接依赖 TCP keepalive + 应用层 3 次心跳。

### 15. IPv6 客户端 ID 会塌成 `0.0.0.0`  ⏳ 未修复
`Core/Net/ServerCore.cpp:50-53`：`peerAddress().toIPv4Address()` 对纯 IPv6 返回 0。服务端监听的是 `QHostAddress::Any`，双栈。用 `peerAddress().toString()` 拼 ID。

### 16. 帧类型判定依赖枚举"数值区间"  ⏳ 未修复
`Core/Protocol/Protocol.h:86-117`、`Core/Net/ServerCore.cpp:217`（`type + 1` 就是 Answer）

新增一个类型插错位置，`GIsChuckAckFrame` / `GIsFileMessageFrame` 就会误判，而且不会有编译错误。改成显式的 `type → channel` 映射表 + `static_assert` 唯一性。

### 17. `mark` 一名三用  ✅ 已修复（第二批）
- `Frame::mark`：bit0 = 方向（0 发送 / 1 接收），bit8 = 加密（`Protocol.h:84`）
- `Message::mark`：1 = 普通，2 = 归档；又被 `pushTask` 改成 0 当"要不要连进度信号"的开关（`FileSession.cpp:53-57`）

`getMapKeyUUIDByMark` 用 `mark != 0` 判方向（`FileSession.cpp:37-44`），一旦 mark 带上加密位就会取错 key —— 现在没炸纯靠 `decryptFrame` 把那一位清掉了。建议拆成独立的 `flags` 字段，方向/加密/归档各自占位。

### 18. 死代码与未用协议类型  ⚠️ 部分清理
- `Answer_Complete`、`Answer_Cancel`、`Answer_Chuck`、`Answer_Ack`、`Answer_Start`：定义未用
- `FileSession::getFileMeta`：无调用者，且逻辑有 UB（条件写反 + 空 `vector` 取 `[0]`，`FileSession.cpp:232-245`）
- `DoubleLinker::bRequest` / `vRequest` / `CmdExecutor`：全项目无调用
- `ServerAlone/`：完整的一份 asio 服务器实现，未接入顶层 `add_subdirectory`，与 `ServerCore` 逻辑 1:1 双份维护

建议：要么补齐（Answer_* 用于完成握手/NACK），要么删掉。两套服务器实现最好只保留一套，另一套用同一份 `Protocol` + 共享的"路由表"逻辑，否则很快就不一致。

### 19. 宏/常量与实际不符  ✅ 已修复（第二批）
`Protocol.cpp:45`、`123`、`Protocol.h` 注释：`HEADER_SIZE` 实际是 **126** 字节（2+2+2+8+8+32+32+36+4），注释写 90。这类"魔数 + 手写偏移"应该集中成一个结构体：

```cpp
struct FrameHeader { /* 固定布局 */ };
static_assert(sizeof(FrameHeader) == 126);
// 用 offsetof 校验每个字段位置
```

### 20. native-endian + 定长字符串  ✅ 已修复（第二批）
`Protocol::UnPack` 直接 `memcpy` 到 `int64_t`，大端机直接坏；`from/to` 固定 32 字节、`fuuid` 固定 36 字节，超长静默截断（`clientId` 现在是 `ip:port`，一旦换成 UUID 就会踩）。

---

## 安全（按当前定位可以说"不承诺"，但要写清楚边界）

> ⏳ **本节四条均未处理**，属于当前定位下接受的风险，不是已修项。

1. **加密不构成安全边界**。`decryptFrame` 对没有加密位的帧直接放行（`CryptoHelper.cpp:79-81`），也就是说任何人只要会讲这个协议，就能用明文帧绕过加密，把文件推到任意在线客户端的任意路径（`fullPath` 完全由发送方指定）。这是一个"降级攻击"的教科书形态。
2. **服务器零认证**，`frame->from` 由客户端自己填，服务器只按 `frame->to` 转发，可任意伪造身份。
3. 口令直接用 SHA-256 当密钥（`CryptoHelper::setKey`），没有 salt / 迭代 / 密钥确认；口令错了的表现是"所有帧被丢弃 + 卡死"，不是明确报错。
4. 控制帧全程明文（路径、文件列表、sha256 结果）。注释说是因为服务器要解析控制帧 —— 其实只需要让服务器**不要**解析 `Ask_ID`/`Ask_ClientList` 之外的帧，就能整体加密。

如果这个工具的定位还是"临时内网、非敏感"，那就在 README 里明确"加密只是防误看，不是访问控制"，并且**接收端加根目录白名单**（所有落盘路径必须落在用户选定根目录下）。

---

## 建议的改造顺序（已按三批执行，逐项状态见上方总览表）

**第一批（不改协议，几天内可做完，消除数据损坏）**

1. `.part` + 校验 + 原子 rename
2. 先写盘后 ACK，写失败即 Cancel
3. `Request_Complete` / `Answer_Complete` 握手
4. `Message`/`FileMeta` 拷贝函数改 `= default`
5. `FileSession` 析构 quit+wait；`onCancelWaitMsg` 的 erase 修正
6. 客户端 `onReadyRead` 加异常边界

**第二批（改协议，一次做干净）**

7. 新帧格式：长度前缀 + 头部 CRC + body 上限 + 变长路由字段 + 显式小端
8. `flags` 拆位（方向 / 加密 / 归档），去掉 `mark` 复用
9. 累积 ACK + 字节窗口 + SACK；握手协商块大小/窗口

**第三批（架构）**

10. `OneFileTrans` 挪进 IO 线程，GUI 只收信号
11. `RunTaskItem` 改信号/条件变量驱动 + 整体超时
12. 批量元数据/采样查询，失败跳过继续
13. 断点续传（`.part` 已就位，加"文件指纹 + 偏移"协商即可）

---

## 第一批改造记录（已完成）

不涉及协议格式变更，编译通过（UCRT64 + Qt6），并跑了端到端验证。

| # | 项 | 落点 | 说明 |
|---|----|------|------|
| 1 | `.part` + 校验 + 原子 rename | `OneFileTrans::initTransfer/finalizeRecvFile/discardPartFile` | 接收数据写 `目标路径.relaypart`；Complete 时 flush、校验字节数、删旧文件、`QFile::rename`；失败/取消/超时删除 `.relaypart` |
| 2 | 先写盘后 ACK | `OneFileTrans::handleRecvChuck` | 写盘（循环处理短写）成功后才 `curBlockIndex_++` 并回 ACK；写失败 `failAndInterrupt` → 通知对端 Cancel |
| 3 | 完成握手 | `sendCompleteFrame` / `answerComplete` / `handleCompleteAnswer` | 发送方发 `Request_Complete` 后进入"等待回执"，收到 `Answer_Complete{ok,err}` 才置 Finished；回执丢失重发 Complete（最多 3 次），接收方对已完成传输幂等重放 Answer |
| 4 | 拷贝语义 | `Message.h/.cpp`、`FileMeta.h/.cpp` | 四个特殊成员全部 `= default`，删掉手写实现（原来漏 `transId`/`uuid`） |
| 5 | 线程退出 | `FileSession::Quit`、`ControlSession::Quit`、`onCancelWaitMsg` | `ClientCore` 回到 worker 线程内销毁；`Quit` 幂等；`onCancelWaitMsg` 先移出容器再在锁外回调，回调加 `done` 标志防重复 `set_value` |
| 6 | 异常边界 | `ClientCore::onReadyRead`、`FileSession::handleFrame`、`ControlSession::handleFrame` | 帧解析/反序列化全部包 try/catch，畸形数据丢弃而不是穿透事件循环 |

顺带修的：
- `onFrameReceive` 在 Finished/Interrupted 状态下收到 `Cancel` 不再回 `Cancel`（原来两端会互相刷 Cancel）。
- 发送方收到意外 `Request_Complete` 时不再删除本地临时归档（原来那行是误路由即毁数据）。
- `Answer_*` 五兄弟里 `Answer_Complete` 正式启用，其余四个仍是死类型（第二批随新协议一起清）。

### 验证

| 场景 | 结果 |
|------|------|
| 200MB 随机文件 → 新目标路径 | `SEND_OK` 4.99s（38MB/s），SHA-256 一致，无 `.relaypart` 残留，发送方在收到 `Answer_Complete` 后才报成功 |
| 5MB 文件 → 已存在的目标文件（500B） | 覆盖成功，`cmp` 一致，仍无残留 |
| 传 1GB 过程中强杀发送端 | 目标文件保持原内容（29B 未被截断），162MB 残片写在 `.relaypart`，接收方 15s 超时后自动删除残片 |
| 进程正常退出 | 不再出现 `QSocketNotifier ... from another thread` / `QThread: Destroyed while thread is still running` |

### 遗留

- 进程被强杀（`kill -9`）时 `.relaypart` 会留在磁盘上，需要启动清理或走断点续传（第三批 13）。
- `Answer_Complete` 走控制连接（500~549 段），依赖 `fuuid + flags` 路由；等第二批把 `mark` 拆成 `flags` 后一起整理。
- `.relaypart` 目前只做"失败即删"，没有做续传；`finalizeRecvFile` 里的长度校验就是续传偏移的挂载点。

---

## 第二批改造记录（已完成：新帧格式 / flags 拆位 / 字节窗口）

协议不兼容旧版本（按约定不做兼容），两端必须一起升级。

### 7. 新帧格式

`Core/Protocol/Protocol.{h,cpp}` 全量重写。线上格式（全部小端）：

```
0   4  magic       'R','F','M','1'
4   2  version     2
6   2  flags       FrameFlags
8   2  type
10  2  reserved
12  8  sessionId
20  8  index
28  4  frameLen    整帧字节数（含 36 字节固定头）
32  4  headerCrc   crc32(前 32 字节)
36  .. payload:
         u8 fromLen + from
         u8 toLen   + to
         u8 uuidLen + uuid
         body[...]
         u32 bodyCrc   (仅 body <= 64KB 时带)
```

- 帧长写在头部且受 `kMaxFrameSize`（32MB）约束 → 巨大的正 `len` 不再让解析器永久等待。
- `headerCrc` 覆盖前 32 字节（含 frameLen）→ 数据体里偶然出现的 magic 会被识别为假头并跳过 1 字节重同步。
- body ≤ 64KB 时带 `bodyCrc`（控制帧/ACK），大块数据靠 TCP + 分块重传 + 结束时的长度校验，不做额外全量 CRC。
- 路由字段变长（u8 长度，上限 255），不再定长 32/36 字节静默截断；超长时 `Pack` 返回空，发送方直接报错。
- 前导垃圾只保留末尾 3 字节，不再"每轮从头重扫"。
- `UnPack` 返回 `UnPackResult{NeedMoreData, Ok, ProtocolError}`：版本不符 / body 损坏是 `ProtocolError`，调用方断开连接，不再靠猜。

### 8. `flags` 拆位

- `OneFrame::mark` → `OneFrame::flags`：
  `kFrameFlag_FromReceiver`（原方向位）、`kFrameFlag_Encrypted`（原 `0x0100`）、`kFrameFlag_BodyCrc`。
  `FileSession::getMapKeyUUIDByMark(uuid, mark)` → `getMapKeyUUIDByDirection(uuid, fromReceiver)`。
- `Message::mark` → `Message::flags`（`MessageFlag::kRecursiveList` / `kArchive`）。
  原来 `pushTask` 把 `mark` 改成 0 当"要不要接进度"的开关，已删掉——那个判断本来就是 `needConnect`。
- 各 `Ask_*` 应答把结果码从 `mark=0/1` 移到 `msgStateCode`，`mark` 不再一名三用。

### 9. 累积 ACK + 字节窗口 + SACK + 协商

`Core/Net/OneFileTrans.{h,cpp}`：

- 窗口按字节（`defWindowBytes` = 4MB），与块大小解耦；块大小由**接收方**按文件大小挑
  （≤4MB → 64KB，≤256MB → 256KB，更大 → 1MB），通过 `Answer_Send` / `Request_Start` 的
  `Message.blockSize / windowBytes` 协商，发送方在开始发送前套用。
- ACK 累积：`index` = 下一个期望块号；`data` 是 SACK 位图（bit i 表示 `index+i` 已收到）。
- ACK 节流：累计收到 1MB 或距上次 ACK 超过 20ms 才回一次（原来每块一个）。
- 接收端允许乱序：按 `idx * blockSize` 定位写入，乱序块进 `recvOutOfOrder_`，空洞补齐后
  `curBlockIndex_` 一次推进；乱序集合超过 8192 判对端异常。
- 发送端 `inflight_` 只保留"确实还没到"的块，收到 SACK 即移除；超时只重传空洞，
  单块重传 5 次仍失败才判整体失败。

### 验证

| 用例 | 结果 |
|---|---|
| `relayFileUnpackTest`（14 项） | 全过：往返、body 内 magic、前导垃圾重同步、粘包、逐字节半包、255/256 路由字段、body CRC 损坏、header CRC 损坏、版本不符、发送侧 SACK 只重传空洞、接收侧乱序落盘、采样比较 |
| 200MB 随机文件 | `SEND_OK` 3.0s = **66.7MB/s**（第一批同机 38MB/s，Debug 构建），`cmp` 一致 |
| 320MB（1MB 块） | 4.8s = 66.2MB/s，一致 |
| 2MB（64KB 块） | 0.16s，一致，日志确认 `blockSize: 65536` |
| 传 320MB 中途 kill 发送端 | 原目标文件未被截断，残片只在 `.relaypart`，15s 超时后自动删除 |
| GUI 目标（`build-gui`，Qt6 Widgets） | 编译通过，0 warning（验证 `Message`/`Protocol` API 变更没有漏改调用点） |

### 第二批遗留

- `OneFrame::index` 仍是 `int64_t`，编码时按 uint64 处理；负数只是内部约定，第三批可以收成 uint64。
- body CRC 只在 ≤64KB 时计算；如果以后要防"落盘后位翻转"，应该在 `finalizeRecvFile` 里按整文件哈希校验（已有 `AskSha256` 可复用）。
- SACK 位图固定 256 位，窗口被夹在 `[2, 256]` 块；要支持更大窗口需要改成多字节变长位图。
- 块大小/窗口只做了"接收方说了算"的单向协商，没有按 RTT/丢包自适应。

---

## 第三批改造记录（已完成：线程模型 / 等待模型 / 批量查询 / 断点续传）

### 10. `OneFileTrans` 挪进 IO 线程，GUI 只收信号

新增 `Core/Net/FileSessionIO.{h,cpp}`：真正的传输引擎（`transferMap_`、所有 `OneFileTrans`）。
`FileSession` 退化成一个门面，只负责持有套接字线程、IO 线程和转发信号，方法全部可跨线程调用
（内部只做 `QMetaObject::invokeMethod` 投递）。

线程模型：

```
GUI 主线程 ──信号──> 传输 IO 线程（读源文件 / 写 .relaypart / ACK / 重传定时器）
    ^                     │
    └───── 队列信号 ───────┘
套接字线程 ── 收帧后直接投递 ──> 传输 IO 线程
```

几个关键点（都是踩过的坑）：

- **收帧必须 `DirectConnection` 在套接字线程里路由**（`DoubleLinker::onDeliverControl`）。
  原来是排队到 GUI 线程再处理：GUI 一在等任务结果，对端的应答就排在队里，双方互相等死。
- **发帧也不能经过门面**：`DoubleLinker` 直接连 `FileSessionIO::signalRequestSend` /
  `signalTaskStateChanged`（`DirectConnection`）。门面信号是投递到 GUI 线程的，
  `RunTaskItem` 一阻塞，数据帧就发不出去 —— 这个问题在测试里表现为"对端永远收不到 Answer_Send"。
- **FileSession 有了自己的 IO 线程**，退出时先 `BlockingQueuedConnection` 停传输、清映射，
  再 `quit()+wait()`，`QFile`/`QTimer` 都在创建它们的线程里销毁。
- **`ClientCore` 的 ClientInfo 加锁**：GUI、IO、套接字三个线程都会读 `clientId`，
  原来是无锁裸读写；`isConnected()` 改成读 `atomic<bool>`（不再跨线程碰 `QTcpSocket::state()`）。
- **心跳搬到控制连接自己的线程**：原来 `QTimer` 在 GUI 线程，界面卡住超过服务器的 15s 心跳超时就
  会被踢掉，随后走控制连接的 `Request_Complete` / `Cancel` 全部发不出去（测试里表现为接收端
  收满了数据却等不到 Complete，白等 15s 然后判失败）。

### 11. `RunTaskItem` 改条件变量驱动 + 总超时

- 去掉 `QThread::msleep(1) + processEvents()` 忙等（这在 GUI 里是空转 CPU，在 Console 里是
  唯一让事件循环转起来的办法，纯属巧合能用）。
- 改成 `std::condition_variable`：任务登记 / 结束 / 取消时由 IO 线程直接通知（`signalTaskStateChanged`）。
- 两级超时：会话迟迟建不起来（对端没应答）30s 判失败；整体兜底 6 小时（中途卡死本身有
  5s/15s 的收发超时负责）。
- `clearCurrentTaskItem()` 会唤醒等待者；`RelayTask::Quit()` 先取消任务再 `join` 线程，
  否则关窗口会一直等到任务超时。

### 12. 批量元数据 / 采样查询

- `BaseAskDF` 新增 `AskFileMetaBatch` / `AskFileSamplesBatch`；远端实现按 400 / 200 条分片，
  一个包查一批，不再一个文件一次往返（1000 个文件：3000 次 RTT → 3 次左右）。
- `Message` 增加 `sampleMap`（路径 → 采样块），批量采样回包按路径对齐。
- 服务端 `Ask_FileMeta` / `Ask_FileSamples` 兼容单条与批量：`strVec` 非空即批量。
- `RelayTask::onBaseCheck` 改成批量化，并且**单个文件查不到只跳过它**，不再整单退出
  （原来一个文件不存在就 `return`，前面几百个文件的检查白做）。

### 13. 断点续传

- `Message` 增加 `resumeOffset`（接收方已连续落盘的字节数）与 `resumeFingerprint`（源文件指纹）。
- 指纹 = `size + 首 64KB 的 SHA-256`（`FileDir::QuickFingerprint`）。接收方算指纹时必须传
  **期望的总长度**——残片只有一部分，用残片自身长度算出来的指纹永远对不上。
- 接收方 `tryReusePartFile`：长度合理 + 有指纹 + 指纹一致，三条同时满足才复用；
  只保留**块对齐**的前缀（崩溃可能留下写了一半的块），否则丢弃残片从 0 重传。
- 发送方 `applyResumeOffset`：从 `offset / blockSize` 块开始发，进度初值也设成续传点。
- 失败语义变了：**超时/网络中断保留 `.relaypart` 供续传，显式取消才删**。

### 验证

| 用例 | 结果 |
|---|---|
| `relayFileUnpackTest`（14 项） | 全过，`TOTAL FAILS: 0` |
| 2MB | 70ms = 28.6MB/s，一致 |
| 200MB | 2.57s = **77.9MB/s**（第二批同机 66.7MB/s），一致 |
| 1GB | 12.5s = **82.2MB/s**，一致 |
| 预置 100MB 残片后续传 | 接收端"复用临时文件 104857600 字节"，发送端"从块 100 开始"，10.1s 完成，一致 |
| 预置 100MB **伪造**残片 | "丢弃不可复用的临时文件"，全量重传，最终一致（不会把两个文件拼起来） |
| GUI 目标（`build-gui`） | 编译通过，0 warning |

速度提升来自"数据帧不再绕 GUI 线程一圈"：以前每个块要 工作线程 → GUI 队列 → 写盘 → GUI 队列 → 套接字。

### 第三批遗留

- `ControlSession` 对象本身活在 GUI 线程（`Gui/relayFile.cpp:26` 创建，`ClientHelper.cpp:275`
  用 `QueuedConnection` 把 `handleFrame` 投进来），但**重活不在 GUI 线程**：
  `Ask_FileList / Ask_Sha256 / Ask_Archive / Ask_FileMeta / Ask_FileSamples` 都经
  `dispatchMessage` 进 `workerPool_`（`ThreadPool(3)`，真 OS 线程），`Ask_ArchivePack`
  的 tar.xz 打包同样入池。请求方的列目录在 `ExplorerControl`/`RelayTask` 各自的
  `WorkerThread` 里，配 `WaitDialog` 嵌套事件循环。所以"列目录 / 算哈希 / 打包"不会卡界面。
  遗留的两处 GUI 牵连是：
  1. `handleFrame` 的一整帧 `deserializeStruct` 在 GUI 线程做。大目录的 `Answer_FileList`
     有几万条 `FileMeta`，解析是 O(帧大小) 的 CPU 拷贝，会有几十毫秒顿挫。
  2. 应答帧的发送要绕一次 GUI 线程：worker 线程 `emit signalRequestSend`，收发两端
     affinity 都是 GUI 线程，AutoConnection 先排到 GUI 再排到套接字线程。界面被别的
     模态操作卡住时，控制应答会跟着延迟。
  真要治就两件事：帧解析下放到 `workerPool_`（或给 `ControlSession` 单独一个线程），
  以及 `signalRequestSend` 用 `DirectConnection` 直达套接字线程。
- `ControlSession.cpp:252` 的 `worker->isDone = true` 在 worker 线程裸写，`clearWorker`
  在 GUI 线程持 `responseWaitLock_` 读，是未定义的数据竞争（实际影响小）。改成
  `std::atomic<bool>` 即可。
- 超时/中断会故意留下 `.relaypart` 供续传，长期不重传就成了垃圾。可以加"启动清理 N 天前的
  `.relaypart`"或者"任务列表里显示可续传项"。
- 断点续传只对"同一路径 + 同一 size + 首 64KB 相同"生效；首 64KB 相同但中间被改过的文件
  会误判为可续传（概率极低，真要严丝合缝就得上全文件哈希，代价是传前先读一遍）。
- `OneFrame::index` 仍是 `int64_t`；`Answer_Start` / `Answer_Ack` / `Answer_Chuck` / `Answer_Cancel`
  仍是死类型，可以下一轮清掉。
