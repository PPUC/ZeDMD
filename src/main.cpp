#define ZEDMD_VERSION_MAJOR 3  // X Digits
#define ZEDMD_VERSION_MINOR 2  // Max 2 Digits
#define ZEDMD_VERSION_PATCH 0  // Max 2 Digits

#ifdef ZEDMD_64_64_4
    #define PANEL_WIDTH    64    // Width: number of LEDs for 1 panel.
    #define PANEL_HEIGHT   64    // Height: number of LEDs.
    #define PANELS_NUMBER  4     // Number of horizontally chained panels.
#endif
#ifdef ZEDMD_128_64_2
    #define PANEL_WIDTH    128   // Width: number of LEDs for 1 panel.
    #define PANEL_HEIGHT   64    // Height: number of LEDs.
    #define PANELS_NUMBER  2     // Number of horizontally chained panels.
#endif
#ifndef PANEL_WIDTH
    #define PANEL_WIDTH    64    // Width: number of LEDs for 1 panel.
    #define PANEL_HEIGHT   32    // Height: number of LEDs.
    #define PANELS_NUMBER  2     // Number of horizontally chained panels.
#endif

#define SERIAL_BAUD    921600
#define SERIAL_TIMEOUT 8      // Time in milliseconds to wait for the next data chunk.
#define SERIAL_BUFFER  8192   // Serial buffer size in byte.
#define FRAME_TIMEOUT  10000  // Time in milliseconds to wait for a new frame.

// ------------------------------------------ ZeDMD by Zedrummer (http://pincabpassion.net)---------------------------------------------
// - If you have blurry pictures, the display is not clean, try to reduce the input voltage of your LED matrix panels, often, 5V panels need
//   between 4V and 4.5V to display clean pictures, you often have a screw in switch-mode power supplies to change the output voltage a little bit
// - While the initial pattern logo is displayed, check you have red in the upper left, green in the lower left and blue in the upper right,
//   if not, make contact between the ORDRE_BUTTON_PIN (default 21, but you can change below) pin and a ground pin several times
// until the display is correct (automatically saved, no need to do it again)
// -----------------------------------------------------------------------------------------------------------------------------------------
// By pressing the RGB button while a game is running or by sending command 99,you can enable the "Debug Mode".
// The output will be:
// 000 number of frames received, regardless if any error happened
// 001 size of compressed frame if compression is enabled
// 002 size of currently received bytes of frame (compressed or decompressed)
// 003 error code if the decompression if compression is enabled
// 004 number of incomplete frames
// 005 number of resets because of communication freezes
// -----------------------------------------------------------------------------------------------------------------------------------------

#define TOTAL_WIDTH (PANEL_WIDTH * PANELS_NUMBER)
#define TOTAL_WIDTH_PLANE (TOTAL_WIDTH >> 3)
#define TOTAL_HEIGHT PANEL_HEIGHT
#define TOTAL_BYTES (TOTAL_WIDTH * TOTAL_HEIGHT * 3)
#define MAX_COLOR_ROTATIONS 8
#define MIN_SPAN_ROT 60

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <LittleFS.h>
#include <miniz.h>
#include <Bounce2.h>

// Pinout derived from ESP32-HUB75-MatrixPanel-I2S-DMA.h
#define R1_PIN  25
#define G1_PIN  26
#define B1_PIN  27
#define R2_PIN  14
#define G2_PIN  12
#define B2_PIN  13
#define A_PIN   23
#define B_PIN   19
#define C_PIN   5
#define D_PIN   17
#define E_PIN   22 // required for 1/32 scan panels, like 64x64. Any available pin would do, i.e. IO32. If 1/16 scan panels, no connection to this pin needed
#define LAT_PIN 4
#define OE_PIN  15
#define CLK_PIN 16

#define ORDRE_BUTTON_PIN 21
Bounce2::Button* rgbOrderButton;

#define LUMINOSITE_BUTTON_PIN 33
Bounce2::Button* brightnessButton;

#define N_CTRL_CHARS 6
#define N_INTERMEDIATE_CTR_CHARS 4
// !!!!! NE METTRE AUCUNE VALEURE IDENTIQUE !!!!!
unsigned char CtrlCharacters[6]={0x5a,0x65,0x64,0x72,0x75,0x6d};
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

bool min_chiffres[3*10*5]={0,1,0, 0,0,1, 1,1,0, 1,1,0, 0,0,1, 1,1,1, 0,1,1, 1,1,1, 0,1,0, 0,1,0,
                           1,0,1, 0,1,1, 0,0,1, 0,0,1, 0,1,0, 1,0,0, 1,0,0, 0,0,1, 1,0,1, 1,0,1,
                           1,0,1, 0,0,1, 0,1,0, 0,1,0, 1,1,1, 1,1,0, 1,1,0, 0,1,0, 0,1,0, 0,1,1,
                           1,0,1, 0,0,1, 1,0,0, 0,0,1, 0,0,1, 0,0,1, 1,0,1, 1,0,0, 1,0,1, 0,0,1,
                           0,1,0, 0,0,1, 1,1,1, 1,1,0, 0,0,1, 1,1,0, 0,1,0, 1,0,0, 0,1,0, 1,1,0};

bool lumtxt[16*5]={0,1,0,0,0,1,0,0,1,0,1,1,0,1,1,0,
                   0,1,0,0,0,1,0,0,1,0,1,0,1,0,1,0,
                   0,1,0,0,0,1,0,0,1,0,1,0,0,0,1,0,
                   0,1,0,0,0,1,0,0,1,0,1,0,0,0,1,0,
                   0,1,1,1,0,0,1,1,1,0,1,0,0,0,1,0};

unsigned char lumval[16]={0,2,4,7,11,18,30,40,50,65,80,100,125,160,200,255}; // Non-linear brightness progression

HUB75_I2S_CFG::i2s_pins _pins={R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN};
HUB75_I2S_CFG mxconfig(
          PANEL_WIDTH,    // width
          PANEL_HEIGHT,   // height
          PANELS_NUMBER,  // chain length
          _pins           // pin mapping
          //HUB75_I2S_CFG::FM6126A  // driver chip
);

MatrixPanel_I2S_DMA *dma_display = nullptr;

int ordreRGB[3*6]={0,1,2, 2,0,1, 1,2,0,
                   0,2,1, 1,0,2, 2,1,0};
int acordreRGB=0;

unsigned char* palette;
unsigned char* renderBuffer;
unsigned char doubleBufferR[TOTAL_HEIGHT][TOTAL_WIDTH] = {0};
unsigned char doubleBufferG[TOTAL_HEIGHT][TOTAL_WIDTH] = {0};
unsigned char doubleBufferB[TOTAL_HEIGHT][TOTAL_WIDTH] = {0};

// for color rotation
unsigned char rotCols[64];
unsigned long nextTime[MAX_COLOR_ROTATIONS];
unsigned char firstCol[MAX_COLOR_ROTATIONS];
unsigned char nCol[MAX_COLOR_ROTATIONS];
unsigned char acFirst[MAX_COLOR_ROTATIONS];
unsigned long timeSpan[MAX_COLOR_ROTATIONS];

