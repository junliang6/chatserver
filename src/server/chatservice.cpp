#include "chatservice.hpp"
#include "public.hpp"
#include "user.hpp"
#include "usermodel.hpp"
#include "friendmodel.hpp"


#include <muduo/base/Logging.h>
#include <string>
#include <vector>
#include <iostream>

using namespace std::placeholders;
using namespace muduo;
using std::cout;
using std::endl;

// 获取单例对象的接口函数
ChatService* ChatService::instance() {
    static ChatService service;
    return &service;
}

// 注册消息以及对应的Handler回调操作
ChatService::ChatService() {
    // 用户基本业务管理相关事件处理回调注册
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGOUT_MSG, std::bind(&ChatService::logout, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

    // 群组业务管理相关事件处理回调注册
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    // 连接redis服务器
    if(_redis.connect()) {
        // 设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}

// 服务器异常, 业务重置方法
void ChatService::reset() {
    // 把online状态的用户, 设置成offline
    _userModel.resetState();
}

// 获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid) {
    // 记录错误日志，msgid没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgid);
    if(it == _msgHandlerMap.end()) {

        // 返回一个默认的处理器，空操作
        return [=](const TcpConnectionPtr& conn, json& js, Timestamp){
            LOG_ERROR << "msgid:" << msgid << "can not find handler!";
        };      
    } else {
        return _msgHandlerMap[msgid];
    } 
}

// 处理登陆业务 id pwd
void ChatService::login(const TcpConnectionPtr& conn, json& js, Timestamp time) {
    int id = js["id"].get<int>();
    std::string pwd = js["password"];

    User user = _userModel.query(id);
    if(user.getId() == id && user.getPwd() == pwd) {
        if(user.getState() == "online") {
            // 该用户已经登陆，不允许重复登陆
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "this count is online, can't login twice";
            conn->send(response.dump());
        } else {
            // 登录成功，记录用户连接信息
            {
                std::lock_guard<std::mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }

            // id 用户登陆成功后，向redis订阅channel(id)
            _redis.subscribe(id);

            // 登陆成功, 更新用户状态信息 state offline => online
            user.setState("online");
            _userModel.updateState(user);

            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();
            LOG_INFO << "login success!";

            // 查询该用户是否有离线消息
            std::vector<std::string> msgVec = _offlineMsgModel.offlineMsgQuery(id);
            if(!msgVec.empty()) {
                // 读取该用户的离线消息后，把该用户的所有的离线消息删除掉
                response["offlinemsg"] = msgVec;
                _offlineMsgModel.remove(id);
            }

            std::vector<User> userVec = _friendModel.query(id);
            if(!userVec.empty()) {
                std::vector<std::string> vec2;
                for(User &user : userVec) {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;
            }

            std::vector<Group> groupVec = _groupModel.queryGroups(id);
            if(!groupVec.empty()) {
                std::vector<std::string> vec3;
                for(Group &group : groupVec) {
                    json groupjs;
                    groupjs["id"] = group.getId();
                    groupjs["groupname"] = group.getName();
                    groupjs["groupdesc"] = group.getDesc();

                    std::vector<std::string> groupUserV;
                    for(GroupUser &gUser : group.getUsers()) {
                        json gUserjs;
                        gUserjs["id"] = gUser.getId();
                        gUserjs["name"] = gUser.getName();
                        gUserjs["state"] = gUser.getState();
                        gUserjs["role"] = gUser.getRole();
                        groupUserV.push_back(gUserjs.dump());
                    }
                    groupjs["users"] = groupUserV;
                    vec3.push_back(groupjs.dump());
                }
                response["groups"] = vec3;
            }

            conn->send(response.dump());
        }
    } else {
        // 登陆失败
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "error account or password";
        conn->send(response.dump());
    }
}

// 处理注册业务 name password
void ChatService::reg(const TcpConnectionPtr& conn, json& js, Timestamp time) {
    std::string name = js["name"];
    std::string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    if(state) {
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
        LOG_INFO << "register success!";
    } else {
        // 注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
        LOG_INFO << "register failed!";
    }
}

// 处理注销业务
void ChatService::logout(const TcpConnectionPtr& conn, json& js, Timestamp time) {
    int userid = js["id"].get<int>();

    {
        std::lock_guard<std::mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if(it != _userConnMap.end()) {
            _userConnMap.erase(it);
        }
    }

    // 用户注销，在redis中取消订阅通道
    _redis.unsubscribe(userid);

    // 更新用户状态信息
    User user;
    user.setId(userid);
    user.setState("offline");
    _userModel.updateState(user);
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr& conn) {

    // 1. 通过该客户端id，从存储连接的map中把该连接删除
    // 2. 把该用户的状态改成offline
    User user;
    {
        std::lock_guard<std::mutex> lock(_connMutex);
        for(auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it) {
            if(it->second == conn) {
                // 从map表中删除用户的连接信息
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    // 用户注销，在redis中取消订阅通道
    _redis.unsubscribe(user.getId());
    
    if(user.getId() != -1) {
        // 更新用户的状态信息
        user.setState("offline");
        _userModel.updateState(user);
    }
}

// 一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr& conn, json& js, Timestamp time) {
    int toId = js["toid"].get<int>();

    {
        // 通过id寻找conn
        std::lock_guard<std::mutex> guard(_connMutex);
        auto it = _userConnMap.find(toId);
        if(it != _userConnMap.end()) {
            // toId 在线, 转发消息, 服务器主动推送消息给toId用户
            it->second->send(js.dump());
            return ;
        }
    }

    // 查询toid是否在线
    User user = _userModel.query(toId);
    if(user.getState() == "online") {
        _redis.publish(toId, js.dump());
        return ;
    }

    // 表示toId离线, 存储离线消息
    _offlineMsgModel.insert(toId, js.dump());
}

// 添加好友业务
void ChatService::addFriend(const TcpConnectionPtr& conn, json& js, Timestamp time) {
    int userid = js["id"].get<int>();
    int friendid = js["friendid"];

    // 存储好友信息
    _friendModel.insert(userid, friendid);
}

// 创建群组业务
void ChatService::createGroup(const TcpConnectionPtr& conn, json& js, Timestamp time) {
    int userid = js["id"].get<int>();
    std::string name = js["groupname"];
    std::string desc = js["groupdesc"];

    // 存储新创建的群组消息
    Group group(-1, name, desc);
    if(_groupModel.createGroup(group)) {
        // 存储群组创建人信息
        _groupModel.addGroup(userid, group.getId(), "creator");
    }
}

// 加入群组业务
void ChatService::addGroup(const TcpConnectionPtr& conn, json& js, Timestamp time) {
    int userid = js["id"].get<int>();
    int groupid = js["groupid"];
    _groupModel.addGroup(userid, groupid, "normal");
}

// 群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr& conn, json& js, Timestamp time) {
    int userid = js["userid"].get<int>();
    int groupid = js["groupid"];

    std::vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);

    std::lock_guard<std::mutex> lock(_connMutex);
    for(int id : useridVec) {
        auto it = _userConnMap.find(id);
        if(it != _userConnMap.end()) {
            // 转发群消息
            it->second->send(js.dump());
        } else {
            // 查询toid是否在线
            User user = _userModel.query(id);
            if(user.getState() == "online") {
                _redis.publish(id, js.dump());
            } else {
                // 存储离线消息
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }
}

// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, std::string msg) {
    std::lock_guard<std::mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if(it != _userConnMap.end()) {
        it->second->send(msg);
        return;
    }
    // 存储该用户的离线消息
    _offlineMsgModel.insert(userid, msg);
}