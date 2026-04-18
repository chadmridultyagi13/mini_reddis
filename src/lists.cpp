#include "lists.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <sys/socket.h>
#include <algorithm>
#include <deque>
#include<queue>
#include<chrono>
using namespace std;
// shared from main.cpp
// part of step 8 
extern unordered_map<string,deque<string>> list_store;
extern mutex mtx;

struct BlockedClient{
    int fd;
    chrono::steady_clock::time_point expiry;
};

extern unordered_map<string,queue<BlockedClient>> blocking_clients; //this is  queue as a part of step 16 to store the blocking clients for blpop command

void handle_rpush(const vector<string>& cmd, int client_fd) {
    // only single element case for this stage
    string key = cmd[1];
    // string value = cmd[2];   implemented this as the part of step 8 

    vector<pair<int,string>>to_notify ;
    int size;
    {
        lock_guard<mutex> lock(mtx);
        // auto-create list if not exists

        // implemented this as the part of step 8 
        // list_store[key].push_back(value);
        // size = list_store[key].size(); 

        // implemented this as the part of step 9 
        for(int i = 2 ; i < cmd.size() ; i++){
            list_store[key].push_back(cmd[i]) ; 
        }
        size = list_store[key].size() ;

        while(!blocking_clients[key].empty() && !list_store[key].empty()){
            auto bc = blocking_clients[key].front() ;
            blocking_clients[key].pop() ; 

            if(chrono::steady_clock::now() > bc.expiry){
                continue;
            }

            string value = list_store[key].front() ; 
            list_store[key].pop_front() ; 

            string response = "*2\r\n$" + to_string(key.size()) + "\r\n" + key + "\r\n$" + to_string(value.size()) + "\r\n" + value + "\r\n" ;
            to_notify.push_back({bc.fd,response}) ;
        }
    }

    for(auto &p : to_notify){
        send(p.first,p.second.c_str(),p.second.size(),0) ;
    }

    string response = ":" + to_string(size) + "\r\n";
    send(client_fd, response.c_str(), response.size(), 0);
}

// part of step 10 and step 11 to implement the lrange command for the list data structure
void handle_lrange(const vector<string>&cmd,int client_fd){
    string key = cmd[1] ; 
    {
        lock_guard<mutex>lock(mtx) ; 
        string response ; 
        auto it = list_store.find(key);
        if(it == list_store.end()){
            response = "*0\r\n" ; 
        }
        else{
            deque<string>&lst = it->second ; 
            int n = lst.size() ;

            int start = stoi(cmd[2]); 
            int end   = stoi(cmd[3]);  

            if(start < 0) start = n + start;
            if(end < 0)   end   = n + end;

            if(start < 0) start = 0;
            if(end < 0)   end   = 0;

            if(start >= n){
                response = "*0\r\n";
            }
            else{
                if(end >= n){
                    end = n - 1;
                }

                if(start > end){
                    response = "*0\r\n";
                }
                else{
                    response = "*" + to_string(end-start+1) + "\r\n" ;
                    for(int i = start ; i <= end ; i++){
                        response += "$" + to_string(lst[i].size()) + "\r\n" + lst[i] + "\r\n";
                    }
                }
            }
        }
        send(client_fd,response.c_str(),response.size(),0) ;
    }
}

// this is the part of step 12 to implement the lpush command for the list data structure
void handle_lpush(const vector<string>&cmd,int client_fd){ 
    string key = cmd[1] ;
    {
        lock_guard<mutex>lock(mtx) ; 
        auto &lst = list_store[key];
        for(int i = 2 ; i < cmd.size() ; i++){
            lst.push_front(cmd[i]) ; 
        }
        int size = lst.size() ;
        string response = ":" + to_string(size) + "\r\n";
        send(client_fd,response.c_str(),response.size(),0) ;
    }
}

// this is the part of step 13 to implement the llen command for the list data structure
void handle_llen(const vector<string>&cmd,int client_fd){
    string key = cmd[1] ; 
    {
        lock_guard<mutex>lock(mtx) ; 
        int size = 0 ;
        auto it = list_store.find(key);
        if(it != list_store.end()){
            size = it->second.size() ;
        }
        string response = ":" + to_string(size) + "\r\n";
        send(client_fd,response.c_str(),response.size(),0) ;
    }
}

// this was the part of step 14 and step 15 to implement the lpop command for the list data structure
void handle_lpop(const vector<string>&cmd,int client_fd){
    string key = cmd[1] ; 
    {
        lock_guard<mutex>lock(mtx) ; 
        string response ; 
        auto it = list_store.find(key);
        if(it==list_store.end()|| it->second.size()==0){
            response = "$-1\r\n" ;
        }
        else{
            int k = 0 ; 
            deque<string>&lst = it->second ;
            if(cmd.size()>=3){
                k = stoi(cmd[2]) ; 
                response  = "" ; 
                response += "*" + to_string(min(k,(int)lst.size())) + "\r\n" ;
                for(int i = 0 ; i < k ; i++){
                    response += "$" + to_string(lst.front().size()) + "\r\n" + lst.front() + "\r\n" ;
                    lst.pop_front() ; 
                }
            }
            else{
                k = 1 ;
                response = "$" + to_string(lst.front().size()) + "\r\n" + lst.front() + "\r\n" ;
                lst.pop_front() ;    
            }
        }
        send(client_fd,response.c_str(),response.size(),0) ;
    }
}

// this is the part of step 16 to implement the blpop command for the list data structure
void handle_blpop(const vector<string>&cmd,int client_fd){
    string key = cmd[1] ; 
    string response = ""; 
    bool should_send = false;

    double timeout = stod(cmd[2]);

    {
        lock_guard<mutex>lock(mtx) ;
        if(list_store.find(key)==list_store.end()||list_store[key].size()==0){

            BlockedClient bc;
            bc.fd = client_fd;

            if(timeout == 0){
                bc.expiry = chrono::steady_clock::time_point::max();
            }else{
                bc.expiry = chrono::steady_clock::now() + chrono::milliseconds((int)(timeout*1000));
            }

            blocking_clients[key].push(bc) ;
            return ;
        }
        else{
            string value = list_store[key].front() ; 
            list_store[key].pop_front() ; 
            response = "*2\r\n$" + to_string(key.size()) + "\r\n" + key + "\r\n$" + to_string(value.size()) + "\r\n" + value + "\r\n" ;
            should_send = true;
        }
    }
    if(should_send){
        send(client_fd,response.c_str(),response.size(),0) ;
    }
}

void handle_timeouts(){
    vector<int> expired_fds;

    {
        lock_guard<mutex> lock(mtx);

        for(auto &it : blocking_clients){
            auto &q = it.second;

            while(!q.empty()){
                auto bc = q.front();

                if(chrono::steady_clock::now() >= bc.expiry){
                    expired_fds.push_back(bc.fd);
                    q.pop();
                }else break;
            }
        }
    }

    for(int fd : expired_fds){
        string resp = "*-1\r\n";
        send(fd,resp.c_str(),resp.size(),0);
    }
}