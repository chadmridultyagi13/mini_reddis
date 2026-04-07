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
    if(cmd.size() != 3) {
        string response = "-ERR wrong number of arguments\r\n";
        send(client_fd, response.c_str(), response.size(), 0);
        return;
    }
    string key = cmd[1];
    string value = cmd[2];
    int size;
    {
        lock_guard<mutex> lock(mtx);
        // auto-create list if not exists
        list_store[key].push_back(value);
        size = list_store[key].size();
    }
    // RESP integer
    string response = ":" + to_string(size) + "\r\n";
    send(client_fd, response.c_str(), response.size(), 0);
}