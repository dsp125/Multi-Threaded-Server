#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>

#include <sys/select.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>

#include <signal.h>
#include <ctype.h>

#define BUFFER_SIZE 1024
#define MAX_CLIENTS 100

typedef struct //Struct for passing arguments to thread function
{
  int sock;
} client_t;

typedef struct
{
  char name[20]; //Userid
  int online; //Online Status (0 == offline | 1 == online)
  int sockfd;
} users_type;

users_type users[20]; //Stores user information
int num_users = 0; //Keeps track of number of files
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void * client_thread(void * arg) //Function called by pthread_create each time a new client comes
{
  client_t* client = (client_t*) arg;
  int newsock = client->sock;
  int n = 0, k = 0;
  char* full_command; //String for passing buffer to string function
  unsigned short tid = (unsigned short) pthread_self();
  
  do //Continues recieving messages until client terminates
  {
    /* can also use read() and write()..... */
    char buffer[ BUFFER_SIZE ];
    fflush(NULL);
    n = recv( newsock, buffer, BUFFER_SIZE, 0 );
    fflush(NULL);
    if ( n < 0 ){
      printf( "recv() failed\n" );
      fflush(NULL);
    }
    else      /***Reading the command happens here***/
    {
      buffer[n] = '\0';  /* assuming text.... */
      
      full_command = (char*) calloc(n+1, sizeof(char));
      memcpy(full_command, buffer, n+1); //Converts char array to char * for using string operations
      
      char* userid = (char*) calloc(20, sizeof(char));
      int size = 0, found = 0;
      char* temp_str = (char*) calloc(50, sizeof(char));
      
      char * pch;
      pch = strchr(buffer,'\n');
      int loc = 0;
      while (pch!=NULL)
      {
          loc = pch-buffer+1;
          break;
      }
      
      //split the array beforehand
      char command[loc];
      int lpe = 0;
      for(lpe = 0; lpe < loc - 1;lpe++){
      	command[lpe] = buffer[lpe];
      }

      command[loc] = '\0';

      k = sscanf(full_command, "%s %s", command, temp_str); //Gets the type of command: 
                                                            //"LOGIN", "WHO", "LOGOUT", "SEND", "BROADCAST", or "SHARE"
      
      ////////////////////////////////////////////////////////////
      if (strcmp(command, "LOGIN") == 0){
        temp_str = (char*) calloc(70, sizeof(char));
        k = sscanf(full_command, "LOGIN %s\n", userid);//Format is: LOGIN <userid>
        if (k != 2){
          printf("%s\n", userid);
          fflush(NULL);
          temp_str = "ERROR LOGIN HAS THESE ARGUMENTS\nLOGIN <userid>";
          k = send( newsock, temp_str, strlen(temp_str), 0 );
          printf("[child %u] Sent ARGUMENT ERROR\n", tid);
          fflush( NULL );
          if ( k != strlen(temp_str) ){
            perror( "send() failed" );
          }
          continue;
        }
        for (k = 0; k < num_users; k++){
          if (strcmp(users[k].name, userid) == 0){
            found = 1; //Marks that the file already exists
          }
        }
        if (found == 1){
          temp_str = "Already connected\n";
          n = send( newsock, temp_str, strlen(temp_str), 0 );
          if ( n != strlen(temp_str) ){
            perror( "send() failed" );
          }
          continue;
        }else{
          //check if valid userid
          int idlen = strlen(userid);
          int i = 0;
          while (isalnum(userid[i])) i++;
          if(idlen <4 || idlen > 16 || i != idlen){
            //invalid id
            temp_str = "Invalid userid\n";
            n = send( newsock, temp_str, strlen(temp_str), 0 );
            if ( n != strlen(temp_str) ){
              perror( "send() failed" );
            }
          }else{
            //new user creation
            temp_str = "OK!\n";
            n = send(newsock, temp_str,strlen(temp_str),0);
            strcpy(users[num_users].name, userid);
            users[num_users].online = 1;
            users[num_users].sockfd = newsock;
            num_users++;
            printf("CHILD %u: Rcvd LOGIN request for userid %s\n", tid, userid);
            fflush( NULL );
          }
        }        
      }
      ////////////////////////////////////////////
      else if (strcmp(command, "WHO") == 0){
        temp_str = (char*) calloc(50, sizeof(char));
        sprintf(temp_str,"%s","OK!\n");
        printf("CHILD %u: Received WHO request\n", tid);
        fflush( stdout );
        if (k > 1){
          temp_str = "ERROR WHO TAKES NO ARGUMENTS\n";
          n = send( newsock, temp_str, strlen(temp_str), 0 );
          printf("CHILD %u: Sent ARGUMENT ERROR\n", tid);
          fflush( NULL );
          if ( n != strlen(temp_str) ){
            perror( "send() failed" );
          }
          continue;
        }
        pthread_mutex_lock( &mutex );
        
        //Print the users in alpahbetical order:
        int count = 0; //Counter for users
        char users_left[num_users][50]; //Stores the names of the unsorted users
        int largest; //Index of the running largest in files_left
        char empty[6] =  "zzzzz"; //What qualifies as empty
        for(n = 0; n < num_users; n++){ //Initializes it to all the users
          strcpy(users_left[n], users[n].name);
        }
        
        char* temp_str2 = (char*) calloc(50, sizeof(char));
        for (count = 0; count < num_users; count++){
          largest = 0;
          for(n = 0; n < num_users; n++){
            if ((strcmp(users_left[n], empty) != 0) && (strcmp(users_left[largest], users_left[n]) > 0)){ 
              //If this is not empty and is smaller than the running largest, it is the new largest
              largest = n;
            }
          }
          sprintf(temp_str2, " %s\n", users_left[largest]);
          strcat(temp_str, temp_str2); //strcat apparently appends to the front...most annoying
          strcpy(users_left[largest], empty);
        }
        
        pthread_mutex_unlock( &mutex );
        
        sprintf(temp_str2, "%d%s\n", n, temp_str);
        n = send( newsock, temp_str2, strlen(temp_str2), 0 );
        if ( n < 0 ){
          perror( "send() failed" );
        }  
        fflush(NULL);
      }
      //////////////////////////////////////////////////////////
      else if (strcmp(command, "LOGOUT") == 0){
        temp_str = (char*) calloc(50, sizeof(char));
        
        printf("CHILD %u: Rcvd LOGOUT request\n for userid %s", tid, userid);
        fflush( stdout );
        if (k > 1){
          temp_str = "ERROR LOGOUT TAKES NO ARGUMENTS\n";
          n = send( newsock, temp_str, strlen(temp_str), 0 );
          printf("CHILD %u: Sent ARGUMENT ERROR\n", tid);
          fflush( NULL );
          if ( n != strlen(temp_str) ){
            perror( "send() failed" );
          }
          continue;
        }
        pthread_mutex_lock( &mutex );
        found = 0;
        for (k = 0; k < num_users; k++){
          if (users[k].sockfd == newsock){
            strcpy(users[k].name,userid); //Marks that the file already exists
            users[k].online = 0;
            found = 1;
            temp_str = "OK!\n";
            n = send(newsock, temp_str, strlen(temp_str),0);
          }
        }
        if (found == 0){
          temp_str = "Not Connected\n";
          n = send( newsock, temp_str, strlen(temp_str), 0 );
          if ( n != strlen(temp_str) ){
            perror( "send() failed" );
          }
          continue;
        }
        pthread_mutex_unlock( &mutex );
      }
      //////////////////////////////////////////////////////////
      else if (strcmp(command, "SEND") == 0){
        temp_str = (char*) calloc(70, sizeof(char));
        int msglen;
        char* message = (char*) calloc(990, sizeof(char));
        k = sscanf(full_command, "SEND %s %d\n%s\n", userid,msglen,message);//Format is: SEND <recipient-userid> <msglen>\n<message>
        if (k != 3){
          fflush(NULL);
          temp_str = "Invalid SEND format";
          k = send( newsock, temp_str, strlen(temp_str), 0 );
          printf("CHILD %u: Sent ERROR (%s)",temp_str);
          fflush( NULL );
          if ( k != strlen(temp_str) ){
            perror( "send() failed" );
          }
          continue;
        }
        printf( "CHILD %u: Rcvd SEND request to userid %s\n", tid, userid); //Newline added automatically
        fflush( stdout );
        int sending_addr;
        for (k = 0; k < num_users; k++){
          if (strcmp(users[k].name, userid) == 0){
            found = 1;
            sending_addr = users[k].sockfd;
          }
        }
        if (found == 1){
          sprintf(temp_str,"FROM %d %d %s\n",newsock,msglen,message);
          n = send( sending_addr, temp_str, msglen, 0 );

        }else{
          temp_str = "Unknown userid\n";
          printf("CHILD %u: Sent ERROR (%s)",temp_str);
          n = send( newsock, temp_str, strlen(temp_str), 0 );
          if ( n != strlen(temp_str) ){
            perror( "send() failed" );
          }
        }
      }
      //////////////////////////////////////////////////////////
      else if (strcmp(command, "BROADCAST") == 0){
        temp_str = (char*) calloc(70, sizeof(char));
        int msglen;
        char* message = (char*) calloc(990, sizeof(char));
        k = sscanf(full_command, "BROADCAST %d\n%s\n", userid,msglen,message);//Format is: SEND <recipient-userid> <msglen>\n<message>
        if (k != 2){
          fflush(NULL);
          temp_str = "Invalid BROADCAST format";
          k = send( newsock, temp_str, strlen(temp_str), 0 );
          printf("CHILD %u: Sent ERROR (%s)",temp_str);
          fflush( NULL );
          if ( k != strlen(temp_str) ){
            perror( "send() failed" );
          }
          continue;
        }
        printf( "CHILD %u: Rcvd BROADCAST request \n", tid); //Newline added automatically
        fflush( stdout );
        int sending_addr;
        for (k = 0; k < num_users; k++){
          found = 1;
          sending_addr = users[k].sockfd;
          sprintf(temp_str,"FROM %d %d %s\n", newsock,msglen,message);
          n = send( sending_addr, temp_str, msglen, 0 );
        }
      }
      //////////////////////////////////////////////////////////
      else if (strcmp(command, "SHARE") == 0){
        int offset = 0;
        temp_str = (char*) calloc(70, sizeof(char));
        //Format is: SHARE <recipient-userid> <filelen>\n<file-contents>
        k = sscanf(full_command, "SHARE %s %d\n", userid, &size); //Ignore the command type, since we already have that. We'll do the file contents below.
        if (k != 2){
          printf("%s %d\n", userid, size);
          fflush(NULL);
          temp_str = "ERROR SHARE HAS THESE ARGUMENTS\nSHARE <userid> <filelen>\n<file-contents>\n";
          k = send( newsock, temp_str, strlen(temp_str), 0 );
          printf("CHILD %u: Sent ARGUMENT ERROR\n", tid);
          fflush( NULL );
          if ( k != strlen(temp_str) ){
            perror( "send() failed" );
          }
          continue;
        }
        printf( "CHILD %u: Rcvd SHARE request\n", tid); //Newline added automatically
        fflush( stdout );
        
        if (offset == 1){
          temp_str = "ERROR FILE EXISTS\n";
          n = send( newsock, temp_str, strlen(temp_str), 0 );
          printf("[child %u] Sent %s", tid, temp_str);
          fflush( NULL );
          if ( n != strlen(temp_str) ){
            perror( "send() failed" );
          }
          continue;
        }
        if (size < 0){
          //If the size or file type is invalid
          temp_str = "ERROR INVALID REQUEST\n";
          n = send( newsock, temp_str, strlen(temp_str), 0 );
          printf("CHILD %u: Sent %s\n", tid, temp_str);
          fflush( NULL );
          if ( n != strlen(temp_str) ){
            perror( "send() failed" );
          }
          continue;
        }

        //After newline, receive the next packet
        //sprintf(temp_str, "%s/%s", dir_name, filename); //Allows storing the file in the new directory
        
        pthread_mutex_lock( &mutex );
        ///////
        ///////
        ///////
        //LEFT OFF HERE/////
        ////////
        ////////////
        FILE* file = fopen(filename, "wb");
        if (file == NULL){
          fprintf(stderr, "fopen(\"%s\") failed, %s\n", temp_str, strerror(errno));
          exit(EXIT_FAILURE);
        }
        else{
          fflush(NULL);
        }
        fwrite(buffer + loc, sizeof(char), n - loc, file); //Write the first packet to the file
        //offset is now the number of bytes that have been read
        offset = n - loc+1;
          
        while (offset < size) //If the file is larger than the given buffer, break into multiple packets
        {
          fflush(NULL);
          n = recv(newsock, buffer, ( size-offset < BUFFER_SIZE ? size-offset : BUFFER_SIZE ), 0 );
          fflush(NULL);
          if ( n < 0 ){
            perror( "recv() failed" );
          }
          fwrite(buffer, sizeof(char), n, file);
          offset += n;
        }
        
        fclose(file);
        pthread_mutex_unlock( &mutex );
        
        
        printf("[child %u] Stored file \"%s\" (%d bytes)\n", tid, filename, size);
        fflush( stdout );
        
        /* send ack message back to the client */
        n = send( newsock, "ACK\n", 4, 0 );
        fflush( NULL );
        if ( n != 4 ){
          perror( "send() failed" );
          fflush(NULL);
        }
        else{
          printf("[child %u] Sent ACK\n", tid);
          fflush(NULL);
        }
      }
      ////////////////////////////////////////////////////////////////////////////
      else if (strcmp(command, "\0") != 0){ //Invalid Command, ignoring blank ones
        sprintf(temp_str, "ERROR INVALID COMMAND \"%s\"--Try STORE, LIST, or READ\n", command);
        n = send( newsock, temp_str, strlen(temp_str), 0 );
        fflush(NULL);
        printf("[child %u] %s\n", tid, temp_str);
        fflush( NULL );
        if ( n != strlen(temp_str) ){
          printf( "send() failed" );
          fflush(NULL);
        }
        
      }
    }
    fflush(NULL);
  }
  while ( n > 0 );
  
  printf( "[child %u] Client disconnected\n", tid );
  fflush( NULL );
  close( newsock );
  return NULL;
}


