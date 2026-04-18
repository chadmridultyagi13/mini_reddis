#pragma once
#include <vector>
#include <string>

using namespace std;

void handle_rpush(const vector<string>& cmd, int client_fd);  // part of step 8 
void handle_lrange(const vector<string>&cmd,int client_fd) ;  // part of step 10 and step 11 
void handle_lpush(const vector<string>&cmd,int client_fd) ; // this is the part of step 12 to implement the lpush command for the list data structure
void handle_llen(const vector<string>&cmd,int client_fd) ; // this is the part of step 13 to implement the llen command for the list data structure
void handle_lpop(const vector<string>&cmd,int client_fd) ; // this is the part of step 14 to implement the lpop command for the list data structure
void handle_blpop(const vector<string>&cmd,int client_fd) ; // this is the part of step 16 to implement the blpop command for the list data structure
void handle_timeouts() ; // this is the part of step 17 to implement the timeout handling for the blocking clients in blpop command