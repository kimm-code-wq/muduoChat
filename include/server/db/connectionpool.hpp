#ifndef CONNECTIONPOOL_H
#define CONNECTIONPOOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>
#include <chrono>

using namespace std;
using namespace chrono;

// 数据库连接池类
class ConnectionPool
{
public:
    // 获取连接池实例
    static ConnectionPool* getInstance();
    // 获取数据库连接
    shared_ptr<MYSQL> getConnection();
    // 释放连接池
    ~ConnectionPool();
    // 销毁单例
    static void destroyInstance();

private:
    // 构造函数
    ConnectionPool();
    // 初始化连接池
    bool init();
    // 创建新连接
    MYSQL* createConnection();
    // 销毁空闲连接
    void destroyIdleConnections();
    // 连接池配置信息
    string _server;
    string _user;
    string _password;
    string _dbname;
    int _port;
    int _maxConn;
    int _minConn;
    // 连接队列，存储连接和其空闲时间
    queue<pair<MYSQL*, time_point<system_clock>>>* _connQueue;
    // 互斥锁
    mutex _mutex;
    // 条件变量
    condition_variable _cond;
    // 单例实例
    static ConnectionPool* _instance;
    // 单例互斥锁
    static mutex _instanceMutex;
    
    // 禁用拷贝构造
    ConnectionPool(const ConnectionPool&) = delete;
    // 禁用赋值操作
    ConnectionPool& operator=(const ConnectionPool&) = delete;
    // 禁用移动构造
    ConnectionPool(ConnectionPool&&) = delete;
    // 禁用移动赋值
    ConnectionPool& operator=(ConnectionPool&&) = delete;
};

#endif