int main(int argc, char * argv[])
{
  if (argc != 2){
    perror("Incorrect arguments. Also enter the port number.");
    exit(EXIT_FAILURE);
  }
  
  DIR* FD;
  struct dirent* in_file;
        

  
  unsigned short port_number = atoi(argv[1]);
  int sd = socket( PF_INET, SOCK_STREAM, 0 );
  if ( sd < 0 )
  {
    perror( "socket() failed" );
    exit( EXIT_FAILURE );
  }

  
  printf("Started server; listening on port: %u\n", port_number);
  fflush( stdout );
  
  /* socket structures */
  struct sockaddr_in server;

  server.sin_family = PF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  
  server.sin_port = htons( port_number );
  int len = sizeof( server );

  if ( bind( sd, (struct sockaddr *)&server, len ) < 0 )
  {
    perror( "bind() failed" );
    exit( EXIT_FAILURE );
  }

  listen( sd, 5 );   /* 5 is the max number of waiting clients */

  struct sockaddr_in client;
  int fromlen = sizeof( client );

  pthread_t tid;
  client_t* new_client = (client_t*)malloc(sizeof(client_t));
    
  while(1) //Continues accepting new clients until program is terminated
  {
    new_client->sock = accept( sd, (struct sockaddr *)&client, (socklen_t*)&fromlen );
    printf("MAIN: Rcvd incoming TCP connection from: %s\n", inet_ntoa( client.sin_addr ));
    fflush( stdout );
    int rc = pthread_create(&tid, NULL, client_thread, (void *)new_client);
  
    if ( rc != 0 )
  	{
  	  fprintf( stderr, "pthread_create() failed (%d): %s\n", rc, strerror( rc ) );
  	  return EXIT_FAILURE;
  	}
  }  	
  close(sd);
  return EXIT_SUCCESS;
}