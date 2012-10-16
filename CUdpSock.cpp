
#include "CUdpSock.h"

////////////////////////////////////////////////////////////////////////////
CUdpSock::CUdpSock()
{
  memset(&m_SockAddr, 0, sizeof(struct sockaddr_in));
  m_socket = -1;
  m_TwoWayEstablished = false;
}
////////////////////////////////////////////////////////////////////////////
int CUdpSock::Initialize(int local_port)
{
  m_TwoWayEstablished = false;

  if (m_socket >= 0)
  {
    close(m_socket);
    m_socket = -1;
  }
  
  m_socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (m_socket<0)
  {
    printf("Error opening udp socket\n");
    return -1;
  }

  int reuse= 1;
  if (setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))< 0) 
  {
    printf("setsockopt() failed\n");
    return -1;
  }

  //Initialize my sockaddr and bind it to the socket. 
  //Once bound, I can recvfrom() data on the socket, and sendto() on it.
  memset(&m_my_sa, 0, sizeof(struct sockaddr_in));

  m_my_sa.sin_family      = AF_INET;
  m_my_sa.sin_port        = htons(local_port);
  m_my_sa.sin_addr.s_addr = INADDR_ANY;
  if (-1 == bind(m_socket, 
                 (struct sockaddr *)&m_my_sa, sizeof(struct sockaddr_in)))
  {
    printf("Error at bind\n");
    return -1;
  }
}
////////////////////////////////////////////////////////////////////////////
int CUdpSock::SetRemoteInfo(char* remote_host, int remote_port)
{
  //If this has been done, no need to proceed.
  if (m_TwoWayEstablished) return 0;

  if (remote_host==NULL)
  {
    printf("Invalid Host Name!\n");
    return -1;
  }
  if (m_socket <0) return -1;
  //Initialize other (receiver's) sockaddr information.
  //This will be later used for "sendto()"
  memset(&m_SockAddr, 0, sizeof(struct sockaddr_in));
  m_SockAddr.sin_family = AF_INET;
  m_SockAddr.sin_port   = htons(remote_port);
  struct hostent *he    = gethostbyname(remote_host);
  if (!he)
  {
    printf("Cannot resolve host\n");
    return -1;
  }
  m_SockAddr.sin_addr   = *((struct in_addr *)he->h_addr);
    
  m_TwoWayEstablished = true;
  return 0;
}
int CUdpSock::ClearRemoteInfo()
{
  memset(&m_SockAddr, 0, sizeof(struct sockaddr_in));
  m_TwoWayEstablished = false;
  return 0;

}

////////////////////////////////////////////////////////////////////////////
int CUdpSock::Initialize(char* remote_host, int remote_port, int local_port)
{
  if (remote_host==NULL)
  {
    printf("Invalid Host Name!\n");
    return -1;
  }

  if (m_socket >= 0)
  {
    close(m_socket);
    m_socket = -1;
  }
  
  m_socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (m_socket<0)
  {
    printf("Error opening udp socket\n");
    return -1;
  }
  int reuse= 1;
  if (setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))< 0)
  {
    printf("setsockopt() failed\n");
    return -1;
  }

  //Initialize other (receiver's) sockaddr information.
  //This will be later used for "sendto()"
  memset(&m_SockAddr, 0, sizeof(struct sockaddr_in));
  m_SockAddr.sin_family = AF_INET;
  m_SockAddr.sin_port   = htons(remote_port);
  struct hostent *he    = gethostbyname(remote_host);
  if (!he)
  {
    printf("Cannot resolve host\n");
    return -1;
  }
  m_SockAddr.sin_addr   = *((struct in_addr *)he->h_addr);
 
  //Initialize my sockaddr and bind it to the socket. 
  //Once bound, I can recvfrom() data on the socket, and sendto() on it.
  memset(&m_my_sa, 0, sizeof(struct sockaddr_in));

  m_my_sa.sin_family      = AF_INET;
  m_my_sa.sin_port        = htons(local_port);
  m_my_sa.sin_addr.s_addr = INADDR_ANY;
  if (-1 == bind(m_socket,
                 (struct sockaddr *)&m_my_sa, sizeof(struct sockaddr_in)))
  {
    printf("Error at bind\n");
    return -1;
    //fcntl(m_socket, F_SETFL, O_NONBLOCK);
  }

  m_TwoWayEstablished = true;
  return 0;
}

////////////////////////////////////////////////////////////////////////////
CUdpSock::~CUdpSock()
{
  if (m_socket>=0)
   close(m_socket);
}

////////////////////////////////////////////////////////////////////////////
int CUdpSock::SendTo(unsigned char* buf, int*len)
{
   //Do not proceed if two way is established. this means the m_SockAddr hasn't been initialized yet.
   if (!m_TwoWayEstablished) 
   {
      printf("Connection hasn't been established yet..!\n");
      return 0;
   }

   int buf_index;     /* Points to the next byte to be transmitted */
   int bytes_to_send; /* number of bytes left to send */
   int n;             /* sendto() return value */

   if ((buf == NULL) || (len == NULL)) {
      return(-1);
   }

   bytes_to_send = *len;
   buf_index = 0;
   n = 0;

   /* Keep sending until the entire buffer has been transmitted */
   while (bytes_to_send > 0) 
   {
      n = sendto(m_socket, buf + buf_index, bytes_to_send, 0,
                 (struct sockaddr *)&m_SockAddr, sizeof(struct sockaddr));
      if (n < 0)
         break;

      bytes_to_send -= n; /* Decrement the number of bytes left to send */
      buf_index += n;     /* Update the buffer index */
   }

   *len = buf_index;      /* pass back number of bytes actually tx here */

   if (n < 0)
      return(-1);
   else
      return(0);

}

////////////////////////////////////////////////////////////////////////////
int CUdpSock::RecvFrom(unsigned char* buf, int* len)
{
   if ((buf == NULL) || (len == NULL)) 
   {
      return(-1);
   }

   struct sockaddr   sa;     /* sockaddr to hold incoming packet's sender info */
   socklen_t fromlen = sizeof(struct sockaddr);
   int n = recvfrom(m_socket, buf, *len, 0, (struct sockaddr *)&sa, (socklen_t *)&fromlen);
   if (n<0) 
   { 
     *len = 0;
     return -1;
   }
   *len = n;
   return 0;
   
   //int buf_index     = 0;    /* Points to the next buffer read entry */
   //int bytes_to_read = *len; /* number of bytes left to read */
   //int n             = 0;    /* recvfrom() return value */
   //struct sockaddr   sa;     /* sockaddr to hold incoming packet's sender info */

   /* Keep reading until all data has been received */
   //while (bytes_to_read > 0) 
   //{
   //   socklen_t fromlen = sizeof(struct sockaddr);
   //   n = recvfrom(m_socket, buf + buf_index, bytes_to_read, 0,
   //                (struct sockaddr *)&sa, (socklen_t *)&fromlen);

      /* Check for errors from the recvfrom() call */
   //   if (n < 0)
   //      break;

   //   bytes_to_read -= n;   /* Update the bytes left to read */
   //   buf_index += n;       /* Update the buffer index */
   //}

   //*len = buf_index;      /* pass back number of bytes actually rx here */

   //if (n < 0)
   //   return(-1);
   //else
   //   return(0);

}


