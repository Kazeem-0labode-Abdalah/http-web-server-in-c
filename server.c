#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ctype.h>
#include <windows.h>
#include <stdbool.h>
#include <mysql.h>

#define BUFFER_SIZE 1024
#define MAX_JOBS 100
#define LOGIN_ERROR  -1
#define LOGIN_FAILED 0
#define LOGIN_SUCCESS 1
#define SIGN_UP_ERROR  -1
#define SIGN_UP_FAILED 0
#define SIGN_UP_SUCCESS 1
#define DB_HOST "localhost"
#define DB_USER "root"
#define DB_PASSWORD "password"
#define DB_NAME "databasename"
#define DB_PORT 3306
                                                    
//structs
typedef struct {
    char method[7];
    char url[1024];
    char query_string[1024];
    char header[1024];
    char body[1024];
} Client_request;

typedef struct{
    SOCKET client_socket;
} Job;

//vairables 
HANDLE job_semaphore;
HANDLE queue_mutex;
int front = 0;
int rear = 0;
DWORD timeout_ms = 60000;
int AMOUNT_OF_THREADS_IN_POOL;

//Arrays
char post_with_json_is_allowed_message[] = "HTTP/1.1 204 No Content\r\n"
                                        "Access-Control-Allow-Origin: *\r\n"
                                        "Access-Control-Allow-Methods: POST, OPTIONS,GET\r\n"
                                        "Access-Control-Allow-Headers: Content-Type\r\n"
                                        "Content-Length: 0\r\n"
                                        "\r\n";

char status_codes[] = {'0','1','2','3','4','5'};
Job jobs_queue[MAX_JOBS];

//functions prototypes
void enqueue(Job job);
Job dequeue();
int MYSQL_login_executor(char *email,char *password, MYSQL * conn);
int MYSQL_signup_executor(char *email,char *password,char *firstname,char *lastname, MYSQL * conn);
char* my_case_insensitive_strstr(const char * mainstr,const char* substr,int lenght);
void  is_connection_alive_converter(char * buffer,bool is_connection_alive);
void request_attender(char total_request_buffer[],SOCKET client_socket,MYSQL * conn,bool is_connection_alive);
void request_parser(char *request_buffer, Client_request *client_request);
void html_response_sender(char html_file_name[],SOCKET socket,bool is_connection_alive);
DWORD WINAPI receive_messages_thread_function(LPVOID arg);

int main(){
    //initialized winsok
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
    printf("WSAStartup failed: %d\n", WSAGetLastError());
    return 1;
}

SYSTEM_INFO sysinfo;
GetSystemInfo(&sysinfo);
AMOUNT_OF_THREADS_IN_POOL = sysinfo.dwNumberOfProcessors;

//array to hold the thread pool
HANDLE thread_pool[AMOUNT_OF_THREADS_IN_POOL];

//initialize the mutex lock
queue_mutex = CreateMutex(NULL, FALSE, NULL);

//initializing job semaphore
job_semaphore = CreateSemaphore(NULL, 0, MAX_JOBS, NULL);

// creating  a job struct
 Job job; 

