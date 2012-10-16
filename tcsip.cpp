/*
 * tcsip.cpp
 * The program entry (main) file.
 * The core of the SIP engine is implemented here. 
 * This file uses the CUdpSock and adpcm classes (implemented 
 * in other files) to effect encoding communication etc.
 *
 */

#include "tcsip.h"


/* Globals */
CUdpSock   oSocSIP;         //socket for SIP communication
CUdpSock   oSocRTP;         //socket for RTP communication
ADPCM      oPCM;            //ADPCM encoder/decoder

SDATA           g_SessionData;
RTPFIXEDHEADER  g_RTP_header;

/* states */
bool g_sip_done          = false;      

/* thread for audio session */
bool      g_au_thread_running = false;
pthread_t g_au_thread;

//bool   g_au_started     = false;   //Use this to identify the beginning of sampling. 
                                   // If not set, create SSRC etc. and set this flag. this should reset to false everytime run_rtp resets.
bool   g_1stSampleDone  = false;    //To denote if the first sample is taken for a given RTP session.
int    g_sip_state      = SIP_IDLE;
int    g_sip_mode       = SIP_NONE;

bool   g_simulate_delay = false;
int    g_sip_seq        = 1;
int    g_audio_fd       = -1;
DWORD  g_ssrc           = 0;          //SSRC, calculated once at the start of the program.
DWORD  g_time_offset    = 0;
WORD   g_seq_num        = 0;          //rtp seq num


UCHAR  g_txBuffer[BUFFER_LEN];
UCHAR  g_rxBuffer[BUFFER_LEN];
UCHAR  g___Buffer[BUFFER_LEN];
UCHAR  g_Buffer[RTP_PACKET_SIZE];
char   g_iBuffer[MAX_STR_LEN];
char   g_oBuffer[MAX_STR_LEN];
char   g_UniqueID[ID_LEN];


//////////////////////////////////////////////////////
//open and configure the audio device. returns the
//audio file descriptor.
int AudioInitialize()
{
    //Open the device
    int audio_fd = -1;
    if ((audio_fd = open("/dev/dsp", O_RDWR, 0)) == -1)  
    { 
        printf("Error opening /dev/dsp\n");
        return -1; 
    }  
    //printf("Open success on /dev/dsp\n");

    // Set duplex mode
    if (ioctl(audio_fd, SNDCTL_DSP_SETDUPLEX, 0) == -1) 
    {
        printf("Error at SNDCTL_DSP_SETDUPLEX\n");  
        return -1;
    }
    if (ioctl(audio_fd, SNDCTL_DSP_RESET, 0) == -1)
    {
        printf("Error at SNDCTL_DSP_RESET\n");
        return -1;
    }
    if (ioctl(audio_fd, SNDCTL_DSP_POST, 0) == -1) 
    {
      printf("Error at SNDCTL_DSP_POST\n");
      return -1;
    }

    //Set the format
    int format = AFMT_U16_LE;
    if (ioctl(audio_fd, SNDCTL_DSP_SETFMT, &format)==-1)  
    {
        printf("Error at SNDCTL_DSP_SETFMT\n");  
        return -1;
    }  
    if (format != AFMT_U16_LE)
    {
        printf("AFMT_U16_LE not supported\n");  
        return -1;
    }
    //printf("Set format to AFMT_U16_LE\n");

    //Set the number of channels
    int channels = OPT_MONO;
    if (ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &channels)==-1)
    { 
        printf("Error at SNDCTL_DSP_CHANNELS\n");  
        return -1;
    }
    if (channels != OPT_MONO)
    { 
        printf("Cannot set channels\n");  
        return -1;
    }
    //printf("Set DSP channels to Mono\n");

    //Set the speed
    int speed = 8000;
    if (ioctl(audio_fd, SNDCTL_DSP_SPEED, &speed)==-1)  
    { 
        printf("Error at SNDCTL_DSP_SPEED\n");  
        return -1;
    }  
    //printf("Speed set to %dHz\n", speed);  

    if (abs(speed-8000)>200)  
    {  
        printf("Cannot set speed\n");  
        return -1;
    }  

    return audio_fd;
}

