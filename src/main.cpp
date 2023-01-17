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

#define SERIAL_TIMEOUT 100   // Time in milliseconds to wait for the next data chunk.
#define FRAME_TIMEOUT  10000 // Time in milliseconds to wait for a new frame.
#define DEBUG_FRAMES   0     // Set to 1 to output number of rendered frames on top and number of error at the bottom.
// ------------------------------------------ ZeDMD by Zedrummer (http://pincabpassion.net)---------------------------------------------
// - Install the ESP32 board in Arduino IDE as explained here https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/
// - Install "ESP32 HUB75 LED MATRIX panel DMA" Display library via the library manager
// - Go to menu "Tools" then click on "ESP32 Sketch Data Upload"
// - Change the values in the 3 first lines above (PANEL_WIDTH, PANEL_HEIGHT, PANELS_NUMBER) according to your panels
// - Inject this code in the board
// - If you have blurry pictures, the display is not clean, try to reduce the input voltage of your LED matrix panels, often, 5V panels need
// between 4V and 4.5V to display clean pictures, you often have a screw in switch-mode power supplies to change the output voltage a little bit
// - While the initial pattern logo is displayed, check you have red in the upper left, green in the lower left and blue in the upper right,
// if not, make contact between the ORDRE_BUTTON_PIN (default 21, but you can change below) pin and a ground pin several times
// until the display is correct (automatically saved, no need to do it again)
// -----------------------------------------------------------------------------------------------------------------------------------------

#define PANE_WIDTH (PANEL_WIDTH*PANELS_NUMBER)
#define PANE_WIDTH_PLANE (PANE_WIDTH>>3)
#define PANE_HEIGHT PANEL_HEIGHT
#define MAX_COLOR_ROTATIONS 8
#define MIN_SPAN_ROT 60

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <LittleFS.h>

/* Pinout from ESP32-HUB75-MatrixPanel-I2S-DMA.h
    #define R1_PIN_DEFAULT  25
    #define G1_PIN_DEFAULT  26
    #define B1_PIN_DEFAULT  27
    #define R2_PIN_DEFAULT  14
    #define G2_PIN_DEFAULT  12
    #define B2_PIN_DEFAULT  13

    #define A_PIN_DEFAULT   23
    #define B_PIN_DEFAULT   19
    #define C_PIN_DEFAULT   5
    #define D_PIN_DEFAULT   17
    #define E_PIN_DEFAULT   -1 // IMPORTANT: Change to a valid pin if using a 64x64px panel.
              
    #define LAT_PIN_DEFAULT 4
    #define OE_PIN_DEFAULT  15
    #define CLK_PIN_DEFAULT 16
 */

// Change these to whatever suits
#define R1_PIN 25
#define G1_PIN 26
#define B1_PIN 27
#define R2_PIN 14
#define G2_PIN 12
#define B2_PIN 13
#define A_PIN 23
#define B_PIN 19
#define C_PIN 5
#define D_PIN 17
#define E_PIN 22 // required for 1/32 scan panels, like 64x64. Any available pin would do, i.e. IO32. If 1/16 scan panels, no connection to this pin needed
#define LAT_PIN 4
#define OE_PIN 15
#define CLK_PIN 16

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
          PANEL_WIDTH,   // width
          PANEL_HEIGHT,   // height
           PANELS_NUMBER,   // chain length
         _pins//,   // pin mapping
         //HUB75_I2S_CFG::FM6126A      // driver chip
);
MatrixPanel_I2S_DMA *dma_display = nullptr;

int ordreRGB[3*6]={0,1,2, 2,0,1, 1,2,0,
                   0,2,1, 1,0,2, 2,1,0};
int acordreRGB=0;

unsigned char Palette4[3*4]; // palettes 4 couleurs et 16 couleurs en RGB
static const float levels4[4]  = {10,33,67,100};
unsigned char Palette16[3*16];
static const float levels16[16]  = {0, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 90, 100}; // SAM brightness seems okay
unsigned char Palette64[3*64];
static const float levels64[64]  = {0, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 90, 100}; // SAM brightness seems okay

unsigned char panel[256*64*3];
unsigned char panel2[256*64];

#define ORDRE_BUTTON_PIN 21
bool OrdreBtnRel=false;
int OrdreBtnPos;
unsigned long OrdreBtnDebounceTime;

#define LUMINOSITE_BUTTON_PIN 33
bool LuminositeBtnRel=false;
int LuminositeBtnPos;
unsigned long LuminositeBtnDebounceTime;

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