//create threads of the pool and add the to the job holder
for(int i =0; i < AMOUNT_OF_THREADS_IN_POOL; i++){
 HANDLE thread = CreateThread(NULL,0,receive_messages_thread_function,NULL,0,NULL);
 if(thread == NULL){
printf("An error occured while creating thread %d",i);
return 1;
 }
 thread_pool[i] = thread;
}


  
    //create the server's socket
    SOCKET server_socket = socket(AF_INET,SOCK_STREAM,0);
    if(server_socket == INVALID_SOCKET){
        printf("an error occured when creating the server's socket\n");
        WSACleanup();
        return 1;
    }else{
        printf("The server's socket of the server has been successfully created \n");
    }

   //set up the struct to hold the server' address
    struct sockaddr_in server_socket_addr;
    memset(&server_socket_addr,0,sizeof(server_socket_addr));
    server_socket_addr.sin_family = AF_INET;
    server_socket_addr.sin_port = htons(8080);
    server_socket_addr.sin_addr.s_addr = INADDR_ANY;
    

    //bind the server's socket to its address struct
   if(bind(server_socket,(struct sockaddr *)&server_socket_addr,sizeof(server_socket_addr)) !=0){
    printf("an error occured binding server socket to its address struct\n");
    printf("error is num : %d ",WSAGetLastError());
    closesocket(server_socket);
    WSACleanup();
    return 1;
    }

    //set the server socket to its listening state
    if(listen(server_socket,10) !=0){
        printf("An error occured setting the server socket into its listening state\n");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }else{
        printf("server has started listening for client's connection request\n");
    }

    //create the client struct and vairable to hold its length
    struct sockaddr_in client_socket_addr;
    memset(&client_socket_addr,0,sizeof(client_socket_addr));
    int client_socket_lenght = sizeof(client_socket_addr);

    while(1){//this loop is to keep accepting connections and adding them to the job queue
    SOCKET client_socket = accept(server_socket,(struct sockaddr *)&client_socket_addr,&client_socket_lenght); // enable server to accept connections from clients
    if(client_socket == INVALID_SOCKET){
        printf("An error occured accepting the connection request of the client\n");
        WSACleanup();
    }else{
        job.client_socket = client_socket;
        enqueue(job);
    }

if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms)) == SOCKET_ERROR) {
    printf("Failed to set timeout of the client socket after accepting it: %d\n", WSAGetLastError());
}

    }//end of connection acceptance loop

    return 0;
}


//functions

void request_parser(char *request_buffer, Client_request *client_request){
    // 1 capture body before strtok destroys the buffer
    char *body_start = strstr(request_buffer, "\r\n\r\n");
    if (body_start) body_start += 4;

    //2 get the method
    char *token = strtok(request_buffer," ");
    strcpy(client_request -> method,token);

    //3 get the url
    token = strtok(NULL," ");
    strcpy(client_request -> url,token);

    //4 get needed header
    char *header_token = strtok(NULL,"\r\n");
    while(header_token != NULL){

    if(strncmp(header_token,"Content-Length:",10) == 0){
        strcpy(client_request ->header,header_token);
        break;
    } else {
        header_token = strtok(NULL,"\r\n");
    }
    }

    //5 get body data
    if (body_start) {
        strcpy(client_request->body, body_start);
    }

    //6 get query string from the url
    if(strchr(client_request -> url,'?') != NULL){
        token = strtok(client_request -> url,"?");
        strcpy(client_request -> url,token);

        token = strtok(NULL,"?");
        strcpy(client_request -> query_string,token);
    }

}


 void  is_connection_alive_converter(char * buffer,bool is_connection_alive){
      if(is_connection_alive){
        strcpy(buffer,"keep-alive");
      }else{
        strcpy(buffer,"close");
      }
    }


void html_response_sender(char html_file_name[],SOCKET socket,bool is_connection_alive){// this function will snd a html file to the client
   
   char connection_status[11];
   is_connection_alive_converter(connection_status,is_connection_alive);

     // Read HTML file
        FILE* file = fopen(html_file_name, "rb"); // we are reading the file in binary read mode
        if (file == NULL) {
            printf("Failed to open file\n");
        }

      if(fseek(file, 0, SEEK_END) !=0){ // this will move the file pointer to the end of the file we do this to get the size of the file
            printf("An error occured while using fseek to change the position of file pointer to the end ");
        } 

    long fileSize = ftell(file);// this return the position of the pointer  which is at the end ot the file hence we get the size of the file
    if(fileSize == 0){
        printf("An error occured using the ftell to get the position of the file pointer");
    }
        
    if(fseek(file,0,SEEK_SET) !=0){ // this will return the pointer to pointing back to the begining of the file
        printf("An error occured while using fseek to change the position of file pointer to the start ");
    }

    // Create HTTP response header
    char header[BUFFER_SIZE];
    if(sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: %s\r\nContent-Length: %ld\r\n\r\n",connection_status,fileSize) < 0){ // this will store the formated headers into the header array /buffer
    printf("An error occured will writing the formated header into the header buffer");
    } 

    // Send HTTP response header
    if(send(socket, header, strlen(header), 0) == SOCKET_ERROR){// firstly we send the header back to the client(browser) to let the know the nature of the response/file we are about to send
    printf("An error occured sending the header back to the client using the send()");
    } 

    // Send HTML file
    char buffer[BUFFER_SIZE];
    size_t bytesRead;
    while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {// we  read the contents of the file/html file using fread() cause we are reading it in binary mode
    if(send(socket, buffer, bytesRead, 0) == SOCKET_ERROR){ // we read 1 kilobyte of data from the html file and send it back to client until its done
    printf("An error occured sending the html file back to the client using the send()");
    }
    }

        fclose(file); // close the file that we are reading
}