bool mode64=false;

int RomWidth=128, RomHeight=32;
int RomWidthPlane=128>>3;

bool debugMode = false;
unsigned int debugLines[6] = {0};

unsigned char lumstep=1;

bool MireActive = true;
bool handshakeSucceeded = false;
bool compression = false;
// 256 is the default buffer size of the CP210x linux kernel driver, we should not exceed it as default.
int serialTransferChunkSize = 256;
unsigned int frameCount = 0;
unsigned int errorCount = 0;
unsigned int watchdogCount = 0;

bool fastReadySent = false;

void sendFastReady() {
  // Indicate (R)eady, even if the frame isn't rendered yet.
  // That would allow to get the buffer filled with the next frame already.
  Serial.write('R');
  fastReadySent = true;
}

void DisplayChiffre(unsigned int chf, int x,int y,int R, int G, int B)
{
  // affiche un chiffre verticalement
  unsigned int c=chf%10;
  const int poscar=3*c;
  for (int ti=0;ti<5;ti++)
  {
    for (int tj=0;tj<4;tj++)
    {
      if (tj<3) {if (min_chiffres[poscar+tj+ti*3*10]==1) dma_display->drawPixelRGB888(x+tj,y+ti,R,G,B); else dma_display->drawPixelRGB888(x+tj,y+ti,0,0,0);}
      else dma_display->drawPixelRGB888(x+tj,y+ti,0,0,0);
    }
  }
}

void DisplayNombre(unsigned int chf,unsigned char nc,int x,int y,int R,int G,int B)
{
  // affiche un nombre verticalement
  unsigned int acc=chf,acd=1;
  for (int ti=0;ti<(nc-1);ti++) acd*=10;
  for (int ti=0;ti<nc;ti++)
  {
    unsigned int val;
    if (nc>1) val=(unsigned int)(acc/acd); else val=chf;
    DisplayChiffre(val,x+4*ti,y,R,G,B);
    acc=acc-val*acd;
    acd/=10;
  }
}

void DisplayVersion()
{
  // display the version number to the lower left
  int ncM,ncm,ncp;
  if (ZEDMD_VERSION_MAJOR>=100) ncM=3; else if (ZEDMD_VERSION_MAJOR>=10) ncM=2; else ncM=1;
  DisplayNombre(ZEDMD_VERSION_MAJOR,ncM,4,TOTAL_HEIGHT-5,150,150,150);
  dma_display->drawPixelRGB888(4+4*ncM,TOTAL_HEIGHT-1,150,150,150);
  if (ZEDMD_VERSION_MINOR>=10) ncm=2; else ncm=1;
  DisplayNombre(ZEDMD_VERSION_MINOR,ncm,4+4*ncM+2,TOTAL_HEIGHT-5,150,150,150);
  dma_display->drawPixelRGB888(4+4*ncM+2+4*ncm,TOTAL_HEIGHT-1,150,150,150);
  if (ZEDMD_VERSION_PATCH>=10) ncp=2; else ncp=1;
  DisplayNombre(ZEDMD_VERSION_PATCH,ncp,4+4*ncM+2+4*ncm+2,TOTAL_HEIGHT-5,150,150,150);
}

void DisplayLum(void)
{
  DisplayNombre(lumstep,2,TOTAL_WIDTH/2-16/2-2*4/2+16,TOTAL_HEIGHT-5,255,255,255);
}

void  DisplayText(bool* text, int width, int x, int y, int R, int G, int B)
{
  // affiche le texte "SCORE" en (x, y)
  for (unsigned int ti=0;ti<width;ti++)
  {
    for (unsigned int tj=0; tj<5;tj++)
    {
      if (text[ti+tj*width]==1) dma_display->drawPixelRGB888(x+ti,y+tj,R,G,B); else dma_display->drawPixelRGB888(x+ti,y+tj,0,0,0);
    }
  }
}

void Say(unsigned char where, unsigned int what)
{
    DisplayNombre(where,3,0,where*5,255,255,255);
    if (what!=(unsigned int)-1) DisplayNombre(what,10,15,where*5,255,255,255);
}

bool CmpColor(unsigned char* px1, unsigned char* px2)
{
  return
    (px1[0] == px2[0]) &&
    (px1[1] == px2[1]) &&
    (px1[2] == px2[2]);
}

void SetColor(unsigned char* px1, unsigned char* px2)
{
  px1[0] = px2[0];
  px1[1] = px2[1];
  px1[2] = px2[2];
}