#define DEBOUNCE_DELAY 100 // in ms, to avoid buttons pushes to be counted several times https://www.arduino.cc/en/Tutorial/BuiltInExamples/Debounce
unsigned char CheckButton(int btnpin,bool *pbtnrel,int *pbtpos,unsigned long *pbtdebouncetime)
{
  // return 1 if the button has been released, 2 if the button has been pushed, 0 if nothing has changed since previous call
  // Debounce the input as explained here https://www.arduino.cc/en/Tutorial/BuiltInExamples/Debounce
  int btnPos=digitalRead(btnpin);
  unsigned long actime=millis();
  if (btnPos != (*pbtpos))
  {
    if (actime > (*pbtdebouncetime) + DEBOUNCE_DELAY)
    {
      if ((btnPos == HIGH) && ((*pbtnrel) == false))
      {
        (*pbtnrel) = true;
        (*pbtdebouncetime) = actime;
        (*pbtpos) = btnPos;
        return 1;
      }
      else if ((btnPos == LOW) && ((*pbtnrel) == true))
      {
        (*pbtnrel) = false;
        (*pbtdebouncetime) = actime;
        (*pbtpos) = btnPos;
        return 2;
      }
      (*pbtdebouncetime) = actime;
      (*pbtpos) = btnPos;
    }
  }
  return 0;
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

unsigned char lumstep=1;

void DisplayLum(void)
{
  DisplayNombre(lumstep,2,PANE_WIDTH/2-16/2-2*4/2+16,PANE_HEIGHT-5,255,255,255);
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

bool CmpColor(unsigned char* px1,unsigned char* px2)
{
  if ((px1[0]==px2[0])&&(px1[1]==px2[1])&&(px1[2]==px2[2])) return true;
  return false;
}

void SetColor(unsigned char* px1,unsigned char* px2)
{
  px1[0]=px2[0];
  px1[1]=px2[1];
  px1[2]=px2[2];
}

void ScaleImage() // scale for non indexed image (RGB24)
{
  int xoffset=0;
  memcpy(panel2,panel,RomWidth*RomHeight*3);
  memset(panel,0,PANE_WIDTH*PANE_HEIGHT*3);
  int scale=0; // 0 - no scale, 1 - half scale, 2 - twice scale
  if ((RomWidth==192)&&(PANE_WIDTH==256)) xoffset=32*3;
  else if (RomWidth==192)
  {
    xoffset=16*3;
    scale=1;
  }
  else if ((RomWidth==256)&&(PANE_WIDTH==128)) scale=1;
  else if ((RomWidth==128)&&(PANE_WIDTH==256)) scale=2;
  else return;

  if (scale==1)
  {
    // for half scaling we take the 4 points and look if there is one colour repeated
    for (int ti=0;ti<RomHeight;ti+=2)
    {
      for (int tj=0;tj<RomWidth;tj+=2)
      {
        unsigned char* pp=&panel2[ti*RomWidth*3+tj*3];
        if (CmpColor(pp,&pp[3])||CmpColor(pp,&pp[3*RomWidth])||CmpColor(pp,&pp[3*RomWidth+3])) SetColor(&panel[xoffset+3*(tj>>1+(ti>>1)*PANE_WIDTH)],pp);
        else if (CmpColor(&pp[3],&pp[3*RomWidth])||CmpColor(&pp[3],&pp[3*RomWidth+3])) SetColor(&panel[xoffset+3*(tj>>1+(ti>>1)*PANE_WIDTH)],&pp[3]);
        else if (CmpColor(&pp[3*RomWidth],&pp[3*RomWidth+3])) SetColor(&panel[xoffset+3*(tj>>1+(ti>>1)*PANE_WIDTH)],&pp[3*RomWidth]);
        else SetColor(&panel[xoffset+3*(tj>>1+(ti>>1)*PANE_WIDTH)],pp);
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
          a=b=d=e=panel2;
          c=f=&panel2[3];
          g=h=&panel2[3*RomWidth];
          i=&panel2[3*(RomWidth+1)];
        }
        else if ((ti==0)&&(tj==RomHeight-1))
        {
          a=b=&panel2[3*(tj-1)*RomWidth];
          c=&panel2[3*((tj-1)*RomWidth+1)];
          d=g=h=e=&panel2[3*tj*RomWidth];
          f=i=&panel2[3*(tj*RomWidth+1)];
        }
        else if ((ti==RomWidth-1)&&(tj==0))
        {
          a=d=&panel2[3*(ti-1)];
          b=c=f=e=&panel2[3*ti];
          g=&panel2[3*(RomWidth+ti-1)];
          h=i=&panel2[3*(RomWidth+ti)];
        }
        else if ((ti==RomWidth-1)&&(tj==RomHeight-1))
        {
          a=&panel2[3*(tj*RomWidth-2)];
          b=c=&panel2[3*(tj*RomWidth-1)];
          d=g=&panel2[3*(RomHeight*RomWidth-2)];
          e=f=h=i=&panel2[3*(RomHeight*RomWidth-1)];
        }
        else if (ti==0)
        {
          a=b=&panel2[3*((tj-1)*RomWidth)];
          c=&panel2[3*((tj-1)*RomWidth+1)];
          d=e=&panel2[3*(tj*RomWidth)];
          f=&panel2[3*(tj*RomWidth+1)];
          g=h=&panel2[3*((tj+1)*RomWidth)];
          i=&panel2[3*((tj+1)*RomWidth+1)];
        }
        else if (ti==RomWidth-1)
        {
          a=&panel2[3*(tj*RomWidth-2)];
          b=c=&panel2[3*(tj*RomWidth-1)];
          d=&panel2[3*((tj+1)*RomWidth-2)];
          e=f=&panel2[3*((tj+1)*RomWidth-1)];
          g=&panel2[3*((tj+2)*RomWidth-2)];
          h=i=&panel2[3*((tj+2)*RomWidth-1)];
        }
        else if (tj==0)
        {
          a=d=&panel2[3*(ti-1)];
          b=e=&panel2[3*ti];
          c=f=&panel2[3*(ti+1)];
          g=&panel2[3*(RomWidth+ti-1)];
          h=&panel2[3*(RomWidth+ti)];
          i=&panel2[3*(RomWidth+ti+1)];
        }
        else if (tj==RomHeight-1)
        {
          a=&panel2[3*((tj-1)*RomWidth+ti-1)];
          b=&panel2[3*((tj-1)*RomWidth+ti)];
          c=&panel2[3*((tj-1)*RomWidth+ti+1)];
          d=g=&panel2[3*(tj*RomWidth+ti-1)];
          e=h=&panel2[3*(tj*RomWidth+ti)];
          f=i=&panel2[3*(tj*RomWidth+ti+1)];
        }
        else
        {
          a=&panel2[3*((tj-1)*RomWidth+ti-1)];
          b=&panel2[3*((tj-1)*RomWidth+ti)];
          c=&panel2[3*((tj-1)*RomWidth+ti+1)];
          d=&panel2[3*(tj*RomWidth+ti-1)];
          e=&panel2[3*(tj*RomWidth+ti)];
          f=&panel2[3*(tj*RomWidth+ti+1)];
          g=&panel2[3*((tj+1)*RomWidth+ti-1)];
          h=&panel2[3*((tj+1)*RomWidth+ti)];
          i=&panel2[3*((tj+1)*RomWidth+ti+1)];
        }
        if (b != h && d != f) {
          if (CmpColor(d,b)) SetColor(&panel[3*(tj*2*PANE_WIDTH+ti*2)+xoffset],d); else SetColor(&panel[3*(tj*2*PANE_WIDTH+ti*2)+xoffset],e); 
          if (CmpColor(b,f)) SetColor(&panel[3*(tj*2*PANE_WIDTH+ti*2+1)+xoffset], f); else SetColor(&panel[3*(tj*2*PANE_WIDTH+ti*2+1)+xoffset], e); 
          if (CmpColor(b,h)) SetColor(&panel[3*((tj*2+1)*PANE_WIDTH+ti*2)+xoffset],d); else SetColor(&panel[3*((tj*2+1)*PANE_WIDTH+ti*2)+xoffset],e); 
          if (CmpColor(h,f)) SetColor(&panel[3*((tj*2+1)*PANE_WIDTH+ti*2+1)+xoffset],f); else SetColor(&panel[3*((tj*2+1)*PANE_WIDTH+ti*2+1)+xoffset],e);
        } else {
          SetColor(&panel[3*(tj*2*PANE_WIDTH+ti*2)+xoffset],e);
          SetColor(&panel[3*(tj*2*PANE_WIDTH+ti*2+1)+xoffset], e);
          SetColor(&panel[3*((tj*2+1)*PANE_WIDTH+ti*2)+xoffset],e); 
          SetColor(&panel[3*((tj*2+1)*PANE_WIDTH+ti*2+1)+xoffset],e);
        }
       }
    }
  }
  else //offset!=0
  {
    for (int tj=0;tj<RomHeight;tj++)
    {
      for (int ti=0;ti<RomWidth;ti++) panel[3*(tj*PANE_WIDTH+ti)+xoffset]=panel2[3*(tj*RomWidth+ti)];
    }
  }
}

void ScaleImage64() // scale for indexed image (all except RGB24)
{
  int xoffset=0;
  memcpy(panel2,panel,RomWidth*RomHeight);
  memset(panel,0,PANE_WIDTH*PANE_HEIGHT);
  int scale=0; // 0 - no scale, 1 - half scale, 2 - twice scale
  if ((RomWidth==192)&&(PANE_WIDTH==256)) xoffset=32;
  else if (RomWidth==192)
  {
    xoffset=16;
    scale=1;
  }
  else if ((RomWidth==256)&&(PANE_WIDTH==128)) scale=1;
  else if ((RomWidth==128)&&(PANE_WIDTH==256)) scale=2;
  else return;
  if (scale==1)
  {
    // for half scaling we take the 4 points and look if there is one colour repeated
    for (int ti=0;ti<RomHeight;ti+=2)
    {
      for (int tj=0;tj<RomWidth;tj+=2)
      {
        unsigned char* pp=&panel2[ti*RomWidth+tj];
        if ((pp[0]==pp[1])||(pp[0]==pp[RomWidth])||(pp[0]==pp[RomWidth+1])) panel[xoffset+(tj/2)+(ti/2)*PANE_WIDTH]=pp[0];
        else if ((pp[1]==pp[RomWidth])||(pp[1]==pp[RomWidth+1])) panel[xoffset+(tj/2)+(ti/2)*PANE_WIDTH]=pp[1];
        else if ((pp[RomWidth]==pp[RomWidth+1])) panel[xoffset+(tj/2)+(ti/2)*PANE_WIDTH]=pp[RomWidth];
        else panel[xoffset+(tj/2)+(ti/2)*PANE_WIDTH]=pp[0];
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
        unsigned int a, b, c, d, e, f, g, h, i;
        if ((ti==0)&&(tj==0))
        {
          a=b=d=e=panel2[0];
          c=f=panel2[1];
          g=h=panel2[RomWidth];
          i=panel2[RomWidth+1];
        }
        else if ((ti==0)&&(tj==RomHeight-1))
        {
          a=b=panel2[(tj-1)*RomWidth];
          c=panel2[(tj-1)*RomWidth+1];
          d=g=h=e=panel2[tj*RomWidth];
          f=i=panel2[tj*RomWidth+1];
        }
        else if ((ti==RomWidth-1)&&(tj==0))
        {
          a=d=panel2[ti-1];
          b=c=f=e=panel2[ti];
          g=panel2[RomWidth+ti-1];
          h=i=panel2[RomWidth+ti];
        }
        else if ((ti==RomWidth-1)&&(tj==RomHeight-1))
        {
          a=panel2[tj*RomWidth-2];
          b=c=panel2[tj*RomWidth-1];
          d=g=panel2[RomHeight*RomWidth-2];
          e=f=h=i=panel2[RomHeight*RomWidth-1];
        }
        else if (ti==0)
        {
          a=b=panel2[(tj-1)*RomWidth];
          c=panel2[(tj-1)*RomWidth+1];
          d=e=panel2[tj*RomWidth];
          f=panel2[tj*RomWidth+1];
          g=h=panel2[(tj+1)*RomWidth];
          i=panel2[(tj+1)*RomWidth+1];
        }
        else if (ti==RomWidth-1)
        {
          a=panel2[tj*RomWidth-2];
          b=c=panel2[tj*RomWidth-1];
          d=panel2[(tj+1)*RomWidth-2];
          e=f=panel2[(tj+1)*RomWidth-1];
          g=panel2[(tj+2)*RomWidth-2];
          h=i=panel2[(tj+2)*RomWidth-1];
        }
        else if (tj==0)
        {
          a=d=panel2[ti-1];
          b=e=panel2[ti];
          c=f=panel2[ti+1];
          g=panel2[RomWidth+ti-1];
          h=panel2[RomWidth+ti];
          i=panel2[RomWidth+ti+1];
        }
        else if (tj==RomHeight-1)
        {
          a=panel2[(tj-1)*RomWidth+ti-1];
          b=panel2[(tj-1)*RomWidth+ti];
          c=panel2[(tj-1)*RomWidth+ti+1];
          d=g=panel2[tj*RomWidth+ti-1];
          e=h=panel2[tj*RomWidth+ti];
          f=i=panel2[tj*RomWidth+ti+1];
        }
        else
        {
          a=panel2[(tj-1)*RomWidth+ti-1];
          b=panel2[(tj-1)*RomWidth+ti];
          c=panel2[(tj-1)*RomWidth+ti+1];
          d=panel2[tj*RomWidth+ti-1];
          e=panel2[tj*RomWidth+ti];
          f=panel2[tj*RomWidth+ti+1];
          g=panel2[(tj+1)*RomWidth+ti-1];
          h=panel2[(tj+1)*RomWidth+ti];
          i=panel2[(tj+1)*RomWidth+ti+1];
        }
        if (b != h && d != f) {
          panel[tj*2*PANE_WIDTH+ti*2+xoffset] = d == b ? d : e;
          panel[tj*2*PANE_WIDTH+ti*2+1+xoffset] = b == f ? f : e;
          panel[(tj*2+1)*PANE_WIDTH+ti*2+xoffset] = d == h ? d : e;
          panel[(tj*2+1)*PANE_WIDTH+ti*2+1+xoffset] = h == f ? f : e;
        } else {
          panel[tj*2*PANE_WIDTH+ti*2+xoffset] = e;
          panel[tj*2*PANE_WIDTH+ti*2+1+xoffset] = e;
          panel[(tj*2+1)*PANE_WIDTH+ti*2+xoffset] = e;
          panel[(tj*2+1)*PANE_WIDTH+ti*2+1+xoffset] = e;
        }
      }
    }
  }
  else //offset!=0
  {
    for (int tj=0;tj<RomHeight;tj++)
    {
      for (int ti=0;ti<RomWidth;ti++) panel[tj*PANE_WIDTH+xoffset+ti]=panel2[tj*RomWidth+ti];
    }
  }
}

void fillpanel()
{
  for (int tj = 0; tj < PANE_HEIGHT; tj++)
  {
    for (int ti = 0; ti < PANE_WIDTH; ti++)
    {
      dma_display->drawPixelRGB888(ti, tj, panel[ti * 3 + tj * 3 * PANE_WIDTH + ordreRGB[acordreRGB * 3]], panel[ti * 3 + tj * 3 * PANE_WIDTH + ordreRGB[acordreRGB * 3 + 1]], panel[ti * 3 + tj * 3 * PANE_WIDTH + ordreRGB[acordreRGB * 3 + 2]]);
    }
  }
}

void fillpanelMode64()
{
  for (int tj = 0; tj < PANE_HEIGHT; tj++)
  {
    for (int ti = 0; ti < PANE_WIDTH; ti++)
    {
      dma_display->drawPixelRGB888(ti, tj, Palette64[rotCols[panel[ti+tj*PANE_WIDTH]]*3+ordreRGB[acordreRGB * 3]], 
        Palette64[rotCols[panel[ti+tj*PANE_WIDTH]]*3+ordreRGB[acordreRGB * 3+1]],Palette64[rotCols[panel[ti+tj*PANE_WIDTH]]*3+ordreRGB[acordreRGB * 3+2]]);
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
  if (PANE_HEIGHT==64) flogo=LittleFS.open("/logoHD.raw", "r"); else flogo=LittleFS.open("/logo.raw", "r");
  if (!flogo) {
    //Serial.println("Failed to open file for reading");
    return;
  }
  for (unsigned int tj = 0; tj < PANE_HEIGHT*PANE_WIDTH*3; tj++)
  {
    panel[tj]=flogo.read();
  }
  flogo.close();
  fillpanel();
}

void InitPalettes(int R, int G, int B)
{
  // initialise les palettes à partir d'une couleur qui représente le 100%
  for (int ti = 0; ti < 4; ti++)
  {
    Palette4[ti * 3] = (unsigned char)((float)R*levels4[ti] / 100.0f);
    Palette4[ti * 3 + 1] = (unsigned char)((float)G*levels4[ti] / 100.0f);
    Palette4[ti * 3 + 2] = (unsigned char)((float)B*levels4[ti] / 100.0f);
  }
  for (int ti = 0; ti < 16; ti++)
  {
    Palette16[ti * 3] = (unsigned char)((float)R*levels16[ti] / 100.0f);
    Palette16[ti * 3 + 1] = (unsigned char)((float)G*levels16[ti] / 100.0f);
    Palette16[ti * 3 + 2] = (unsigned char)((float)B*levels16[ti] / 100.0f);
  }
  for (int ti = 0; ti < 64; ti++)
  {
    Palette64[ti * 3] = (unsigned char)((float)R*levels64[ti] / 100.0f);
    Palette64[ti * 3 + 1] = (unsigned char)((float)G*levels64[ti] / 100.0f);
    Palette64[ti * 3 + 2] = (unsigned char)((float)B*levels64[ti] / 100.0f);
  }
}
void Say(unsigned char where,unsigned int what)
{
    DisplayNombre(where,3,0,where*5,255,255,255);
    if (what!=(unsigned int)-1) DisplayNombre(what,10,15,where*5,255,255,255);
}

bool MireActive = true;
bool handshakeSucceeded = false;
// 256 is the default buffer size of the CP210x linux kernel driver, we should not exceed it as default.
int serialTransferChunkSize = 256;
unsigned char img2[3*64+6*256/8*64+3*MAX_COLOR_ROTATIONS];
unsigned int frameCount = 0;
unsigned int errorCount = 0;
unsigned int watchdogCount = 0;

void setup()
{
  pinMode(ORDRE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LUMINOSITE_BUTTON_PIN, INPUT_PULLUP);
    
  mxconfig.clkphase = false; // change if you have some parts of the panel with a shift
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();

  if (!LittleFS.begin()) {
    dma_display->drawChar((uint16_t) 0, (uint16_t) 0, (unsigned char) 'E', (uint16_t) 255, (uint16_t) 0, (uint8_t) 8);
    delay(4000);
  }

  Serial.begin(921600);
  while (!Serial);
  Serial.setRxBufferSize(serialTransferChunkSize);
  Serial.setTimeout(SERIAL_TIMEOUT);

  LoadLum();

  dma_display->setBrightness8(lumval[lumstep]);    // range is 0-255, 0 - 0%, 255 - 100%
  dma_display->clearScreen();

  LoadOrdreRGB();

  DisplayLogo();
  DisplayText(lumtxt,16,PANE_WIDTH/2-16/2-2*4/2,PANE_HEIGHT-5,255,255,255);
  DisplayLum();

  InitPalettes(255,109,0);
}

bool SerialReadBuffer(unsigned char* pBuffer, unsigned int BufferSize)
{
  memset(pBuffer, 0, BufferSize);

  // We always receive chunks of 256 bytes (maximum).
  // At this point, the control chars and the one byte command have been read already.
  // So we only need to read the remaining bytes of the first chunk as full 256 byte chunks afterwards.
  int chunkSize = serialTransferChunkSize - N_CTRL_CHARS - 1;
  int remainingBytes = BufferSize;
  while (remainingBytes > 0)
  {
    int receivedBytes = Serial.readBytes(pBuffer + BufferSize - remainingBytes, (remainingBytes > chunkSize) ? chunkSize : remainingBytes);
    if (receivedBytes != remainingBytes && receivedBytes != chunkSize)
    {
      if (DEBUG_FRAMES) Say(4, ++errorCount);

      // Send an (E)rror signal to tell the client that no more chunks should be send or to repeat the entire frame from the beginning.
      Serial.write('E');
      // Flush the buffer.
      Serial.readBytes(img2, sizeof(img2));
      memset(img2, 0, sizeof(img2));

      return false;
    }

    // Send an (A)cknowledge signal to tell the client that we successfully read the chunk.
    Serial.write('A');
    remainingBytes -= chunkSize;
    // From now on read full amount of byte chunks.
    chunkSize = serialTransferChunkSize;
  }
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
  if (rotfound==true) fillpanelMode64();
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
      if (DEBUG_FRAMES) Say(5, ++watchdogCount);

      // Send an (E)rror signal.
      Serial.write('E');
      // Flush the buffer.
      Serial.readBytes(img2, sizeof(img2));
      memset(img2, 0, sizeof(img2));
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
    if (CheckButton(ORDRE_BUTTON_PIN, &OrdreBtnRel, &OrdreBtnPos, &OrdreBtnDebounceTime) == 2)
    {
      acordreRGB++;
      if (acordreRGB >= 6) acordreRGB = 0;
      SaveOrdreRGB();
      fillpanel();
      DisplayText(lumtxt,16,PANE_WIDTH/2-16/2-2*4/2,PANE_HEIGHT-5,255,255,255);
      DisplayLum();
    }
    if (CheckButton(LUMINOSITE_BUTTON_PIN, &LuminositeBtnRel, &LuminositeBtnPos, &LuminositeBtnDebounceTime) == 2)
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

  // After handshake, send a (R)eady signal to indicate that a new command could be sent.
  // The client has to wait for it to avoid buffer issues. The handshake it self works without it.
  if (handshakeSucceeded) Serial.write('R');
  wait_for_ctrl_chars();

  // Updates to mode64 color rotations have been handled within wait_for_ctrl_chars(), now reset it to false.
  mode64 = false;

  // Commands:
  //  2: receive rom frame size 
  //  3: render raw data
  //  6: init palettes
  //  7: render 16 couleurs avec 1 palette 4 couleurs (4*3 bytes) suivis de 2 pixels par byte
  //  8: render 4 couleurs avec 1 palette 4 couleurs (4*3 bytes) suivis de 4 pixels par byte
  //  9: render 16 couleurs avec 1 palette 16 couleurs (16*3 bytes) suivis de 4 bytes par groupe de 8 points (séparés en plans de bits 4*512 bytes)
  // 10: clear screen
  // 11: render 64 couleurs avec 1 palette 64 couleurs (64*3 bytes) suivis de 6 bytes par groupe de 8 points (séparés en plans de bits 6*512 bytes)
  // 12: handshake + report resolution
  // 13: set serial transfer chunk size
  // 99: display number
  unsigned char c4;
  while (Serial.available()==0);
  c4=Serial.read();
  
  if (c4 == 12) // ask for resolution (and shake hands)
  {
    for (int ti=0;ti<N_INTERMEDIATE_CTR_CHARS;ti++) Serial.write(CtrlCharacters[ti]);
    Serial.write(PANE_WIDTH&0xff);
    Serial.write((PANE_WIDTH>>8)&0xff);
    Serial.write(PANE_HEIGHT&0xff);
    Serial.write((PANE_HEIGHT>>8)&0xff);
    handshakeSucceeded=true;
  }
  else if (c4 == 2) // get rom frame dimension
  {
    unsigned char tbuf[4];
    if (SerialReadBuffer(tbuf,4))
    {
      RomWidth=(int)(tbuf[0])+(int)(tbuf[1]<<8);
      RomHeight=(int)(tbuf[2])+(int)(tbuf[3]<<8);
    }
    RomWidthPlane=RomWidth>>3;
  }
  else if (c4 == 13) // set serial transfer chunk size
  {
    int serialTransferChunkSize = Serial.read();
    Serial.setRxBufferSize(serialTransferChunkSize);
    // Send an (A)cknowledge signal to tell the client that we successfully read the chunk.
    Serial.write('A');
  }
  if (c4 == 99) // communication debug
  {
    unsigned int number = Serial.read();
    // Send an (A)cknowledge signal to tell the client that we successfully read the chunk.
    Serial.write('A');
    Say(2, number);
  }
  else if (c4 == 6) // reinit palettes
  {
    // Send an (A)cknowledge signal to tell the client that we successfully read the chunk.
    Serial.write('A');
    InitPalettes(255, 109, 0);
  }    
  else if (c4 == 10) // clear screen
  {
    // Send an (A)cknowledge signal to tell the client that we successfully read the chunk.
    Serial.write('A');
    dma_display->clearScreen();
  }
  else if (c4 == 3) // mode RGB24
  {
    if (SerialReadBuffer(panel,RomHeight*RomWidth*3))
    {
      mode64=false;
      if ((RomHeight!=PANE_HEIGHT)||(RomWidth!=PANE_WIDTH)) ScaleImage();
      fillpanel();
    }
  }
  else if (c4 == 8) // mode 4 couleurs avec 1 palette 4 couleurs (4*3 bytes) suivis de 4 pixels par byte
  {
    if (SerialReadBuffer(img2,3*4+2*RomWidthPlane*RomHeight))
    {
      for (int ti = 3; ti >= 0; ti--)
      {
        Palette64[ti * 3] = img2[ti*3];
        Palette64[ti * 3 + 1] = img2[ti*3+1];
        Palette64[ti * 3 + 2] = img2[ti*3+2];
      }
      unsigned char* img=&img2[3*4];
      for (int tj = 0; tj < RomHeight; tj++)
      {
        for (int ti = 0; ti < RomWidthPlane; ti++)
        {
          unsigned char mask = 1;
          unsigned char planes[2];
          planes[0] = img[ti + tj * RomWidthPlane];
          planes[1] = img[RomWidthPlane*RomHeight + ti + tj * RomWidthPlane];
          for (int tk = 0; tk < 8; tk++)
          {
            unsigned char idx = 0;
            if ((planes[0] & mask) > 0) idx |= 1;
            if ((planes[1] & mask) > 0) idx |= 2;
            panel[(ti * 8 + tk)+tj * RomWidth]=idx;
            mask <<= 1;
          }
        }
      }
      mode64=false;
      for (int ti=0;ti<64;ti++) rotCols[ti]=ti;
      if ((RomHeight!=PANE_HEIGHT)||(RomWidth!=PANE_WIDTH)) ScaleImage64();
      fillpanelMode64();
    }
  }
  else if (c4 == 7) // mode 16 couleurs avec 1 palette 4 couleurs (4*3 bytes) suivis de 2 pixels par byte
  {
    if (SerialReadBuffer(img2,3*4+4*RomWidthPlane*RomHeight))
    {
      for (int ti = 3; ti >= 0; ti--)
      {
        Palette64[(4 * ti + 3)* 3] = img2[ti*3];
        Palette64[(4 * ti + 3) * 3 + 1] = img2[ti*3+1];
        Palette64[(4 * ti + 3) * 3 + 2] = img2[ti*3+2];
      }
      Palette64[0]=Palette64[1]=Palette64[2]=0;
      Palette64[3]=Palette64[3*3]/3;
      Palette64[4]=Palette64[3*3+1]/3;
      Palette64[5]=Palette64[3*3+2]/3;
      Palette64[6]=2*(Palette64[3*3]/3);
      Palette64[7]=2*(Palette64[3*3+1]/3);
      Palette64[8]=2*(Palette64[3*3+2]/3);
      
      Palette64[12]=Palette64[3*3]+(Palette64[7*3]-Palette64[3*3])/4;
      Palette64[13]=Palette64[3*3+1]+(Palette64[7*3+1]-Palette64[3*3+1])/4;
      Palette64[14]=Palette64[3*3+2]+(Palette64[7*3+2]-Palette64[3*3+2])/4;
      Palette64[15]=Palette64[3*3]+2*((Palette64[7*3]-Palette64[3*3])/4);
      Palette64[16]=Palette64[3*3+1]+2*((Palette64[7*3+1]-Palette64[3*3+1])/4);
      Palette64[17]=Palette64[3*3+2]+2*((Palette64[7*3+2]-Palette64[3*3+2])/4);
      Palette64[18]=Palette64[3*3]+3*((Palette64[7*3]-Palette64[3*3])/4);
      Palette64[19]=Palette64[3*3+1]+3*((Palette64[7*3+1]-Palette64[3*3+1])/4);
      Palette64[20]=Palette64[3*3+2]+3*((Palette64[7*3+2]-Palette64[3*3+2])/4);
      
      Palette64[24]=Palette64[7*3]+(Palette64[11*3]-Palette64[7*3])/4;
      Palette64[25]=Palette64[7*3+1]+(Palette64[11*3+1]-Palette64[7*3+1])/4;
      Palette64[26]=Palette64[7*3+2]+(Palette64[11*3+2]-Palette64[7*3+2])/4;
      Palette64[27]=Palette64[7*3]+2*((Palette64[11*3]-Palette64[7*3])/4);
      Palette64[28]=Palette64[7*3+1]+2*((Palette64[11*3+1]-Palette64[7*3+1])/4);
      Palette64[29]=Palette64[7*3+2]+2*((Palette64[11*3+2]-Palette64[7*3+2])/4);
      Palette64[30]=Palette64[7*3]+3*((Palette64[11*3]-Palette64[7*3])/4);
      Palette64[31]=Palette64[7*3+1]+3*((Palette64[11*3+1]-Palette64[7*3+1])/4);
      Palette64[32]=Palette64[7*3+2]+3*((Palette64[11*3+2]-Palette64[7*3+2])/4);
      
      Palette64[36]=Palette64[11*3]+(Palette64[15*3]-Palette64[11*3])/4;
      Palette64[37]=Palette64[11*3+1]+(Palette64[15*3+1]-Palette64[11*3+1])/4;
      Palette64[38]=Palette64[11*3+2]+(Palette64[15*3+2]-Palette64[11*3+2])/4;
      Palette64[39]=Palette64[11*3]+2*((Palette64[15*3]-Palette64[11*3])/4);
      Palette64[40]=Palette64[11*3+1]+2*((Palette64[15*3+1]-Palette64[11*3+1])/4);
      Palette64[41]=Palette64[11*3+2]+2*((Palette64[15*3+2]-Palette64[11*3+2])/4);
      Palette64[42]=Palette64[11*3]+3*((Palette64[15*3]-Palette64[11*3])/4);
      Palette64[43]=Palette64[11*3+1]+3*((Palette64[15*3+1]-Palette64[11*3+1])/4);
      Palette64[44]=Palette64[11*3+2]+3*((Palette64[15*3+2]-Palette64[11*3+2])/4);
      
      unsigned char* img=&img2[3*4];
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
            panel[(ti * 8 + tk)+tj * RomWidth]=idx;
            mask <<= 1;
          }
        }
      }
      mode64=false;
      for (int ti=0;ti<64;ti++) rotCols[ti]=ti;
      if ((RomHeight!=PANE_HEIGHT)||(RomWidth!=PANE_WIDTH)) ScaleImage64();
      fillpanelMode64();
    }
  }
  else if (c4 == 9) // mode 16 couleurs avec 1 palette 16 couleurs (16*3 bytes) suivis de 4 bytes par groupe de 8 points (séparés en plans de bits 4*512 bytes)
  {
    if (SerialReadBuffer(img2,3*16+4*RomWidthPlane*RomHeight))
    {
      for (int ti = 15; ti >= 0; ti--)
      {
        Palette64[ti * 3] = img2[ti*3];
        Palette64[ti * 3 + 1] = img2[ti*3+1];
        Palette64[ti * 3 + 2] = img2[ti*3+2];
      }
      unsigned char* img=&img2[3*16];
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
            panel[(ti * 8 + tk)+tj * RomWidth]=idx;
            mask <<= 1;
          }
        }
      }
      mode64=false;
      for (int ti=0;ti<64;ti++) rotCols[ti]=ti;
      if ((RomHeight!=PANE_HEIGHT)||(RomWidth!=PANE_WIDTH)) ScaleImage64();
      fillpanelMode64();
    }
  }
  else if (c4 == 11) // mode 64 couleurs avec 1 palette 64 couleurs (64*3 bytes) suivis de 6 bytes par groupe de 8 points (séparés en plans de bits 6*512 bytes) suivis de 3*8 bytes de rotations de couleurs
  {
    if (SerialReadBuffer(img2,3*64+6*RomWidthPlane*RomHeight+3*MAX_COLOR_ROTATIONS))
    {
      for (int ti = 63; ti >= 0; ti--)
      {
        Palette64[ti * 3] = img2[ti*3];
        Palette64[ti * 3 + 1] = img2[ti*3+1];
        Palette64[ti * 3 + 2] = img2[ti*3+2];
      }
      unsigned char* img=&img2[3*64];
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
            panel[(ti * 8 + tk)+tj * RomWidth]=idx;
            mask <<= 1;
          }
        }
      }
      img=&img2[3*64+6*RomWidthPlane*RomHeight];
      unsigned long actime=millis();
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
      mode64=true;
      if ((RomHeight!=PANE_HEIGHT)||(RomWidth!=PANE_WIDTH)) ScaleImage64();
      fillpanelMode64();
    }
  }
  if (DEBUG_FRAMES)
  {
    // An overflow of the unsigned int counters should not be an issue, they just reset to 0.
    Say(0, ++frameCount);
    Say(4, errorCount);
    Say(5, watchdogCount);
  }
}
