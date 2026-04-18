#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include<thread> 
#include<queue>
#include <vector>
#include<chrono> // for getting the current time in milliseconds
#include<unordered_map>
#include<mutex> // to prevent the race condition 
#include "lists.h" // part of steep 8 to implement the list data structure
#include <deque>
#include "streams.h"
using namespace std ;


struct BlockedClient_Streams{
    int client_fd;
    pair<long long,long long> last_id;
    chrono::steady_clock::time_point expire_time;
    atomic<bool> done{false};
    bool infinite; 
    string response;
};



struct StreamEntry{
    string id ; 
    vector<pair<string,string>>fields ; 
};  // this is the part of step 19 to implement the stream data structure and handle the xadd command, here we have defined a structure for each entry in the stream which contains the id and the fields of the entry, and we will store the stream entries in an unordered map where the key is the stream name and the value is a vector of stream entries.

// part of step  7 
struct ValueEntry{
  string value ; 
  time_t expiry ; 
};
struct BlockedClient{
    int fd;
    chrono::steady_clock::time_point expiry;
};
unordered_map<string, deque<shared_ptr<BlockedClient_Streams>>> blocked_xread;

unordered_map<string,queue<BlockedClient>> blocking_clients;//this is queue as a part of step 16 to store the blocking clients for blpop command  

unordered_map<string,deque<string>>list_store ; // part of step 8 to implement the list data structure 

// unordered_map<string,>store ;  modified this below for the step 7 , actually this was the part of step 6 
unordered_map<string,ValueEntry>store ; // part of step 7 

mutex mtx ; // mutex to protect the shared resource (the store) from concurrent access by multiple threads

unordered_map<string,vector<StreamEntry>> stream_store ; // this is the part of step 19 to implement the stream data structure and handle the xadd command, here we have defined a structure for each entry in the stream which contains the id and the fields of the entry, and we will store the stream entries in an unordered map where the key is the stream name and the value is a vector of stream entries.


// used this as the part of step 7 to get the current time in milliseconds
long long current_time_ms() {
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now().time_since_epoch()
    ).count();
}


// this is the part of the step 5 where i actually parsed the resp bulk string so that i can implement various commands , as all are encoded in resp format as a bulk string 
vector<string> parse_resp(const string &buffer) {  
    vector<string> result;
    int i = 0;
    if (buffer[i] != '*') return result;
    i++;
    int num = 0;
    while (buffer[i] != '\r') {
        num = num * 10 + (buffer[i] - '0');
        i++;
    }
    i += 2; // skip \r\n
    for (int j = 0; j < num; j++) {
        i++; // skip '$'
        int len = 0;
        while (buffer[i] != '\r') {
            len = len * 10 + (buffer[i] - '0');
            i++;
        }
        i += 2;
        string word = buffer.substr(i, len);
        result.push_back(word);
        i += len + 2;
    }
    return result;
}