void ScaleImage() // scale for non indexed image (RGB24)
{
  int xoffset=0;
  int yoffset=0;
  int scale=0; // 0 - no scale, 1 - half scale, 2 - twice scale

  if ((RomWidth==192)&&(TOTAL_WIDTH==256))
  {
    xoffset=32*3;
  }
  else if (RomWidth==192)
  {
    xoffset=16*3;
    scale=1;
  }
  else if ((RomWidth==256)&&(TOTAL_WIDTH==128))
  {
    scale=1;
  }
  else if ((RomWidth==128)&&(TOTAL_WIDTH==256))
  {
    // Scaling doesn't look nice for real RGB tables like Diablo.
    // @todo we should add a command to turn scaling on or off from the client.
    scale=2;

    // Optional: just center the DMD.
    // xoffset = 64 * 3;
    // yoffset = 16 * 3;
  }
  else return;

  unsigned char* panel = (unsigned char*) malloc(RomWidth * RomHeight * 3);
  memcpy(panel, renderBuffer, RomWidth * RomHeight * 3);

  if (scale==1)
  {
    memset(renderBuffer, 0, TOTAL_BYTES);

    // for half scaling we take the 4 points and look if there is one colour repeated
    for (int ti=0;ti<RomHeight;ti+=2)
    {
      for (int tj=0;tj<RomWidth;tj+=2)
      {
        unsigned char* pp = &panel[ti * RomWidth * 3 + tj*3];
        if (CmpColor(pp, &pp[3]) || CmpColor(pp, &pp[3*RomWidth]) || CmpColor(pp, &pp[3*RomWidth+3]))
        {
          SetColor(&renderBuffer[xoffset + 3*(tj>>1+(ti>>1)*TOTAL_WIDTH)], pp);
        }
        else if (CmpColor(&pp[3], &pp[3*RomWidth]) || CmpColor(&pp[3], &pp[3*RomWidth+3]))
        {
          SetColor(&renderBuffer[xoffset + 3*(tj>>1+(ti>>1)*TOTAL_WIDTH)], &pp[3]);
        }
        else if (CmpColor(&pp[3*RomWidth], &pp[3*RomWidth+3]))
        {
          SetColor(&renderBuffer[xoffset + 3*(tj>>1+(ti>>1)*TOTAL_WIDTH)], &pp[3*RomWidth]);
        }
        else
        {
          SetColor(&renderBuffer[xoffset + 3*(tj>>1+(ti>>1)*TOTAL_WIDTH)], pp);
        }
      }
    }
  }
  else if (scale==2)
  {
    // we implement scale2x http://www.scale2x.it/algorithm
    for (int tj=0;tj<RomHeight;tj++)
    {
      for (int ti=0;ti<RomWidth;ti++)
      {
        unsigned char *a, *b, *c, *d, *e, *f, *g, *h, *i;
        if ((ti==0)&&(tj==0))
        {
          a=b=d=e=panel;
          c=f=&panel[3];
          g=h=&panel[3*RomWidth];
          i=&panel[3*(RomWidth+1)];
        }
        else if ((ti==0)&&(tj==RomHeight-1))
        {
          a=b=&panel[3*(tj-1)*RomWidth];
          c=&panel[3*((tj-1)*RomWidth+1)];
          d=g=h=e=&panel[3*tj*RomWidth];
          f=i=&panel[3*(tj*RomWidth+1)];
        }
        else if ((ti==RomWidth-1)&&(tj==0))
        {
          a=d=&panel[3*(ti-1)];
          b=c=f=e=&panel[3*ti];
          g=&panel[3*(RomWidth+ti-1)];
          h=i=&panel[3*(RomWidth+ti)];
        }
        else if ((ti==RomWidth-1)&&(tj==RomHeight-1))
        {
          a=&panel[3*(tj*RomWidth-2)];
          b=c=&panel[3*(tj*RomWidth-1)];
          d=g=&panel[3*(RomHeight*RomWidth-2)];
          e=f=h=i=&panel[3*(RomHeight*RomWidth-1)];
        }
        else if (ti==0)
        {
          a=b=&panel[3*((tj-1)*RomWidth)];
          c=&panel[3*((tj-1)*RomWidth+1)];
          d=e=&panel[3*(tj*RomWidth)];
          f=&panel[3*(tj*RomWidth+1)];
          g=h=&panel[3*((tj+1)*RomWidth)];
          i=&panel[3*((tj+1)*RomWidth+1)];
        }
        else if (ti==RomWidth-1)
        {
          a=&panel[3*(tj*RomWidth-2)];
          b=c=&panel[3*(tj*RomWidth-1)];
          d=&panel[3*((tj+1)*RomWidth-2)];
          e=f=&panel[3*((tj+1)*RomWidth-1)];
          g=&panel[3*((tj+2)*RomWidth-2)];
          h=i=&panel[3*((tj+2)*RomWidth-1)];
        }
        else if (tj==0)
        {
          a=d=&panel[3*(ti-1)];
          b=e=&panel[3*ti];
          c=f=&panel[3*(ti+1)];
          g=&panel[3*(RomWidth+ti-1)];
          h=&panel[3*(RomWidth+ti)];
          i=&panel[3*(RomWidth+ti+1)];
        }
        else if (tj==RomHeight-1)
        {
          a=&panel[3*((tj-1)*RomWidth+ti-1)];
          b=&panel[3*((tj-1)*RomWidth+ti)];
          c=&panel[3*((tj-1)*RomWidth+ti+1)];
          d=g=&panel[3*(tj*RomWidth+ti-1)];
          e=h=&panel[3*(tj*RomWidth+ti)];
          f=i=&panel[3*(tj*RomWidth+ti+1)];
        }
        else
        {
          a=&panel[3*((tj-1)*RomWidth+ti-1)];
          b=&panel[3*((tj-1)*RomWidth+ti)];
          c=&panel[3*((tj-1)*RomWidth+ti+1)];
          d=&panel[3*(tj*RomWidth+ti-1)];
          e=&panel[3*(tj*RomWidth+ti)];
          f=&panel[3*(tj*RomWidth+ti+1)];
          g=&panel[3*((tj+1)*RomWidth+ti-1)];
          h=&panel[3*((tj+1)*RomWidth+ti)];
          i=&panel[3*((tj+1)*RomWidth+ti+1)];
        }
        if (b != h && d != f) {
          if (CmpColor(d,b)) SetColor(&renderBuffer[3*(tj*2*TOTAL_WIDTH+ti*2)+xoffset],d); else SetColor(&renderBuffer[3*(tj*2*TOTAL_WIDTH+ti*2)+xoffset],e);
          if (CmpColor(b,f)) SetColor(&renderBuffer[3*(tj*2*TOTAL_WIDTH+ti*2+1)+xoffset], f); else SetColor(&renderBuffer[3*(tj*2*TOTAL_WIDTH+ti*2+1)+xoffset], e);
          if (CmpColor(b,h)) SetColor(&renderBuffer[3*((tj*2+1)*TOTAL_WIDTH+ti*2)+xoffset],d); else SetColor(&renderBuffer[3*((tj*2+1)*TOTAL_WIDTH+ti*2)+xoffset],e);
          if (CmpColor(h,f)) SetColor(&renderBuffer[3*((tj*2+1)*TOTAL_WIDTH+ti*2+1)+xoffset],f); else SetColor(&renderBuffer[3*((tj*2+1)*TOTAL_WIDTH+ti*2+1)+xoffset],e);
        } else {
          SetColor(&renderBuffer[3*(tj*2*TOTAL_WIDTH+ti*2)+xoffset],e);
          SetColor(&renderBuffer[3*(tj*2*TOTAL_WIDTH+ti*2+1)+xoffset], e);
          SetColor(&renderBuffer[3*((tj*2+1)*TOTAL_WIDTH+ti*2)+xoffset],e);
          SetColor(&renderBuffer[3*((tj*2+1)*TOTAL_WIDTH+ti*2+1)+xoffset],e);
        }
       }
    }
  }
  else //offset!=0
  {
    memset(renderBuffer, 0, TOTAL_BYTES);

    for (int tj=0; tj<RomHeight; tj++)
    {
      for (int ti=0; ti<RomWidth; ti++)
      {
        for (int i=0; i <= 2; i++) {
          renderBuffer[yoffset * TOTAL_WIDTH + 3 * (tj * TOTAL_WIDTH + ti) + xoffset + i] = panel[3 * (tj * RomWidth + ti) + i];
        }
      }
    }
  }

  free(panel);
}

