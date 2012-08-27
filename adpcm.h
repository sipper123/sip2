/*
 * ADPCM.H
 * Defined the ADPCM class and the related data structures.
 */

#ifndef _ADPCM_H_
#define _ADPCM_H_

////////////////////////////
typedef unsigned char UCHAR;
typedef unsigned short WORD;
////////////////////////////
#define OPT_RECORD      0
#define OPT_PLAYBACK    1
#define OPT_MONO        1
#define OPT_STEREO      2
////////////////////////////
typedef union _b
{
    UCHAR pcmbytes[2];
    WORD  pcmsample;
} PCMSAMPLE;
////////////////////////////
typedef struct _a
{
    float stepSizeMultiplier;
    float rangeMin;
    float rangeMax;
} QTABLE_ENTRY;
////////////////////////////
static int g_IndexAdj[] =                  /* Index adjustment for the next iteration */
{
    -1, -1, -1, -1, 2, 4, 6, 8 
};     
static QTABLE_ENTRY g_Qtable[] =            /* Quantization table */
{   //multiplier    //min   //max
    0,              -10000,  0.25,
    0.25,           0.25,    0.50,
    0.50,           0.50,    0.75,
    0.75,           0.75,    1.00,
    1.00,           1.00,    1.25,
    1.25,           1.25,    1.50,
    1.50,           1.50,    1.75,
    1.75,           1.75,    10000,
};
static WORD g_index_to_step[] =             /* index to step mapping */
{
    7,      8,    9,     10,   11,     12,   13,     14,
   16,     17,   19,     21,   23,     25,   28,     31,
   34,     37,   41,     45,   50,     55,   60,     66,
   73,     80,   88,     97,  107,    118,  130,    143,
  157,    173,  190,    209,  230,    253,  279,    307,
  337,    371,  408,    449,  494,    544,  598,    658,
  724,    796,  876,    963, 1060,   1166, 1282,   1411,
 1552,   1707, 1878,  2066,  2272,   2499, 2749,   3024,
 3327,   3660, 4026,  4428,  4871,   5358, 5894,   6484,
 7132,   7845, 8630,  9493, 10442,  11487,12635,  13899,
15289,  16818,18500, 20350, 22385,  24623,27086,  29794,
32767,
};
///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////
class ADPCM
{
public:
    //constructor;
    ADPCM();
    ~ADPCM();

    //Functions to setup the class arrays
//    void  SetInitialSample(WORD seed);          /* used to setup the initial 2 byte sample. both encoder & decoder should use this */
    void  InitializeEncoder(UCHAR* buffer, int size); /* initialize the encoderwith the buffer read from device */
    void  InitializeDecoder(UCHAR* buffer, int size); /* initialize the decoder with the buffer received from network */
    void  InitializeDecoder(UCHAR* buffer, int size, WORD first, int indx);

    void  DeInitializeEncoder();                /* clean up the encoder's buffers and initial data */
    void  DeInitializeDecoder();                /* clean up the decoder's buffers and initial data */

    int   Encode(UCHAR* buffer, int * size);    /* Encode m_pcmBytes and return the encoded data in buffer */
    int   Decode(UCHAR* buffer, int * size);    /* Decode m_adpcmBytes and return the decoded data in buffer */

    //Lookup functions
    int   LookupIndexAdjustment(WORD n);        /* Return the index adjustment value corresponding to n */
    WORD  QTLookup(WORD delta, WORD stepsize);  /* Returns 'Quantization Output' corresponding to stepsize/delta */
    float QTLookup(int QuantOutput);            /* Returns the 'Step-size Multiplier' corresponding to the 'Quantization Output' */
    WORD  LookupStepsize(WORD index);           /* Return the stepsize corresponding to index */

    int   GetTable2Index() 
          { return m_table2Index; };            /* For use by RTP packets in the payload header */
    WORD  GetFirstWord()
          { return m_firstWord;   };            /* For RTP payload header use */
private:
    UCHAR *m_pcmBytes;                          /* buffer to hold the PCM data */
    UCHAR *m_adpcmBytes;                        /* buffer to hold the ADPCM data */
    int   m_pcmBufferLength;                    /* PCM buffer length */
    int   m_adpcmBufferLength;                  /* ADPCM buffer length */

    WORD  m_firstWord;                          /* The first sample of 16-bits */
    int   m_table2Index;                        /* Table2 index from the last iteration */

};
////////////////////////////
#endif

