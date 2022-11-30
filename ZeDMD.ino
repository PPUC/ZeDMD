#define PANEL_WIDTH 64 // width: number of LEDs for 1 pannel
#define PANEL_HEIGHT 32 // height: number of LEDs
#define PANELS_NUMBER 2   // Number of horizontally chained panels 
// ------------------------------------------ ZeDMD by Zedrummer (http://pincabpassion.net)---------------------------------------------
// - Install the ESP32 board in Arduino IDE as explained here https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/
// - Install SPIFFS file system as explained here https://randomnerdtutorials.com/install-esp32-filesystem-uploader-arduino-ide/
// - Install "ESP32 HUB75 LED MATRIX PANNEL DMA" Display library via the library manager
// - Go to menu "Tools" then click on "ESP32 Sketch Data Upload"
// - Change the values in the 3 first lines above (PANEL_WIDTH, PANEL_HEIGHT, PANELS_NUMBER) according to your pannels
// - Inject this code in the board
// - If you have blurry pictures, the display is not clean, try to reduce the input voltage of your LED matrix pannels, often, 5V pannels need
// between 4V and 4.5V to display clean pictures, you often have a screw in switch-mode power supplies to change the output voltage a little bit
// - While the initial pattern logo is displayed, check you have red in the upper left, green in the lower left and blue in the upper right,
// if not, make contact between the ORDRE_BUTTON_PIN (default 21, but you can change below) pin and a ground pin several times
// until the display is correct (automatically saved, no need to do it again)
// -----------------------------------------------------------------------------------------------------------------------------------------

#define PANE_WIDTH (PANEL_WIDTH*PANELS_NUMBER)
#define PANE_WIDTH_PLANE (PANE_WIDTH>>3)
#define PANE_HEIGHT PANEL_HEIGHT
#define MAX_COLOR_ROTATIONS 8
#define MIN_SPAN_ROT 50

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <SPIFFS.h>

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

unsigned char pannel[PANE_WIDTH*PANE_HEIGHT*3];
unsigned char pannel2[PANE_WIDTH*PANE_HEIGHT];

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

bool mode64=false;;


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
  unsigned int acc=chf,acd=10;
  if (nc>2) {for (int ti=0;ti<(nc-1);ti++) acd*=10;}
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

void DisplayText(bool* text, int width, int x, int y, int R, int G, int B)
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

void fillpannel()
{
  for (int tj = 0; tj < PANE_HEIGHT; tj++)
  {
    for (int ti = 0; ti < PANE_WIDTH; ti++)
    {
      dma_display->drawPixelRGB888(ti, tj, pannel[ti * 3 + tj * 3 * PANE_WIDTH + ordreRGB[acordreRGB * 3]], pannel[ti * 3 + tj * 3 * PANE_WIDTH + ordreRGB[acordreRGB * 3 + 1]], pannel[ti * 3 + tj * 3 * PANE_WIDTH + ordreRGB[acordreRGB * 3 + 2]]);
    }
  }
  //delayMicroseconds(6060);
}

void fillpannel2()
{
  for (int tj = 0; tj < PANE_HEIGHT; tj++)
  {
    for (int ti = 0; ti < PANE_WIDTH; ti++)
    {
      dma_display->drawPixelRGB888(ti, tj, Palette64[rotCols[pannel2[ti+tj*PANE_WIDTH]]*3+ordreRGB[acordreRGB * 3]], 
      Palette64[rotCols[pannel2[ti+tj*PANE_WIDTH]]*3+ordreRGB[acordreRGB * 3+1]],Palette64[rotCols[pannel2[ti+tj*PANE_WIDTH]]*3+ordreRGB[acordreRGB * 3+2]]);
    }
  }
  mode64=true;
}

File fordre;

void LoadOrdreRGB()
{
  fordre=SPIFFS.open("/ordrergb.val");
  if (!fordre) return;
  acordreRGB=fordre.read();
  fordre.close();
}

void SaveOrdreRGB()
{
  fordre=SPIFFS.open("/ordrergb.val","w");
  fordre.write(acordreRGB);
  fordre.close();
}

File flum;

void LoadLum()
{
  flum=SPIFFS.open("/lum.val");
  if (!flum) return;
  lumstep=flum.read();
  flum.close();
}

void SaveLum()
{
  flum=SPIFFS.open("/lum.val","w");
  flum.write(lumstep);
  flum.close();
}

