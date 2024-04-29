#include "json.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <atomic>

#include "group.hpp"
#include "user.hpp"
#include "public.hpp"

using json = nlohmann::json;
using std::cout;
using std::endl;
using std::cerr;
using std::cin;

// 记录当前系统登录的用户信息
User g_currentUser;
// 记录当前登录用户的好友列表信息
std::vector<User> g_currentUserFriendList;
// 记录当前登录用户的群组列表信息
std::vector<Group> g_currentUserGroupList;


// 控制聊天页面程序
bool isMainMenuRunning = false;

// 用于读写线程之间的通信
sem_t rwsem;
// 记录登陆状态
std::atomic_bool g_isLoginSuccess{false};

// 接收线程
void readTaskHandler(int clientfd);
// 获取系统时间 (聊天信息需要添加时间信息)
std::string getCurrentTime();
// 主页面聊天程序
void mainMenu(int clientfd);
// 显示当前登录成功用户的基本信息
void showCurrentUserData();

// 聊天客户端程序实现, main线程用作发送线程, 子线程用作接收线程
int main(int argc, char* *argv) {

    if (argc < 3) {
        cerr << "command invalid! example: ./ChatClient 127.0.0.1 6666" << endl;
        exit(-1);
    }

    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 创建client端的socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if(-1 == clientfd) {
        cerr << "socket create error" << endl;
        exit(-1);
    }

    // 填写client需要连接的server信息ip + port
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    // client和server进行连接
    if(-1 == connect(clientfd, (sockaddr*) &server, sizeof(sockaddr_in))) {
        cerr << "connect server error" << endl;
        close(clientfd);
        exit(-1);
    }

    // 初始化读写线程通信用的信号量
    sem_init(&rwsem, 0, 0);

    // 连接服务器成功，启动接受子线程
    std::thread readTask(readTaskHandler, clientfd);
    readTask.detach();

    // main线程用于接收用户输入，负责发送数据
    for(;;) {
        // 显示首页面菜单 登录、注册、退出
        cout << "=================================" << endl;
        cout << "1. login" << endl;
        cout << "2. register" << endl;
        cout << "3. quit" << endl;
        cout << "=================================" << endl;
        cout << "choice:";
        int choice = 0;
        cin >> choice;
        cin.get(); // 读掉缓冲区残留的回车

        switch(choice) {
            case 1 : {// 登陆业务
                int id = 0;
                char pwd[50] = {0};
                cout << "userid:" ;
                cin >> id;
                cin.get();
                cout << "user password:";
                cin.getline(pwd, 50);

                json js;
                js["msgid"] = LOGIN_MSG;
                js["id"] = id;
                js["password"] = pwd;
                std::string login_request = js.dump();

                g_isLoginSuccess = false;

                int len = send(clientfd, login_request.c_str(), strlen(login_request.c_str()), 0);
                if(-1 == len) {
                    cerr << "send login msg error:" << login_request << endl;
                }

                sem_wait(&rwsem); // 等待信号量, 由子线程处理完登录的响应消息后，通知这里

                if(g_isLoginSuccess) {
                    // 进入聊天主菜单页面
                    isMainMenuRunning = true;
                    mainMenu(clientfd);
                }  
            }
            break;
            case 2 : { // 注册业务
                char name[50] = {0};
                char pwd[50] = {0};
                cout << "username:";
                cin.getline(name, 50);
                cout << "userpassword:";
                cin.getline(pwd, 50);

                json js;
                js["msgid"] = REG_MSG;
                js["name"] = name;
                js["password"] = pwd;
                std::string reg_request = js.dump();

                int len = send(clientfd, reg_request.c_str(), strlen(reg_request.c_str())+1, 0);
                if(len == -1) {
                    cerr << "send reg msg error:" << reg_request << endl;
                }

                sem_wait(&rwsem); // 等待信号量, 子线程处理完注册消息会通知
            }
            break;
            case 3 : {
                close(clientfd);
                sem_destroy(&rwsem);
                exit(0);
            }
            break;
            default: {
                cerr << "invalid input" << endl;
                break;
            }
        }
    }

    return 0;
}

