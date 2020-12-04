#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>

#define MAX_CLIENTS 32
#define BUFFER_SIZE 1024

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
fd_set readfds;

char** users;         // Global array of userids
int* global_sockfds;  // Global array of socket fds
int client_count = 0; // Number of connected TCP clients


// =============================================================================
// ============================= HELPER METHODS ================================
// =============================================================================


// Create a buffer of size BUFFER_SIZE
char* make_buffer() {
  char* buffer = calloc(BUFFER_SIZE, sizeof(char));
  return buffer;
}

// Return 0 if the client with username 'userid' exists, -1 otherwise
int find_client(const char* userid) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (global_sockfds[i] == -1)
      continue;
    if (strcmp(users[i], userid) == 0)
      return 0;
  }
  return -1;
}

// Get the userid associated with the socket fd
char* get_userid(int sockfd) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (global_sockfds[i] == sockfd) {
      return users[i];
    }
  }
  return NULL;
}

// Returns 0 if the string cannot be converted to a number, 1 otherwise
int is_number(char* num) {
  for (int i = 0; i < strlen(num); i++) {
    if (!isdigit(num[i])) {
      return 0;
    }
  }
  return 1;
}

/*
// Return 1 if total ascii value of a is less than b, 0 otherwise
int asciicompare(const char* a, const char* b) {
  int value_a = 0, value_b = 0;
  for (int i = 0; i < strlen(a); i++) {
    value_a += a[i];
  }
  for (int i = 0; i < strlen(b); i++) {
    value_b += b[i];
  }
  return value_a <= value_b ? 1 : 0;
}
*/



// Erase the clients connection information from the global arrays
void clear_client_from_global(int sockfd) {
  for (int i = 0; i < client_count; i++) {
    if (global_sockfds[i] == sockfd) {
      global_sockfds[i] = -1;
      client_count--;
      free(users[i]);
      return;
    }
  }
}

// Returns 1 if userid is all alnum, 0 otherwise
int alnum(const char* userid) {
  for (int i = 0; i < strlen(userid); i++) {
    if (!isalnum(userid[i]))
      return 0;
  }
  return 1;
}

// Returns 1 if the size is between 1 and 990, inclusive; 0 otherwise
int valid_size(const char* msg) {
  int len = strlen(msg);
  if (len < 1 || len > 990) {
    return 0;
  }
  return 1;
}

// SEND: Checks to see if another recv call needs to be used
int send_recv_more(const char* msg) {
  int count = 0;
  for (int i = 0; i < strlen(msg); i++) {
    if (msg[i] == '\n')
      count++;
  }
  if (count == 1)
    return 1;
  return 0;
}

// BROADCAST: Checks to see if another recv call needs to be used
int broadcast_recv_more(const char* msg) {
  int count = 0;
  for (int i = 0; i < strlen(msg); i++) {
    if (msg[i] == '\n')
      count++;
  }
  if (count == 1)
    return 1;
  return 0;
}


// =============================================================================
// =============================================================================
// =============================================================================


int compare(const void* a, const void* b) {
  return strcmp (*(const char **) a, *(const char **) b);
}

// Use insertion sort to sort all of the users currently in the chat server
char** sort_users(int* k) {
  int i, j;
  char** sorted_users = calloc(MAX_CLIENTS, sizeof(char *));

  j = 0;
  for (i = 0; i < MAX_CLIENTS; i++) { // Copy 2D array
    if (global_sockfds[i] != -1) {
      sorted_users[j] = calloc(64, sizeof(char));
      strcpy(sorted_users[j++], users[i]);
    }
  }
  *k = j;
  char** arr = realloc(sorted_users, sizeof(char *) * j);
  size_t size = sizeof(arr) / sizeof(char *);
  qsort(arr, size, sizeof(char *), compare);
  return arr;
}


// =============================================================================
// =============================================================================