/////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////
//When an audio packet is available at the RTP socket
//this function is called.
int ReceiveRTPBufferAndPlay()
{
  UCHAR recvBuffer[82];
  UCHAR decoded_buffer[322];
  int   len=82,
        decoded_len=0,
        written = 0,
        RTP_len = RTP_PACKET_SIZE;

  memset(recvBuffer, 0, 82);
  memset(decoded_buffer, 0, 322);
  memset(g_Buffer, 0, RTP_PACKET_SIZE);

  if (oSocRTP.RecvFrom(g_Buffer, &RTP_len) < 0)
     return 0;

  if (RTP_len <=0 ) return 0;
  //printf("Received %d from network\n", RTP_len);

  memcpy(&g_RTP_header, g_Buffer, sizeof(RTPFIXEDHEADER));
  PPAYLOADHEADER ph = (PPAYLOADHEADER)(g_Buffer+sizeof(RTPFIXEDHEADER));
  UCHAR* data = g_Buffer + sizeof(RTPFIXEDHEADER) + sizeof(PAYLOADHEADER);
  int dataLen = RTP_len - (sizeof(RTPFIXEDHEADER) + sizeof(PAYLOADHEADER));

  oPCM.InitializeDecoder(data, dataLen, ph->FirstSample, ph->Table2Index);

  oPCM.Decode(decoded_buffer, &decoded_len);

  if (write(g_audio_fd, decoded_buffer, decoded_len)<0)
  {
     printf("Error writing to device\n");
     g_au_thread_running = false;
     //g_au_started = true;
  }

  oPCM.DeInitializeDecoder();

  return 0;
}
//////////////////////////////////////////////////////////////////
//When select() returns from audio file descriptor, this function
//is called
int CaptureAudioAndSend()
{
  UCHAR tmpBuffer[322];
  UCHAR audio_buffer[322];
  UCHAR encoded_buffer[82];
  int   len         = 0,
        tmpLen      = 0,
        encoded_len = 82,
        num_tries   = 0,
        RTP_len     = RTP_PACKET_SIZE;

  memset(audio_buffer,   0, 322);
  memset(tmpBuffer,      0, 322);
  memset(encoded_buffer, 0, 82);
  memset(g_Buffer,       0, RTP_PACKET_SIZE);

  tmpLen = read(g_audio_fd, tmpBuffer, 322);
  if (tmpLen<0) return -1;
  if (tmpLen == 0) return 0;
  memcpy(audio_buffer, tmpBuffer, tmpLen);
  len = len + tmpLen;
  while (len<322 && num_tries++ < 50)
  {
    memset(tmpBuffer,0,322);
    tmpLen = read(g_audio_fd, tmpBuffer, 322-len);
    if (tmpLen<0) return -1;
    if (tmpLen == 0) return 0;
    memcpy(audio_buffer+len, tmpBuffer, tmpLen);
    len = len + tmpLen;
  }

  // Do this so that the SSRC, initial sequence number etc. will be done once per RTP session
  if (!g_1stSampleDone)
  {
    InitHeaderData();
    g_1stSampleDone = true;
  }

  UCHAR *data=audio_buffer;
  int dataLen=len;

  oPCM.InitializeEncoder(data, dataLen);

  oPCM.Encode(encoded_buffer, &encoded_len);

  CreateRTPBuffer(g_Buffer, &RTP_len, encoded_buffer, encoded_len);

  if (!InsertRandomDelayLoss()) //Check we should simulate dropped pkt. If not send.
  {
    oSocRTP.SendTo(g_Buffer, &RTP_len);
    //printf("Sent %d bytes\n", RTP_len);
  }

  oPCM.DeInitializeEncoder();

  return 0;
}


void *AudioFunction(void * p)
{
  fd_set fds2;

  //open the device for the appropriate action
  if ((g_audio_fd = AudioInitialize()) < 0)
      exit(1);

  while (g_au_thread_running)
  {
    FD_ZERO(&fds2);
    FD_SET(g_audio_fd, &fds2);
    FD_SET(oSocRTP.m_socket, &fds2);
    int max_fd = oSocRTP.m_socket;
    if (g_audio_fd > max_fd) max_fd = g_audio_fd; 

    if (select(max_fd+1, &fds2, NULL, NULL, NULL) < 0)
    {
      printf("Error on on select\n");
      exit(1);
    }

    if (FD_ISSET(oSocRTP.m_socket, &fds2))
    {
      if (ReceiveRTPBufferAndPlay()<0) continue;
    }
   
    if (FD_ISSET(g_audio_fd, &fds2))
    {
      if (CaptureAudioAndSend()<0) continue;
    }
  }
  close(g_audio_fd);
  g_audio_fd = -1;
}
/////////////////////////////////////////////////////
int StartAudioThread()
{
  if (0==pthread_create(&g_au_thread, NULL, AudioFunction, (void*)NULL))
  {
    g_au_thread_running = true;
    return 0;
  }
  return -1;
}
/////////////////////////////////////////////////////
int StopAudioThread()
{
  if (g_au_thread_running)
  {
    g_1stSampleDone = false;
    g_au_thread_running = false;
    pthread_join(g_au_thread, NULL);
  }
  return 0;
}
/////////////////////////////////////////////////////


//////////////////////////////////////////////////////
//Check the command line arguments. If everything is 
//okay then return the action to be performed.
// Usage: %s -x 
int CheckParams(int argc, char* argv[])
{
    if (argc>1)
    {
      if (!strcmp(argv[1], "-x"))
        g_simulate_delay = true;
    }
    return 0;   
}