void doLoginResponse(json& login_response) {
    if(0 != login_response["errno"]) { // 登陆失败
        g_isLoginSuccess = false;
        cerr << login_response["errmsg"] << endl;
    } else { // 登陆成功
        // 记录当前用户的id和name
        g_currentUser.setId(login_response["id"].get<int>());
        g_currentUser.setName(login_response["name"]);
        // 记录当前用户的好友列表信息
        if (login_response.contains("friends")) {
            // 初始化
            g_currentUserFriendList.clear();
            std::vector<std::string> vec = login_response["friends"];
            for(std::string &str : vec) {
                json myFriends = json::parse(str);
                User user;
                user.setId(myFriends["id"].get<int>());
                user.setName(myFriends["name"]);
                user.setState(myFriends["state"]);
                g_currentUserFriendList.push_back(user);
            }
        }

        // 记录当前用户的群组列表信息
        if(login_response.contains("groups")) {
            // 初始化
            g_currentUserGroupList.clear();
            std::vector<std::string> vec1 = login_response["groups"];
            for(std::string &groupstr : vec1) {
                json groupjs = json::parse(groupstr);
                Group group;
                group.setId(groupjs["id"].get<int>());
                group.setName(groupjs["groupname"]);
                group.setDesc(groupjs["groupdesc"]);

                std::vector<std::string> vec2 = groupjs["users"];
                for(std::string &userstr : vec2) {
                    GroupUser user;
                    json userjs = json::parse(userstr);
                    user.setId(userjs["id"].get<int>());
                    user.setName(userjs["name"]);
                    user.setState(userjs["state"]);
                    user.setRole(userjs["role"]);
                    group.getUsers().push_back(user);
                }
                g_currentUserGroupList.push_back(group);
            }
        }

        // 显示登录用户的基本信息
        showCurrentUserData();

        // 显示当前用户的离线消息 个人聊天信息或者群组消息
        if(login_response.contains("offlinemsg")) {
            std::vector<std::string> vec = login_response["offlinemsg"];
            for(std::string &str : vec) {
                json js = json::parse(str);
                if(ONE_CHAT_MSG == js["msgid"].get<int>()) {
                    cout << js["time"].get<std::string>() << " [" << js["id"] << "]" << js["name"].get<std::string>()
                        << " said: " << js["msg"].get<std::string>() << endl;
                } else {
                    cout << "群消息[" << js["groupid"] << "]: " << js["time"].get<std::string>() << " [" << js["userid"] << "]" << js["name"].get<std::string>()
                        << " said: " << js["msg"].get<std::string>() << endl;
                }
            }
        }
        g_isLoginSuccess = true;
    }
}

void doRegResponse (json& reg_response) {
    if(0 != reg_response["errno"].get<int>()) {// 注册失败
        cerr << "name is already exist, register error" << endl;
    } else { // 注册成功
        cout << "name register success, userid is :" << reg_response["id"] << ", do not forget it!" << endl;
    }
}

// 接收线程
void readTaskHandler(int clientfd) {
    for (;;) {

        char buffer[1024] = {0};
        int len = recv(clientfd, buffer, 1024, 0);
        if(-1 == len || 0 == len) {
            close(clientfd);
            exit(-1);
        }

        // 接收ChatServer转发的数据，反序列化生成json数据对象
        json js = json::parse(buffer);
        int msgtype = js["msgid"].get<int>();
        if(ONE_CHAT_MSG == msgtype) {
            cout << js["time"].get<std::string>() << " [" << js["id"] << "]" << js["name"].get<std::string>()
                << " said: " << js["msg"].get<std::string>() << endl;
            continue;
        }

        if(GROUP_CHAT_MSG == msgtype) {
            cout << "群消息[" << js["groupid"] << "]: " << js["time"].get<std::string>() << " [" << js["userid"] << "]" << js["name"].get<std::string>()
                << " said: " << js["msg"].get<std::string>() << endl;
            continue;
        }

        if(LOGIN_MSG_ACK == msgtype) {
            doLoginResponse(js); // 处理登陆相应的业务逻辑
            sem_post(&rwsem);    // 通知主线程， 登陆结果处理完成
            continue;
        }

        if(REG_MSG_ACK == msgtype) {
            doRegResponse(js);
            sem_post(&rwsem);    // 通知主线程， 注册结果处理完成
            continue;
        }
    }
}

// "help" command handler
void help(int fd = 0, std::string str = "");

// "chat" command handler
void chat(int fd, std::string str);

// "addfriend" command handler
void addfriend(int fd, std::string str);

// "creategroup" command handler
void creategroup(int fd, std::string str);

// "addgroup" command handler
void addgroup(int fd, std::string str);

// "groupchat" command handler
void groupchat(int fd, std::string str);

// "logout" command handler
void logout(int fd, std::string str);

// 系统支持的客户端命令列表
std::unordered_map<std::string, std::string> commandMap = {
    {"help", "显示所有支持的命令, 格式help"},
    {"chat", "一对一聊天, 格式chat:friendid:message"},
    {"addfriend", "添加好友, 格式addfriend:friendid"},
    {"creategroup", "创建群组, 格式creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组, 格式addgroup:groupid"},
    {"groupchat", "群聊, 格式groupchat:groupid:message"},
    {"logout", "注销,logout"},
};

// 注册系统支持的客户端命令处理
std::unordered_map<std::string, std::function<void(int, std::string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"logout", logout}
};