void ScaleImage64() // scale for indexed image (all except RGB24)
{
  int xoffset = 0;
  int scale = 0; // 0 - no scale, 1 - half scale, 2 - double scale

  if (RomWidth == 192 && TOTAL_WIDTH == 256)
  {
    xoffset = 32;
  }
  else if (RomWidth == 192)
  {
    xoffset = 16;
    scale = 1;
  }
  else if (RomWidth == 256 && TOTAL_WIDTH == 128)
  {
    scale = 1;
  }
  else if (RomWidth == 128 && TOTAL_WIDTH == 256)
  {
    scale = 2;
  }
  else
  {
    return;
  }

  unsigned char* panel = (unsigned char*) malloc(RomWidth * RomHeight);
  memcpy(panel, renderBuffer, RomWidth * RomHeight);

  if (scale == 1)
  {
    memset(renderBuffer, 0, TOTAL_WIDTH * TOTAL_HEIGHT);

    // for half scaling we take the 4 points and look if there is one colour repeated
    for (int ti = 0; ti < RomHeight; ti += 2)
    {
      for (int tj = 0; tj < RomWidth; tj += 2)
      {
        unsigned char* pp = &panel[ti*RomWidth+tj];
        if (pp[0]==pp[1] || pp[0]==pp[RomWidth] || pp[0]==pp[RomWidth+1])
        {
          renderBuffer[xoffset + (tj/2 ) + (ti/2) * TOTAL_WIDTH] = pp[0];
        }
        else if (pp[1]==pp[RomWidth] || pp[1]==pp[RomWidth+1])
        {
          renderBuffer[xoffset+ (tj/2) + (ti/2) * TOTAL_WIDTH] = pp[1];
        }
        else if (pp[RomWidth]==pp[RomWidth+1])
        {
          renderBuffer[xoffset + (tj/2) + (ti/2) * TOTAL_WIDTH] = pp[RomWidth];
        }
        else
        {
          renderBuffer[xoffset + (tj/2) + (ti/2) * TOTAL_WIDTH] = pp[0];
        }
      }
    }
  }
  else if (scale == 2)
  {
    // we implement scale2x http://www.scale2x.it/algorithm
    for (int tj = 0; tj < RomHeight; tj++)
    {
      for (int ti = 0; ti < RomWidth; ti++)
      {
        unsigned int a, b, c, d, e, f, g, h, i;
        if (ti == 0 && tj == 0)
        {
          a=b=d=e=panel[0];
          c=f=panel[1];
          g=h=panel[RomWidth];
          i=panel[RomWidth+1];
        }
        else if ((ti==0)&&(tj==RomHeight-1))
        {
          a=b=panel[(tj-1)*RomWidth];
          c=panel[(tj-1)*RomWidth+1];
          d=g=h=e=panel[tj*RomWidth];
          f=i=panel[tj*RomWidth+1];
        }
        else if ((ti==RomWidth-1)&&(tj==0))
        {
          a=d=panel[ti-1];
          b=c=f=e=panel[ti];
          g=panel[RomWidth+ti-1];
          h=i=panel[RomWidth+ti];
        }
        else if ((ti==RomWidth-1)&&(tj==RomHeight-1))
        {
          a=panel[tj*RomWidth-2];
          b=c=panel[tj*RomWidth-1];
          d=g=panel[RomHeight*RomWidth-2];
          e=f=h=i=panel[RomHeight*RomWidth-1];
        }
        else if (ti==0)
        {
          a=b=panel[(tj-1)*RomWidth];
          c=panel[(tj-1)*RomWidth+1];
          d=e=panel[tj*RomWidth];
          f=panel[tj*RomWidth+1];
          g=h=panel[(tj+1)*RomWidth];
          i=panel[(tj+1)*RomWidth+1];
        }
        else if (ti==RomWidth-1)
        {
          a=panel[tj*RomWidth-2];
          b=c=panel[tj*RomWidth-1];
          d=panel[(tj+1)*RomWidth-2];
          e=f=panel[(tj+1)*RomWidth-1];
          g=panel[(tj+2)*RomWidth-2];
          h=i=panel[(tj+2)*RomWidth-1];
        }
        else if (tj==0)
        {
          a=d=panel[ti-1];
          b=e=panel[ti];
          c=f=panel[ti+1];
          g=panel[RomWidth+ti-1];
          h=panel[RomWidth+ti];
          i=panel[RomWidth+ti+1];
        }
        else if (tj==RomHeight-1)
        {
          a=panel[(tj-1)*RomWidth+ti-1];
          b=panel[(tj-1)*RomWidth+ti];
          c=panel[(tj-1)*RomWidth+ti+1];
          d=g=panel[tj*RomWidth+ti-1];
          e=h=panel[tj*RomWidth+ti];
          f=i=panel[tj*RomWidth+ti+1];
        }
        else
        {
          a=panel[(tj-1)*RomWidth+ti-1];
          b=panel[(tj-1)*RomWidth+ti];
          c=panel[(tj-1)*RomWidth+ti+1];
          d=panel[tj*RomWidth+ti-1];
          e=panel[tj*RomWidth+ti];
          f=panel[tj*RomWidth+ti+1];
          g=panel[(tj+1)*RomWidth+ti-1];
          h=panel[(tj+1)*RomWidth+ti];
          i=panel[(tj+1)*RomWidth+ti+1];
        }
        if (b != h && d != f) {
          renderBuffer[tj*2*TOTAL_WIDTH+ti*2+xoffset] = d == b ? d : e;
          renderBuffer[tj*2*TOTAL_WIDTH+ti*2+1+xoffset] = b == f ? f : e;
          renderBuffer[(tj*2+1)*TOTAL_WIDTH+ti*2+xoffset] = d == h ? d : e;
          renderBuffer[(tj*2+1)*TOTAL_WIDTH+ti*2+1+xoffset] = h == f ? f : e;
        } else {
          renderBuffer[tj*2*TOTAL_WIDTH+ti*2+xoffset] = e;
          renderBuffer[tj*2*TOTAL_WIDTH+ti*2+1+xoffset] = e;
          renderBuffer[(tj*2+1)*TOTAL_WIDTH+ti*2+xoffset] = e;
          renderBuffer[(tj*2+1)*TOTAL_WIDTH+ti*2+1+xoffset] = e;
        }
      }
    }
  }
  else //offset!=0
  {
    memset(renderBuffer, 0, TOTAL_WIDTH * TOTAL_HEIGHT);

    for (int tj = 0; tj < RomHeight; tj++)
    {
      for (int ti = 0; ti < RomWidth; ti++)
      {
        renderBuffer[tj * TOTAL_WIDTH + xoffset + ti] = panel[tj * RomWidth + ti];
      }
    }
  }

  free(panel);
}

void fillPanelRaw()
{
  int r;
  int g;
  int b;

  int pos;

  for (int tj = 0; tj < TOTAL_HEIGHT; tj++)
  {
    for (int ti = 0; ti < TOTAL_WIDTH; ti++)
    {
      pos = ti + tj * TOTAL_WIDTH;
      r = renderBuffer[ti * 3 + tj * 3 * TOTAL_WIDTH + ordreRGB[acordreRGB * 3]];
      g = renderBuffer[ti * 3 + tj * 3 * TOTAL_WIDTH + ordreRGB[acordreRGB * 3 + 1]];
      b = renderBuffer[ti * 3 + tj * 3 * TOTAL_WIDTH + ordreRGB[acordreRGB * 3 + 2]];

      if (r != doubleBufferR[tj][ti] || g != doubleBufferG[tj][ti] || b != doubleBufferB[tj][ti])
      {
        doubleBufferR[tj][ti] = r;
        doubleBufferG[tj][ti] = g;
        doubleBufferB[tj][ti] = b;

        dma_display->drawPixelRGB888(ti, tj, r, g, b);
      }
    }
  }
}

