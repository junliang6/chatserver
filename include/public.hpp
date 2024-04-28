#ifndef PUBLIC_H
#define PUBLIC_H

/*
server 和 client 的公共文件
*/

enum EnMsgType {
    LOGIN_MSG = 1,      // 登陆消息 {"msgid":1,"id":24, "password":"lovecwj"}
    LOGIN_MSG_ACK,      // 登陆响应消息
    LOGOUT_MSG,         // 注销消息
    REG_MSG,            // 注册消息 
    REG_MSG_ACK,        // 注册响应消息
    ONE_CHAT_MSG,       // 聊天消息 
    ADD_FRIEND_MSG,     // 添加好友消息 {"msgid":6, "id":24, "friendid":25}

    CREATE_GROUP_MSG,   // 创建群组
    ADD_GROUP_MSG,      // 加入群组
    GROUP_CHAT_MSG,     // 群聊天

    
};

#endif