void request_attender(char total_request_buffer[],SOCKET client_socket,MYSQL * conn,bool is_connection_alive){//this function will attends to the requests of the client(post,get,options) and unexpected requests

      char connection_status[11];
     is_connection_alive_converter(connection_status,is_connection_alive);
     
             Client_request client_request = {0};
          request_parser(total_request_buffer,&client_request);
             //This will handle all of our urls
           if(strcmp(client_request.method,"GET") == 0 && strcmp(client_request.url,"/") == 0){// this will handle GET/  for the root url
           html_response_sender("homepage.html",client_socket,is_connection_alive);                                 
        }else if(strcmp(client_request.method,"GET") == 0 && strcmp(client_request.url,"/sign_up") == 0){// this will handle GET/sign_up
             html_response_sender("login&signup.html",client_socket,is_connection_alive);
        }else if(strcmp(client_request.method,"GET") == 0 && strcmp(client_request.url,"/login") == 0){// this will handle GET/login
             html_response_sender("login&signup.html",client_socket,is_connection_alive);
        }
        else if(strcmp(client_request.method,"POST") == 0 && strcmp(client_request.url,"/login") == 0){// this will handle POST/login
        int counter = 0;
        int holder[8] = {0};

        char *email = calloc(1024,sizeof(char)); // aquire memory space of 1024 bytes for the entered email address
        if(email == NULL){
            printf("An error occured using calloc for email");
            //send faliure message and end their thread
        }
        char *password = calloc(1024,sizeof(char));// aquire memory space of 1024 bytes for the entered password
       if(password == NULL){
            printf("An error occured using calloc for password");
             //send faliure message and end their thread
        }

        // Iterate over the client_request.body
        for (int i = 0; i < sizeof(client_request.body) / sizeof(client_request.body[0]); i++) {
        if (client_request.body[i] == '\"') {
        counter++;
        if (counter <= 8) {
        holder[counter - 1] = i;  
        }
        } else if (client_request.body[i] == '\0') { // Check for null terminator to break out of loop
        break;
        }
        }


        int email_lenght = holder[3] - holder[2]; // claculating the length of the entered email
        email_lenght--;
        int email_index =10;
        for(int i = 0; i < email_lenght; i++){
            email[i] = client_request.body[email_index];
            email_index++;
        }
        
        int password_lenght = holder[7] - holder[6]; // calculating the length of the entered password
        password_lenght --;
        int password_index = holder[6];
        for(int i = 0; i < password_lenght; i++){
            password[i] = client_request.body[password_index + 1];
            password_index++;
        }


        char *newEmailP = realloc(email,(email_lenght + 1) * sizeof(char)); // resize the aquired memory for the entered email to the size of the entered email + 1
        if(newEmailP == NULL){
        printf("An error occured realocating the email");
        //send faliure message and end their thread
        }else{
        email = newEmailP;
        newEmailP = NULL;
        email[email_lenght] = '\0'; // adding a null ternimator to the end of the email after realocation works this is so i can print it as a string using printf()
        }

        char *newPasswordP = realloc(password,(password_lenght + 1) * sizeof(char)); // resize the aquired memory for the entered epassword to the size of the entered password + 1
        if(newPasswordP == NULL){
        printf("An error occured realocatiing the password");
        //send faliure message and end their thread
        }else{
        password = newPasswordP;
        newPasswordP  = NULL;
        password[password_lenght] = '\0'; // adding a null ternimator to the end of the password after realocation works this is so i can print it as a string using printf()
        }

        char *response_buffer = calloc(1024,sizeof(char));
        int result_of_login_attempt  = MYSQL_login_executor(email,password,conn);

      if(result_of_login_attempt == LOGIN_SUCCESS){//they are verifies users
    sprintf(response_buffer,"HTTP/1.1 200 Ok\r\nContent-Type:text/plain\r\nConnection: %s\r\nContent-Length:%d\r\n\r\n%c",connection_status,sizeof(status_codes[2]),status_codes[2]);
       if(send(client_socket, response_buffer, strlen(response_buffer), 0) == SOCKET_ERROR){ //send response to the client 
     printf("An error occured sending the response before the status code of try again back to the client using the send()");
    }
      } else if(result_of_login_attempt == LOGIN_FAILED){//they dint exist in database
       sprintf(response_buffer,"HTTP/1.1 200 Ok\r\nContent-Type:text/plain\r\nConnection: %s\r\nContent-Length:%d\r\n\r\n%c",connection_status,sizeof(status_codes[1]),status_codes[1]);
       if(send(client_socket, response_buffer, strlen(response_buffer), 0) == SOCKET_ERROR){ //send response to the client 
     printf("An error occured sending the response before the status code of try again back to the client using the send()");
    } 

      }else{//error occured while trying to verify login
      sprintf(response_buffer,"HTTP/1.1 400 Ok\r\nContent-Type:text/plain\r\nConnection: %s\r\nContent-Length:%d\r\n\r\n%c",connection_status,sizeof(status_codes[0]),status_codes[0]);
       if(send(client_socket, response_buffer, strlen(response_buffer), 0) == SOCKET_ERROR){ //send response to the client 
       printf("An error occured sending the logging  response  the status code of try again back to the client using the send()");
       } 
      }

      free(email);
      free(password);
      free(response_buffer);
        
    
        }else if(strcmp(client_request.method,"POST") == 0 && strcmp(client_request.url,"/sign_up") == 0){// this will handle POST/sign_up
        int counter = 0;
        int holder[16] = {0};

        char *first_name = calloc(1024,sizeof(char)); // aquire memory space of 1024 bytes for the entered first_name
        if(first_name == NULL){
            printf("An error occured using calloc for first name");
            //send faliure message and end their thread
        }
        char *last_name = calloc(1024,sizeof(char));// aquire memory space of 1024 bytes for the entered last_name
       if(last_name == NULL){
            printf("An error occured using calloc for lastname");
             //send faliure message and end their thread
        }

         char *email = calloc(1024,sizeof(char)); // aquire memory space of 1024 bytes for the entered email address
        if(email == NULL){
            printf("An error occured using calloc for email");
            //send faliure message and end their thread
        }
        char *password= calloc(1024,sizeof(char));// aquire memory space of 1024 bytes for the entered password
       if(password == NULL){
            printf("An error occured using calloc for password");
             //send faliure message and end their thread
        }


        // Iterate over the client_request.body
        for (int i = 0; i < sizeof(client_request.body) / sizeof(client_request.body[0]); i++) {
        if (client_request.body[i] == '\"') {
        counter++;
        if (counter <= 16) {
        holder[counter - 1] = i;  
        }
        } else if (client_request.body[i] == '\0') { // Check for null terminator to break out of loop
        break;
        }
        }

      
     int first_name_lenght = holder[3] - holder[2]; // calculating the length of the entered password
        first_name_lenght --;
        int first_name_index = holder[2];
        for(int i = 0; i < first_name_lenght; i++){
            first_name[i] = client_request.body[first_name_index + 1];
            first_name_index++;
        }

         int last_name_lenght = holder[7] - holder[6]; // calculating the length of the entered password
        last_name_lenght --;
        int last_name_index = holder[6];
        for(int i = 0; i < last_name_lenght; i++){
            last_name[i] = client_request.body[last_name_index + 1];
            last_name_index++;
        }


        int email_lenght = holder[11] - holder[10]; // claculating the length of the entered email
        email_lenght--;
        int email_index = holder[10];
        for(int i = 0; i < email_lenght; i++){
            email[i] = client_request.body[email_index + 1];
            email_index++;
        }
        
        int password_lenght = holder[15] - holder[14]; // calculating the length of the entered password
        password_lenght --;
        int password_index = holder[14];
        for(int i = 0; i < password_lenght; i++){
            password[i] = client_request.body[password_index + 1];
            password_index++;
        }

         char *newFirstNameP = realloc(first_name,(first_name_lenght + 1) * sizeof(char)); // resize the aquired memory for the entered email to the size of the entered email + 1
        if(newFirstNameP == NULL){
        printf("An error occured realocating the first name");
        //send faliure message and end their thread
        }else{
        first_name = newFirstNameP;
        newFirstNameP = NULL;
        first_name[first_name_lenght] = '\0'; // adding a null ternimator to the end of the email after realocation works this is so i can print it as a string using printf()
        }

         char *newLastNameP = realloc(last_name,(last_name_lenght + 1) * sizeof(char)); // resize the aquired memory for the entered lastname to the size of the entered lastname + 1
        if(newLastNameP == NULL){
        printf("An error occured realocating the lastname");
        //send faliure message and end their thread
        }else{
        last_name = newLastNameP;
        newLastNameP = NULL;
        last_name[last_name_lenght] = '\0'; // adding a null ternimator to the end of the email after realocation works this is so i can print it as a string using printf()
        }

        char *newEmailP = realloc(email,(email_lenght + 1) * sizeof(char)); // resize the aquired memory for the entered email to the size of the entered email + 1
        if(newEmailP == NULL){
        printf("An error occured realocating the email");
        //send faliure message and end their thread
        }else{
        email = newEmailP;
        newEmailP = NULL;
        email[email_lenght] = '\0'; // adding a null ternimator to the end of the email after realocation works this is so i can print it as a string using printf()
        }

        char *newPasswordP = realloc(password,(password_lenght + 1) * sizeof(char)); // resize the aquired memory for the entered epassword to the size of the entered password + 1
        if(newPasswordP == NULL){
        printf("An error occured realocatiing the password");
        //send faliure message and end their thread
        }else{
        password = newPasswordP;
        newPasswordP  = NULL;
        password[password_lenght] = '\0'; // adding a null ternimator to the end of the password after realocation works this is so i can print it as a string using printf()
        }

    // first step is to confirm if email already exist
     char *response_buffer = calloc(1024,sizeof(char));
     int result_of_login_attempt  = MYSQL_login_executor(email,password,conn);

      if(result_of_login_attempt == LOGIN_SUCCESS){//they are users that already exist
    sprintf(response_buffer,"HTTP/1.1 200 Ok\r\nContent-Type:text/plain\r\nConnection: %s\r\nContent-Length:%d\r\n\r\n%c",connection_status,sizeof(status_codes[5]),status_codes[5]);
       if(send(client_socket, response_buffer, strlen(response_buffer), 0) == SOCKET_ERROR){ //send response to the client 
     printf("An error occured sending the response before the status code of try again back to the client using the send()");
    }
      } else if(result_of_login_attempt == LOGIN_FAILED){//they dint exist in database, so we will sign then up
    
 //since they dont exist yet , add them to the database(sign them up)

        int result_of_signup_attempt  = MYSQL_signup_executor(email,password,first_name,last_name,conn);

      if(result_of_signup_attempt == SIGN_UP_SUCCESS){//they have been added to the database as verified users
    sprintf(response_buffer,"HTTP/1.1 200 Ok\r\nContent-Type:text/plain\r\nConnection: %s\r\nContent-Length:%d\r\n\r\n%c",connection_status,sizeof(status_codes[4]),status_codes[4]);
       if(send(client_socket, response_buffer, strlen(response_buffer), 0) == SOCKET_ERROR){ //send response to the client 
     printf("An error occured sending the response before the status code of try again back to the client using the send()");
    }
      } else if(result_of_signup_attempt == SIGN_UP_FAILED){//the attempt to add the to the database as verified users failed
       sprintf(response_buffer,"HTTP/1.1 200 Ok\r\nContent-Type:text/plain\r\nConnection: %s\r\nContent-Length:%d\r\n\r\n%c",connection_status,sizeof(status_codes[3]),status_codes[3]);
       if(send(client_socket, response_buffer, strlen(response_buffer), 0) == SOCKET_ERROR){ //send response to the client 
     printf("An error occured sending the response before the status code of try again back to the client using the send()");
    } 

      }else{//error occured while trying to sign up the new user
      sprintf(response_buffer,"HTTP/1.1 400 Ok\r\nContent-Type:text/plain\r\nConnection: %s\r\nContent-Length:%d\r\n\r\n%c",connection_status,sizeof(status_codes[3]),status_codes[3]);
       if(send(client_socket, response_buffer, strlen(response_buffer), 0) == SOCKET_ERROR){ //send response to the client 
       printf("An error occured sending the logging  response  the status code of try again back to the client using the send()");
       } 
      }


 //end of adding them to the database(sign them up)


      }else{//error occured while trying to check if user already exist
      sprintf(response_buffer,"HTTP/1.1 400 Ok\r\nContent-Type:text/plain\r\nConnection: %s\r\nContent-Length:%d\r\n\r\n%c",connection_status,sizeof(status_codes[3]),status_codes[3]);
       if(send(client_socket, response_buffer, strlen(response_buffer), 0) == SOCKET_ERROR){ //send response to the client 
       printf("An error occured sending the logging  response  the status code of try again back to the client using the send()");
       } 
      }

        free(email);
        free(password);
        free(first_name);
        free(last_name);
        free(response_buffer);


        }else if(strcmp(client_request.method,"OPTIONS") == 0){//This will handle OPTIONS end point for CORS

        if(send(client_socket,post_with_json_is_allowed_message,strlen(post_with_json_is_allowed_message),0) == SOCKET_ERROR){
         printf("An error occured sending the options header back to the client");
        }else{
        printf("this is an OPTION endpoint was reached");
        }
   
        }
          
        else{// this will handle unexpected requests from the client(unexpected urls)
            html_response_sender("url_not_found.html",client_socket,is_connection_alive); //this will handle request of urls that are not ours (incorrect)
        }
}


