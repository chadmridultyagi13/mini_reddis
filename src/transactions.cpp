#include "streams.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <sys/socket.h>
#include <chrono>
#include <atomic>
#include <deque>
#include <memory>
#include <thread>
#include <climits>
#include "lists.h"
#include "streams.h"
#include<chrono> // for getting the current time in milliseconds
using namespace std;

struct ValueEntry{
  string value ; 
  time_t expiry ; 
};

extern mutex mtx;
extern unordered_map<string,ValueEntry> store;
extern unordered_map<int, vector<vector<string>>> queued_commands; 
extern unordered_map<int,bool>in_multi ;
extern long long current_time_ms() ; 
bool check(string &str){
    if(str.size() == 0) return false;
    int i = 0;
    if(str[0] == '-') {
        if(str.size() == 1) return false;
        i = 1;
    }
    for(; i < str.size(); i++){
        int ch = (int)str[i];
        if(ch >= 48 && ch <= 57){
            continue;
        }
        else return false;
    }
    return true;
}
void handle_incr(const vector<string>& cmd, int client_fd){
    string key = cmd[1];
    string response;
    long long new_value = 1;
    {
        lock_guard<mutex> lock(mtx);
        if(store.find(key) == store.end()){
            store[key] = {"1", -1};
            new_value = 1;
            response = ":1\r\n";
        }
        else if(check(store[key].value)){
            string current = store[key].value;
            long long num = stoll(current);

            if(num == LLONG_MAX){
                response = "-ERR value is not an integer or out of range\r\n";
            }
            else{
                num++;
                new_value = num;
                store[key].value = to_string(num);
                response = ":" + to_string(new_value) + "\r\n";
            }
        }
        else{
            response = "-ERR value is not an integer or out of range\r\n";
        }
    }
    send(client_fd, response.c_str(), response.size(), 0);
}

void handle_multi(const vector<string>& cmd, int client_fd){
    string response = "+OK\r\n" ; 
    in_multi[client_fd] = true ; 
    send(client_fd,response.c_str(),response.size(),0) ;
    
}
string execute_simple_command(const vector<string>& cmd) {
    // ----- SET -----
    if(cmd[0] == "SET"){
        string key = cmd[1];
        string value = cmd[2];
        long long expiry = -1;
        if(cmd.size() == 5 && cmd[3] == "PX"){
            long long duration = stoll(cmd[4]);
            expiry = current_time_ms() + duration;
        }
        {
            lock_guard<mutex> lock(mtx);
            store[key] = {value, expiry};
        }
        return "+OK\r\n";
    }
    // ----- GET -----
    if(cmd[0] == "GET"){
        string key = cmd[1];
        string value;
        bool found = false;
        {
            lock_guard<mutex> lock(mtx);
            auto it = store.find(key);
            if(it != store.end()){
                ValueEntry &entry = it->second;
                if(entry.expiry != -1 && current_time_ms() > entry.expiry){
                    store.erase(it);
                } else {
                    value = entry.value;
                    found = true;
                }
            }
        }
        if(!found) return "$-1\r\n";
        return "$" + to_string(value.size()) + "\r\n" + value + "\r\n";
    }
    // ----- INCR -----
    if(cmd[0] == "INCR"){
        string key = cmd[1];
        lock_guard<mutex> lock(mtx);
        if(store.find(key) == store.end()){
            store[key] = {"1", -1};
            return ":1\r\n";
        }
        if(!check(store[key].value)){
            return "-ERR value is not an integer or out of range\r\n";
        }
        long long num = stoll(store[key].value);
        if(num == LLONG_MAX){
            return "-ERR value is not an integer or out of range\r\n";
        }
        num++;
        store[key].value = to_string(num);
        return ":" + to_string(num) + "\r\n";
    }
    return "-ERR unknown command\r\n";
}
void handle_exec(const vector<string>&cmd,int client_fd){
    if(in_multi.find(client_fd)==in_multi.end() || !in_multi[client_fd]){
        string response = "-ERR EXEC without MULTI\r\n";
        send(client_fd,response.c_str(),response.size(),0);
        return;
    }
    vector<vector<string>> cmds = queued_commands[client_fd];
    in_multi[client_fd] = false;
    queued_commands[client_fd].clear();
    string resp = "*" + to_string(cmds.size()) + "\r\n";
    for(auto &c : cmds){
        resp += execute_simple_command(c);
    }
    send(client_fd, resp.c_str(), resp.size(), 0);
}
void handle_discard(const vector<string>& cmd, int client_fd){
    if(in_multi.find(client_fd)==in_multi.end() || !in_multi[client_fd]){
        string response = "-ERR DISCARD without MULTI\r\n";
        send(client_fd, response.c_str(), response.size(), 0);
        return;
    }
    in_multi[client_fd] = false;
    queued_commands[client_fd].clear();
    string response = "+OK\r\n";
    send(client_fd, response.c_str(), response.size(), 0);
}