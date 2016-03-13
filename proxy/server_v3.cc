/*
** server.c -- a stream socket server demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <iostream>
#include <string>
#include <algorithm>

#define PORT "1337"            // the port users will be connecting to
#define INTERNET_PORT "80"
#define BACKLOG 10	       // how many pending connections queue will hold
#define MAXBUFFSIZE 6000       // buffer size for the GET-request

void sigchld_handler(int s)
{
	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

std::string client(std::string &, bool &,bool &, int);
std::string hostExtract(std::string const &);
void  modifyrequest(std::string &);
bool filter(std::string const &);
bool findIfText(std::string const &);
bool findIfGzip(std::string const &);
int getLength(std::string &);

int main()
{
	int sockfd, new_fd;  // listen on sockfd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes = 1;
	char s[INET6_ADDRSTRLEN];
	int rv;
	bool filterText;
	bool gzip;
	std::string const & error_1 = "HTTP/1.1 302 Found\nLocation: http://www.ida.liu.se/~TDTS04/labs/2011/ass2/error1.html\r\n";
	std::string const & error_2 = "HTTP/1.1 302 Found\nLocation: http://www.ida.liu.se/~TDTS04/labs/2011/ass2/error2.html\r\n";


	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}
		if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	char buffer[MAXBUFFSIZE];
	while(true) {  // main accept() loop
	  std::string requestmsg;
	  sin_size = sizeof their_addr;
	  new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
	  
	  std::string ip = inet_ntop(their_addr.ss_family,
				     get_in_addr((struct sockaddr *)&their_addr),
				     s, sizeof s);
	  std::cout << "server: got connection from " << ip << std::endl;
	  
	  if (!fork()) { 
	    close(sockfd);                                                    // child doesn't need the listener
	    if(recv(new_fd, buffer, MAXBUFFSIZE, 0) > 0)                      // recieves msg from user (client)
	      requestmsg.append(buffer);
	    modifyrequest(requestmsg);
	    if(filter(requestmsg))     
	      {                                                               //BAD URL
		send(new_fd, error_1.c_str(), error_1.length(), 0);
		close(new_fd);
		break;
	      }
	    else
	      {
		//requestmsg.replace(0,requestmsg.find("User-Agent: "),error_1);
		
		std::string page = client(requestmsg, filterText, gzip, new_fd);    //recieve the body
		
		if(filterText && !gzip)                                             //is it text? Is it gzip?
		  if(filter(page))               
		    {                                                          // BAD PAGE
		      send(new_fd, error_2.c_str(), error_2.length(), 0);
		      close(new_fd);
		      break;
		    }
		if(!gzip)
		  send(new_fd, page.c_str(), page.length(), 0);
		close(new_fd);
		exit(0);
	      }
	  }
	  close(new_fd);                                                      // parent doesn't need this
	}	
	return 0;
}

/*
/*
** client.c -- a stream socket client demo
*/

