# Chat Server

This server is written in C and can hold up to 32 TCP clients, allocating a thread to deal with each. The message format is strict, but users can SEND a message to another user. Users can send a WHO request to see all users logged in.Users can LOGIN and LOGOUT and also issue a BROADCAST request in which they send a message to all other logged in clients.

## How To Run

* Run ```server.c``` with ```gcc -Wall -pthread -o a.out server.c``` then ```./a.out```
* Run ```client.c``` to test with ```gcc -Wall -pthread -o b.out client.c``` then ```./b.out```