//Check the first line of the message
//If it is INVITE, ACK or BYE, returns the corresponding number and the host
//If it starts with SIP, then return the status message (eg: "200 OK")
//IN:  char* s   : buffer whose first line we will examine
//OUT: char* msg : buffer that will be filled in with hostname or status message
//RET: the numeric code of the message type
int GetSockMsgType(char* s, char* msg)
{
  int ret = -1;
  if (!s || !msg) return ret;

  char str[MAX_STR_LEN]={0};
  msg[0] = '\0';
  char* sep=" \n";
  char* p  = str;
  char* s1=NULL, *s2=NULL, *s3=NULL;
  int i;
  for (i=0;(i<MAX_STR_LEN)&&(s[i]!='\n')&&(s[i]!='\0');i++)
    str[i] = s[i];
  str[i]='\0';

  if (p) s1 = strsep(&p, " \n");
  if (p) s2 = strsep(&p, " \n");
  if (p) s3 = strsep(&p, " \n");

  if (s1 && s2 && s3)
  {
    if (strncmp(s1,"SIP", 3))
    {
           if (!strcmp(s1, "INVITE")) ret = MSG_INVITE;
      else if (!strcmp(s1, "BYE"))    ret = MSG_BYE;
      else if (!strcmp(s1, "ACK"))    ret = MSG_ACK;
      strsep(&s2, "@");
      if (s2)
           strncpy(msg, strsep(&s2, ">\n"), 256);
      else
           msg[0]='\0';
    }
    else
    {
      ret = MSG_RESPONSE;
      strncpy(msg, s2, 256);
      strcat(msg, " ");
      strncat(msg, s3, 256);
    }
    return ret;
  }
}
///////////////////////////////////////////////////
int GetCommandType(char* s, char* msg)
{
  int ret = -1;
  if (!s || !msg) return ret;
  char* p  = s;
  char* s1 = NULL;

  if (p) s1 = strsep(&p, " ");

  if (s1)
  {
          if (!strcmp(s1, "invite")) ret = CMD_INVITE;
     else if (!strcmp(s1, "bye"))    ret = CMD_BYE;
     else if (!strcmp(s1, "accept")) ret = CMD_ACCEPT;
     else if (!strcmp(s1, "reject")) ret = CMD_REJECT;
     else if (!strcmp(s1, "exit"))   ret = CMD_EXIT;
  }

  if (ret == CMD_INVITE)
  {
      if (p!=NULL) strncpy(msg, p, MAX_STR_LEN);
      else ret = -1;                       // if invite does not have an argument
      if (NULL==strchr(msg,'@')) ret = -1;  // if there is no @ in the argument
  }

  return ret;
}
///////////////////////////////////////////////////
void generateUniqueID(void)
{
  char *str, *ptr;
  int len = 0, ins = 0, i, j;

  ptr = g_UniqueID;
  memset(ptr, 0x00, ID_LEN);

  for(i = 0, j = 0; i < 16; i++) 
  {
    sprintf(ptr + j, "%02x", (unsigned int)lrand48());
    j += 2;
    if ( i == 3 || i == 5 || i == 7 || i == 9) 
    {
      *(ptr+j) = '-';
      ++j;
    }
  }
  g_UniqueID[j] = '\0';
  return;
}
///////////////////////////////////////////////////
void CreateSessionData(char * user_at_host)
{
  char myhost[MAX_STR_LEN]={0};
  char username[MAX_STR_LEN]={0};

  gethostname(myhost, MAX_STR_LEN);
  cuserid(username);
  generateUniqueID();

  sprintf(g_SessionData.uri, "sip:%s", user_at_host);
  sprintf(g_SessionData.CallID, "%s@%s", g_UniqueID, myhost);
  sprintf(g_SessionData.MyHost, "%s", myhost);
  sprintf(g_SessionData.From, "%s", username, myhost);
  sprintf(g_SessionData.FromAdrs, "%s@%s", username, myhost);
  sprintf(g_SessionData.ToAdrs, "%s", user_at_host);
  sprintf(g_SessionData.To, "%s", strsep(&user_at_host, "@"));
  sprintf(g_SessionData.RemoteHost, "%s", user_at_host);    

}
void ClearSessionData()
{
  memset(&g_SessionData, 0, sizeof(SDATA));
}

