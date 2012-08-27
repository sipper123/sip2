#ifndef _TCSIP_H_
#define _TCSIP_H_

#include <sys/ioctl.h>
#include <sys/unistd.h>
#include <sys/fcntl.h>
#include <sys/soundcard.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <memory.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>

#include "CUdpSock.h"
#include "adpcm.h"

#define RTP_PORT  5004
#define RTCP_PORT 5005
#define SIP_PORT  5060

/* SIP states */
#define SIP_IDLE            0
#define SIP_CONX_PENDING    1
#define SIP_CONNECTED       2
#define SIP_DISCONX_PENDING 3

/* SIP modes*/
#define SIP_NONE           0
#define SIP_CLIENT         1
#define SIP_SERVER         2

/* buffer sizes */
#define MAX_STR_LEN        256
#define BUFFER_LEN         1240 
#define MAX_TRIES          10
#define ID_LEN             32

/* SIP message types*/
#define MSG_INVITE         1
#define MSG_BYE            2
#define MSG_ACK            3
#define MSG_RESPONSE       4

/* Typed-in message types */
#define CMD_INVITE         11
#define CMD_ACCEPT         12
#define CMD_REJECT         13
#define CMD_BYE            14
#define CMD_EXIT           15

#define INVITE_HDR   ("INVITE sip:%s SIP/2.0\nVia: SIP/2.0/UDP %s:%d\nFrom: %s <sip:%s>\nTo: %s <sip:%s>\nCall-ID: %s\nCSeq: %d INVITE\nContent-Type: application/sdp\nContent-Length: %d\n\n")
#define INVITE_MSG   ("v=0\no=%s %u %u IN IP4 %s\ns=-\nc=IN IP4 %s\nt=0 0\nm=audio %d RTP/AVP 0\na=rtpmap:0 ADPCM/8000\n")
#define ACK_HDR      ("ACK sip:%s SIP/2.0\nVia: SIP/2.0/UDP %s:%d\nFrom: %s <sip:%s>\nTo: %s <sip:%s>\nCall-ID: %s\nCSeq: %d ACK\nContent-Length: 0\n")
#define BYE_HDR      ("BYE sip:%s SIP/2.0\nVia: SIP/2.0/UDP %s:%d\nFrom: %s <sip:%s>\nTo: %s <sip:%s>\nCall-ID: %s\nCSeq: %d BYE\nContent-Length: 0\n")
#define RESPONSE_MSG ("SIP/2.0 %d %s\n%s\n\n%s\n")  //the last %s is optional; if not needed use "\0", and set len to len-1

#define TIMEOUTTIME         300

typedef unsigned long DWORD;

void  HexOut(UCHAR* s, int len);
int   CheckParams(int argc, char* argv[]);
void  Usage(char* s);
bool  InsertRandomDelayLoss();
int   AudioInitialize(int action);
void  GenerateSSRC();
void  UpdateStatOnPktRecv(UCHAR*, int);
void  CreateRTPBuffer(UCHAR* RTP_buffer, int* RTP_len, UCHAR* encoded_buffer, int encoded_len);
void  InitHeaderData();
DWORD gettime(void);
WORD  IncrementSeq();
void generateUniqueID(void);
void dump_state();
int getmaxfd(int a, int b, int c);


/* RTP */
typedef struct _RTPFIXEDHD
{
  _RTPFIXEDHD(void)
  { memset((void*)this, 0, sizeof(this)); };

  DWORD Version   :2;
  DWORD Padding   :1;
  DWORD Extension :1;
  DWORD CSRCCount :4;
  DWORD Marker    :1;
  DWORD PayLdType :7;
  DWORD SeqNum    :16;
  DWORD TimeStamp;
  DWORD SSRC;
} RTPFIXEDHEADER, *PRTPFIXEDHEADER;

typedef struct _PAYLOADHD
{
  _PAYLOADHD(void)
  { memset((void*)this, 0, sizeof(this)); };

  DWORD FirstSample :16;
  DWORD Table2Index : 8;
  DWORD Reserved    : 8;
} PAYLOADHEADER, *PPAYLOADHEADER;

#define RTP_PACKET_SIZE (sizeof(RTPFIXEDHEADER) + sizeof(PAYLOADHEADER) + 80)

/* RTCP */
typedef struct _BASICRTCPHD
{
  _BASICRTCPHD(void)
  { memset((void*)this, 0, sizeof(this)); };

  DWORD Version    : 2;
  DWORD Padding    : 1;
  DWORD ItemCount  : 5;
  DWORD PacketType : 8;
  DWORD Length     :16; 
} BASICRTCPHEADER, *PBASICRTCPHEADER;

typedef struct _RTCPPACK
{
  _RTCPPACK(void)
  { memset((void*)this, 0, sizeof(this)); };

  BASICRTCPHEADER hd;
  DWORD           my_ssrc; 
  DWORD           ot_ssrc;
  DWORD           fraction:8;  
  DWORD           lost:24;
  DWORD           last_seq;   
  DWORD           jitter;        
  DWORD           lsr;          
  DWORD           dlsr;    
} RTCPPACKET, *PRTCPPACKET;

//Data structure for maintaining RTCP statistics
typedef struct _STATDATA
{
  _STATDATA(void)
  { memset((void*)this, 0, sizeof(this)); };

  DWORD  ot_ssrc;  
  WORD   firstSeq;
  WORD   lastSeq;
  DWORD  count;
  int    prev_transit;
  double prev_jitter;

} STATDATA, *PSTATDATA;

//Data structure to maintain the SIP session states
typedef struct _SESDAT
{
  _SESDAT(void)
  { memset((void*)this, 0, sizeof(this)); };

  char  uri[MAX_STR_LEN];
  char  MyHost[MAX_STR_LEN];
  char  RemoteHost[MAX_STR_LEN];
  char  From[MAX_STR_LEN];
  char  FromAdrs[MAX_STR_LEN];
  char  To[MAX_STR_LEN];
  char  ToAdrs[MAX_STR_LEN];
  char  CallID[MAX_STR_LEN];
  char  remoteLineOne[MAX_STR_LEN];
  char  remoteRestHdr[BUFFER_LEN]; 
  char  remoteRestBdy[BUFFER_LEN]; 
} SDATA, *PSDATA;


#endif

















