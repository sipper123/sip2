/*
 * ADPCM.CPP
 * Implements the methods for the ADPCM class, for encoding and decoding.
 */

#include <math.h>
#include <memory.h>
#include <stdlib.h>
#include "adpcm.h"

///////////////////////////////////////////////////////////////
//Constructor.
ADPCM::ADPCM()
{
    m_firstWord = 0;
    m_table2Index = 0;
    m_pcmBytes = NULL;
    m_adpcmBytes = NULL;
    m_pcmBufferLength = 0;
    m_adpcmBufferLength = 0;
}
///////////////////////////////////////////////////////////////
//Destructor
ADPCM::~ADPCM()
{
    if (m_pcmBytes)
    {
        delete [] m_pcmBytes;
        m_pcmBytes = NULL;
    }
    if (m_adpcmBytes)
    {
        delete [] m_adpcmBytes;
        m_adpcmBytes = NULL;
    }
    m_pcmBufferLength = 0;
    m_adpcmBufferLength = 0;
}
///////////////////////////////////////////////////////////////
//This function initializes the previous sample for the ADPCM
//encode/decoder. This sample will be used as the I-sample
//for the subsequent encoding/decoding
//
// IN: The sample
// OUT: None
// RETURN: None
/*
void ADPCM::SetInitialSample(WORD sample)
{
    m_PreviousSample = sample;
    m_table2Index = 0;
}
*/