// Returns the key word of the message
int get_keyword(const char* message, int len) {

  // Compare the message to various keywords to look for a match
  if (strncmp(message, "LOGIN", 5) == 0) {
    return 0;
  } if (strncmp(message, "WHO", 3) == 0) {
    return 1;
  } if (strncmp(message, "LOGOUT", 6) == 0) {
    return 2;
  } if (strncmp(message, "SEND", 4) == 0) {
    return 3;
  } if (strncmp(message, "BROADCAST", 9) == 0) {
    return 4;
  } else {
    return -1;
  }
}


// =============================================================================
// =============================================================================


// Broadcast a messgae to all users
void parse_broadcast_and_send(const char* msg, int len, char* userid, char* conn_type,
                const char* handler, int sockfd, struct sockaddr_in client) {

  char* msg_copy = make_buffer();
  char* out_msg = make_buffer();
  char* broadcast_msg = make_buffer();
  char* msg_len = calloc(64, sizeof(char));
  strcpy(msg_copy, msg);

  // Read in more of the broadcast message if needed
  if (broadcast_recv_more(msg)) {
    char* buffer = make_buffer();
    if (strcmp(conn_type, "TCP")) {
      recv(sockfd, buffer, BUFFER_SIZE, 0);
    } else {
      int fromlen = sizeof(client);
      recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&client, (socklen_t *)&fromlen);
    }
    strcat(msg_copy, buffer);
    free(buffer);
  }

  strcpy(msg_len, strtok(msg_copy, "\n") + 10);
  strcpy(broadcast_msg, strtok(NULL, "\n"));
  int rc = sprintf(out_msg, "FROM %s %s %s\n", userid, msg_len, broadcast_msg);

  // Error check the message
  int error = 0;
  if (rc < 0 || !valid_size(broadcast_msg)) {
    strcpy(out_msg, "ERROR Invalid BROADCAST format\n");
    printf("%s: Sent ERROR (Invalid BROADCAST format)\n", handler);
    error = 1;
  } else if (!is_number(msg_len)) {
    strcpy(out_msg, "ERROR Invalid msglen\n");
    printf("%s: Sent ERROR (Invalid msglen)\n", handler);
    error = 1;
  }

  // Broadcast the appropriate error message and free memory
  int n;
  int out_len = strlen(out_msg);
  int client_len = sizeof(client);
  if (strcmp(conn_type, "UDP") == 0) {
    if (error)
      n = sendto(sockfd, out_msg, out_len, 0, (struct sockaddr *)&client, client_len);
    else
      n = sendto(sockfd, "OK!\n", 4, 0, (struct sockaddr *)&client, client_len);
    if (n <= 0)
      fprintf(stderr, "%s: ERROR <couldn't send back to client>\n", handler);
  }
  else if (strcmp(conn_type, "TCP") == 0) {
    if (error)
      n = send(sockfd, out_msg, out_len, 0);
    else
      n = send(sockfd, "OK!\n", 4, 0);
    if (n <= 0)
      fprintf(stderr, "%s: ERROR <couldn't send back to client>\n", handler);
  }
  if (error) {
    free(msg_copy);
    free(out_msg);
    free(broadcast_msg);
    free(msg_len);
    return;
  }

  // Broadcast the message to all users on the chat server
  pthread_mutex_lock(&lock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (global_sockfds[i] != -1) {
      if (strcmp(conn_type, "UDP") == 0)
        n = sendto(global_sockfds[i], out_msg, out_len, 0, (struct sockaddr *)&client, client_len);
      else
        n = send(global_sockfds[i], out_msg, out_len, 0);
      if (n <= 0)
        fprintf(stderr, "%s: ERROR <couldn't BROADCAST to %d>\n", handler, global_sockfds[i]);
    }
  }
  pthread_mutex_unlock(&lock);
  free(msg_copy);
  free(out_msg);
  free(broadcast_msg);
  free(msg_len);
}


// =============================================================================
// =============================================================================