void DisplayLogo(void)
{
  File flogo;
  if (PANEL_HEIGHT==64) flogo=SPIFFS.open("/logoHD.raw"); else flogo=SPIFFS.open("/logo.raw");
  if (!flogo) {
    //Serial.println("Failed to open file for reading");
    return;
  }
  for (unsigned int tj = 0; tj < PANE_HEIGHT*PANE_WIDTH*3; tj++)
  {
    pannel[tj]=flogo.read();
  }
  flogo.close();
  fillpannel();
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

bool MireActive=true;
bool handshake=false;
// 256 is the default buffer size of the CP210x linux kernel driver, we should not exceed it as default.
int serialTransferChunkSize = 256;
unsigned char img2[3*64+6 * PANE_WIDTH/8*PANE_HEIGHT];

void setup()
{
  Serial.begin(921600);
  while (!Serial);
  Serial.setRxBufferSize(serialTransferChunkSize);
  Serial.setTimeout(100);

  if (!SPIFFS.begin(true)) return;

  pinMode(ORDRE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LUMINOSITE_BUTTON_PIN, INPUT_PULLUP);
    
  mxconfig.clkphase = false; // change if you have some parts of the pannel with a shift
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
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
  // So we only need to read the remaining bytes of the first chunk abd full 256 byte chunks afterwards.
  int chunkSize = serialTransferChunkSize - N_CTRL_CHARS - 1;
  int remainingBytes = BufferSize;
  while (remainingBytes > 0)
  {
    int receivedBytes = Serial.readBytes(pBuffer + BufferSize - remainingBytes, (remainingBytes > chunkSize) ? chunkSize : remainingBytes);
    if (receivedBytes != remainingBytes && receivedBytes != chunkSize)
    {
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
  return true;
}

void Say(unsigned char where,unsigned int what)
{
  DisplayNombre(where,3,0,where*5,255,255,255);
  if (what!=(unsigned int)-1) DisplayNombre(what,10,15,where*5,255,255,255);
  delay(1000);
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
  if (rotfound==true) fillpannel2();
}

void wait_for_ctrl_chars(void)
{
  unsigned char nCtrlCharFound=0;
  while (nCtrlCharFound<N_CTRL_CHARS)
  {
    while (Serial.available()==0)
    {
        if (mode64==true) updateColorRotations();
    }
    if (Serial.read()==CtrlCharacters[nCtrlCharFound]) nCtrlCharFound++; else nCtrlCharFound=0;
  }
}

void loop()
{
  while (MireActive == true)
  {
    if (CheckButton(ORDRE_BUTTON_PIN, &OrdreBtnRel, &OrdreBtnPos, &OrdreBtnDebounceTime) == 2)
    {
      acordreRGB++;
      if (acordreRGB >= 6) acordreRGB = 0;
      SaveOrdreRGB();
      fillpannel();
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
  if (handshake) Serial.write('R');
  wait_for_ctrl_chars();

  // Commands:
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
  mode64=false;
  while (Serial.available()==0);
  c4=Serial.read();
  if (c4 == 12) // ask for resolution (and shake hands)
  {
    for (int ti=0;ti<N_INTERMEDIATE_CTR_CHARS;ti++) Serial.write(CtrlCharacters[ti]);
    Serial.write(PANE_WIDTH&0xff);
    Serial.write((PANE_WIDTH>>8)&0xff);
    Serial.write(PANE_HEIGHT&0xff);
    Serial.write((PANE_HEIGHT>>8)&0xff);
    handshake=true;
  }
  else if (c4 == 13) // set serial transfer chunk size
  {
    int serialTransferChunkSize = Serial.read();
    // Send an (A)cknowledge signal to tell the client that we successfully read the chunk.
    Serial.write('A');
    Serial.setRxBufferSize(serialTransferChunkSize);
  }
  if (c4 == 99) // communication debug
  {
    unsigned int number = Serial.read();
    // Send an (A)cknowledge signal to tell the client that we successfully read the chunk.
    Serial.write('A');
    Say(0, number);
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
  else if (c4 == 3)
  {
    if (SerialReadBuffer(pannel,PANE_WIDTH*PANE_HEIGHT*3))
    {
      fillpannel();
    }
  }
  else if (c4 == 8) // mode 4 couleurs avec 1 palette 4 couleurs (4*3 bytes) suivis de 4 pixels par byte
  {
    if (SerialReadBuffer(img2,3*4+2*PANE_WIDTH/8*PANE_HEIGHT))
    {
      for (int ti = 3; ti >= 0; ti--)
      {
        Palette4[ti * 3] = img2[ti*3];
        Palette4[ti * 3 + 1] = img2[ti*3+1];
        Palette4[ti * 3 + 2] = img2[ti*3+2];
      }
      unsigned char* img=&img2[3*4];
      for (int tj = 0; tj < PANE_HEIGHT; tj++)
      {
        for (int ti = 0; ti < PANE_WIDTH / 8; ti++)
        {
          unsigned char mask = 1;
          unsigned char planes[2];
          planes[0] = img[ti + tj * PANE_WIDTH/8];
          planes[1] = img[PANE_WIDTH_PLANE*PANE_HEIGHT + ti + tj * PANE_WIDTH_PLANE];
          for (int tk = 0; tk < 8; tk++)
          {
            unsigned char idx = 0;
            if ((planes[0] & mask) > 0) idx |= 1;
            if ((planes[1] & mask) > 0) idx |= 2;
            pannel[(ti * 8 + tk) * 3 + tj * PANE_WIDTH * 3] = Palette4[idx * 3];
            pannel[(ti * 8 + tk) * 3 + tj * PANE_WIDTH * 3 + 1] = Palette4[idx * 3 + 1];
            pannel[(ti * 8 + tk) * 3 + tj * PANE_WIDTH * 3 + 2] = Palette4[idx * 3 + 2];
            mask <<= 1;
          }
        }
      }
      fillpannel();
    }
  }
  else if (c4 == 7) // mode 16 couleurs avec 1 palette 4 couleurs (4*3 bytes) suivis de 2 pixels par byte
  {
    if (SerialReadBuffer(img2,3*4+4*PANE_WIDTH/8*PANE_HEIGHT))
    {
      for (int ti = 3; ti >= 0; ti--)
      {
        Palette16[ti * 3] = img2[ti*3];
        Palette16[ti * 3 + 1] = img2[ti*3+1];
        Palette16[ti * 3 + 2] = img2[ti*3+2];
      }
      unsigned char* img=&img2[3*4];
      for (int tj = 0; tj < PANE_HEIGHT; tj++)
      {
        for (int ti = 0; ti < PANE_WIDTH / 8; ti++)
        {
          unsigned char mask = 1;
          unsigned char planes[4];
          planes[0] = img[ti + tj * PANE_WIDTH/8];
          planes[1] = img[PANE_WIDTH_PLANE*PANE_HEIGHT + ti + tj * PANE_WIDTH_PLANE];
          planes[2] = img[2*PANE_WIDTH_PLANE*PANE_HEIGHT + ti + tj * PANE_WIDTH_PLANE];
          planes[3] = img[3*PANE_WIDTH_PLANE*PANE_HEIGHT + ti + tj * PANE_WIDTH_PLANE];
          for (int tk = 0; tk < 8; tk++)
          {
            unsigned char idx = 0;
            if ((planes[0] & mask) > 0) idx |= 1;
            if ((planes[1] & mask) > 0) idx |= 2;
            if ((planes[2] & mask) > 0) idx |= 4;
            if ((planes[3] & mask) > 0) idx |= 8;
            float fvalue = (float)idx / 4.0f;
            float fvalueR = (float)Palette16[((int)fvalue + 1) * 3] * (fvalue - (int)fvalue) + (float)Palette16[((int)fvalue) * 3] * (1.0f - (fvalue - (int)fvalue));
            if (fvalueR>255) fvalueR=255.0f; else if (fvalueR<0) fvalueR=0.0f;
            float fvalueG = (float)Palette16[((int)fvalue + 1) * 3 + 1] * (fvalue - (int)fvalue) + (float)Palette16[((int)fvalue) * 3 + 1] * (1.0f - (fvalue - (int)fvalue));
            if (fvalueG>255) fvalueG=255.0f; else if (fvalueG<0) fvalueG=0.0f;
            float fvalueB = (float)Palette16[((int)fvalue + 1) * 3 + 2] * (fvalue - (int)fvalue) + (float)Palette16[((int)fvalue) * 3 + 2] * (1.0f - (fvalue - (int)fvalue));
            if (fvalueB>255) fvalueB=255.0f; else if (fvalueB<0) fvalueB=0.0f;
            pannel[(ti * 8 + tk) * 3 + tj * PANE_WIDTH * 3] = (int)fvalueR;
            pannel[(ti * 8 + tk) * 3 + tj * PANE_WIDTH * 3 + 1] = (int)fvalueG;
            pannel[(ti * 8 + tk) * 3 + tj * PANE_WIDTH * 3 + 2] = (int)fvalueB;
            mask <<= 1;
          }
        }
      }
      fillpannel();
    }
  }
  else if (c4 == 9) // mode 16 couleurs avec 1 palette 16 couleurs (16*3 bytes) suivis de 4 bytes par groupe de 8 points (séparés en plans de bits 4*512 bytes)
  {
    if (SerialReadBuffer(img2,3*16+4*PANE_WIDTH/8*PANE_HEIGHT))
    {
      for (int ti = 15; ti >= 0; ti--)
      {
        Palette16[ti * 3] = img2[ti*3];
        Palette16[ti * 3 + 1] = img2[ti*3+1];
        Palette16[ti * 3 + 2] = img2[ti*3+2];
      }
      unsigned char* img=&img2[3*16];
      for (int tj = 0; tj < PANE_HEIGHT; tj++)
      {
        for (int ti = 0; ti < PANE_WIDTH / 8; ti++)
        {
          // on reconstitue un indice à partir des plans puis une couleur à partir de la palette
          unsigned char mask = 1;
          unsigned char planes[4];
          planes[0] = img[ti + tj * PANE_WIDTH/8];
          planes[1] = img[PANE_WIDTH_PLANE*PANE_HEIGHT + ti + tj * PANE_WIDTH_PLANE];
          planes[2] = img[2*PANE_WIDTH_PLANE*PANE_HEIGHT + ti + tj * PANE_WIDTH_PLANE];
          planes[3] = img[3*PANE_WIDTH_PLANE*PANE_HEIGHT + ti + tj * PANE_WIDTH_PLANE];
          for (int tk = 0; tk < 8; tk++)
          {
            unsigned char idx = 0;
            if ((planes[0] & mask) > 0) idx |= 1;
            if ((planes[1] & mask) > 0) idx |= 2;
            if ((planes[2] & mask) > 0) idx |= 4;
            if ((planes[3] & mask) > 0) idx |= 8;
            pannel[(ti * 8 + tk) * 3 + tj * PANE_WIDTH * 3] = Palette16[idx * 3];
            pannel[(ti * 8 + tk) * 3 + tj * PANE_WIDTH * 3 + 1] = Palette16[idx * 3 + 1];
            pannel[(ti * 8 + tk) * 3 + tj * PANE_WIDTH * 3 + 2] = Palette16[idx * 3 + 2];
            mask <<= 1;
          }
        }
      }
      fillpannel();
    }
  }
  else if (c4 == 11) // mode 64 couleurs avec 1 palette 64 couleurs (64*3 bytes) suivis de 6 bytes par groupe de 8 points (séparés en plans de bits 6*512 bytes) suivis de 3*8 bytes de rotations de couleurs
  {
    if (SerialReadBuffer(img2,3*64+6*PANE_WIDTH/8*PANE_HEIGHT+3*MAX_COLOR_ROTATIONS))
    {
      for (int ti = 63; ti >= 0; ti--)
      {
        Palette64[ti * 3] = img2[ti*3];
        Palette64[ti * 3 + 1] = img2[ti*3+1];
        Palette64[ti * 3 + 2] = img2[ti*3+2];
      }
      unsigned char* img=&img2[3*64];
      for (int tj = 0; tj < PANE_HEIGHT; tj++)
      {
        for (int ti = 0; ti < PANE_WIDTH / 8; ti++)
        {
          // on reconstitue un indice à partir des plans puis une couleur à partir de la palette
          unsigned char mask = 1;
          unsigned char planes[6];
          planes[0] = img[ti + tj * PANE_WIDTH/8];
          planes[1] = img[PANE_WIDTH_PLANE*PANE_HEIGHT + ti + tj * PANE_WIDTH_PLANE];
          planes[2] = img[2*PANE_WIDTH_PLANE*PANE_HEIGHT + ti + tj * PANE_WIDTH_PLANE];
          planes[3] = img[3*PANE_WIDTH_PLANE*PANE_HEIGHT + ti + tj * PANE_WIDTH_PLANE];
          planes[4] = img[4*PANE_WIDTH_PLANE*PANE_HEIGHT + ti + tj * PANE_WIDTH_PLANE];
          planes[5] = img[5*PANE_WIDTH_PLANE*PANE_HEIGHT + ti + tj * PANE_WIDTH_PLANE];
          for (int tk = 0; tk < 8; tk++)
          {
            unsigned char idx = 0;
            if ((planes[0] & mask) > 0) idx |= 1;
            if ((planes[1] & mask) > 0) idx |= 2;
            if ((planes[2] & mask) > 0) idx |= 4;
            if ((planes[3] & mask) > 0) idx |= 8;
            if ((planes[4] & mask) > 0) idx |= 0x10;
            if ((planes[5] & mask) > 0) idx |= 0x20;
            /*pannel[(ti * 8 + tk) * 3 + tj * PANE_WIDTH * 3] = Palette64[idx * 3];
            pannel[(ti * 8 + tk) * 3 + tj * PANE_WIDTH * 3 + 1] = Palette64[idx * 3 + 1];
            pannel[(ti * 8 + tk) * 3 + tj * PANE_WIDTH * 3 + 2] = Palette64[idx * 3 + 2];*/
            pannel2[(ti * 8 + tk)+tj * PANE_WIDTH]=idx;
            mask <<= 1;
          }
        }
      }
      img=&img2[3*64+6*PANE_WIDTH/8*PANE_HEIGHT];
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
      fillpannel2();
    }
  }
}