//Triggered by a INVITE message. So parse data as if parsing an INVITE message
void CopySessionData(UCHAR * data)
{
  if (!data) return;
  memset(&g_SessionData, 0, sizeof(SDATA));
  char  temp[BUFFER_LEN]={0};
  strncpy(temp, (char*)data, BUFFER_LEN);
  char* pData = temp;
  char* pLineOne = strsep(&pData, "\n");
  if (!pData || !pLineOne) return;

  char* pBody = strstr(pData, "\n\n");             //Body starts after 2 \n\n
  if (pBody) 
  {
    *pBody = '\0';                                 //Do this so that pData string ends here
    pBody+=2;
  }

  strcpy(g_SessionData.remoteLineOne, pLineOne);
  strcpy(g_SessionData.remoteRestHdr, pData);

  /* 
     Now we have remoteLineOne and remoteRestHdr properly initialized 
     We need to modify remoteRestHdr though!   
  */

  // First parse my hostname from line_1
  char* myHost__=NULL;                             // my host name as reported by the other party
  strsep(&pLineOne, "@");
  if (pLineOne)
    myHost__ = strsep(&pLineOne, " ");

  // Then read the same from the system
  gethostname(g_SessionData.MyHost, MAX_STR_LEN);  // my host name, as obtained by me

  //Compare to maek sure they match!

  //create a dummy uri for the other guy
  strcpy(g_SessionData.uri, "sip:someone@somewhere.com");

  //get remote host name
  // from "Via: SIP/2.0/UDP client.atlanta.example.com:5060"
  // i am assuming Via is always the second line
  if (!strncmp(pData, "Via", 3))
  {
    char* pRemHost=NULL;
    strsep(&pData, " ");
    if (pData)
      strsep(&pData, " ");
    if (pData)
      pRemHost=strsep(&pData, ":");
    if (pRemHost)
    {
      strncpy(g_SessionData.RemoteHost, pRemHost, MAX_STR_LEN);
      strncpy(g_SessionData.uri, "sip:someone@%s", MAX_STR_LEN);
    }
  }

  // Finally, go through the pBody, and replace the sendername
  if (!pBody) return;

  char username[MAX_STR_LEN]={0};
  cuserid(username);

  long d1=0, d2=0;
  char* po = strstr(pBody, "o=");     // searching in "o=bob 2890844527 2890844527 IN IP4 client.biloxi.example.com"
  if (po) po+=2;
  strsep(&po, " ");
  char* sd1=NULL, *sd2=NULL;
  if (po)  sd1 = strsep(&po, " ");
  if (po)  sd2 = strsep(&po, " ");
  if (sd1 && sd2)
  {
    d1 = atol(sd1); d1++;
    d2 = atol(sd2); d2++;
  }
  if (po) strsep(&po, "\n");
  if (po)
  {
    sprintf(g_SessionData.remoteRestBdy, "v=0\no=%s %u %u IN IP4 %s\n%s\n", username, d1, d2, g_SessionData.MyHost, po);
  }

  /* now we have remoteLineOne, remoteRestHdr, and remoteRestBdy properly initialized */
  /* 
      Need to fill in these variables sometime. Should I?

  char  From[MAX_STR_LEN];
  char  FromAdrs[MAX_STR_LEN];
  char  To[MAX_STR_LEN];
  char  ToAdrs[MAX_STR_LEN];
  char  Call_ID[MAX_STR_LEN];
  */

}
///////////////////////////////////////////////////
//This function implements the state diagram for
//user inputs
int ProcessUserCommand(char* command, char* result)
{
  if (!(command && result)) return -1;
  memset(result, 0, MAX_STR_LEN);

  int  cmdType = 0;
  char msgExtra[MAX_STR_LEN];
  cmdType = GetCommandType(command, msgExtra);
  if (cmdType < 0)
  {
    strcpy(result,"Please use one of the following commands:\n"
                  "       invite user@host  : To initiate a session with host\n"
                  "       accept            : To accept a request from remote host\n"
                  "       reject            : To reject a request from remote host\n"
                  "       bye               : To terminate an open session\n"
                  "       exit              : To terminate the program\n");
    return -1;
  }

  /* change the mode only if it is none; otherwise leave it as it is */
  if (g_sip_mode==SIP_NONE && cmdType==CMD_INVITE)
      g_sip_mode =SIP_CLIENT;

  /* client */
  if (g_sip_mode == SIP_CLIENT)
  {
    if (g_sip_state==SIP_IDLE)
    {
      if (cmdType==CMD_INVITE)
      { //Client at idle receives invite user@host: createSession;SetRemote sockaddr in oSocSIP, cook buffer and send.
        CreateSessionData(msgExtra);
         
        if (oSocSIP.SetRemoteInfo(g_SessionData.RemoteHost, SIP_PORT)<0)
        {
          strcpy(result, "Invalid host entered");
          return -1;
        }
        sprintf((char*)g___Buffer, INVITE_MSG, g_SessionData.From, (DWORD)lrand48(), (DWORD)lrand48(), g_SessionData.MyHost, g_SessionData.MyHost, SIP_PORT);
        int c_len = strlen((char*)g___Buffer);
        sprintf((char*)g_txBuffer, INVITE_HDR, g_SessionData.ToAdrs, g_SessionData.MyHost, SIP_PORT, g_SessionData.From, g_SessionData.FromAdrs, g_SessionData.To, g_SessionData.ToAdrs, g_SessionData.CallID, ++g_sip_seq, c_len);
        strcat((char*)g_txBuffer, (char*)g___Buffer);
        c_len = strlen((char*)g_txBuffer);
        oSocSIP.SendTo(g_txBuffer, &c_len);

        g_sip_state = SIP_CONX_PENDING;
      }
      else if (cmdType==CMD_ACCEPT)
      {
        strcpy(result, "Nothing to accept");
        return -1;
      }
      else if (cmdType==CMD_REJECT)
      {
        strcpy(result, "Nothing to reject");
        return -1;
      }
      else if (cmdType==CMD_BYE)
      {
        strcpy(result, "No active session to close");
        return -1;
      }
      else if (cmdType==CMD_EXIT)
      {
         g_sip_done = true;  
         //g_run_rtp = false;
         StopAudioThread();
         return 0;
      }
    }
    else if (g_sip_state==SIP_CONX_PENDING)
    {
      if (cmdType==CMD_INVITE)
      {
        strcpy(result, "Connection already in progress. Cannot invite now.");
        return -1;
      }
      else if (cmdType==CMD_ACCEPT)
      {
        strcpy(result, "Nothing to accept");
        return -1;
      }
      else if (cmdType==CMD_REJECT)
      {
        strcpy(result, "Nothing to reject");
        return -1;
      }
      else if (cmdType==CMD_BYE)
      {
        strcpy(result, "Connection in progress. Please try again");
        return -1;
      }
      else if (cmdType==CMD_EXIT)
      {
        strcpy(result, "Connection in progress. Please try again");
        return -1;
      }
    }
    else if (g_sip_state==SIP_CONNECTED)
    {
      if (cmdType==CMD_INVITE)
      {
        strcpy(result, "You are already connected. Please close this session (with bye) first before initiating a new session.");
        return -1;
      }
      else if (cmdType==CMD_ACCEPT)
      {
        strcpy(result, "You are already connected. Nothing to accept");
        return -1;
      }
      else if (cmdType==CMD_REJECT)
      {
        strcpy(result, "You are already connected. Nothing to reject");
        return -1;
      }
      else if (cmdType==CMD_BYE)
      {
        sprintf((char*)g_txBuffer, BYE_HDR, g_SessionData.ToAdrs, g_SessionData.MyHost, SIP_PORT, g_SessionData.From, g_SessionData.FromAdrs,
                                     g_SessionData.To, g_SessionData.ToAdrs, g_SessionData.CallID, ++g_sip_seq);

        int c_len = strlen((char*)g_txBuffer);
        oSocSIP.SendTo(g_txBuffer, &c_len);
        //g_run_rtp = false;
        StopAudioThread();
        //g_au_started = true;
        g_sip_state = SIP_DISCONX_PENDING;
      }
      else if (cmdType==CMD_EXIT)
      {
        strcpy(result, "You are currently connected. Please close this session (with bye) first, and then exit.");
        return -1;
      }
    }
    else if (g_sip_state==SIP_DISCONX_PENDING)
    {
      if (cmdType==CMD_INVITE)
      {
        strcpy(result, "Disconnection in progress. Please try again.");
        return -1;
      }
      else if (cmdType==CMD_ACCEPT)
      {
        strcpy(result, "Disconnection in progress. Nothing to accept.");
        return -1;
      }
      else if (cmdType==CMD_REJECT)
      {
        strcpy(result, "Disconnection in progress. Nothing to reject.");
        return -1;
      }
      else if (cmdType==CMD_BYE)
      {
        strcpy(result, "Disconnection in progress. Please try again.");
        return -1;
      }
      else if (cmdType==CMD_EXIT)
      {
        strcpy(result, "Disconnection in progress. Please try again.");
        return -1;
      }
    }
  }
  else
  /* server mode */
  if (g_sip_mode == SIP_SERVER)
  {
    if (g_sip_state==SIP_IDLE)
    {
      if (cmdType==CMD_INVITE)
      {
        printf("Invalid condition in the state machine!\n");
        return -1;
      }
      else if (cmdType==CMD_ACCEPT)
      {
        strcpy(result, "Nothing to accept");
        return -1;
      }
      else if (cmdType==CMD_REJECT)
      {
        strcpy(result, "Nothing to reject");
        return -1;
      }
      else if (cmdType==CMD_BYE)
      {
        strcpy(result, "No active session to close");
        return -1;
      }
      else if (cmdType==CMD_EXIT)
      {
         g_sip_done = true;  
         //g_run_rtp = false;
         StopAudioThread();
         return 0;
      }
    }
    else if (g_sip_state==SIP_CONX_PENDING)
    {
      if (cmdType==CMD_INVITE)
      {
        strcpy(result, "Connection already in progress. Cannot invite now.");
        return -1;
      }
      else if (cmdType==CMD_ACCEPT)
      {
        sprintf((char*)g_txBuffer, RESPONSE_MSG, 200, "OK", g_SessionData.remoteRestHdr, g_SessionData.remoteRestBdy);
        int c_len = strlen((char*)g_txBuffer);
        oSocSIP.SendTo(g_txBuffer, &c_len);
      }
      else if (cmdType==CMD_REJECT)
      {
        sprintf((char*)g_txBuffer, RESPONSE_MSG, 603, "Decline", g_SessionData.remoteRestHdr, "\0");
        int c_len = strlen((char*)g_txBuffer);
        c_len--;
        oSocSIP.SendTo(g_txBuffer, &c_len);
        g_sip_state = SIP_IDLE;
        g_sip_mode  = SIP_NONE;
      }
      else if (cmdType==CMD_BYE)
      {
        printf("Invalid condition in the state machine!\n");
        return -1;
      }
      else if (cmdType==CMD_EXIT)
      {
        strcpy(result, "Connection in progress. Please try again");
        return -1;
      }
    }
    else if (g_sip_state==SIP_CONNECTED)
    {
      if (cmdType==CMD_INVITE)
      {
        strcpy(result, "You are already connected. Please close this session (with bye) first before initiating a new session.");
        return -1;
      }
      else if (cmdType==CMD_ACCEPT)
      {
        strcpy(result, "You are already connected. Nothing to accept");
        return -1;
      }
      else if (cmdType==CMD_REJECT)
      {
        strcpy(result, "You are already connected. Nothing to reject");
        return -1;
      }
      else if (cmdType==CMD_BYE)
      {
        /*
           This is what i actually need to do. but resorting to short cut @@@@
        sprintf(g_txBuffer, BYE_HDR, g_SessionData.ToAdrs, g_SessionData.MyHost, g_SessionData.From, g_SessionData.FromAdrs,
                                     g_SessionData.To, g_SessionData.ToAdrs, g_SessionData.CallID, ++g_sip_seq);
        */
        sprintf((char*)g_txBuffer, "BYE %s SIP/2.0\n%s\n", g_SessionData.uri, g_SessionData.remoteRestHdr);
        int c_len = strlen((char*)g_txBuffer);
        oSocSIP.SendTo(g_txBuffer, &c_len);
        // g_run_rtp = false;
        StopAudioThread();
        //g_au_started= true;
        g_sip_state = SIP_DISCONX_PENDING;
      }
      else if (cmdType==CMD_EXIT)
      {
        strcpy(result, "You are currently connected. Please close this session (with bye) first, and then exit.");
        return -1;
      }
    }
    else if (g_sip_state==SIP_DISCONX_PENDING)
    {
      if (cmdType==CMD_INVITE)
      {
        strcpy(result, "Disconnection in progress. Please try again.");
        return -1;
      }
      else if (cmdType==CMD_ACCEPT)
      {
        strcpy(result, "Disconnection in progress. Nothing to accept.");
        return -1;
      }
      else if (cmdType==CMD_REJECT)
      {
        strcpy(result, "Disconnection in progress. Nothing to reject.");
        return -1;
      }
      else if (cmdType==CMD_BYE)
      {
        strcpy(result, "Disconnection in progress. Please try again.");
        return -1;
      }
      else if (cmdType==CMD_EXIT)
      {
        strcpy(result, "Disconnection in progress. Please try again.");
        return -1;
      }
    }
  }
  else
  if (g_sip_mode == SIP_NONE)
  {
    if( g_sip_state == SIP_IDLE)
    {
      if (cmdType==CMD_ACCEPT)
      {
        strcpy(result, "Nothing to accept");
        return 0;
      }
      else if (cmdType==CMD_REJECT)
      {
        strcpy(result, "Nothing to accept");
        return 0;
      }
      else if (cmdType==CMD_EXIT)
      {
         g_sip_done = true;  
         //g_run_rtp  = false;
         StopAudioThread();
         return 0;
      }
    }
  }

  return 0;
}
//////////////////////////////////////////////////////
//This fucntion implements the state transitions for
//SIP messages coming from the network.
int ProcessSipMsg(UCHAR* rxStr, char* result)
{
  if (!(rxStr && result)) return -1;
  memset(result, 0, MAX_STR_LEN);

  int  msgType = 0;
  char* msg = (char*)rxStr;
  char msgExtra[MAX_STR_LEN]={0};
  msgType = GetSockMsgType(msg, msgExtra);
  if (msgType < 0)
  {
    //Some non SIP message arrived here. Ignore.
    return -1;
  }

  /* change the mode only if it is none; otherwise leave it as it is */
  if (g_sip_mode==SIP_NONE && msgType==MSG_INVITE)
      g_sip_mode =SIP_SERVER;

  /* client */
  if (g_sip_mode == SIP_CLIENT)
  {
    if (g_sip_state==SIP_IDLE)
    {
      printf("Invalid condition in the state machine!\n");
      return -1;
      /*
      if (msgType==MSG_INVITE)
      {
      } 
      else if (msgType==MSG_BYE)
      {
      } 
      else if (msgType==MSG_ACK)
      {
      } 
      else if (msgType==MSG_RESPONSE)
      {
      } 
      */
    }
    else if (g_sip_state==SIP_CONX_PENDING)
    {
      if (msgType==MSG_INVITE)
      {
        //486 busy
        sprintf((char*)g_txBuffer, RESPONSE_MSG, 486, "Busy", g_SessionData.remoteRestHdr, "\0");
        int c_len = strlen((char*)g_txBuffer);
        c_len--;
        oSocSIP.SendTo(g_txBuffer, &c_len);
      } 
      else if (msgType==MSG_BYE)
      {
        printf("Invalid condition in the state machine!\n");
        return -1;
      } 
      else if (msgType==MSG_ACK)
      {
        printf("Invalid condition in the state machine!\n");
        return -1;
      } 
      else if (msgType==MSG_RESPONSE)
      {
        int code = atoi(msgExtra);
        switch (code/100)
        {
          case 1://1xx message
            strcpy(result, msgExtra);
            return 0;
          case 2:
            {
              sprintf((char*)g_txBuffer, ACK_HDR, g_SessionData.ToAdrs, g_SessionData.MyHost, SIP_PORT, 
                                           g_SessionData.From, g_SessionData.FromAdrs, g_SessionData.To, g_SessionData.ToAdrs, 
                                           g_SessionData.CallID, ++g_sip_seq);
              int c_len = strlen((char*)g_txBuffer);
              oSocSIP.SendTo(g_txBuffer, &c_len);
              g_sip_state = SIP_CONNECTED;
              if (oSocRTP.Initialize(g_SessionData.RemoteHost, RTP_PORT, RTP_PORT) < 0)
              {
                strcpy(result, "Error initializing RTP socket..");
                g_sip_done = true; 
                return -1;
              }
              //g_run_rtp = true;
              StartAudioThread();
          
              strcpy(result, "Remote user has accepted your invite. Starting voice session....");
            }
            break;
          case 3:
            strcpy(result, msgExtra);
            return 0;
          case 4:
            strcpy(result, msgExtra);
            ClearSessionData();
            oSocSIP.ClearRemoteInfo();
            g_sip_state = SIP_IDLE;
            g_sip_mode  = SIP_NONE;
            return -1;
          case 5:
            strcpy(result, msgExtra);
            return 0;
          case 6:
            strcpy(result, msgExtra);
            ClearSessionData();
            oSocSIP.ClearRemoteInfo();
            g_sip_state = SIP_IDLE;
            g_sip_mode  = SIP_NONE;
            return -1;
          default:
            break;
        }
      } 
    }
    else if (g_sip_state==SIP_CONNECTED)
    {
      if (msgType==MSG_INVITE)
      {
        sprintf((char*)g_txBuffer, RESPONSE_MSG, 486, "Busy", g_SessionData.remoteRestHdr, "\0");
        int c_len = strlen((char*)g_txBuffer);
        c_len--;
        oSocSIP.SendTo(g_txBuffer, &c_len);
      } 
      else if (msgType==MSG_BYE)
      {
        //g_run_rtp = false;
        sprintf((char*)g_txBuffer, RESPONSE_MSG, 200, "OK", g_SessionData.remoteRestHdr, g_SessionData.remoteRestBdy);
        int c_len = strlen((char*)g_txBuffer);
        oSocSIP.SendTo(g_txBuffer, &c_len);

        StopAudioThread();

        oSocSIP.ClearRemoteInfo();
        ClearSessionData();
        g_sip_state = SIP_IDLE;
        g_sip_mode  = SIP_NONE;
        strcpy(result, "Remote party says BYE. Closing connection..");
      } 
      else if (msgType==MSG_ACK)
      {
        printf("Invalid state\n"); return -1;
      } 
      else if (msgType==MSG_RESPONSE)
      {
        int code = atoi(msgExtra);
        switch (code/100)
        {
          case 1://1xx message
            strcpy(result, msgExtra);
            return 0;
          case 2:
            {
              //Nothing..
            }
            break;
          case 3:
            strcpy(result, msgExtra);
            return 0;
          case 4:
            strcpy(result, msgExtra);
            ClearSessionData();
            oSocSIP.ClearRemoteInfo();
            g_sip_state = SIP_IDLE;
            g_sip_mode  = SIP_NONE;
            return -1;
          case 5:
            strcpy(result, msgExtra);
            return 0;
          case 6:
            strcpy(result, msgExtra);
            ClearSessionData();
            oSocSIP.ClearRemoteInfo();
            g_sip_state = SIP_IDLE;
            g_sip_mode  = SIP_NONE;
            return -1;
          default:
            break;
        }
      } 
    }
    else if (g_sip_state==SIP_DISCONX_PENDING)
    {
      if (msgType==MSG_INVITE)
      {
        sprintf((char*)g_txBuffer, RESPONSE_MSG, 486, "Busy", g_SessionData.remoteRestHdr, "\0");
        int c_len = strlen((char*)g_txBuffer);
        c_len--;
        oSocSIP.SendTo(g_txBuffer, &c_len);
      } 
      else if (msgType==MSG_BYE)
      {
        //g_run_rtp = false;
        //g_au_started= true;
        sprintf((char*)g_txBuffer, RESPONSE_MSG, 200, "OK", g_SessionData.remoteRestHdr, g_SessionData.remoteRestBdy);
        int c_len = strlen((char*)g_txBuffer);
        oSocSIP.SendTo(g_txBuffer, &c_len);

        StopAudioThread();

        oSocSIP.ClearRemoteInfo();
        ClearSessionData();
        g_sip_state = SIP_IDLE;
        g_sip_mode  = SIP_NONE;
        strcpy(result, "Remote party says BYE. Closing connection..");
      } 
      else if (msgType==MSG_ACK)
      {
        printf("Invalid state\n"); return -1;
      } 
      else if (msgType==MSG_RESPONSE)
      {
        int code = atoi(msgExtra);
        switch (code/100)
        {
          case 1://1xx message
            strcpy(result, msgExtra);
            return 0;
          case 2:
            {
              oSocSIP.ClearRemoteInfo();
              ClearSessionData();
              g_sip_state = SIP_IDLE;
              g_sip_mode  = SIP_NONE;
            }
            break;
          case 3:
            strcpy(result, msgExtra);
            return 0;
          case 4:
            strcpy(result, msgExtra);
            ClearSessionData();
            oSocSIP.ClearRemoteInfo();
            g_sip_state = SIP_IDLE;
            g_sip_mode  = SIP_NONE;
            return -1;
          case 5:
            strcpy(result, msgExtra);
            return 0;
          case 6:
            strcpy(result, msgExtra);
            ClearSessionData();
            oSocSIP.ClearRemoteInfo();
            g_sip_state = SIP_IDLE;
            g_sip_mode  = SIP_NONE;
            return -1;
          default:
            break;
        }
      } 
    }
  }
  else
  /* server */
  if (g_sip_mode == SIP_SERVER)
  {
    if (g_sip_state==SIP_IDLE)
    {
      if (msgType==MSG_INVITE)
      {
        CopySessionData(rxStr);
        oSocSIP.SetRemoteInfo(g_SessionData.RemoteHost, SIP_PORT );

        sprintf((char*)g_txBuffer, RESPONSE_MSG, 180, "Ringing", g_SessionData.remoteRestHdr, "\0");
        int c_len = strlen((char*)g_txBuffer);
        c_len--;
        oSocSIP.SendTo(g_txBuffer, &c_len);
        printf("You have got an incoming call from %s. Do you want to accept? [accept/reject]", g_SessionData.RemoteHost);fflush(stdout);
        g_sip_state = SIP_CONX_PENDING;
        return 0;
      } 
      else if (msgType==MSG_BYE)
      {
        //invalid...
      } 
      else if (msgType==MSG_ACK)
      {
        //invalid..
      } 
      else if (msgType==MSG_RESPONSE)
      {
        //invalid..
      } 
    }
    else if (g_sip_state==SIP_CONX_PENDING)
    {
      if (msgType==MSG_INVITE)
      {
        sprintf((char*)g_txBuffer, RESPONSE_MSG, 486, "Busy", g_SessionData.remoteRestHdr, "\0");
        int c_len = strlen((char*)g_txBuffer);
        c_len--;
        oSocSIP.SendTo(g_txBuffer, &c_len);
      } 
      else if (msgType==MSG_BYE)
      {
        printf("Invalid state\n"); return -1;
      } 
      else if (msgType==MSG_ACK)
      {
        sprintf(result, "Remote user at [%s] confirmed your acceptance. Starting audio communication...", g_SessionData.RemoteHost);
        if (oSocRTP.Initialize(g_SessionData.RemoteHost, RTP_PORT, RTP_PORT) < 0)
        {
           strcpy(result, "Error initializing RTP socket..");
           g_sip_done = true; 
           return -1;
        }
        //g_run_rtp = true;
        StartAudioThread();
        g_sip_state = SIP_CONNECTED;
      } 
      else if (msgType==MSG_RESPONSE)
      {
        int code = atoi(msgExtra);
        switch (code/100)
        {
          case 1://1xx message
            //invalid
            return 0;
          case 2:
            {
              //invalid 
            }
            break;
          case 3:
            //invalid
            return 0;
          case 4:
            strcpy(result, msgExtra);
            ClearSessionData();
            oSocSIP.ClearRemoteInfo();
            g_sip_state = SIP_IDLE;
            g_sip_mode  = SIP_NONE;
            return -1;
          case 5:
            strcpy(result, msgExtra);
            return 0;
          case 6:
            strcpy(result, msgExtra);
            ClearSessionData();
            oSocSIP.ClearRemoteInfo();
            g_sip_state = SIP_IDLE;
            g_sip_mode  = SIP_NONE;
            return -1;
          default:
            break;
        }
      } 
    }
    else if (g_sip_state==SIP_CONNECTED)
    {
      if (msgType==MSG_INVITE)
      {
        sprintf((char*)g_txBuffer, RESPONSE_MSG, 486, "Busy", g_SessionData.remoteRestHdr, "\0");
        int c_len = strlen((char*)g_txBuffer);
        c_len--;
        oSocSIP.SendTo(g_txBuffer, &c_len);
      } 
      else if (msgType==MSG_BYE)
      {
        //g_run_rtp = false;
        StopAudioThread();
        //g_au_started= true;
        sprintf((char*)g_txBuffer, RESPONSE_MSG, 200, "OK", g_SessionData.remoteRestHdr, g_SessionData.remoteRestBdy);
        int c_len = strlen((char*)g_txBuffer);
        oSocSIP.SendTo(g_txBuffer, &c_len);

        oSocSIP.ClearRemoteInfo();
        ClearSessionData();
        g_sip_state = SIP_IDLE;
        g_sip_mode  = SIP_NONE;
        strcpy(result, "Remote party says BYE. Closing connection..");
      } 
      else if (msgType==MSG_ACK)
      {
        //invalid..
      } 
      else if (msgType==MSG_RESPONSE)
      {
        int code = atoi(msgExtra);
        switch (code/100)
        {
          case 1://1xx message
            //invalid
            return 0;
          case 2:
            {
              //invalid 
            }
            break;
          case 3:
            strcpy(result, msgExtra);
            return 0;
          case 4:
            strcpy(result, msgExtra);
            ClearSessionData();
            oSocSIP.ClearRemoteInfo();
            g_sip_state = SIP_IDLE;
            g_sip_mode  = SIP_NONE;
            return -1;
          case 5:
            strcpy(result, msgExtra);
            return 0;
          case 6:
            strcpy(result, msgExtra);
            ClearSessionData();
            oSocSIP.ClearRemoteInfo();
            g_sip_state = SIP_IDLE;
            g_sip_mode  = SIP_NONE;
            return -1;
          default:
            break;
        }
      } 
    }
    else if (g_sip_state==SIP_DISCONX_PENDING)
    {
      if (msgType==MSG_INVITE)
      {
        sprintf((char*)g_txBuffer, RESPONSE_MSG, 486, "Busy", g_SessionData.remoteRestHdr, "\0");
        int c_len = strlen((char*)g_txBuffer);
        c_len--;
        oSocSIP.SendTo(g_txBuffer, &c_len);
      } 
      else if (msgType==MSG_BYE)
      {
        //g_run_rtp = false;
        //g_au_started= true;
        sprintf((char*)g_txBuffer, RESPONSE_MSG, 200, "OK", g_SessionData.remoteRestHdr, g_SessionData.remoteRestBdy);
        int c_len = strlen((char*)g_txBuffer);
        oSocSIP.SendTo(g_txBuffer, &c_len);
        StopAudioThread();

        oSocSIP.ClearRemoteInfo();
        ClearSessionData();
        g_sip_state = SIP_IDLE;
        g_sip_mode  = SIP_NONE;
        strcpy(result, "Remote party says BYE. Closing connection..");
      } 
      else if (msgType==MSG_ACK)
      {
        //invalid..
      } 
      else if (msgType==MSG_RESPONSE)
      {
        int code = atoi(msgExtra);
        switch (code/100)
        {
          case 1://1xx message
            strcpy(result, msgExtra);
            return 0;
          case 2:
            {
              oSocSIP.ClearRemoteInfo();
              ClearSessionData();
              g_sip_state = SIP_IDLE;
              g_sip_mode  = SIP_NONE;
            }
            break;
          case 3:
            strcpy(result, msgExtra);
            return 0;
          case 4:
            strcpy(result, msgExtra);
            ClearSessionData();
            oSocSIP.ClearRemoteInfo();
            g_sip_state = SIP_IDLE;
            g_sip_mode  = SIP_NONE;
            return -1;
          case 5:
            strcpy(result, msgExtra);
            return 0;
          case 6:
            strcpy(result, msgExtra);
            ClearSessionData();
            oSocSIP.ClearRemoteInfo();
            g_sip_state = SIP_IDLE;
            g_sip_mode  = SIP_NONE;
            return -1;
          default:
            break;
        }
      } 
    }
  }

  return 0;
}

