#include "connectionpool.hpp"

#include <muduo/base/Logging.h>
#include <iostream>
#include <fstream>
#include "../../../thirdparty/json.hpp"

using json = nlohmann::json;

// 从配置文件读取配置
static json loadConfig()
{
    std::ifstream configFile("config.json");
    if (!configFile.is_open())
    {
        LOG_INFO << "Failed to open config.json, using default configuration";
        // 返回默认配置
        return json({
            {"mysql", {
                {"server", "127.0.0.1"},
                {"user", "root"},
                {"password", "123456"},
                {"dbname", "chat"},
                {"port", 3306}
            }},
            {"connectionPool", {
                {"maxConn", 10},
                {"minConn", 5}
            }}
        });
    }
    
    json config;
    try
    {
        configFile >> config;
    }
    catch (const json::parse_error& e)
    {
        LOG_INFO << "Failed to parse config.json: " << e.what() << ", using default configuration";
        // 返回默认配置
        return json({
            {"mysql", {
                {"server", "127.0.0.1"},
                {"user", "root"},
                {"password", "123456"},
                {"dbname", "chat"},
                {"port", 3306}
            }},
            {"connectionPool", {
                {"maxConn", 10},
                {"minConn", 5}
            }}
        });
    }
    
    return config;
}

// 数据库配置信息
static json config = loadConfig();
static string server = config["mysql"]["server"];
static string user = config["mysql"]["user"];
static string password = config["mysql"]["password"];
static string dbname = config["mysql"]["dbname"];
static int port = config["mysql"]["port"];
static int maxConn = config["connectionPool"]["maxConn"];
static int minConn = config["connectionPool"]["minConn"];

// 连接池实例
ConnectionPool* ConnectionPool::_instance = nullptr;

// 互斥锁，用于保证单例模式的线程安全
mutex ConnectionPool::_instanceMutex;

// 获取连接池实例
ConnectionPool* ConnectionPool::getInstance()
{
    if (_instance == nullptr)
    {
        lock_guard<mutex> lock(_instanceMutex);
        if (_instance == nullptr)
        {
            _instance = new ConnectionPool();
        }
    }
    return _instance;
}

// 销毁单例
void ConnectionPool::destroyInstance()
{
    lock_guard<mutex> lock(_instanceMutex);
    if (_instance != nullptr)
    {
        delete _instance;
        _instance = nullptr;
        LOG_INFO << "Connection pool instance destroyed";
    }
}

// 构造函数
ConnectionPool::ConnectionPool()
    : _connQueue(nullptr)
{
    _server = server;
    _user = user;
    _password = password;
    _dbname = dbname;
    _port = port;
    _maxConn = maxConn;
    _minConn = minConn;
    _connQueue = new queue<pair<MYSQL*, time_point<system_clock>>>();
    
    // 初始化连接池
    if (!init())
    {
        LOG_INFO << "Connection pool initialization failed";
    }
}

// 初始化连接池
bool ConnectionPool::init()
{
    for (int i = 0; i < _minConn; ++i)
    {
        MYSQL* conn = createConnection();
        if (conn == nullptr)
        {
            LOG_INFO << "Failed to create connection during initialization!";
            // 清理已创建的连接
            while (!_connQueue->empty())
            {
                MYSQL* c = _connQueue->front().first;
                _connQueue->pop();
                mysql_close(c);
            }
            return false;
        }
        
        _connQueue->push({conn, system_clock::now()});
    }
    
    LOG_INFO << "Connection pool initialized with " << _minConn << " connections";
    return true;
}

// 创建新连接
MYSQL* ConnectionPool::createConnection()
{
    MYSQL* conn = mysql_init(nullptr);
    if (conn == nullptr)
    {
        LOG_INFO << "mysql_init failed!";
        return nullptr;
    }
    
    MYSQL* p = mysql_real_connect(conn, _server.c_str(), _user.c_str(),
                                  _password.c_str(), _dbname.c_str(), _port, nullptr, 0);
    if (p == nullptr)
    {
        LOG_INFO << "mysql_real_connect failed!";
        mysql_close(conn);
        return nullptr;
    }
    
    mysql_query(conn, "set names gbk");
    return conn;
}

// 销毁空闲连接
void ConnectionPool::destroyIdleConnections()
{
    unique_lock<mutex> lock(_mutex);
    
    // 当连接数超过最小连接数时，销毁多余的空闲连接
    while (_connQueue->size() > _minConn)
    {
        // 检查连接的空闲时间
        auto now = system_clock::now();
        auto idleTime = duration_cast<chrono::seconds>(now - _connQueue->front().second).count();
        
        if (idleTime > 1)
        {
            // 空闲时间超过1秒，销毁连接
            MYSQL* conn = _connQueue->front().first;
            _connQueue->pop();
            mysql_close(conn);
            LOG_INFO << "Destroyed idle connection (idle time: " << idleTime << "s), current connections: " << _connQueue->size();
        }
        else
        {
            // 空闲时间不足1秒，停止销毁
            break;
        }
    }
}

// 获取数据库连接
shared_ptr<MYSQL> ConnectionPool::getConnection()
{
    unique_lock<mutex> lock(_mutex);
    
    // 等待连接可用
    while (_connQueue->empty())
    {
        if (cv_status::timeout == _cond.wait_for(lock, chrono::seconds(1)))
        {
            // 超时，检查是否需要创建新连接
            if (_connQueue->size() < _maxConn)
            {
                MYSQL* conn = createConnection();
                if (conn != nullptr)
                {
                    _connQueue->push({conn, system_clock::now()});
                    LOG_INFO << "Created new connection, current connections: " << _connQueue->size();
                }
                else
                {
                    return nullptr;
                }
            }
            else
            {
                LOG_INFO << "Connection pool exhausted!";
                return nullptr;
            }
        }
    }
    
    // 获取连接
    MYSQL* conn = _connQueue->front().first;
    _connQueue->pop();
    
    // 检查连接是否有效
    if (mysql_ping(conn) != 0)
    {
        LOG_INFO << "Connection is invalid, creating a new one";
        mysql_close(conn);
        
        // 创建新连接
        conn = createConnection();
        if (conn == nullptr)
        {
            return nullptr;
        }
    }
    
    // 使用智能指针管理连接，当智能指针析构时，将连接归还到连接池
    shared_ptr<MYSQL> sp(conn, [this](MYSQL* p) {
        unique_lock<mutex> lock(_mutex);
        // 检查连接是否有效
        if (mysql_ping(p) == 0)
        {
            _connQueue->push({p, system_clock::now()});
            _cond.notify_one();
            
            // 检查是否需要销毁空闲连接
            destroyIdleConnections();
        }
        else
        {
            LOG_INFO << "Invalid connection, closing it";
            mysql_close(p);
        }
    });
    
    return sp;
}

// 释放连接池
ConnectionPool::~ConnectionPool()
{
    unique_lock<mutex> lock(_mutex);
    if (_connQueue != nullptr)
    {
        while (!_connQueue->empty())
        {
            MYSQL* conn = _connQueue->front().first;
            _connQueue->pop();
            mysql_close(conn);
        }
        delete _connQueue;
        _connQueue = nullptr;
    }
    
    LOG_INFO << "Connection pool destroyed";
}