// Send a message to a specific user on the server
void parse_message_and_send(const char* msg, int msg_len, const char* conn_type,
                            const char* sender, const char* handler, int sockfd) {

  // Assumptions made for buffer size
  char* userid = calloc(64, sizeof(char));
  char* parsed_msg_len_buf = calloc(16, sizeof(char));
  char* parsed_msg = make_buffer();
  char* msg_copy = make_buffer();
  char* left = make_buffer();
  strcpy(msg_copy, msg);

  // Another recv call to read in the rest of the message
  if (send_recv_more(msg)) {
    char* temp = make_buffer();
    recv(sockfd, temp, BUFFER_SIZE, 0);
    printf("[%s]\n", temp);
    strcat(msg_copy, temp);
    free(temp);
  }

  // Split up the message for easier parsing
  strcpy(left, strtok(msg_copy, "\n"));
  strcpy(parsed_msg, strtok(NULL, "\n"));

  // Extract recipient and message length
  int i, idx = 0;
  for (i = 5; isalnum(left[i]); i++) {
    userid[idx++] = left[i];
  }
  userid[idx] = '\0';
  for (++i, idx = 0; isalnum(left[i]); i++) {
    parsed_msg_len_buf[idx++] = left[i];
  }
  parsed_msg_len_buf[idx] = '\0';
  free(msg_copy);
  free(left);

  char* out_msg = make_buffer();
  int rc = sprintf(out_msg, "FROM %s %s %s\n", sender, parsed_msg_len_buf, parsed_msg);
  printf("%s: Rcvd SEND request to userid %s\n", handler, userid);

  // See if the receiving fd is in the chat server
  int recvfd = -1;
  pthread_mutex_lock(&lock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (global_sockfds[i] == -1)
      continue;
    if (strcmp(users[i], userid) == 0) {
      recvfd = global_sockfds[i];
      break;
    }
  }
  pthread_mutex_unlock(&lock);

  // Error checking for SEND
  int error = 0;
  if (rc < 0) {
    strcpy(out_msg, "ERROR Invalid SEND format\n");
    printf("%s: Sent ERROR (Invalid SEND format)\n", handler);
    error = 1;
  } else if (recvfd == -1) {
    strcpy(out_msg, "ERROR Unknown userid\n");
    printf("%s: Sent ERROR (Unknown userid)\n", handler);
    error = 1;
  } else if (!is_number(parsed_msg_len_buf) || !valid_size(parsed_msg)) {
    strcpy(out_msg, "ERROR Invalid msglen\n");
    printf("%s: Sent ERROR (Invalid msglen)\n", handler);
    error = 1;
  }

  // See if the sending fd is in the chat server
  int sendfd = -1;
  pthread_mutex_lock(&lock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (global_sockfds[i] == -1)
      continue;
    if (strcmp(users[i], sender) == 0) {
      sendfd = global_sockfds[i];
      if (error) { // Send the error msg
        send(sendfd, out_msg, strlen(out_msg), 0);
      } else { // Reply OK! and relay message
        send(sendfd, "OK!\n", 4, 0);
        int out_len = strlen(out_msg);
        if (send(recvfd, out_msg, out_len, 0) != out_len) {
          fprintf(stderr, "ERROR <couldn't complete SEND request>\n");
        }
      }
      break;
    }
  }
  pthread_mutex_unlock(&lock);
  free(userid);
  free(parsed_msg_len_buf);
  free(parsed_msg);
}


// =============================================================================
// =============================================================================