void fillPanelUsingPalette()
{
  int r;
  int g;
  int b;

  int pos;

  for (int tj = 0; tj < TOTAL_HEIGHT; tj++)
  {
    for (int ti = 0; ti < TOTAL_WIDTH; ti++)
    {
      pos = ti + tj * TOTAL_WIDTH;
      r = palette[rotCols[renderBuffer[pos]] * 3 + ordreRGB[acordreRGB * 3]];
      g = palette[rotCols[renderBuffer[pos]] * 3 + ordreRGB[acordreRGB * 3 + 1]];
      b = palette[rotCols[renderBuffer[pos]] * 3 + ordreRGB[acordreRGB * 3 + 2]];

      if (r != doubleBufferR[tj][ti] || g != doubleBufferG[tj][ti] || b != doubleBufferB[tj][ti])
      {
        doubleBufferR[tj][ti] = r;
        doubleBufferG[tj][ti] = g;
        doubleBufferB[tj][ti] = b;

        dma_display->drawPixelRGB888(ti, tj, r, g, b);
      }
    }
  }
}

void LoadOrdreRGB()
{
  File fordre=LittleFS.open("/ordrergb.val", "r");
  if (!fordre) return;
  acordreRGB=fordre.read();
  fordre.close();
}

void SaveOrdreRGB()
{
  File fordre=LittleFS.open("/ordrergb.val", "w");
  fordre.write(acordreRGB);
  fordre.close();
}

void LoadLum()
{
  File flum=LittleFS.open("/lum.val", "r");
  if (!flum) return;
  lumstep=flum.read();
  flum.close();
}

void SaveLum()
{
  File flum=LittleFS.open("/lum.val", "w");
  flum.write(lumstep);
  flum.close();
}

void DisplayLogo(void)
{
  File flogo;
  if (TOTAL_HEIGHT==64) flogo=LittleFS.open("/logoHD.raw", "r"); else flogo=LittleFS.open("/logo.raw", "r");
  if (!flogo) {
    //Serial.println("Failed to open file for reading");
    return;
  }
  renderBuffer = (unsigned char*) malloc(TOTAL_BYTES);
  for (unsigned int tj = 0; tj < TOTAL_BYTES; tj++)
  {
    renderBuffer[tj] = flogo.read();
  }
  flogo.close();
  fillPanelRaw();

  free(renderBuffer);

  DisplayVersion();
}

void setup()
{
  rgbOrderButton = new Bounce2::Button();
  rgbOrderButton->attach(ORDRE_BUTTON_PIN, INPUT_PULLUP);
  rgbOrderButton->interval(100);
  rgbOrderButton->setPressedState(LOW);

  brightnessButton = new Bounce2::Button();
  brightnessButton->attach(LUMINOSITE_BUTTON_PIN, INPUT_PULLUP);
  brightnessButton->interval(100);
  brightnessButton->setPressedState(LOW);

  mxconfig.clkphase = false; // change if you have some parts of the panel with a shift
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();

  if (!LittleFS.begin()) {
    Say(0, 9999);
    delay(4000);
  }

  Serial.setRxBufferSize(SERIAL_BUFFER);
  Serial.setTimeout(SERIAL_TIMEOUT);
  Serial.begin(SERIAL_BAUD);
  while (!Serial);

  LoadLum();

  dma_display->setBrightness8(lumval[lumstep]);    // range is 0-255, 0 - 0%, 255 - 100%
  dma_display->clearScreen();

  LoadOrdreRGB();

  DisplayLogo();
  DisplayText(lumtxt,16,TOTAL_WIDTH/2-16/2-2*4/2,TOTAL_HEIGHT-5,255,255,255);
  DisplayLum();
}

bool SerialReadBuffer(unsigned char* pBuffer, unsigned int BufferSize)
{
  memset(pBuffer, 0, BufferSize);

  unsigned int transferBufferSize = BufferSize;
  unsigned char* transferBuffer;

  if (compression)
  {
    uint8_t byteArray[2];
    Serial.readBytes(byteArray, 2);
    transferBufferSize = (
      (((unsigned int) byteArray[0]) << 8) +
      ((unsigned int) byteArray[1])
    );

    transferBuffer = (unsigned char*) malloc(transferBufferSize);
  }
  else
  {
    transferBuffer = pBuffer;
  }

  if (debugMode)
  {
    Say(1, transferBufferSize);
    debugLines[1] = transferBufferSize;
  }

  // We always receive chunks of "serialTransferChunkSize" bytes (maximum).
  // At this point, the control chars and the one byte command have been read already.
  // So we only need to read the remaining bytes of the first chunk and full chunks afterwards.
  int chunkSize = serialTransferChunkSize - N_CTRL_CHARS - 1 - (compression ? 2 : 0);
  int remainingBytes = transferBufferSize;
  while (remainingBytes > 0)
  {
    int receivedBytes = Serial.readBytes(transferBuffer + transferBufferSize - remainingBytes, (remainingBytes > chunkSize) ? chunkSize : remainingBytes);
    if (debugMode)
    {
      Say(2, receivedBytes);
      debugLines[2] = receivedBytes;
    }
    if (receivedBytes != remainingBytes && receivedBytes != chunkSize)
    {
      if (debugMode)
      {
        Say(9, remainingBytes);
        Say(10, chunkSize);
        Say(11, receivedBytes);
        debugLines[4] = ++errorCount;
      }

      // Send an (E)rror signal to tell the client that no more chunks should be send or to repeat the entire frame from the beginning.
      Serial.write('E');

      return false;
    }

    // Send an (A)cknowledge signal to tell the client that we successfully read the chunk.
    Serial.write('A');
    remainingBytes -= chunkSize;
    // From now on read full amount of byte chunks.
    chunkSize = serialTransferChunkSize;
  }

  if (compression) {
    mz_ulong uncompressed_buffer_size = (mz_ulong) BufferSize;
    int status = uncompress(pBuffer, &uncompressed_buffer_size, transferBuffer, (mz_ulong) transferBufferSize);
    free(transferBuffer);

    if (debugMode && (Z_OK != status))
    {
      int tmp_status = (status >= 0) ? status : (-1 * status) + 100;
      Say(3, tmp_status);
      debugLines[3] = tmp_status;
    }

    if ((Z_OK == status) && (uncompressed_buffer_size == BufferSize)) {
      // Some HD panels take too long too render. The small ZeDMD seems to be fast enough to send a fast (R)eady signal here.
      if (TOTAL_WIDTH <= 128) {
        //sendFastReady();
      }

      if (debugMode)
      {
        Say(3, 0);
        debugLines[3] = 0;
      }

      return true;
    }

    if (debugMode && (Z_OK == status))
    {
      // uncrompessed data isn't of expected size
      Say(3, 99);
      debugLines[3] = 99;
    }

    Serial.write('E');
    return false;
  }

  sendFastReady();
  return true;
}