// this is the part of step 4 where we will handle multiple clients using multi-threading, we will create a thread for each client connection and in that thread we will handle the client connection, this way we can handle multiple clients simultaneously.
void handle_client(int client_fd){
  while(true){
    char buffer[1024] ;
    int read_bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if(read_bytes <=0) {
      std::cerr << "Failed to read from client\n";
      close(client_fd);
      break ;  
    }
    std::string input(buffer, read_bytes); // Doing this as the step 5 as Converting the buffer to a string for easier handling


    //  const char *response = "+PONG\r\n" ; this was implemented as the part of step 4 
    //   send(client_fd,response,strlen(response),0);

    // implemented this as part of step 5 
    vector<string>cmd = parse_resp(input);
    if(cmd.size()==0){
      continue ;
    }
    
    if (cmd[0] == "PING"){
    string response = "+PONG\r\n";
    send(client_fd, response.c_str(), response.size(), 0);
    }       
    // added as the part of step 5  
    if(cmd[0]=="ECHO"){
      string response = "$" + to_string(cmd[1].size()) + "\r\n" + cmd[1] + "\r\n";
      send(client_fd,response.c_str(),response.size(),0);
    }
    // added GET AND SET AS THE PART OF STEP 6 
if(cmd[0]=="SET"){
    if(cmd.size() < 3){
        continue;
    }

    string key = cmd[1];
    string value = cmd[2];

    long long expiry = -1;

    // Handle PX option
    // added as a part of step 7 to handle the expiry of the keys
    if(cmd.size() == 5 && cmd[3] == "PX"){
        long long duration = stoll(cmd[4]);
        expiry = current_time_ms() + duration;
    }

    {
        lock_guard<mutex> lock(mtx);
        store[key] = {value, expiry};
    }

    string response = "+OK\r\n";
    send(client_fd, response.c_str(), response.size(), 0);
}

if(cmd[0]=="GET"){
    if(cmd.size()!=2){
        continue;
    }
    string key = cmd[1];
    string value;
    bool found = false;
    {
        lock_guard<mutex> lock(mtx);
        auto it = store.find(key);
        if(it != store.end()){
            ValueEntry &entry = it->second;
            // Check expiry
            if(entry.expiry != -1 && current_time_ms() > entry.expiry){
                // expired → delete
                store.erase(it);
            } else {
                value = entry.value;
                found = true;
            }
        }
    }
    if(!found){
        string response = "$-1\r\n";
        send(client_fd, response.c_str(), response.size(), 0);
    } else {
        string response = "$" + to_string(value.size()) + "\r\n" + value + "\r\n";
        send(client_fd, response.c_str(), response.size(), 0);
    }
}
if(cmd[0]=="RPUSH"){
    handle_rpush(cmd,client_fd); // this is the part of step 8 to implement the list data structure and handle the rpush command
}
if(cmd[0]=="LRANGE"){
    handle_lrange(cmd,client_fd) ; // this is the part of step 10 to implement the lrange command for the list data structure
}
if(cmd[0]=="LPUSH"){
    handle_lpush(cmd,client_fd) ; // this is the part of step 12 to implement the lpush command for the list data structure
}
if(cmd[0]=="LLEN"){
    handle_llen(cmd,client_fd) ; // this is the part of step 13 to implement the llen command for the list data structure
}
if(cmd[0]=="LPOP"){
    handle_lpop(cmd,client_fd) ; // this is the part of step 14 to implement the lpop command for the list data structure
}
if(cmd[0]=="BLPOP"){
    handle_blpop(cmd,client_fd) ; // this is the part of step 16 to implement the blpop command for the list data structure
}
if(cmd[0]=="TYPE"){ // build this part of step 18 , here i have to tell the type of value stored at the key 
    string key = cmd[1];
    string result;

{
    lock_guard<mutex> lock(mtx);

    auto it = store.find(key);

    if(it != store.end()){
        // check expiry
        if(it->second.expiry != -1 && current_time_ms() > it->second.expiry){
            store.erase(it);
            result = "none";
        } else {
            result = "string";
        }
    }
    // this part has been added as the part of step 19 
    else if(stream_store.find(key) != stream_store.end()){
        result = "stream";
    }
    else{
        result = "none";
    }
}

string resp = "+" + result + "\r\n";
send(client_fd, resp.c_str(), resp.size(), 0);

    

}
if(cmd[0]=="XADD"){
    handle_xadd(cmd,client_fd) ; // this is the part of step 19 to implement the stream data structure and handle the xadd commandx
}
if(cmd[0]=="XRANGE"){
    handle_xrange(cmd,client_fd) ; 
}
if(cmd[0]=="XREAD"){
    handle_xread(cmd,client_fd) ; // this is the part of step 26 to implement the xread command for the stream data structure
}
}
}
int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }
  
  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }
  
  struct sockaddr_in server_addr;  // it tells me the port and ip address 
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY; // this mean it listens on all network interfaces, including localhost and any public IPs assigned to the machine
  server_addr.sin_port = htons(6379); // htons converts the port number from host byte order to network byte order, which is necessary for correct communication over the network. Port 6379 is the default port for Redis, so this server will listen for incoming connections on that port.
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 6379\n";
    return 1;
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }
  
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);
  std::cout << "Waiting for a client to connect...\n";

  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cout << "Logs from your program will appear here!\n";


  thread([](){
      while(true){
          handle_timeouts();
          this_thread::sleep_for(chrono::milliseconds(10));
      }
  }).detach();



  //added as the part of step 2 in this we just had to send the response to the client without reading any data from the client, we just accept the client connection and send the response and close the connection, this is a very basic implementation and it can handle only one client at a time, if you want to handle multiple clients then you need to call accept in a loop and maybe use multi-threading or asynchronous I/O to handle multiple clients simultaneously.
  // int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len); 
  // std::cout << "Client connected\n";
  // const char *response = "+PONG\r\n" ;
  // send(client_fd,response,strlen(response),0); 
  // close(client_fd);
  // close(server_fd);




  
  // this is the step 3  , this was all for the single client handling, now we need to handle multiple clients, so we will use multi-threading for that, we will create a thread for each client connection and in that thread we will handle the client connection, this way we can handle multiple clients simultaneously.
  // In this step, we enter a loop to continuously read data from the client and respond with "+PONG\r\n". If the client disconnects or if there's an error while reading, we close the client socket and break out of the loop. Finally, we close the server socket before exiting the program.`
  // CALLING ACCEPT WILL ONLY ACCEPT ONE CLIENT CONNECTION, IF YOU WANT TO ACCEPT MULTIPLE CLIENTS THEN YOU NEED TO CALL ACCEPT IN A LOOP AND MAYBE USE MULTI-THREADING OR ASYNCHRONOUS I/O TO HANDLE MULTIPLE CLIENTS SIMULTANEOUSLY.

  //this was all for the single client handling, now we need to handle multiple clients, so we will use multi-threading for that, we will create a thread for each client connection and in that thread we will handle the client connection, this way we can handle multiple clients simultaneously.
  // int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
  // while(true){
  //   char buffer[1024];
  //   int read_bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
  //   if(read_bytes <=0) {
  //     std::cerr << "Failed to read from client\n";
  //     close(client_fd);
  //     break ;  
  //   }
  //   const char *response = "+PONG\r\n" ;  
  //   send(client_fd,response,strlen(response),0);
  // }
  // close(server_fd); 




  // this is step 4 
  // now the multiple client handling is done using multi-threading, we will create a thread for each client connection and in that thread we will handle the client connection, this way we can handle multiple clients simultaneously.
  while(true){
    int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
    if(client_fd<0){
      std ::cerr << "Failed to accept client connection\n";
      continue;
    }
    std :: thread client_thread(handle_client,client_fd);
    client_thread.detach(); // Detach the thread to allow it to run independently
  }

}