// Take in the message, its length, and the printed handler (i.e. CHILD or MAIN)
// and find the keyword and take appropriate action
void handle_message(char* message, int len, const char* handler,
                    int* connected, int sockfd, const char* conn_type, struct sockaddr_in client) {

  int rc = get_keyword(message, len);
  if (rc == 0) { // handle LOGIN

    // Keep reading in until there is a newline
    while (message[strlen(message)-1] != '\n') {
      char* temp_buffer = make_buffer();
      recv(sockfd, temp_buffer, BUFFER_SIZE, 0);
      strcat(message, temp_buffer);
      len = strlen(message);
      free(temp_buffer);
    }

    int userid_len = len - 6;
    char* userid = calloc(userid_len + 1, sizeof(char));

    // See if the user is already connected or if userid is invalid
    int error = 0;
    char* out_msg = make_buffer();
    if (*connected == 1) {
      printf("%s: Sent ERROR (Already connected)\n", handler);
      strcpy(out_msg, "Already connected\n");
      error = 1;
    }

    // Process successful login request
    for (int i = 6; i < len-1; i++) {
      userid[i-6] = message[i];
    }
    userid[len] = '\0';
    strcpy(out_msg, "OK!\n");
    printf("%s: Rcvd LOGIN request for userid %s\n", handler, userid);
    if (userid_len < 4 || userid_len > 16 || !alnum(userid)) {
      printf("%s: Sent ERROR (Invalid userid)\n", handler);
      strcpy(out_msg, "ERROR Invalid userid\n");
      error = 1;
    }

    // Send message back and free memory
    int n = -1;
    pthread_mutex_lock(&lock);
    int client_status = find_client(userid);
    pthread_mutex_unlock(&lock);
    if (strcmp(conn_type, "UDP") == 0) { // If UDP, send message
      int client_len = sizeof(client);
      n = sendto(sockfd, out_msg, strlen(out_msg), 0, (struct sockaddr *)&client, client_len);
    }
    else if (client_status == 0 && *connected == 0) { // User tries to login with someone elses ID
      printf("%s: Sent ERROR (Already connected)\n", handler);
      strcpy(out_msg, "ERROR Already connected\n");
      n = send(sockfd, out_msg, strlen(out_msg), 0);
      error = 1;
    }
    else if (strcmp(conn_type, "TCP") == 0) { // If TCP, send the message
      n = send(sockfd, out_msg, strlen(out_msg), 0);
    }

    // Check return code of send and return if UDP
    if (n != strlen(out_msg)) {
      fprintf(stderr, "%s: ERROR <couldn't send message: [%s]>", handler, out_msg);
    }
    if (strcmp(conn_type, "UDP") == 0) {
      return;
    }

    // Free memory and return if an error occurred
    if (error) {
      free(userid);
      return;
    }
    *connected = 1;

    // Check to see if client is logging back in
    int logged_out = 0;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if ((global_sockfds[i] == sockfd) && (strcmp("", users[i]) == 0)) {
        strcpy(users[i], userid);
        free(userid);
        logged_out = 1;
        break;
      }
    }
    pthread_mutex_unlock(&lock);
    if (logged_out) return;

    // Client fd is not present in the server, so add it
    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (global_sockfds[i] == -1) {
        global_sockfds[i] = sockfd;
        users[i] = calloc(userid_len + 1, sizeof(char));
        strcpy(users[i], userid);
        client_count++;
        break;
      }
    }
    pthread_mutex_unlock(&lock);
    free(userid);
  }

// =============================================================================

  // TCP user must first be logged on before command can be sent
  else if ((strcmp(conn_type, "TCP") == 0) && (*connected == 0) && (rc > -1)) {
    return;
  }

// =============================================================================

  else if (rc == 1) { // handle WHO
    printf("%s: Rcvd WHO request\n", handler);
    char* send_buffer = make_buffer();
    strcat(send_buffer, "OK!\n");

    // Extract the sorted array and send its contents
    int temp_len = 0;
    pthread_mutex_lock(&lock);
    char** temp = sort_users(&temp_len);
    pthread_mutex_unlock(&lock);
    for (int i = 0; i < temp_len; i++) {
      if (strcmp("", temp[i]) == 0)
        continue;
      strcat(send_buffer, temp[i]);
      strcat(send_buffer, "\n");
    }
    int n;
    int send_len = strlen(send_buffer);
    if (strcmp(conn_type, "TCP") == 0) { // Send TCP
      n = send(sockfd, send_buffer, send_len, 0);
    }
    else if (strcmp(conn_type, "UDP") == 0) { // Send UDP
      int client_len = sizeof(client);
      n = sendto(sockfd, send_buffer, send_len, 0, (struct sockaddr *)&client, client_len);
    }
    if (n != send_len) { // ERROR check
      fprintf(stderr, "%s: ERROR <couldn't send WHO message>\n", handler);
    }
    for (int i = 0; i < temp_len; i++)
      free(temp[i]);
    free(temp);
    free(send_buffer);
  }