//////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
  int  len=0,
       ret=0;

  //command line argument check
  if (CheckParams(argc, argv) < 0)
      exit(0);

  printf("\033[1;32m\n");
  printf("** %s: a simple SIP based talk program **\n", argv[0]);
  printf("** Please enter help to see the command options **\n\n");

  if (oSocSIP.Initialize(SIP_PORT) < 0)
  {
    printf("Error on SIP Socket Initialize\n");
    printf("\033[0m");
    exit(1);
  }

  fd_set fds;
  memset(g_rxBuffer, 0, BUFFER_LEN);
  memset(g_txBuffer, 0, BUFFER_LEN);

  g_sip_done  = false;
  g_1stSampleDone = false;
  g_sip_done   = false;

  g_sip_state = SIP_IDLE;
  g_sip_mode  = SIP_NONE;

  double oldTime=0;
  double newTime=0;
 
  timeval tim;
  gettimeofday(&tim, NULL);
  oldTime = tim.tv_sec;

  printf("\ntcsip> ");
  fflush(stdout); 
  while (!g_sip_done)
  {
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    FD_SET(oSocSIP.m_socket, &fds);

    ret = select(1+oSocSIP.m_socket, &fds, NULL, NULL, NULL);

    //some activity has happened on the socket
    if(ret < 0) 
    {
      printf("Error on on select\n");
      break;
    }
    //dump_state();
    //User entry, coming in through stdin
    if (FD_ISSET(0, &fds))
    {
      memset(g_iBuffer, 0, MAX_STR_LEN);
      memset(g_oBuffer, 0, MAX_STR_LEN);
      
      fgets(g_iBuffer, MAX_STR_LEN, stdin);
      len = strlen(g_iBuffer);
      g_iBuffer[--len] = '\0';
      printf("tcsip> "); fflush(stdout);

      gettimeofday(&tim, NULL);
      newTime = tim.tv_sec;
      if (newTime - oldTime > TIMEOUTTIME)
      {
        if (g_sip_state==SIP_CONX_PENDING || g_sip_state==SIP_DISCONX_PENDING)
        {
          printf("Timing out...\n");  
          g_sip_done = true;
          continue;
        }
      }
      else
      {
         oldTime = newTime;
      }
      
      if (len>0)
      {
         ProcessUserCommand(g_iBuffer, g_oBuffer); 
      }
      if (strlen((char*)g_oBuffer))
         printf("%s\ntcsip> ", g_oBuffer); fflush(stdout);
    }

    // Something arrived at the listening socket. read the packet, check if it is DATA type or ACK type,
    // and do the necessary.
    if (FD_ISSET(oSocSIP.m_socket, &fds))
    {

      memset(g_rxBuffer, 0, BUFFER_LEN);
      len = BUFFER_LEN;
      if (oSocSIP.RecvFrom(g_rxBuffer, &len) < 0)
      {
        printf("Error on SIP socket\n");
        return 0;
      }
      g_rxBuffer[len]='\0';
      //printf("____________________\nReceived from socket:\n%s\n_____\n", (char*)g_rxBuffer);

      int prompt=0;
      prompt = ProcessSipMsg(g_rxBuffer, g_oBuffer);
      if (strlen((char*)g_oBuffer))
      {
        if (prompt!=5)
           printf("%s\ntcsip> ", g_oBuffer); fflush(stdout);
      }
    }
  }

  // in case the audio thread did not terminate...
  if (g_au_thread_running)
  {
    pthread_join(g_au_thread, NULL);
  }

  if (g_audio_fd > 0)  
     close(g_audio_fd);

  printf("\033[0m\n");
  return 0;
}

