# 二 项目基本技术介绍
  项目开发遵循“高内聚、低耦合”的思想，将网络层面、业务层面和数据层面的服务分离开来，依据基于对象的方式进行开发。
### 项目介绍：
  1. 网络服务部分：
    底层使用muduo网络库实现：
  - 包含TcpServer对象和EventLoop对象；
  - 注册三个事件：因为始使低流量的业务（基于聊天的），没有考虑粘包问题，所以只需要关注服务器的连接、断开和读事件发生的回调函数。setConnectionCallback注册连接和断开的回调函数，setMessageCallback注册读事件发生的会带函数。如果存在粘包问题，就需要通过setWriteCompleteCallback注册相应的回调函数。这都结合muduo网络库来实现；
  - 设置线程数量，如果不设置，默认是单线程工作。
  2. 数据库操作的类封装：
  - mysql数据库结构，包含五张表：
    
  a. user表
  
  字段名称 | 字段类型 | 字段说明 | 约束 |
  ---|--- | --- | ---
  id | int | 用户id |PRIMARY KEY、AUTO_INCREMENT
  name | varchar(50) |用户名 | NOT NULL, UNIQUE
  password | varchar(50) | 用户密码 |NOT NULL
  state | enum('online','offline') |当前登录状态 | DEFAULT 'offline'
  
  b. friend表
  
  字段名称 | 字段类型 | 字段说明 | 约束 |
  ---|--- | --- | ---
  userid | int | 用户id |NOT NULL、联合主键
  friendid | int | 好友id | NOT NULL、联合主键
  
  c. allgroup表
  
  字段名称 | 字段类型 | 字段说明 | 约束 |
  ---|--- | --- | ---
  id | int | 组id |PRIMARY KEY、AUTO_INCREMENT
  groupname | varchar(50) |组名称 | NOT NULL, UNIQUE
  groupdesc | varchar(100) | 组功能描述 |DEFAULT ''
  
  d. groupuser表
  
  字段名称 | 字段类型 | 字段说明 | 约束 |
  ---|--- | --- | ---
  groupid | int | 组id |NOT NULL、联合主键
  userid | int |组员id | NOT NULL, UNIQUE
  grouprole | ENUM('creator', 'normal') | 组内角色 |色DEFAULT ‘normal’
  
  e. offlinemessage表
  
  字段名称 | 字段类型 | 字段说明 | 约束 |
  ---|--- | --- | ---
  userid | int | 用户id |NOT NULL
  message | VARCHAR(500) | 离线消息（存储Json字符串） | NOT NULL
  
  - 数据库类封装
    封装了MySQL类，实现数据的连接，更新和查询操作
    
  - 数据库操作类封装：
    - User、UserModel：user表映射和表操作的类
    - FriendModel：较为简单，friend操作类
    - OfflineMsgModel：离线消息存储类(当对方用户不在线时，先将发送的消息存储在数据库中，等对方上线后先进行检查，然后清空)
    - Group、GroupModel：针对群组业务表映射和操作的类
    - GroupUser继承自User：针对群组中的用户，增加了其在群组的角色信息
  
  3. 业务实现部分：
  chatservice.hpp和chatservice.cpp
  类的设计采用单例模式(Singleton Pattern)，保证一个类仅有一个实例，并提供一个访问它的全局访问点，该实例被所有程序模块共享。具体业务有：
  - instance：提供接口，用于获取单例对象
  - 数据成员： 
  msgHandlerMap：存储消息和其对应的业务处理方法；
  userConnMap：存储用户id和其对应的Tcp连接TcpConnectionPtr
  connMutex：互斥锁
  针对数据库操作的四个对象：UserModel、OfflineMsgModel、FriendModel和GroupModel对象
  对后面集群服务的redis封装类
  
  - ctor中：采用类似于muduo网络库中事件回调的方式注册针对各种消息的回调函数，当有该消息id的事件到来时，调用对应的函数进行处理
  - 当对userConnMap进行操作时，需要进行线程安全保护
    
  4. 使用nginx实现服务器的负载均衡
  作用：
  - 把用户的请求按照负载算法分发到具体的业务服务器上面，分发根据分发的权重进行分发
  - 能够和服务器保持心态机制，检测服务器故障
  - 能够发现新添加的服务器设备，方便服务器数量的扩展
  
  操作流程：
  - 安装nginx，安装时需要注意配置tcp的负载均衡(加上--with-stream)
  - 配置nginx.conf(这里我选择的服务器是两个不同的端口，client连接时，只需要连接8000端口即可)：
  ```
  stream {
	upstream MyServer {
		server 127.0.0.1:6666 weight=1 max_fails=3 fail_timeout=30s;
		server 127.0.0.1:6667 weight=1 max_fails=3 fail_timeout=30s;
	}
	
	server {
		#连接时超过1秒判定为连接失败
		proxy_connect_timeout 1s;
		#nginx监听8000端口，客户端连接该端口
		listen 8000;
		proxy_pass MyServer;
		tcp_nodelay on;
	}
  ```
  
  5. redis
  如果客户端登陆在不同的服务器上，则是无法完成通信的，这里采用redis的发布订阅功能来完成(这个类似于观察者模式)
  redis.hpp和redis.cpp，实现功能是：
  - 向redis指定的通道channel发布消息
  - 向redis指定的通道subscribe订阅消息
  - 向redis指定的通道unsubscribe取消订阅消息
  - 在独立线程中接收订阅通道中的消息
  - 初始化向业务层上报通道消息的回调对象
  
  数据成员：
  负责发布消息的上下文对象和负责订阅消息的上下文对象
  负责收到订阅的消息，给service层上报的成员
  
  需要注意的是，订阅和发布都是执行三个步骤：
  - redisAppendCommand
  - redisBufferWrite
  - redisGetReply
  但是publish执行后立马会得到反馈，不会阻塞，所以可以通过redisCommand一步实现
  而subscribe通道后，系统会以阻塞的方式等待消息的到来，所以订阅和取消订阅函数只需要实现命令的发送，实际的数据另开一个独立的线程(也就是观察者来进行接收)
  
  
### 目录组装结构

- bin: 可执行文件
- include: 头文件
- src: 对应的函数实现文件
- thirdparty: 使用的第三方库文件，这里主要是json库，没有采用protobuf来实现，而是借助json来解析消息
- redis: 采用的是hiredis

# 三 编译
执行./build.sh

# 四 测试
启动服务器：
ChatServer ip port

服务器连接：
ChatClient ip 8000
这里的ip是nginx服务器的ip地址，而nginx默认的80端口，它可以进行一些静态资源的访问，该项目中没有用到