// =============================================================================

  else if (rc == 2) { // handle LOGOUT
    if (strcmp(conn_type, "UDP") == 0) // Don't process UDP logout
      return;
    printf("%s: Rcvd LOGOUT request\n", handler);
    *connected = 0;
    pthread_mutex_lock(&lock);
    strcpy(get_userid(sockfd), "");
    pthread_mutex_unlock(&lock);
    int n = send(sockfd, "OK!\n", 4, 0);
    if (n != 4) {
      fprintf(stderr, "%s: ERROR <couldn't send LOGOUT message>\n", handler);
    }
  }

// =============================================================================

  else if (rc == 3) { // handle SEND / TCP only
    if (strcmp(conn_type, "UDP") == 0) {
      char* out_msg = "SEND not supported over UDP";
      int client_len = sizeof(client);
      int n = sendto(sockfd, out_msg, strlen(out_msg), 0, (struct sockaddr *)&client, client_len);
      if (n != strlen(out_msg)) {
        fprintf(stderr, "%s: ERROR <sendto() failed>\n", handler);
      }
      printf("%s: Sent ERROR (SEND not supported over UDP)\n", handler);
      return;
    }
    pthread_mutex_lock(&lock);
    char* sender = get_userid(sockfd);
    pthread_mutex_unlock(&lock);
    if (sender != NULL) {
      parse_message_and_send(message, len, conn_type, sender, handler, sockfd);
    } else {
      fprintf(stderr, "%s ERROR: <could not find sender id>\n", handler);
    }
  }

// =============================================================================

  else if (rc == 4) { // handle BROADCAST
    printf("%s: Rcvd BROADCAST request\n", handler);

    // Check to see if sender is USP or TCP
    if (strcmp(conn_type, "UDP") == 0) {
      parse_broadcast_and_send(message, len, "UDP-client", "UDP", handler, sockfd, client);
    } else {
      char* userid;
      pthread_mutex_lock(&lock);
      for (int i = 0; i < MAX_CLIENTS; i++) {
        if (global_sockfds[i] == sockfd) {
          userid = users[i];
          break;
        }
      }
      pthread_mutex_unlock(&lock);
      parse_broadcast_and_send(message, len, userid, "TCP", handler, sockfd, client);
    }
  }

  else {
    fprintf(stderr, "%s: ERROR <invalid client message>\n", handler);
  }
}


// =============================================================================
// =============================================================================


// Handle connection to client
void* TCP_socket_thread(void* arg) {
  int newsock = *((int *)arg);
  pthread_t tid = pthread_self();
  char* buffer = make_buffer();
  char* conn_type = "TCP";

  // Keep communicating until connection is closed
  int n;
  int connected = 0;         // Set to 1 after LOGIN
  struct sockaddr_in client; // Unused in TCP
  while (1) {

    // Read message from the client
    n = recv(newsock, buffer, BUFFER_SIZE-1, 0);
    if (n == 0) {
      printf("CHILD %lu: Client disconnected\n", tid);
      pthread_mutex_lock(&lock);
      clear_client_from_global(newsock);
      pthread_mutex_unlock(&lock);
      break;
    } else if (n < 0) {
      fprintf(stderr, "CHILD %lu: ERROR <recv() failed>\n", tid);
    } else { // Handle the message
      char* handler = calloc(64, sizeof(char));
      sprintf(handler, "CHILD %lu", tid);
      handle_message(buffer, n, handler, &connected, newsock, conn_type, client);
      free(handler);
    }
  }
  free(buffer);
  close(newsock);
  pthread_exit(NULL);
}


// =============================================================================
// =============================================================================


