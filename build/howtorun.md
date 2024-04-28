cmake : version >= 3.20

nginx : nginx.conf 配置在与 events 和 http 同级，添加如下内容：

`

    stream {
        upstream MyServer {
            server 127.0.0.1:6000 weight=1 max_fails=3 fail_timeout=30s;
            server 127.0.0.1:6002 weight=1 max_fails=3 fail_timeout=30s;
        }

        server {
            proxy_connect_timeout 1s;
            listen 8000;
            proxy_pass MyServer;
            tcp_nodelay on;
        }
    }

`

由于我的mysql与redis均配置在docker中，请查询相应下载配置流程自行下载，如果有问题请发送邮件到：

`junliang1lxs@163.com`

mysql : `docker run 的时候请指定 -p 3301:3306`，或修改`src/server/db/db.cpp`中的端口号为你设置的mysql端口号，以及密码和其他内容的修改

redis ： `docker run 的时候请指定 -p 6371:6379`，或修改
`src/server/redis/redis.cpp`中`connect`方法中连接时的端口号为你指定的端口号

环境配置完毕后，到build目录下：
执行

`rm -rf *`

和

`cmake ..`

和 

`make`

然后生成的服务器与客户端的可执行文件在 `../bin` 目录下，即可做测试

启动服务端可用以下命令(在nginx.conf中就配置了两个server)：

`./ChatServer 127.0.0.1 6000`

`./ChatServer 127.0.0.1 6002`

启动客户端可用以下命令(在nginx.conf中listen的端口为8000)：

`./ChatClient 127.0.0.1 8000`