// 主聊天页面程序
void mainMenu(int clientfd) {
    help();
    
    char buffer[1024] = {0};
    while(isMainMenuRunning) {
        cin.getline(buffer, 1024);
        std::string commandbuf(buffer);
        std::string command; // 存储命令
        int idx = commandbuf.find(":");
        if(-1 == idx) {
            command = commandbuf;
        } else {
            command = commandbuf.substr(0, idx);
        }
        auto it = commandHandlerMap.find(command);
        if(it == commandHandlerMap.end()) {
            cerr << "invalid input command!" << endl;
            continue;
        }

        // 调用相应命令的事件处理回调
        it->second(clientfd, commandbuf.substr(idx+1, commandbuf.size()-idx));
    }
}

// "help" command handler
void help(int fd, std::string str) {
    cout << "show command list >>>" << endl;
    for(auto &p : commandMap) {
        cout<< p.first << " : " << p.second << endl;
    }
    cout << endl;
}

// "chat" command handler
void chat(int fd, std::string str) {
    int idx = str.find(":"); // friendid:message
    if(-1 == idx) {
        cerr << "chat command invalid!" << endl;
        return;
    }

    int friendid = atoi(str.substr(0, idx).c_str());
    std::string message = str.substr(idx+1, str.size()-idx);
    
    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["toid"] = friendid;
    js["msg"] = message;
    js["time"] = getCurrentTime();

    std::string buffer = js.dump();

    int len = send(fd, buffer.c_str(), strlen(buffer.c_str())+1, 0);

    if(-1 == len) {
        cerr << "send chat msg error ->" << buffer << endl;
    }
}

// "addfriend" command handler 
void addfriend(int fd, std::string str) {
    int friendid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["friendid"] = friendid;
    std::string buffer = js.dump();

    int len = send(fd, buffer.c_str(), strlen(buffer.c_str())+1, 0);
    if(-1 == len) {
        cerr << "send addfriend msg error -> " << buffer << endl;
    }
}

// "creategroup" command handler  groupname:groupdesc
void creategroup(int fd, std::string str) {
    int idx = str.find(":");
    if(-1 == idx) {
        cerr << "chat command invalid!" << endl;
        return;
    }

    std::string groupname = str.substr(0, idx);
    std::string groupdesc = str.substr(idx+1, str.size()-idx);

    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;

    std::string buffer = js.dump();

    int len = send(fd, buffer.c_str(), buffer.size(), 0);
    if(-1 == len) {
        cerr << "send creategroup msg error -> " << buffer << endl;
    }
}

// "addgroup" command handler addgroup:groupid
void addgroup(int fd, std::string str) {
    int groupid = atoi(str.c_str());
    int id = g_currentUser.getId();

    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = id;
    js["groupid"] = groupid;

    std::string buffer = js.dump();

    int len = send(fd, buffer.c_str(), buffer.size(), 0);
    if(-1 == len) {
        cerr << "send addgroup msg error -> " << buffer << endl;
    }
}

// "groupchat" command handler  groupid:message
void groupchat(int fd, std::string str) {
    /*
    找到当前群组的所有人的id发送给他们消息
    */
    int idx = str.find(":");
    if(-1 == idx) {
        cerr << "groupchat command invalid!" << endl;
        return;
    }

    int groupid = atoi(str.substr(0, idx).c_str());
    std::string message = str.substr(idx+1, str.size()-idx);

    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["userid"] = g_currentUser.getId();
    js["groupid"] = groupid;
    js["msg"] = message;
    js["name"] = g_currentUser.getName();
    js["time"] = getCurrentTime();

    std::string buffer = js.dump();

    int len = send(fd, buffer.c_str(), buffer.size(), 0);
    if(-1 == len) {
        cerr << "send groupchat msg error -> " << buffer << endl;
    }

}

// "logout" command handler
void logout(int fd, std::string str) {
    json js;
    js["msgid"] = LOGOUT_MSG;
    js["id"] = g_currentUser.getId();

    std::string buffer = js.dump();

    int len = send(fd, buffer.c_str(), buffer.size(), 0);

    if(-1 == len) {
        cerr << "send logout msg error" << buffer << endl;
    } else {
        isMainMenuRunning = false;
    }
}


void showCurrentUserData() {
    cout << "==========================login user===============================" << endl;
    cout << "current login user => id: " << g_currentUser.getId() << " name:" << g_currentUser.getName() << endl;
    cout << "--------------------------friend list------------------------------" << endl;;
    if(!g_currentUserFriendList.empty()) {
        for(User &user : g_currentUserFriendList) {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }
    cout << "--------------------------group list-------------------------------" << endl;
    if(!g_currentUserGroupList.empty()) {
        for(Group &group : g_currentUserGroupList) {
            cout << group.getId() << " " << group.getName() << " " << group.getDesc() << endl;
            for(GroupUser &user : group.getUsers()) {
                cout << user.getId() << " " << user.getName() << " " << user.getState() << " " << user.getRole() << endl;
            }
        }
    }

    cout << "===================================================================" << endl;
}

std::string getCurrentTime() {
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return std::string(date);
}
