项目简介
项目名称：基于 muduo 的分布式即时通讯服务器

项目描述
本项目实现了一个支持单聊、群聊、好友管理、离线消息的高性能聊天服务器。整体采用“高内聚、低耦合”的分层架构，将网络层、业务层与数据层分离。通过 Nginx 实现负载均衡，并利用 Redis 的发布/订阅功能解决跨服务器通信问题，具备一定的分布式部署能力。

主要技术栈
C++17 · muduo · MySQL · Redis (hiredis) · Nginx (stream) · JSON (nlohmann/json) 

核心工作与特点
1. 网络层
基于 muduo 网络库 构建 TCP 服务，采用 Reactor + 多线程模型，充分利用非阻塞 I/O 和事件驱动机制。

为主线程配置多个工作线程（setThreadNum），提升并发处理能力。

注册连接回调、断开回调与读事件回调，当前业务数据量较小且协议简单，暂未处理粘包问题（可扩展固定长度前缀方案）。

2. 业务层
使用 单例模式 管理 ChatService 对象，全局唯一。

建立消息 ID 到处理函数的映射表，模仿 muduo 的回调风格实现业务分发。

使用 互斥锁 保护在线用户的 TcpConnectionPtr 映射表（userConnMap），确保多线程安全。

集成数据库操作对象（UserModel、FriendModel、GroupModel、OfflineMsgModel）与 Redis 工具类。

3. 数据层
设计 5 张 MySQL 表：user、friend、allgroup、groupuser、offlinemessage。

封装 MySQL 类，提供 connect、update、query 等基础接口。

为每个表设计对应的实体类（User、Group、GroupUser）和模型类（UserModel 等），实现对象-关系映射（ORM）风格的数据操作。

离线消息处理：当目标用户不在线时，将消息 JSON 存入 offlinemessage 表；用户上线后自动拉取并清空。

4. 分布式支持
Nginx 负载均衡（TCP 层，stream 模块）：

配置 upstream 多台后端服务器（不同端口，如 6666、6667），支持权重、最大失败次数、超时踢除等健康检查机制。

客户端只需连接 Nginx 监听的端口（如 8000），由 Nginx 按策略分发请求，并可动态扩缩容后端服务器。

Redis 跨服通信（发布/订阅）：

针对不同业务服务器上的用户，通过 Redis 实现消息转发。

使用 hiredis 库，区分发布上下文与订阅上下文。订阅部分在独立线程中阻塞接收消息，通过回调向上层上报。

业务层根据用户在线状态决定：若目标用户位于另一台服务器，则向 Redis 对应频道发布消息；若本地直接发送。

5. 构建与测试
提供 build.sh 一键编译脚本，将可执行文件输出至 bin/ 目录。

第三方库（json、hiredis）置于 thirdparty/ 目录，便于管理。

服务器启动命令：ChatServer <ip> <port>；客户端连接命令：ChatClient <nginx_ip> 8000。

项目亮点与思考
分层清晰：网络、业务、数据三层解耦，易于维护和扩展。

并发安全：使用互斥锁保护共享连接表，数据库操作采用数据库连接池，支持并发读写。

分布式设计：引入 Nginx + Redis 实现负载均衡与跨服通信，突破单机性能瓶颈。

离线消息保障：暂存于 MySQL，避免因目标用户离线或 Redis 发布/订阅的不可靠性导致消息丢失。

可扩展性：消息队列（如 Kafka）替换 Redis Pub/Sub 以获得更可靠的消息交付。