///////////////////////////////////////////////////////////////
// This function initializes the ADPCM object for Encoding
// The internal PCM buffer is filled with the input data.
// An ADPCM buffer is created with 4 times smaller size to 
// hold the encoded data. 
// IN: buffer = the data to be encoded, unsigned char buffer
//     size   = the size of data to be encoded
// OUT: None
// RETURN: None
void ADPCM::InitializeEncoder(UCHAR* buffer, int size)
{
    if (!buffer || size<=0) return;
    m_pcmBytes   = new UCHAR[size-2];
    m_adpcmBytes = new UCHAR[(size-2)>>2];
    if (m_pcmBytes && m_adpcmBytes)
    {
        memset(m_pcmBytes, 0, size-2);
        memset(m_adpcmBytes, 0, (size-2)>>2);

        memcpy(m_pcmBytes, buffer+2, size-2);
        m_pcmBufferLength = size-2;

        m_adpcmBufferLength = m_pcmBufferLength>>2;

        WORD hw = (WORD)(buffer[1]);
        WORD lw = (WORD)(buffer[0]);
        m_firstWord = (hw<<8)|lw; 
    }
    return;
}
///////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////
// This function initializes the ADPCM object for Decoding
// 
// IN: buffer = the data to be decoded, unsigned char buffer
//     size   = the size of data to be decoded
// OUT: None
// RETURN: None
void ADPCM::InitializeDecoder(UCHAR* buffer, int size)
{
    if (!buffer || size<=0) return;
    m_pcmBytes   = new UCHAR[(size-2)<<2];
    m_adpcmBytes = new UCHAR[size-2];
    if (m_pcmBytes && m_adpcmBytes)
    {
        memset(m_pcmBytes, 0, (size-2)<<2);
        memset(m_adpcmBytes, 0, size-2);

        memcpy(m_adpcmBytes, buffer+2, size-2); 
        m_adpcmBufferLength = size-2;

        m_pcmBufferLength = (size-2)<<2;

        WORD hw = (WORD)(buffer[1]);
        WORD lw = (WORD)(buffer[0]);
        m_firstWord = (hw<<8)|lw; 
    }
    return;
}
///////////////////////////////////////////////////////////////
// In this overloaded function, the actual buffer and first word are 
// given separately. Also, the table2Index is updated.
void ADPCM::InitializeDecoder(UCHAR* buffer, int size, WORD firstWord, int table2Indx)
{
    if (!buffer || size<=0) return;
    m_pcmBytes   = new UCHAR[size<<2];
    m_adpcmBytes = new UCHAR[size];
    if (m_pcmBytes && m_adpcmBytes)
    {
        memset(m_pcmBytes, 0, size<<2);
        memset(m_adpcmBytes, 0, size);

        memcpy(m_adpcmBytes, buffer, size); 
        m_adpcmBufferLength = size;

        m_pcmBufferLength = size<<2;

        m_firstWord   = firstWord; 
        //m_table2Index = table2Indx;
    }
    return;
}
///////////////////////////////////////////////////////////////
// IN: None
// OUT: None
// RETURN: None
void ADPCM::DeInitializeEncoder()
{
    if (m_pcmBytes)     delete[] m_pcmBytes;
    if (m_adpcmBytes)   delete[] m_adpcmBytes;
    m_pcmBytes   = NULL;
    m_adpcmBytes = NULL;
    m_pcmBufferLength   = 0;
    m_adpcmBufferLength = 0;
    m_firstWord = 0;
}
///////////////////////////////////////////////////////////////
// IN: None 
// OUT: None
// RETURN: None
void ADPCM::DeInitializeDecoder()
{
    if (m_pcmBytes)     delete[] m_pcmBytes;
    if (m_adpcmBytes)   delete[] m_adpcmBytes;
    m_pcmBytes   = NULL;
    m_adpcmBytes = NULL;
    m_pcmBufferLength   = 0;
    m_adpcmBufferLength = 0;
    m_firstWord = 0;
}
///////////////////////////////////////////////////////////////
// This function does a ADPCM encoding of the data that is present
// in the m_pcmBytes. The encoded data is copied into the input 
// buffer 'buffer'.
// IN: buffer   = pointer to the buffer
//     psize    = pointer to the size of the input buffer
//     firstTime= to indicate if this is the first packet so that
//                it could be sent over reliable connection  
// OUT: filled up buffer, and the amounbt of data filled up.  
// RETURN: None
int ADPCM::Encode(UCHAR* buffer, int * psize)
{
    if (!buffer || !psize) return (-1);

    WORD   wCurr       = 0;
    WORD   wStep       = 0;
    WORD   wQuantVal   = 0;
    WORD   wDelta      = 0;
    long   lDelta      = 0; 
    float  fMultiplier = 0.0;
    int    indexAdj    = 0;
    int    sign        = 1;

    WORD   PreviousSample = m_firstWord;
    UCHAR  EncodedByte = 0x0;                          //this gets inserted to adpcm buffer              

    for (int i=0;i<m_pcmBufferLength/2;i++)  
    {
      wCurr       = (WORD)(m_pcmBytes[2*i+1]);
      wCurr       = (0x00FF&m_pcmBytes[2*i])|(wCurr<<8); 
      lDelta      = wCurr - PreviousSample;            //read signed diff in lDelta
      sign        = (lDelta<0)?(-1):(1);               //YYY

      wDelta      = (WORD)abs(lDelta);
      wStep       = LookupStepsize(m_table2Index);

      wQuantVal   = QTLookup(wDelta, wStep);           //This goes into the output
      fMultiplier = QTLookup(wQuantVal);
      indexAdj    = LookupIndexAdjustment(wQuantVal);
 
      m_table2Index = m_table2Index + indexAdj;
      m_table2Index = (m_table2Index<0) ? 
                        0 : m_table2Index;             //index for next iteration
      PreviousSample = (WORD)
                       (0.5+ PreviousSample + 
                        sign*wStep*fMultiplier);       //decoded data for next iteration  
                                                       //XXX change delta to step; add 0.5 
                                                       // ^ YYY
      wQuantVal   = (sign==-1)?(wQuantVal|0x8):wQuantVal; //set 4th bit to 1 if negative delta

      if (i%2==0)
      {                                                // If it is even byte, then cook up the 
         EncodedByte = wQuantVal<<4;                   // upper nibble
      }
      else
      {                                                // If odd byte, then append the lower 
         EncodedByte = EncodedByte | wQuantVal;        // nibble and set the new byte in the 
         m_adpcmBytes[i/2] = EncodedByte;              // adpcm buffer 
         EncodedByte = 0x0;
      }
    }

    buffer[1] = (m_firstWord>>8) & 0xff;                //SWAP ORDER ( XXXX )
    buffer[0] = m_firstWord & 0xff;
    memcpy(buffer+2, m_adpcmBytes, m_adpcmBufferLength);
    *psize = 2+m_adpcmBufferLength;
    
    return 0;
}
///////////////////////////////////////////////////////////////
// This function decodes the ADPCM encodede data that is present
// in the m_adpcmBytes. The decoded data is copied into the input 
// buffer 'buffer'.
// IN: buffer   = pointer to the buffer
//     psize    = pointer to the size of the input buffer
//     firstTime= to indicate if this is the first packet so that
//                it could be sent over reliable connection  
// OUT: filled up buffer, and the amounbt of data filled up.  
// RETURN: None
int ADPCM::Decode(UCHAR* buffer, int * psize)
{
    if (!buffer || !psize) return (-1);
    
    WORD   wCurr       = 0;
    WORD   wStep       = 0;
    WORD   wQuantVal   = 0;
    WORD   wDelta      = 0;
    long   lDelta      = 0; 
    float  fMultiplier = 0.0;
    int    indexAdj    = 0;
    UCHAR  nibble      = 0x0;
    int    sign        = 1;

    WORD   PreviousSample = m_firstWord;

    for (int i=0;i<2*m_adpcmBufferLength;i++)
    {
      nibble            = m_adpcmBytes[i/2];
      nibble            = (i%2==0)?(nibble>>4):(nibble&0x0f); // XXX change order
      sign              = ((nibble&0x8)==0)?(1):(-1);

      wQuantVal         = nibble & 0x7;
      fMultiplier       = QTLookup(wQuantVal);
      wStep             = LookupStepsize(m_table2Index);
      wDelta            = (WORD)(0.5 + wStep * fMultiplier); // XXX add 0.5
      lDelta            = wDelta * sign;                         //read signed diff in lDelta
      wCurr             = (WORD)(PreviousSample + lDelta);

      indexAdj          = LookupIndexAdjustment(wQuantVal);
 
      m_table2Index     = m_table2Index + indexAdj;
      m_table2Index     = (m_table2Index<0)?0:m_table2Index;         //index for next iteration
      PreviousSample    = wCurr;

      m_pcmBytes[2*i]   = wCurr & 0x00FF;//   (wCurr & 0xF0)>>8;
      m_pcmBytes[2*i+1] = (wCurr & 0xFF00)>>8;
    }

    *psize = 0;
    buffer[1] = (m_firstWord>>8) & 0xff;
    buffer[0] = m_firstWord & 0xff;
    memcpy(buffer+2, m_pcmBytes, m_pcmBufferLength);
    *psize = 2+m_pcmBufferLength;

    return 0;
}

///////////////////////////////////////////////////////////////
// Gets the index adjustment value for the given quantization value
// IN: Quantization value 
// RETURN: Index Adj
int ADPCM::LookupIndexAdjustment(WORD n)
{
    return g_IndexAdj[n];
}
///////////////////////////////////////////////////////////////
// Returns the Quantization Table value for the given delta and 
// stepsize.
WORD ADPCM::QTLookup(WORD delta, WORD stepsize)
{
    float fraction = (float)delta/(float)stepsize; //XXXX
    for (int i=0;i<8;i++)
    {
       	if (fraction>=g_Qtable[i].rangeMin && fraction< g_Qtable[i].rangeMax)
            return (WORD)i;
    }
    return 0;
}
///////////////////////////////////////////////////////////////
// IN: Quantization output
// RETURN: Step size multiplier
float ADPCM::QTLookup(int QuantOutput)
{
    return g_Qtable[QuantOutput].stepSizeMultiplier;
}
///////////////////////////////////////////////////////////////
// IN: table index
// RETURN: Stepsize
WORD ADPCM::LookupStepsize(WORD index)
{
    return g_index_to_step[index];
}
///////////////////////////////////////////////////////////////