void updateColorRotations(void)
{
  unsigned long actime=millis();
  bool rotfound=false;
  for (int ti=0;ti<MAX_COLOR_ROTATIONS;ti++)
  {
    if (firstCol[ti]==255) continue;
    if (actime >= nextTime[ti])
    {
        nextTime[ti] = actime + timeSpan[ti];
        acFirst[ti]++;
        if (acFirst[ti] == nCol[ti]) acFirst[ti] = 0;
        rotfound=true;
        for (unsigned char tj = 0; tj < nCol[ti]; tj++)
        {
            rotCols[tj + firstCol[ti]] = tj + firstCol[ti] + acFirst[ti];
            if (rotCols[tj + firstCol[ti]] >= firstCol[ti] + nCol[ti]) rotCols[tj + firstCol[ti]] -= nCol[ti];
        }
    }
  }
  if (rotfound==true) fillPanelUsingPalette();
}

void wait_for_ctrl_chars(void)
{
  unsigned long ms = millis();
  unsigned char nCtrlCharFound = 0;

  while (nCtrlCharFound < N_CTRL_CHARS)
  {
    if (Serial.available())
    {
      if (Serial.read() != CtrlCharacters[nCtrlCharFound++]) nCtrlCharFound = 0;
    }

    if (mode64 && nCtrlCharFound == 0)
    {
      // While waiting for the next frame, perform in-frame color rotations.
      updateColorRotations();
    }

    // Watchdog: "reset" the communictaion if it took too long between two frames.
    if (handshakeSucceeded && ((millis() - ms) > FRAME_TIMEOUT))
    {
      if (debugMode)
      {
        Say(5, ++watchdogCount);
        debugLines[5] = watchdogCount;
      }

      // Send an (E)rror signal.
      Serial.write('E');
      // Send a (R)eady signal to tell the client to send the next command.
      Serial.write('R');

      ms = millis();
      nCtrlCharFound = 0;
    }
  }
}