std::string client(std::string & requestmsg, bool & filterText, bool & gzip,  int new_fd)
{
	int sockfd, numbytes;
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	int rv;
	char s[INET6_ADDRSTRLEN];	
	socklen_t sin_size;
	int totalbytes = 0;
	std::string webpage = ""; 
	std::string host = hostExtract(requestmsg);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(host.c_str(), INTERNET_PORT, &hints, &servinfo)) != 0)
	  fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("client: connect");
			close(sockfd);
			continue;
		}
		break;
	}

	if (p == NULL)
	  fprintf(stderr, "client: failed to connect\n");

	std::string ip = inet_ntop(p->ai_family,
				   get_in_addr((struct sockaddr *)p->ai_addr),
				   s,
				   sizeof s);
	std::cout << "client: connecting to " << ip << std::endl;
	freeaddrinfo(servinfo);
	//std::cout << requestmsg << std::endl;
	send(sockfd, requestmsg.c_str(), requestmsg.length(), 0);
	// forward request   
	char tmpbuf[MAXBUFFSIZE];                                    // temporary buffer to store the HTTP OK
	
	if((numbytes = recv(sockfd, tmpbuf, MAXBUFFSIZE-1, 0)) > 0); // FIRST RESPONSE FROM SERVER
	{
	  for(int i = 0; i < numbytes; ++i)
	    webpage.push_back(tmpbuf[i]);   
	  gzip = findIfGzip(webpage);                        // find if it is gzip
	  filterText = findIfText(webpage);                  // find if it is text

	  int length = getLength(webpage);                   // get content length
	  char truebuf[length];                              // create new buffer

	  if(!gzip)                                          
	    {
	      while((numbytes = recv(sockfd, truebuf, length-1, 0)) > 0)
	      {
		for(int i = 0; i < numbytes; ++i)
		  webpage.push_back(truebuf[i]); 
		totalbytes += numbytes;
		std::fill(&truebuf[0], &truebuf[sizeof(truebuf)], 0);    // clear buf
	      }
	    }
	  else                                                           // if gzip = send instantly
	    {
	      totalbytes += numbytes;
	      send(new_fd, tmpbuf, numbytes, 0);                         // send the first response 
	      std::fill(&tmpbuf[0], &tmpbuf[sizeof(tmpbuf)], 0);         // clear buffer

	      while((numbytes = recv(sockfd, truebuf, length-1, 0)) > 0) // read into the buffer while sending it to the user
	      {
		totalbytes += numbytes;
		send(new_fd, truebuf, numbytes, 0);
		std::fill(&truebuf[0], &truebuf[sizeof(truebuf)], 0); 
	      }
	    }
	}
	close(sockfd);
	return webpage;
}

bool filter(std::string const & text)
{
  std::string tmp = text;
  std::transform(tmp.begin(), tmp.end(), tmp.begin(), ::tolower);
  std::cout << tmp << std::endl;
  std::string forbidden [] = {"britney spears", "paris hilton", "spongebob", "norrköping" }; 

  for(std::string testword : forbidden)
    {
      if(tmp.find(testword) != std::string::npos)
        return true;
    }
  return false;
}

std::string hostExtract(std::string const & requestmsg)
{
  std::string tmp;
  for( int x = requestmsg.find("Host:") + 6; requestmsg.at(x+1) != '\n'; x++) // Extract Hostname
    tmp.push_back(requestmsg.at(x));
  return tmp;
}

bool findIfText(std::string const & webpage) //returns false if no filter!
{
  if(webpage.find("Content-Type: text/", 0) != std::string::npos) // text ska filtreras
      return true;
  return false;
}

bool findIfGzip(std::string const & webpage)
{
  if(webpage.find("Content-Encoding: gzip", 0) != std::string::npos)  // gzip ska ej filtreras (kan va gzip och text samtidigt)
      return true;
  return false;
}

void modifyrequest(std::string & requestmsg)
{
  size_t tmp = requestmsg.find("keep-alive", 0);
  //size_t tmp_2 = requestmsg.find("Keep-Alive", 0);
  size_t post = requestmsg.find("POST", 0);

  if(tmp != std::string::npos)
      requestmsg.replace(tmp,10, "close");
  /*if(tmp_2 != std::string::npos)
    requestmsg.replace(tmp_2,10, "close");*/
  if(post != std::string::npos)
    {
      std::cout << "POST REQUEST? I DONT TINK SÅ" << std::endl;
      exit(1);
    }
}

int getLength(std::string & webpage)
{
  std::string tmp;
  size_t tmp_pos = webpage.find("Content-Length:", 0);
  if(tmp_pos != std::string::npos)                             // If there exists Content-Length in the HTTP OK response
    {
      for( int x = tmp_pos + 16; webpage.at(x+1) != '\n'; x++) // Extract Length from response
	tmp.push_back(webpage.at(x));
      
      int length = std::stoi(tmp);                             // transform string to int. May throw error ocassionally... still loads the page though
      
      if(length == 0)
	{
	  std::cout << "CONTENT LENGTH: 0!??? U CANT FOOL ME!" << std::endl;
	  return 200000;
	}
      return length;
    }
  return 200000; 
}
