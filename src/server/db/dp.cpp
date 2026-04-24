#include "db.hpp"
#include <muduo/base/Logging.h>

// 初始化数据库连接
MySQL::MySQL()
    : _conn(nullptr)
{}
// 释放数据库连接资源
MySQL::~MySQL()
{
    // 连接由智能指针管理，不需要手动释放
}
// 连接数据库（使用连接池）
bool MySQL::connect()
{
    _connPtr = ConnectionPool::getInstance()->getConnection();
    if (_connPtr != nullptr)
    {
        _conn = _connPtr.get();
        LOG_INFO << "connect mysql success!";
        return true;
    }
    else
    {
        LOG_INFO << "connect mysql failed!";
        return false;
    }
}
// 更新操作
bool MySQL::update(string sql)
{
    if (_conn == nullptr)
    {
        if (!connect())
        {
            return false;
        }
    }
    
    if (mysql_query(_conn, sql.c_str()))
    {
        LOG_INFO << _conn << ":" << sql.c_str();
        LOG_INFO << __FILE__ << ":" << __LINE__ << ":"
                 << sql << "更新失败!";
        return false;
    }
    return true;
}
// 查询操作
MYSQL_RES *MySQL::query(string sql)
{
    if (_conn == nullptr)
    {
        if (!connect())
        {
            return nullptr;
        }
    }
    
    if (mysql_query(_conn, sql.c_str()))
    {
        LOG_INFO << __FILE__ << ":" << __LINE__ << ":"
                 << sql << "查询失败!";
        return nullptr;
    }
    return mysql_use_result(_conn);
}

MYSQL *MySQL::getConnection()
{
    if (_conn == nullptr)
    {
        connect();
    }
    return _conn;
}