int main(int argc, char** argv) {

  // Ensure flushed buffer and check for input
  setvbuf(stdout, NULL, _IONBF, 0);
  if (argc != 2) {
    fprintf(stderr, "MAIN: ERROR <missing port number argument>\n");
    return EXIT_FAILURE;
  }

  // Read in command line port value
  short port = (short) atoi(argv[1]);
  if (port < 1 || port > 65535) {
    fprintf(stderr, "MAIN: ERROR <invalid port number>\n");
    return EXIT_FAILURE;
  }

  // Allocate memory for global info about clients
  global_sockfds = calloc(MAX_CLIENTS, sizeof(int));
  for (int i = 0; i < MAX_CLIENTS; i++) global_sockfds[i] = -1;
  users = calloc(MAX_CLIENTS, sizeof(char *));

// =============================================================================

  // Create listener socket as a TCP socket (SOCK_STREAM); check call success
  int sd_TCP = socket(AF_INET, SOCK_STREAM, 0);
  if (sd_TCP < 0) {
    perror("MAIN: ERROR <TCP socket() failed>");
    return EXIT_FAILURE;
  }

  // Create listener socket as a UDP socket (SOCK_DGRAM); check call success
  int sd_UDP = socket(AF_INET, SOCK_DGRAM, 0);
  if (sd_UDP < 0) {
    perror("MAIN: ERROR <UDP socket() failed>");
    return EXIT_FAILURE;
  }

  // Create the socket structures, allow any IP to connect, confugure port
  struct sockaddr_in server;
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  server.sin_port = htons(port);
  printf("MAIN: Started server\n");

  // Attempt to bind the port with the socket
  if (bind(sd_TCP, (struct sockaddr *)&server, sizeof(server)) < 0) {
    perror("MAIN: ERROR <TCP bind() failed>");
    return EXIT_FAILURE;
  }

  // Identify this port as a TCP listener port
  if (listen(sd_TCP, 10) == -1) {
    perror("MAIN: ERROR <listen() failed>");
    return EXIT_FAILURE;
  }
  printf("MAIN: Listening for TCP connections on port: %d\n", port);

  // Attempt to bind the port with the socket
  if (bind(sd_UDP, (struct sockaddr *)&server, sizeof(server)) < 0) {
    perror("MAIN: ERROR <UDP bind() failed>");
    return EXIT_FAILURE;
  }
  printf("MAIN: Listening for UDP datagrams on port: %d\n", port);
  FD_ZERO(&readfds);

// =============================================================================

  // Create array to hold all of the thread indeces
  pthread_t* tid = calloc(MAX_CLIENTS, sizeof(pthread_t));
  int t = 0;

  // Create the client struct
  struct sockaddr_in client;
  int fromlen = sizeof(client);
  while (1) {

    // Create a timeout interval: 2 seconds and 500 microseconds
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 500;

    //  Watch stdin (fd 0) to see when it has input
    FD_ZERO(&readfds);
    FD_SET(sd_TCP, &readfds);
    FD_SET(sd_UDP, &readfds);

    // Give it a timeout so that it does not block indefinitely
    int ready = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
    if (ready == 0)
      continue;

    // Check to see if there is activity on the listener descriptor TCP
    if (FD_ISSET(sd_TCP, &readfds)) {
      int newsock = accept(sd_TCP, (struct sockaddr *)&client, (socklen_t *)&fromlen);
      printf("MAIN: Rcvd incoming TCP connection from: %s\n", inet_ntoa(client.sin_addr));
      //fprintf(stderr, "TALKING TO CLIENT ON FD [%d]\n", newsock);
      if (pthread_create(&tid[t++], NULL, TCP_socket_thread, &newsock) != 0) {
        perror("ERROR: <failed to create thread>\n");
      }
    }

    // Check to see if there is activity on the listener descriptor UDP
    if (FD_ISSET(sd_UDP, &readfds)) {
      int connected = 0;
      char* buffer = make_buffer();
      int n = recvfrom(sd_UDP, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&client, (socklen_t *)&fromlen);
      printf("MAIN: Rcvd incoming UDP datagram from: %s\n", inet_ntoa(client.sin_addr));
      //fprintf(stderr, "SOCKET: %d\n", sd_UDP);
      handle_message(buffer, n, "MAIN", &connected, sd_UDP, "UDP", client);
      free(buffer);
    }
  }
  free(tid);
  return EXIT_SUCCESS;
}
