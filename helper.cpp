/*
 * helper.cpp
 *
 * Helper functions for the talk program 
 * 
 */

#include "tcsip.h"

extern DWORD g_time_offset;
extern WORD  g_seq_num;
extern DWORD g_ssrc;
extern bool  g_simulate_delay;
extern bool  g_sip_state;
extern bool  g_sip_mode;
extern ADPCM oPCM;

//////////////////////////////////////////////////////
//Increments the global seq no with wraparound
WORD IncrementSeq()
{
  return (g_seq_num == 0xFFFF)?(g_seq_num=0):(g_seq_num++);
}

//////////////////////////////////////////////////////
DWORD gettime(void)
{
  struct timeval ltim;
  gettimeofday(&ltim, NULL);
  return (((DWORD)ltim.tv_sec)*((DWORD)(1000000))+((DWORD)ltim.tv_usec));
}

//////////////////////////////////////////////////////
//Initialize the fixed data required for the RTP headers
void InitHeaderData()
{
  srand(time(NULL));
  g_time_offset = rand();
  g_seq_num     = rand();

  GenerateSSRC();
}
//////////////////////////////////////////////////////
//Creates the RTP buffer by appropriately filing in the header fields
//and then appending the adpcm buffer. The variable names are
//self explanatory
void CreateRTPBuffer(UCHAR* RTP_buffer, int* RTP_len, UCHAR* encoded_buffer, int encoded_len)
{
  PRTPFIXEDHEADER pfh = (PRTPFIXEDHEADER)RTP_buffer;
  PPAYLOADHEADER  pph = (PPAYLOADHEADER)(RTP_buffer + sizeof(RTPFIXEDHEADER));
  UCHAR*         data = RTP_buffer + sizeof(RTPFIXEDHEADER)+ sizeof(PAYLOADHEADER);

  pfh->Version   = 2;
  pfh->Padding   = 0;
  pfh->Extension = 0;
  pfh->CSRCCount = 0;
  pfh->Marker    = 1;
  pfh->PayLdType = 5;
  pfh->SeqNum    = (WORD)IncrementSeq();
  pfh->TimeStamp = (DWORD)(gettime() + g_time_offset);
  pfh->SSRC      = (DWORD)g_ssrc;

  pph->FirstSample = (WORD)oPCM.GetFirstWord();
  pph->Table2Index = (UCHAR)oPCM.GetTable2Index();
  pph->Reserved    = (UCHAR)0x00;

  memcpy(data, encoded_buffer+2,encoded_len-2);
  *RTP_len = sizeof(RTPFIXEDHEADER)+ sizeof(PAYLOADHEADER) + encoded_len - 2;

  return;
}

//////////////////////////////////////////////////////
bool InsertRandomDelayLoss()
{
 if (!g_simulate_delay) return false;

 srand(time(NULL));
 int x = rand() % 10;

 /*
  *  if x is 2, 3  => delay (2->4ms, 3->6ms)
  *  if x is 7,8,9 => loss
  */
  if (x==2) {
    usleep(4000);
  } else if (x==3) {
    usleep(6000);
  } else if (x==7 || x==9 ) {
    return true;
  }
  return false;
}

////////////////////////////////////////////////////////
// Generate an SSRC with a combination of IP address and
// a random number
void GenerateSSRC()
{
  g_ssrc = 0;
  srand(time(NULL));
  struct hostent* hostStruct = NULL;
  struct in_addr* hostNode = NULL;
  char hostname[255];
  gethostname(hostname, 255);
  hostStruct = gethostbyname(hostname);
  if (hostStruct)
  {
    hostNode = (struct in_addr*) hostStruct->h_addr;
    g_ssrc   = hostNode->s_addr;
  }
  g_ssrc     += rand();
}

//////////////////////
//Helper function to print out Hex display of data
void HexOut(UCHAR* s, int len)
{
  printf("--\n");

  for (int i=0;i<len;i++)
  {
    if (i%16==0)
      printf("\n");
    else if (i%8==0)
      printf(" ");

    printf("[%0.2X]", s[i]);
  }
  printf("\n");
}

/////////////////////////////////////

void dump_state()
{
  char* state = NULL;
  char* mode  = NULL;
       if (g_sip_mode == 0) mode = "NONE";
  else if (g_sip_mode == 1) mode = "CLIENT";
  else if (g_sip_mode == 2) mode = "SERVER";

       if (g_sip_state == 0) state = "Idle";
  else if (g_sip_state == 1) state = "Connect Pending";
  else if (g_sip_state == 2) state = "Connected";
  else if (g_sip_state == 3) state = "Disiconnect Pending";

  if (state && mode)
  {
    printf("                                                                       %s %s\n", mode, state);
  }
}

/////////////////////////////////////

int getmaxfd(int a, int b, int c)
{
  int largest = a;
  if (b>largest) largest = b;
  if (c>largest) largest = c;
  return largest;
}
