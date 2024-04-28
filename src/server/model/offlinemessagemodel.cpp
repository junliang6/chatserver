#include "offlinemessagemodel.hpp"
#include "db.h"

#include <string>
#include <vector>
#include <iostream>

// 存储用户的离线消息
void offlineMsgModel::insert(int userid, std::string msg){
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into offlinemessage values(%d, '%s')", userid, msg.c_str());

    std::cout<<sql<<std::endl;
    MySQL mysql;
    if(mysql.connect()) {
        mysql.update(sql);
    }
}

// 删除用户的离线消息
void offlineMsgModel::remove(int userid){
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "delete from offlinemessage where userid=%d", userid);

    std::cout<<sql<<std::endl;
    MySQL mysql;
    if(mysql.connect()) {
        mysql.update(sql);
    }
}

// 查询用户的离线消息
std::vector<std::string> offlineMsgModel::offlineMsgQuery(int userid){
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select message from offlinemessage where userid = %d", userid);

    std::vector<std::string> vec;
    MySQL mysql;
    if(mysql.connect()) {
        MYSQL_RES *res = mysql.query(sql);
        if(res) {
            // 把userid用户的所有离线消息放入vec中返回
            MYSQL_ROW row;
            while((row = mysql_fetch_row(res)) != nullptr) {
                vec.push_back(row[0]);
            }
            mysql_free_result(res);
            return vec;
        }
    }
    return vec;
}