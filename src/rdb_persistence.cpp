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
#include <fstream>
#include <iterator>
vector<string> rdb_keys;
using namespace std;

string dir = "" ; 
string dbfilename = "" ;

void save_to_rdb(int client_fd,vector<string>cmd){
    if(cmd.size()<3)return ;
    string response ="" ; 
    if(cmd[0]=="CONFIG"&&cmd[1]=="GET"){
        if(cmd[2]=="dir"){
            response = "*2\r\n$3\r\ndir\r\n$"+to_string(dir.size())+"\r\n"+dir+"\r\n" ;
        }
        else if(cmd[2]=="dbfilename"){
            response = "*2\r\n$10\r\ndbfilename\r\n$"+to_string(dbfilename.size())+"\r\n"+dbfilename+"\r\n" ;
        }
        send(client_fd,response.c_str(),response.size(),0) ;
    }

}
void load_rdb(){

    string path = dir + "/" + dbfilename;

    ifstream file(path, ios::binary);

    if(!file.is_open()){
        return;
    }

    vector<unsigned char> bytes(
        (istreambuf_iterator<char>(file)),
        istreambuf_iterator<char>()
    );

    for(int i = 0 ; i + 2 < (int)bytes.size() ; i++){

        if(bytes[i] == 0x00){

            int key_len = bytes[i + 1];

            if(key_len <= 0 || key_len > 100){
                continue;
            }

            if(i + 2 + key_len >= (int)bytes.size()){
                continue;
            }

            string key;

            for(int j = 0 ; j < key_len ; j++){
                key += (char)bytes[i + 2 + j];
            }

            bool printable = true;

            for(char c : key){
                if(c < 32 || c > 126){
                    printable = false;
                }
            }

            if(printable){
                rdb_keys.push_back(key);
                break;
            }
        }
    }
}

void handle_keys(int client_fd){

    string response =
        "*" + to_string(rdb_keys.size()) + "\r\n";

    for(auto &key : rdb_keys){

        response += "$";
        response += to_string(key.size());
        response += "\r\n";

        response += key;
        response += "\r\n";
    }

    send(client_fd,response.c_str(),response.size(),0);
}   
