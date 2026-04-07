#pragma once
#include <vector>
#include <string>

using namespace std;

void handle_rpush(const vector<string>& cmd, int client_fd);  // part of step 8 
void handle_lrange(const vector<string>&cmd,int client_fd) ;  // part of step 10
