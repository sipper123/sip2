#ifndef _CUDPSOCK_H_
#define _CUDPSOCK_H_

#include <stdio.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>

class CUdpSock
{
public:
  CUdpSock();
  ~CUdpSock();
  int  Initialize(char* remote_host, int remote_port, int local_port);
  int  Initialize(int local_port);
  int  SetRemoteInfo(char* remote_host, int remote_port);
  int  ClearRemoteInfo();
  int  RecvFrom(unsigned char* buffer, int* n);
  int  SendTo(unsigned char* buffer, int* n);
  int  m_socket;
  bool m_TwoWayEstablished;
private:
  struct sockaddr_in m_SockAddr;
  struct sockaddr_in m_my_sa;

};

#endif

