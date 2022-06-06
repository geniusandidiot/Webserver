# Webserver
Linux下C++轻量级Web服务器

* 使用 线程池 + 非阻塞socket + epoll(ET和LT均实现) + 事件处理(Reactor和模拟Proactor均实现) 的并发模型

* 利用正则与状态机解析HTTP请求报文，实现处理静态资源的请求；

* 利用标准库容器封装char，实现自动增长的缓冲区；

* 基于小根堆实现的定时器，关闭超时的非活动连接；

* 利用单例模式与阻塞队列实现异步的日志系统，记录服务器运行状态；

* 利用RAII机制实现了数据库连接池，减少数据库连接建立与关闭的开销，同时实现了用户注册登录功能。

项目启动
--
* 确认已安装MySQL数据库
```cpp
// 建立yourdb库
create database yourdb;

// 创建user表
USE yourdb;
CREATE TABLE user(
    username char(50) NULL,
    passwd char(50) NULL
)ENGINE=InnoDB;

// 添加数据
INSERT INTO user(username, passwd) VALUES('name', 'passwd');
```

* 修改main.cpp中的数据库初始化信息
```cpp
//数据库登录名,密码,库名
string user = "root";
string passWord = "root";
string databasename = "yourdb";
```

* build
```cpp
sh ./build.sh
```

* 启动server
```cpp
./server
```

* 浏览器端输入ip:port进行访问测试
```cpp
ip:9006
```
