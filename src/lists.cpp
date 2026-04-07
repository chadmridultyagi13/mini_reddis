#include "lists.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <sys/socket.h>
using namespace std;
// shared from main.cpp
// part of step 8 
extern unordered_map<string, vector<string>> list_store;
extern mutex mtx;
void handle_rpush(const vector<string>& cmd, int client_fd) {
    // only single element case for this stage
    string key = cmd[1];
    // string value = cmd[2];   implemented this as the part of step 8 
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
    }
    // RESP integer
    string response = ":" + to_string(size) + "\r\n";
    send(client_fd, response.c_str(), response.size(), 0);
}

// part of step 10 
void handle_lrange(const vector<string>&cmd,int client_fd){
    string key = cmd[1] ; 
    {
        lock_guard<mutex>lock(mtx) ; 
        string response ; 
        if(list_store.find(key)==list_store.end()){
            response = "*0\r\n" ; 
        }
        else if(stoi(cmd[2])+1>=list_store[key].size()||stoi(cmd[2])>stoi(cmd[3])){
            response = "*0\r\n" ; 
        }
        else{
            vector<string>&lst = list_store[key] ; 
            int start = stoi(cmd[2]) ; 
            int end = stoi(cmd[3]) ; 
            int n = lst.size() ; 
            if(end>=n){
                end = n-1 ; 
            }
            response = "*" + to_string(end-start+1) + "\r\n" ;
            for(int i = start ; i <= end ; i++){
                response = response + "$" + to_string(lst[i].size())+ "\r\n" + lst[i] + "\r\n" ;
            }
        }
        send(client_fd,response.c_str(),response.size(),0) ;

    }
}