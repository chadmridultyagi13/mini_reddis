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

// part of step 10 and step 11 to implement the lrange command for the list data structure
void handle_lrange(const vector<string>&cmd,int client_fd){
    string key = cmd[1] ; 
    {
        lock_guard<mutex>lock(mtx) ; 
        string response ; 
        if(list_store.find(key)==list_store.end()){
            response = "*0\r\n" ; 
        }
        else{
            vector<string>&lst = list_store[key] ; 
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