void loop()
{
  while (MireActive)
  {
    rgbOrderButton->update();
    if (rgbOrderButton->pressed())
    {
      acordreRGB++;
      if (acordreRGB >= 6) acordreRGB = 0;
      SaveOrdreRGB();
      fillPanelRaw();
      DisplayText(lumtxt,16,TOTAL_WIDTH/2-16/2-2*4/2,TOTAL_HEIGHT-5,255,255,255);
      DisplayLum();
    }

    brightnessButton->update();
    if (brightnessButton->pressed())
    {
      lumstep++;
      if (lumstep>=16) lumstep=1;
      dma_display->setBrightness8(lumval[lumstep]);
      DisplayLum();
      SaveLum();
    }

    if (Serial.available()>0)
    {
      dma_display->clearScreen();
      MireActive = false;
    }
  }

  rgbOrderButton->update();
  if (rgbOrderButton->pressed())
  {
    debugMode = !debugMode;
  }

  // After handshake, send a (R)eady signal to indicate that a new command could be sent.
  // The client has to wait for it to avoid buffer issues. The handshake it self works without it.
  if (handshakeSucceeded) {
    if (!fastReadySent) {
      Serial.write('R');
    }
    else {
      fastReadySent = false;
    }
  }

  wait_for_ctrl_chars();

  // Updates to mode64 color rotations have been handled within wait_for_ctrl_chars(), now reset it to false.
  mode64 = false;

  // Commands:
  //  2: set rom frame size
  //  3: render raw data
  //  6: init palette (deprectated)
  //  7: render 16 colors using a 4 color palette (3*4 bytes), 2 pixels per byte
  //  8: render 4 colors using a 4 color palette (3*4 bytes), 4 pixels per byte
  //  9: render 16 colors using a 16 color palette (3*16 bytes), 4 bytes per group of 8 pixels (encoded as 4*512 bytes planes)
  // 10: clear screen
  // 11: render 64 colors using a 64 color palette (3*64 bytes), 6 bytes per group of 8 pixels (encoded as 6*512 bytes planes)
  // 12: handshake + report resolution
  // 13: set serial transfer chunk size
  // 14: enable serial transfer compression
  // todo 20: turn off upscaling
  // todo 21: turn on upscaling
  // todo 22: set brightness
  // 98: disable debug mode
  // 99: enable debug mode
  unsigned char c4;
  while (Serial.available()==0);
  c4=Serial.read();

  if (c4 == 12) // ask for resolution (and shake hands)
  {
    for (int ti=0;ti<N_INTERMEDIATE_CTR_CHARS;ti++) Serial.write(CtrlCharacters[ti]);
    Serial.write(TOTAL_WIDTH&0xff);
    Serial.write((TOTAL_WIDTH>>8)&0xff);
    Serial.write(TOTAL_HEIGHT&0xff);
    Serial.write((TOTAL_HEIGHT>>8)&0xff);
    handshakeSucceeded=true;
  }
  else if (c4 == 2) // get rom frame dimension
  {
    unsigned char tbuf[4];
    if (SerialReadBuffer(tbuf,4))
    {
      RomWidth=(int)(tbuf[0])+(int)(tbuf[1]<<8);
      RomHeight=(int)(tbuf[2])+(int)(tbuf[3]<<8);
      RomWidthPlane=RomWidth>>3;
      if (debugMode) {
        DisplayNombre(RomWidth, 3, TOTAL_WIDTH - 7*4, 4, 150, 150, 150);
        DisplayNombre(RomHeight, 2, TOTAL_WIDTH - 3*4, 4, 150, 150, 150);
      }
    }
  }
  else if (c4 == 13) // set serial transfer chunk size
  {
    while (Serial.available() == 0);
    int tmpSerialTransferChunkSize = ((int) Serial.read()) * 256;
    if (tmpSerialTransferChunkSize <= SERIAL_BUFFER) {
      serialTransferChunkSize = tmpSerialTransferChunkSize;
      // Send an (A)cknowledge signal to tell the client that we successfully read the chunk.
      Serial.write('A');
    }
    else {
      Serial.write('E');
    }
  }
  else if (c4 == 14) // enable serial transfer compression
  {
    compression = true;
    // Send an (A)cknowledge signal to tell the client that we successfully read the chunk.
    Serial.write('A');
  }
  else if (c4 == 98) // disable debug mode
  {
    debugMode = false;
    Serial.write('A');
  }
  else if (c4 == 99) // enable debug mode
  {
    debugMode = true;
    Serial.write('A');
  }
  else if (c4 == 6) // reinit palette (deprecated)
  {
    // Just backward compatibility. We don't need that command anymore.

    // Send an (A)cknowledge signal to tell the client that we successfully read the chunk.
    Serial.write('A');
  }
  else if (c4 == 10) // clear screen
  {
    // Send an (A)cknowledge signal to tell the client that we successfully read the chunk.
    Serial.write('A');
    dma_display->clearScreen();
  }
  else if (c4 == 3) // mode RGB24
  {
    // We need to cover downscaling, too.
    int renderBufferSize = RomWidth < TOTAL_WIDTH ? TOTAL_BYTES : RomWidth * RomHeight * 3;
    renderBuffer = (unsigned char*) malloc(renderBufferSize);
    memset(renderBuffer, 0, renderBufferSize);

    if (SerialReadBuffer(renderBuffer, RomHeight * RomWidth * 3))
    {
      mode64=false;
      ScaleImage();
      fillPanelRaw();
    }

    free(renderBuffer);
  }
  else if (c4 == 8) // mode 4 couleurs avec 1 palette 4 couleurs (4*3 bytes) suivis de 4 pixels par byte
  {
    int bufferSize = 3*4 + 2*RomWidthPlane*RomHeight;
    unsigned char* buffer = (unsigned char*) malloc(bufferSize);

    if (SerialReadBuffer(buffer, bufferSize))
    {
      // We need to cover downscaling, too.
      int renderBufferSize = RomWidth < TOTAL_WIDTH ? TOTAL_WIDTH * TOTAL_HEIGHT : RomWidth * RomHeight;
      renderBuffer = (unsigned char*) malloc(renderBufferSize);
      memset(renderBuffer, 0, renderBufferSize);
      palette = (unsigned char*) malloc(3*4);
      memset(palette, 0, 3*4);

      for (int ti = 3; ti >= 0; ti--)
      {
        palette[ti * 3] = buffer[ti*3];
        palette[ti * 3 + 1] = buffer[ti*3+1];
        palette[ti * 3 + 2] = buffer[ti*3+2];
      }
      unsigned char* frame = &buffer[3*4];
      for (int tj = 0; tj < RomHeight; tj++)
      {
        for (int ti = 0; ti < RomWidthPlane; ti++)
        {
          unsigned char mask = 1;
          unsigned char planes[2];
          planes[0] = frame[ti + tj * RomWidthPlane];
          planes[1] = frame[RomWidthPlane*RomHeight + ti + tj * RomWidthPlane];
          for (int tk = 0; tk < 8; tk++)
          {
            unsigned char idx = 0;
            if ((planes[0] & mask) > 0) idx |= 1;
            if ((planes[1] & mask) > 0) idx |= 2;
            renderBuffer[(ti * 8 + tk) + tj * RomWidth]=idx;
            mask <<= 1;
          }
        }
      }
      free(buffer);

      mode64=false;
      for (int ti=0;ti<64;ti++) rotCols[ti]=ti;

      ScaleImage64();
      fillPanelUsingPalette();

      free(renderBuffer);
      free(palette);
    }
    else
    {
      free(buffer);
    }
  }
  else if (c4 == 7) // mode 16 couleurs avec 1 palette 4 couleurs (4*3 bytes) suivis de 2 pixels par byte
  {
    int bufferSize = 3*4 + 4*RomWidthPlane*RomHeight;
    unsigned char* buffer = (unsigned char*) malloc(bufferSize);

    if (SerialReadBuffer(buffer, bufferSize))
    {
      // We need to cover downscaling, too.
      int renderBufferSize = RomWidth < TOTAL_WIDTH ? TOTAL_WIDTH * TOTAL_HEIGHT : RomWidth * RomHeight;
      renderBuffer = (unsigned char*) malloc(renderBufferSize);
      memset(renderBuffer, 0, renderBufferSize);
      palette = (unsigned char*) malloc(48);
      memset(palette, 0, 48);

      for (int ti = 3; ti >= 0; ti--)
      {
        palette[(4 * ti + 3)* 3] = buffer[ti*3];
        palette[(4 * ti + 3) * 3 + 1] = buffer[ti*3+1];
        palette[(4 * ti + 3) * 3 + 2] = buffer[ti*3+2];
      }
      palette[0]=palette[1]=palette[2]=0;
      palette[3]=palette[3*3]/3;
      palette[4]=palette[3*3+1]/3;
      palette[5]=palette[3*3+2]/3;
      palette[6]=2*(palette[3*3]/3);
      palette[7]=2*(palette[3*3+1]/3);
      palette[8]=2*(palette[3*3+2]/3);

      palette[12]=palette[3*3]+(palette[7*3]-palette[3*3])/4;
      palette[13]=palette[3*3+1]+(palette[7*3+1]-palette[3*3+1])/4;
      palette[14]=palette[3*3+2]+(palette[7*3+2]-palette[3*3+2])/4;
      palette[15]=palette[3*3]+2*((palette[7*3]-palette[3*3])/4);
      palette[16]=palette[3*3+1]+2*((palette[7*3+1]-palette[3*3+1])/4);
      palette[17]=palette[3*3+2]+2*((palette[7*3+2]-palette[3*3+2])/4);
      palette[18]=palette[3*3]+3*((palette[7*3]-palette[3*3])/4);
      palette[19]=palette[3*3+1]+3*((palette[7*3+1]-palette[3*3+1])/4);
      palette[20]=palette[3*3+2]+3*((palette[7*3+2]-palette[3*3+2])/4);

      palette[24]=palette[7*3]+(palette[11*3]-palette[7*3])/4;
      palette[25]=palette[7*3+1]+(palette[11*3+1]-palette[7*3+1])/4;
      palette[26]=palette[7*3+2]+(palette[11*3+2]-palette[7*3+2])/4;
      palette[27]=palette[7*3]+2*((palette[11*3]-palette[7*3])/4);
      palette[28]=palette[7*3+1]+2*((palette[11*3+1]-palette[7*3+1])/4);
      palette[29]=palette[7*3+2]+2*((palette[11*3+2]-palette[7*3+2])/4);
      palette[30]=palette[7*3]+3*((palette[11*3]-palette[7*3])/4);
      palette[31]=palette[7*3+1]+3*((palette[11*3+1]-palette[7*3+1])/4);
      palette[32]=palette[7*3+2]+3*((palette[11*3+2]-palette[7*3+2])/4);

      palette[36]=palette[11*3]+(palette[15*3]-palette[11*3])/4;
      palette[37]=palette[11*3+1]+(palette[15*3+1]-palette[11*3+1])/4;
      palette[38]=palette[11*3+2]+(palette[15*3+2]-palette[11*3+2])/4;
      palette[39]=palette[11*3]+2*((palette[15*3]-palette[11*3])/4);
      palette[40]=palette[11*3+1]+2*((palette[15*3+1]-palette[11*3+1])/4);
      palette[41]=palette[11*3+2]+2*((palette[15*3+2]-palette[11*3+2])/4);
      palette[42]=palette[11*3]+3*((palette[15*3]-palette[11*3])/4);
      palette[43]=palette[11*3+1]+3*((palette[15*3+1]-palette[11*3+1])/4);
      palette[44]=palette[11*3+2]+3*((palette[15*3+2]-palette[11*3+2])/4);

      unsigned char* img=&buffer[3*4];
      for (int tj = 0; tj < RomHeight; tj++)
      {
        for (int ti = 0; ti < RomWidthPlane; ti++)
        {
          unsigned char mask = 1;
          unsigned char planes[4];
          planes[0] = img[ti + tj * RomWidthPlane];
          planes[1] = img[RomWidthPlane*RomHeight + ti + tj * RomWidthPlane];
          planes[2] = img[2*RomWidthPlane*RomHeight + ti + tj * RomWidthPlane];
          planes[3] = img[3*RomWidthPlane*RomHeight + ti + tj * RomWidthPlane];
          for (int tk = 0; tk < 8; tk++)
          {
            unsigned char idx = 0;
            if ((planes[0] & mask) > 0) idx |= 1;
            if ((planes[1] & mask) > 0) idx |= 2;
            if ((planes[2] & mask) > 0) idx |= 4;
            if ((planes[3] & mask) > 0) idx |= 8;
            renderBuffer[(ti * 8 + tk)+tj * RomWidth]=idx;
            mask <<= 1;
          }
        }
      }
      free(buffer);

      mode64=false;
      for (int ti=0;ti<64;ti++) rotCols[ti]=ti;

      ScaleImage64();
      fillPanelUsingPalette();

      free(renderBuffer);
      free(palette);
    }
    else
    {
      free(buffer);
    }
  }
  else if (c4 == 9) // mode 16 couleurs avec 1 palette 16 couleurs (16*3 bytes) suivis de 4 bytes par groupe de 8 points (séparés en plans de bits 4*512 bytes)
  {
    int bufferSize = 3*16 + 4*RomWidthPlane*RomHeight;
    unsigned char* buffer = (unsigned char*) malloc(bufferSize);

    if (SerialReadBuffer(buffer, bufferSize))
    {
      // We need to cover downscaling, too.
      int renderBufferSize = RomWidth < TOTAL_WIDTH ? TOTAL_WIDTH * TOTAL_HEIGHT : RomWidth * RomHeight;
      renderBuffer = (unsigned char*) malloc(renderBufferSize);
      memset(renderBuffer, 0, renderBufferSize);
      palette = (unsigned char*) malloc(3*16);
      memset(palette, 0, 3*16);

      for (int ti = 15; ti >= 0; ti--)
      {
        palette[ti * 3] = buffer[ti*3];
        palette[ti * 3 + 1] = buffer[ti*3+1];
        palette[ti * 3 + 2] = buffer[ti*3+2];
      }
      unsigned char* img=&buffer[3*16];
      for (int tj = 0; tj < RomHeight; tj++)
      {
        for (int ti = 0; ti < RomWidthPlane; ti++)
        {
          // on reconstitue un indice à partir des plans puis une couleur à partir de la palette
          unsigned char mask = 1;
          unsigned char planes[4];
          planes[0] = img[ti + tj * RomWidthPlane];
          planes[1] = img[RomWidthPlane*RomHeight + ti + tj * RomWidthPlane];
          planes[2] = img[2*RomWidthPlane*RomHeight + ti + tj * RomWidthPlane];
          planes[3] = img[3*RomWidthPlane*RomHeight + ti + tj * RomWidthPlane];
          for (int tk = 0; tk < 8; tk++)
          {
            unsigned char idx = 0;
            if ((planes[0] & mask) > 0) idx |= 1;
            if ((planes[1] & mask) > 0) idx |= 2;
            if ((planes[2] & mask) > 0) idx |= 4;
            if ((planes[3] & mask) > 0) idx |= 8;
            renderBuffer[(ti * 8 + tk)+tj * RomWidth]=idx;
            mask <<= 1;
          }
        }
      }
      free(buffer);

      mode64=false;
      for (int ti=0;ti<64;ti++) rotCols[ti]=ti;

      ScaleImage64();
      fillPanelUsingPalette();

      free(renderBuffer);
      free(palette);
    }
    else
    {
      free(buffer);
    }
  }
  else if (c4 == 11) // mode 64 couleurs avec 1 palette 64 couleurs (64*3 bytes) suivis de 6 bytes par groupe de 8 points (séparés en plans de bits 6*512 bytes) suivis de 3*8 bytes de rotations de couleurs
  {
    int bufferSize = 3*64 + 6*RomWidthPlane*RomHeight + 3*MAX_COLOR_ROTATIONS;
    unsigned char* buffer = (unsigned char*) malloc(bufferSize);

    if (SerialReadBuffer(buffer, bufferSize))
    {
      // We need to cover downscaling, too.
      int renderBufferSize = RomWidth < TOTAL_WIDTH ? TOTAL_WIDTH * TOTAL_HEIGHT : RomWidth * RomHeight;
      renderBuffer = (unsigned char*) malloc(renderBufferSize);
      memset(renderBuffer, 0, renderBufferSize);
      palette = (unsigned char*) malloc(3*64);
      memset(palette, 0, 3*64);

      for (int ti = 63; ti >= 0; ti--)
      {
        palette[ti * 3] = buffer[ti*3];
        palette[ti * 3 + 1] = buffer[ti*3+1];
        palette[ti * 3 + 2] = buffer[ti*3+2];
      }
      unsigned char* img = &buffer[3*64];
      for (int tj = 0; tj < RomHeight; tj++)
      {
        for (int ti = 0; ti < RomWidthPlane; ti++)
        {
          // on reconstitue un indice à partir des plans puis une couleur à partir de la palette
          unsigned char mask = 1;
          unsigned char planes[6];
          planes[0] = img[ti + tj * RomWidthPlane];
          planes[1] = img[RomWidthPlane*RomHeight + ti + tj * RomWidthPlane];
          planes[2] = img[2*RomWidthPlane*RomHeight + ti + tj * RomWidthPlane];
          planes[3] = img[3*RomWidthPlane*RomHeight + ti + tj * RomWidthPlane];
          planes[4] = img[4*RomWidthPlane*RomHeight + ti + tj * RomWidthPlane];
          planes[5] = img[5*RomWidthPlane*RomHeight + ti + tj * RomWidthPlane];
          for (int tk = 0; tk < 8; tk++)
          {
            unsigned char idx = 0;
            if ((planes[0] & mask) > 0) idx |= 1;
            if ((planes[1] & mask) > 0) idx |= 2;
            if ((planes[2] & mask) > 0) idx |= 4;
            if ((planes[3] & mask) > 0) idx |= 8;
            if ((planes[4] & mask) > 0) idx |= 0x10;
            if ((planes[5] & mask) > 0) idx |= 0x20;
            renderBuffer[(ti * 8 + tk)+tj * RomWidth]=idx;
            mask <<= 1;
          }
        }
      }
      img = &buffer[3*64 + 6*RomWidthPlane*RomHeight];
      unsigned long actime = millis();
      for (int ti=0;ti<64;ti++) rotCols[ti]=ti;
      for (int ti=0;ti<MAX_COLOR_ROTATIONS;ti++)
      {
        firstCol[ti]=img[ti*3];
        nCol[ti]=img[ti*3+1];
        // acFirst[ti]=0;
        timeSpan[ti]=10*img[ti*3+2];
        if (timeSpan[ti]<MIN_SPAN_ROT) timeSpan[ti]=MIN_SPAN_ROT;
        nextTime[ti]=actime+timeSpan[ti];
      }
      free(buffer);

      mode64=true;

      ScaleImage64();
      fillPanelUsingPalette();

      free(renderBuffer);
      free(palette);
    }
    else
    {
      free(buffer);
    }
  }
  if (debugMode)
  {
    // An overflow of the unsigned int counters should not be an issue, they just reset to 0.
    debugLines[0] = ++frameCount;
    for (int i = 0; i < 6; i++)
    {
      Say((unsigned char) i, debugLines[i]);
    }
  }
}