char* my_case_insensitive_strstr(const char * mainstr,const char* substr,int length){

    if (*substr == '\0') return (char*)mainstr;

    for (int mainstr_index = 0; mainstr[mainstr_index] != '\0'; mainstr_index++)
    {
        int i;

        for(i = 0; i < length; i++){

            if(mainstr[mainstr_index + i] == '\0' ||
               tolower(mainstr[mainstr_index + i]) != tolower(substr[i])){
                break;
            }
        }

        if(i == length){
            return (char*)&mainstr[mainstr_index]; 
        }
    }

    return NULL;
}


int MYSQL_login_executor(char *email,char *password, MYSQL * conn){//this will handle login request from the client

int LOGIN_STATUS;

    if(email == NULL || password == NULL || conn == NULL){
    return LOGIN_ERROR;
}
    
    MYSQL_STMT * stmt = mysql_stmt_init(conn);//initialize the statemwnt
    if(stmt == NULL){
        printf("mysql_stmt_init failed\n");
        return LOGIN_ERROR;
    }

    const char * query = "SELECT user_id FROM events_around_the_world_users WHERE user_email_address = ? AND user_password = ?";//write the query statement

    if(mysql_stmt_prepare(stmt,query,strlen(query)) !=0){//prepare the query statement
        printf("Preparing the query statment faliled :%s",mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return LOGIN_ERROR;
    }

    MYSQL_BIND bind[2];  //an array of bind struct to hold user input
    memset(bind,0,sizeof(bind));// set the values of the bind struct to 0

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = email;
    bind[0].buffer_length = strlen(email);

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = password;
    bind[1].buffer_length = strlen(password);

if(mysql_stmt_bind_param(stmt,bind) !=0){// bind the paramenters to the prepared statement
   printf("binding the paramenters to the prepared statement faliled :%s",mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    return LOGIN_ERROR;
}


if(mysql_stmt_execute(stmt) !=0){//execute the prepared query statement
     printf("executing the prepared query statement faliled :%s",mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    return LOGIN_ERROR;
}

if(mysql_stmt_store_result(stmt) !=0){// store the result of the last executed prepared query statement
printf("storing the result of the last executed prepared query statement faliled :%s",mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    return LOGIN_ERROR;
}


if(mysql_stmt_num_rows(stmt) == 0){//check the amount of rows in the result that was stored
LOGIN_STATUS = LOGIN_FAILED;
}else{
LOGIN_STATUS = LOGIN_SUCCESS;
}

//clean up
mysql_stmt_free_result(stmt);
mysql_stmt_close(stmt);

return LOGIN_STATUS;
}


int MYSQL_signup_executor(char *email,char *password,char *firstname,char *lastname, MYSQL * conn){//this will handle the sign up request from the client

int SIGN_UP_STATUS;

    if(email==NULL || password == NULL|| firstname == NULL,lastname == NULL,conn ==NULL){
    return SIGN_UP_ERROR;
}
    
    MYSQL_STMT * stmt = mysql_stmt_init(conn);//initialize the statemwnt
    if(stmt == NULL){
        printf("mysql_stmt_init failed\n");
        return SIGN_UP_ERROR;
    }

    const char * query = "insert into events_around_the_world_users(user_first_name,user_last_name,user_email_address,user_password) values(?,?,?,?)";//write the query statement

    if(mysql_stmt_prepare(stmt,query,strlen(query)) !=0){//prepare the query statement
        printf("Preparing the query statment faliled :%s",mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return SIGN_UP_ERROR;
    }

    MYSQL_BIND bind[4];  //an array of bind struct to hold user input
    memset(bind,0,sizeof(bind));// set the values of the bind struct to 0

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = firstname;
    bind[0].buffer_length = strlen(firstname);

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = lastname;
    bind[1].buffer_length = strlen(lastname);

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = email;
    bind[2].buffer_length = strlen(email);

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = password;
    bind[3].buffer_length = strlen(password);

if(mysql_stmt_bind_param(stmt,bind) !=0){// bind the paramenters to the prepared statement
   printf("binding the paramenters to the prepared statement faliled :%s",mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    return SIGN_UP_ERROR;
}


if(mysql_stmt_execute(stmt) == 0){//execute the prepared query statement
    printf("user was signed up");
SIGN_UP_STATUS = SIGN_UP_SUCCESS;
}else{
 printf("executing the prepared query statement faliled :%s",mysql_stmt_error(stmt));
    return SIGN_UP_FAILED;
}

//clean up
mysql_stmt_free_result(stmt);
mysql_stmt_close(stmt);

return SIGN_UP_STATUS;
}



DWORD WINAPI receive_messages_thread_function(LPVOID arg){// this is the functions of the threads that were created in the thread pool
    Job job ; // job struct to hold the dequeued job
    SOCKET client_socket;// the socket that is contained in the dequeue job
    char *total_request_buffer;// the buffer to hold the full request from the client
    int total_received ;// vairable to hold the amount of bytes of data recieve at an iteration 
    MYSQL * conn; //create a mysql connection object
    int content_length_int;
    int total_request_buffer_size = 8192;

    //initiallize the connection object
    conn = mysql_init(NULL);

    //connect to the mysql server
    if(
    mysql_real_connect(
         conn,DB_HOST,DB_USER,DB_PASSWORD,DB_NAME,DB_PORT,NULL,0
    )
    == NULL){
     printf("Connection failed: %s\n", mysql_error(conn));
    return 1;
    }else{
        printf("connection to the database was a success\n");
  }



  while(1){// beginning of first inner loop, thread lives forever and recieves jobs forever

job = dequeue();//blocks until a job exixst
client_socket = job.client_socket;
total_request_buffer = calloc(1, total_request_buffer_size);
total_received = 0;
bool is_connection_alive = true;
total_request_buffer_size = 8192;

while (is_connection_alive){// beginning of second  inner loop
    char temp[1024];
    int bytes = recv(client_socket, temp, sizeof(temp), 0);

    if (bytes == 0){//==0 means thet the client ended the connection 
    closesocket(client_socket);
    free(total_request_buffer);
    is_connection_alive = false;
    break;
    } else if(bytes == SOCKET_ERROR){// if a timeout occured , due to no message/request being sent
    int error = WSAGetLastError();
    if(error == WSAETIMEDOUT){
    closesocket(client_socket);
    free(total_request_buffer);
    is_connection_alive = false;
    break;
    }
     }

     if(total_received + bytes >= total_request_buffer_size){
        char * new_total_request_buffer_p = realloc(total_request_buffer,total_request_buffer_size + bytes + 1);//+1 is for the null terminator
        if(new_total_request_buffer_p != NULL){
            total_request_buffer = new_total_request_buffer_p;
            new_total_request_buffer_p = NULL;
            total_request_buffer_size += bytes + 1;
        }else{
            printf("An error occured reallocing total_request_buffer");
            closesocket(client_socket);
            free(total_request_buffer);
            break;
        }
     }

     if (total_request_buffer_size > 65536) {
    printf("Request too large\n");
    closesocket(client_socket);
    free(total_request_buffer);
    break;
}

    memcpy(total_request_buffer + total_received, temp, bytes);//appent th newly recieved message to the previouly recieved message
    total_received += bytes;
    total_request_buffer[total_received] = '\0';// im adding this null ternimator so strstr()an work properlt

while(1){// beginning of third inner loop

    if(!is_connection_alive){
    closesocket(client_socket);
    free(total_request_buffer);
    break;
    }

    // check for end of headers
    char *headers_end = strstr(total_request_buffer, "\r\n\r\n");


    if (headers_end) {
      char *connection_header =  my_case_insensitive_strstr(total_request_buffer,"Connection: close",17);
         if (connection_header && connection_header < headers_end) {
            is_connection_alive = false;
    }

        int header_length = headers_end - total_request_buffer + 4;

        // check content-length
        char *content_lenght_p = strstr(total_request_buffer, "Content-Length:");
        if (content_lenght_p) {
            content_length_int = atoi(content_lenght_p + strlen("Content-Length:"));
            int body_received = total_received - header_length;

            if (body_received >= content_length_int) {
                request_attender(total_request_buffer,client_socket,conn,is_connection_alive);//post/full request received
                int request_size = header_length + content_length_int;
                int remaining = total_received - (request_size);
                if(remaining > 0){
                    memmove(total_request_buffer,total_request_buffer + request_size,remaining);
                }
                total_received = remaining;
                total_request_buffer[total_received] = '\0';
            }else{
                break;// we have not recived the body completly yet
            }
        } else {
            request_attender(total_request_buffer,client_socket,conn,is_connection_alive);// get request recieved
            int request_size = header_length;
            int remaining = total_received - header_length;
                if(remaining > 0){
                    memmove(total_request_buffer,total_request_buffer + request_size,remaining);
                }
                total_received = remaining;
                total_request_buffer[total_received] = '\0';
        }
    }else{
        break;// we have not gotten the /r/n/r/n we have to wait and get more data
    }


}//end of third inner loop
}// beginning of second  inner loop
  
}// beginning of first inner loop
    return 0;
}

void enqueue(Job job){
WaitForSingleObject(queue_mutex, INFINITE);
jobs_queue[rear] = job;
rear = (rear + 1) % MAX_JOBS;
    ReleaseMutex(queue_mutex);
     ReleaseSemaphore(job_semaphore, 1, NULL); //signal that  a job is available
}

Job dequeue(){
     WaitForSingleObject(job_semaphore, INFINITE);
     WaitForSingleObject(queue_mutex, INFINITE);
    Job job = jobs_queue[front];
front = (front + 1) % MAX_JOBS;
    ReleaseMutex(queue_mutex);
return job;
}