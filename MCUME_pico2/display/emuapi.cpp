#define KEYMAP_PRESENT 1

#define PROGMEM

#include "pico.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include <stdio.h>
#include <string.h>

extern "C" {
  #include "emuapi.h"
  #include "iopins.h"
}

#ifdef HAS_USBHOST
#include <ctype.h>
#include "tusb.h"
#include "kbd.h"
#endif

static bool emu_writeConfig(void);
static bool emu_readConfig(void);
static bool emu_eraseConfig(void);
static bool emu_writeGfxConfig(void);
static bool emu_readGfxConfig(void);
static bool emu_eraseGfxConfig(void);

#include "pico_dsp.h"
extern PICO_DSP tft;

#define MAX_FILENAME_PATH   64
#define NB_FILE_HANDLER     4
#define AUTORUN_FILENAME    "autorun.txt"
#define GFX_CFG_FILENAME    "gfxmode.txt"
#define KBD_CFG_FILENAME    "kbdmode.txt"

#define MAX_FILES           64
#define MAX_FILENAME_SIZE   32
#define MAX_MENULINES       9
#define TEXT_HEIGHT         16
#define TEXT_WIDTH          8
#define MENU_FILE_XOFFSET   (6*TEXT_WIDTH)
#define MENU_FILE_YOFFSET   (2*TEXT_HEIGHT)
#define MENU_FILE_W         (MAX_FILENAME_SIZE*TEXT_WIDTH)
#define MENU_FILE_H         (MAX_MENULINES*TEXT_HEIGHT)
#define MENU_FILE_BGCOLOR   RGBVAL16(0x00,0x00,0x40)
#define MENU_JOYS_YOFFSET   (12*TEXT_HEIGHT)
#define MENU_VBAR_XOFFSET   (0*TEXT_WIDTH)
#define MENU_VBAR_YOFFSET   (MENU_FILE_YOFFSET)

#define MENU_TFT_XOFFSET    (MENU_FILE_XOFFSET+MENU_FILE_W+8)
#define MENU_TFT_YOFFSET    (MENU_VBAR_YOFFSET+32)
#define MENU_VGA_XOFFSET    (MENU_FILE_XOFFSET+MENU_FILE_W+8)
#define MENU_VGA_YOFFSET    (MENU_VBAR_YOFFSET+MENU_FILE_H-32-37)



static int nbFiles=0;
static int curFile=0;
static int topFile=0;
static char selection[MAX_FILENAME_PATH]="";
static char selected_filename[MAX_FILENAME_SIZE]="";
static char files[MAX_FILES][MAX_FILENAME_SIZE];
static bool menuRedraw=true;

#if ( defined(PICO2ZX) )
#define RN 6
#define CN 8
static uint8_t rows[RN] = {KROWIN0,KROWIN1,KROWIN2,KROWIN3,KROWIN4,KROWIN5}; 
static unsigned char keymatrix[RN];
static const unsigned short * keys;
static int keymatrix_hitrow=-1;
static bool key_fn=false;
static bool key_alt=false;
static uint32_t keypress_t_ms=0;
static uint32_t last_t_ms=0;
static uint32_t hundred_ms_cnt=0;
static bool ledflash_toggle=false;
#endif
static int keyMap;

static bool joySwapped = false;
static uint16_t bLastState;
static int xRef;
static int yRef;
static uint16_t usbnavpad=0;

static bool menuOn=true;
static bool autorun=false;


/********************************
 * Generic output and malloc
********************************/ 
void emu_printf(const char * text)
{
  printf("%s\n",text);
}

void emu_printf(int val)
{
  printf("%d\n",val);
}

void emu_printi(int val)
{
  printf("%d\n",val);
}

void emu_printh(int val)
{
  printf("0x%.8\n",val);
}

static int malbufpt = 0;
static char malbuf[EXTRA_HEAP];

void * emu_Malloc(int size)
{
  void * retval =  malloc(size);
  if (!retval) {
    emu_printf("failled to allocate");
    emu_printf(size);
    emu_printf("fallback");
    if ( (malbufpt+size) < sizeof(malbuf) ) {
      retval = (void *)&malbuf[malbufpt];
      malbufpt += size;      
    }
    else {
      emu_printf("failure to allocate");
    }
  }
  else {
    emu_printf("could allocate dynamic ");
    emu_printf(size);    
  }
  
  return retval;
}

void * emu_MallocI(int size)
{
  void * retval =  NULL; 

  if ( (malbufpt+size) < sizeof(malbuf) ) {
    retval = (void *)&malbuf[malbufpt];
    malbufpt += size;
    emu_printf("could allocate static ");
    emu_printf(size);          
  }
  else {
    emu_printf("failure to allocate");
  }

  return retval;
}
void emu_Free(void * pt)
{
  free(pt);
}

void emu_drawText(unsigned short x, unsigned short y, const char * text, unsigned short fgcolor, unsigned short bgcolor, int doublesize)
{
  tft.drawText(x, y, text, fgcolor, bgcolor, doublesize?true:false);
}


/********************************
 * OSKB handling
********************************/ 
#if (defined(ILI9341) || defined(ST7789)) && defined(USE_VGA)
// On screen keyboard position
#define KXOFF      28 //64
#define KYOFF      96
#define KWIDTH     11 //22
#define KHEIGHT    3

static bool oskbOn = false;
static int cxpos = 0;
static int cypos = 0;
static int oskbMap = 0;
static uint16_t oskbBLastState = 0;

static void lineOSKB2(int kxoff, int kyoff, char * str, int row)
{
  char c[2] = {'A',0};
  const char * cpt = str;
  for (int i=0; i<KWIDTH; i++)
  {
    c[0] = *cpt++;
    c[1] = 0;
    uint16_t bg = RGBVAL16(0x00,0x00,0xff);
    if ( (cxpos == i) && (cypos == row) ) bg = RGBVAL16(0xff,0x00,0x00);
    tft.drawTextNoDma(kxoff+8*i,kyoff, &c[0], RGBVAL16(0x00,0xff,0xff), bg, ((i&1)?false:true));
  } 
}

static void lineOSKB(int kxoff, int kyoff, char * str, int row)
{
  char c[4] = {' ',0,' ',0};
  const char * cpt = str;
  for (int i=0; i<KWIDTH; i++)
  {
    c[1] = *cpt++;
    uint16_t bg;
    if (row&1) bg = (i&1)?RGBVAL16(0xff,0xff,0xff):RGBVAL16(0xe0,0xe0,0xe0);
    else bg = (i&1)?RGBVAL16(0xe0,0xe0,0xe0):RGBVAL16(0xff,0xff,0xff);
    if ( (cxpos == i) && (cypos == row) ) bg = RGBVAL16(0x00,0xff,0xff);
    tft.drawTextNoDma(kxoff+24*i,kyoff+32*row+0 , "   ", RGBVAL16(0x00,0x00,0x00), bg, false);
    tft.drawTextNoDma(kxoff+24*i,kyoff+32*row+8 , &c[0], RGBVAL16(0x00,0x00,0x00), bg, true);
    tft.drawTextNoDma(kxoff+24*i,kyoff+32*row+24, "   ", RGBVAL16(0x00,0x00,0x00), bg, false);
  } 
}


static void drawOskb(void)
{
//  lineOSKB2(KXOFF,KYOFF+0,  (char *)"Q1W2E3R4T5Y6U7I8O9P0<=",  0);
//  lineOSKB2(KXOFF,KYOFF+16, (char *)"  A!S@D#F$G%H+J&K*L-EN",  1);
//  lineOSKB2(KXOFF,KYOFF+32, (char *)"  Z(X)C?V/B\"N<M>.,SP  ", 2);
/*  
  if (oskbMap == 0) {
    lineOSKB(KXOFF,KYOFF, keylables_map1_0,  0);
    lineOSKB(KXOFF,KYOFF, keylables_map1_1,  1);
    lineOSKB(KXOFF,KYOFF, keylables_map1_2,  2);
  }
  else if (oskbMap == 1) {
    lineOSKB(KXOFF,KYOFF, keylables_map2_0,  0);
    lineOSKB(KXOFF,KYOFF, keylables_map2_1,  1);
    lineOSKB(KXOFF,KYOFF, keylables_map2_2,  2);
  }
  else {
    lineOSKB(KXOFF,KYOFF, keylables_map3_0,  0);
    lineOSKB(KXOFF,KYOFF, keylables_map3_1,  1);
    lineOSKB(KXOFF,KYOFF, keylables_map3_2,  2);
  }
*/  
}

void toggleOskb(bool forceoff) {
  if (forceoff) oskbOn=true; 
  if (oskbOn) {
    oskbOn = false;
    tft.fillScreenNoDma(RGBVAL16(0x00,0x00,0x00));
    tft.drawTextNoDma(0,32, "Press USER2 to toggle onscreen keyboard.", RGBVAL16(0xff,0xff,0xff), RGBVAL16(0x00,0x00,0x00), true);
  } else {
    oskbOn = true;
    tft.fillScreenNoDma(RGBVAL16(0x00,0x00,0x00));
    tft.drawTextNoDma(0,32, " Press USER2 to exit onscreen keyboard. ", RGBVAL16(0xff,0xff,0xff), RGBVAL16(0x00,0x00,0x00), true);
    tft.drawTextNoDma(0,64, "    (USER1 to toggle between keymaps)   ", RGBVAL16(0x00,0xff,0xff), RGBVAL16(0x00,0x00,0xff), true);
    tft.drawRectNoDma(KXOFF,KYOFF, 22*8, 3*16, RGBVAL16(0x00,0x00,0xFF));
    drawOskb();        
  }
}

static int handleOskb(void)
{  
  int retval = 0;

  uint16_t bClick = bLastState & ~oskbBLastState;
  oskbBLastState = bLastState;
  /*
  static const char * digits = "0123456789ABCDEF";
  char buf[5] = {0,0,0,0,0};
  int val = bClick;
  buf[0] = digits[(val>>12)&0xf];
  buf[1] = digits[(val>>8)&0xf];
  buf[2] = digits[(val>>4)&0xf];
  buf[3] = digits[val&0xf];
  tft.drawTextNoDma(0,KYOFF+ 64,buf,RGBVAL16(0x00,0x00,0x00),RGBVAL16(0xFF,0xFF,0xFF),1);
  */
  if (bClick & MASK_KEY_USER2)
  { 
    toggleOskb(false);
  }
  if (oskbOn)
  {
    bool updated = true;
    if (bClick & MASK_KEY_USER1)
    { 
      oskbMap += 1;
      if (oskbMap == 3) oskbMap = 0;
    }    
    else if (bClick & MASK_JOY2_LEFT)
    {  
      cxpos++;
      if (cxpos >= KWIDTH) cxpos = 0;
    }
    else if (bClick & MASK_JOY2_RIGHT)
    {  
      cxpos--;
      if (cxpos < 0) cxpos = KWIDTH-1;
    }
    else if (bClick & MASK_JOY2_DOWN)
    {  
      cypos++;
      if (cypos >= KHEIGHT) cypos = 0;
    }
    else if (bClick & MASK_JOY2_UP)
    {  
      cypos--;
      if (cypos < 0) cypos = KHEIGHT-1;
    }
    else if (oskbBLastState & MASK_JOY2_BTN)
    {  
      retval = cypos*KWIDTH+cxpos+1;
      if (retval) {
        retval--;
        //if (retval & 1) retval = key_map2[retval>>1];
        //else retval = key_map1[retval>>1];
        if (oskbMap == 0) {
          retval = key_map1[retval];
        }
        else if (oskbMap == 1) {
          retval = key_map2[retval];
        }
        else {
          retval = key_map3[retval];
        }
        //if (retval) { toggleOskb(true); updated=false; };
      }
    }
    else {
      updated=false;
    }    
    if (updated) drawOskb();
  }

  return retval;    
}
#endif

/********************************
 * Input and keyboard
********************************/ 
int emu_ReadAnalogJoyX(int min, int max) 
{
  adc_select_input(0);
  int val = adc_read();
#if INVX
  val = 4095 - val;
#endif
  val = val-xRef;
  val = ((val*140)/100);
  if ( (val > -512) && (val < 512) ) val = 0;
  val = val+2048;
  return (val*(max-min))/4096;
}

int emu_ReadAnalogJoyY(int min, int max) 
{
  adc_select_input(1);  
  int val = adc_read();
#if INVY
  val = 4095 - val;
#endif
  val = val-yRef;
  val = ((val*120)/100);
  if ( (val > -512) && (val < 512) ) val = 0;
  //val = (val*(max-min))/4096;
  val = val+2048;
  //return val+(max-min)/2;
  return (val*(max-min))/4096;
}


static uint16_t readAnalogJoystick(void)
{
  uint16_t joysval = 0;
#ifdef PIN_JOY2_A1X
  int xReading = emu_ReadAnalogJoyX(0,256);
  if (xReading > 128) joysval |= MASK_JOY2_LEFT;
  else if (xReading < 128) joysval |= MASK_JOY2_RIGHT;
  
  int yReading = emu_ReadAnalogJoyY(0,256);
  if (yReading < 128) joysval |= MASK_JOY2_UP;
  else if (yReading > 128) joysval |= MASK_JOY2_DOWN;
#endif 
  // First joystick
#if INVY
#ifdef PIN_JOY2_1
  if ( !gpio_get(PIN_JOY2_1) ) joysval |= MASK_JOY2_DOWN;
#endif
#ifdef PIN_JOY2_2
  if ( !gpio_get(PIN_JOY2_2) ) joysval |= MASK_JOY2_UP;
#endif
#else
#ifdef PIN_JOY2_1
  if ( !gpio_get(PIN_JOY2_1) ) joysval |= MASK_JOY2_UP;
#endif
#ifdef PIN_JOY2_2
  if ( !gpio_get(PIN_JOY2_2) ) joysval |= MASK_JOY2_DOWN;
#endif
#endif
#if INVX
#ifdef PIN_JOY2_3
  if ( !gpio_get(PIN_JOY2_3) ) joysval |= MASK_JOY2_LEFT;
#endif
#ifdef PIN_JOY2_4
  if ( !gpio_get(PIN_JOY2_4) ) joysval |= MASK_JOY2_RIGHT;
#endif
#else
#ifdef PIN_JOY2_3
  if ( !gpio_get(PIN_JOY2_3) ) joysval |= MASK_JOY2_RIGHT;
#endif
#ifdef PIN_JOY2_4
  if ( !gpio_get(PIN_JOY2_4) ) joysval |= MASK_JOY2_LEFT;
#endif
#endif
#ifdef PIN_JOY2_BTN
  joysval |= (gpio_get(PIN_JOY2_BTN) ? 0 : MASK_JOY2_BTN);
#endif

  return (joysval);     
}


int emu_SwapJoysticks(int statusOnly) {
  if (!statusOnly) {
    if (joySwapped) {
      joySwapped = false;
    }
    else {
      joySwapped = true;
    }
  }
  return(joySwapped?1:0);
}

int emu_GetPad(void) 
{
  return(bLastState/*|((joySwapped?1:0)<<7)*/);
}

int emu_ReadKeys(void) 
{
  uint16_t retval;
  uint16_t j1 = readAnalogJoystick();
  uint16_t j2 = 0;
  
  // Second joystick
#if INVY
#ifdef PIN_JOY1_1
  if ( !gpio_get(PIN_JOY1_1) ) j2 |= MASK_JOY2_DOWN;
#endif
#ifdef PIN_JOY1_2
  if ( !gpio_get(PIN_JOY1_2) ) j2 |= MASK_JOY2_UP;
#endif
#else
#ifdef PIN_JOY1_1
  if ( !gpio_get(PIN_JOY1_1) ) j2 |= MASK_JOY2_UP;
#endif
#ifdef PIN_JOY1_2
  if ( !gpio_get(PIN_JOY1_2) ) j2 |= MASK_JOY2_DOWN;
#endif
#endif
#if INVX
#ifdef PIN_JOY1_3
  if ( !gpio_get(PIN_JOY1_3) ) j2 |= MASK_JOY2_LEFT;
#endif
#ifdef PIN_JOY1_4
  if ( !gpio_get(PIN_JOY1_4) ) j2 |= MASK_JOY2_RIGHT;
#endif
#else
#ifdef PIN_JOY1_3
  if ( !gpio_get(PIN_JOY1_3) ) j2 |= MASK_JOY2_RIGHT;
#endif
#ifdef PIN_JOY1_4
  if ( !gpio_get(PIN_JOY1_4) ) j2 |= MASK_JOY2_LEFT;
#endif
#endif
#ifdef PIN_JOY1_BTN
  if ( !gpio_get(PIN_JOY1_BTN) ) j2 |= MASK_JOY2_BTN;
#endif


  if (joySwapped) {
    retval = ((j1 << 8) | j2);
  }
  else {
    retval = ((j2 << 8) | j1);
  }

  if (usbnavpad & MASK_JOY2_UP) retval |= MASK_JOY2_UP;
  if (usbnavpad & MASK_JOY2_DOWN) retval |= MASK_JOY2_DOWN;
  if (usbnavpad & MASK_JOY2_LEFT) retval |= MASK_JOY2_LEFT;
  if (usbnavpad & MASK_JOY2_RIGHT) retval |= MASK_JOY2_RIGHT;
  if (usbnavpad & MASK_JOY2_BTN) retval |= MASK_JOY2_BTN;
  if (usbnavpad & MASK_KEY_USER1) retval |= MASK_KEY_USER1;
  if (usbnavpad & MASK_KEY_USER2) retval |= MASK_KEY_USER2;
  if (usbnavpad & MASK_KEY_USER3) retval |= MASK_KEY_USER3;
  if (usbnavpad & MASK_KEY_USER4) retval |= MASK_KEY_USER4;

#ifdef PIN_KEY_USER1 
  if ( !gpio_get(PIN_KEY_USER1) ) retval |= MASK_KEY_USER1;
#endif
#ifdef PIN_KEY_USER2 
  if ( !gpio_get(PIN_KEY_USER2) ) retval |= MASK_KEY_USER2;
#endif
#ifdef PIN_KEY_USER3 
  if ( !gpio_get(PIN_KEY_USER3) ) retval |= MASK_KEY_USER3;
#endif
#ifdef PIN_KEY_USER4 
  if ( !gpio_get(PIN_KEY_USER4) ) retval |= MASK_KEY_USER4;
#endif

#if ( defined(PICO2ZX) )
#define SAM  4
#define STIM 50  
  uint8_t row0=0;
  uint8_t row1=0;
  uint8_t row2=0;
  uint8_t row3=0;
  uint8_t row4=0;
  uint8_t row5=0;  
  uint8_t bit=1; 
  gpio_put(RP_DAT, 1);
  for (int j=0;j<SAM;j++) row0 |= (gpio_get(KROWIN0) ? 0 : bit);
  for (int j=0;j<SAM;j++) row1 |= (gpio_get(KROWIN1) ? 0 : bit);
  for (int j=0;j<SAM;j++) row2 |= (gpio_get(KROWIN2) ? 0 : bit);
  for (int j=0;j<SAM;j++) row3 |= (gpio_get(KROWIN3) ? 0 : bit);
  for (int j=0;j<SAM;j++) row4 |= (gpio_get(KROWIN4) ? 0 : bit);
  for (int j=0;j<SAM;j++) row5 |= (gpio_get(KROWIN5) ? 0 : bit);
  bit = bit << 1;
  gpio_put(RP_CLK, 0);
  sleep_us(STIM);
  for(int i = 0; i < (CN-1); i++) {
    gpio_put(RP_CLK, 1);
    sleep_us(STIM); 
    for (int j=0;j<SAM;j++) row0 |= (gpio_get(KROWIN0) ? 0 : bit);
    for (int j=0;j<SAM;j++) row1 |= (gpio_get(KROWIN1) ? 0 : bit);
    for (int j=0;j<SAM;j++) row2 |= (gpio_get(KROWIN2) ? 0 : bit);
    for (int j=0;j<SAM;j++) row3 |= (gpio_get(KROWIN3) ? 0 : bit);
    for (int j=0;j<SAM;j++) row4 |= (gpio_get(KROWIN4) ? 0 : bit);
    for (int j=0;j<SAM;j++) row5 |= (gpio_get(KROWIN5) ? 0 : bit);    
    gpio_put(RP_CLK, 0);
    sleep_us(STIM);
    bit = bit << 1;
  }
  gpio_put(RP_DAT, 0);
  gpio_put(RP_CLK, 1);
  sleep_us(STIM);
  key_fn = false;
  key_alt = false;
  if ( row4 & 0x20 ) {key_fn = true; row4 &= ~0x20;}
  if ( row3 & 0x80 ) {key_alt = true; row3 &= ~0x80;}
  keymatrix[0] = row0;
  keymatrix[1] = row1;
  keymatrix[2] = row2;
  keymatrix[3] = row3;  
  keymatrix[4] = row4;
  keymatrix[5] = row5;  

  if ( row5 & 0x01 ) retval |= MASK_KEY_USER1;
  if ( row5 & 0x04 ) retval |= MASK_KEY_USER2;
  if ( row5 & 0x02 ) retval |= MASK_KEY_USER3;

#if INVX
  if ( row5 & 0x10  ) retval |= MASK_JOY2_LEFT;
  if ( row5 & 0x40  ) retval |= MASK_JOY2_RIGHT;
#else
  if ( row5 & 0x40  ) retval |= MASK_JOY2_LEFT;
  if ( row5 & 0x10  ) retval |= MASK_JOY2_RIGHT;
#endif
#if INVY
  if ( row5 & 0x20  ) retval |= MASK_JOY2_DOWN;
  if ( row5 & 0x08  ) retval |= MASK_JOY2_UP;  
#else
  if ( row5 & 0x08  ) retval |= MASK_JOY2_DOWN;
  if ( row5 & 0x20  ) retval |= MASK_JOY2_UP;  
#endif
  if ( row5 & 0x80 ) retval |= MASK_JOY2_BTN;  


  keymatrix_hitrow = -1;
  for (int i=0;i<RN;i++){
    if (keymatrix[i]) keymatrix_hitrow=i;
  }
#endif

  //Serial.println(retval,HEX);

  if ( ((retval & (MASK_KEY_USER1+MASK_KEY_USER2)) == (MASK_KEY_USER1+MASK_KEY_USER2))
     || (retval & MASK_KEY_USER4 ) )
  {  
  }

#if (defined(ILI9341) || defined(ST7789)) && defined(USE_VGA)
  if (oskbOn) {
    retval |= MASK_OSKB; 
  }  
#endif  
  
  return (retval);
}

unsigned short emu_DebounceLocalKeys(void)
{
#ifdef HAS_USBHOST
  tuh_task();
#endif
  uint16_t bCurState = emu_ReadKeys();
  uint16_t bClick = bCurState & ~bLastState;
  bLastState = bCurState;
  return (bClick);
}

int emu_ReadI2CKeyboard(void) {
  int retval=0;
#if ( defined(PICO2ZX) )
  if (key_alt) {
    keys = (const unsigned short *)key_map3;
  }
  else if (key_fn) {
    keys = (const unsigned short *)key_map2;
  }
  else {
    keys = (const unsigned short *)key_map1;
  }
  if (keymatrix_hitrow >=0 ) {
    unsigned short match = ((unsigned short)keymatrix_hitrow<<8) | keymatrix[keymatrix_hitrow];  
    for (int i=0; i<sizeof(matkeys)/sizeof(unsigned short); i++) {
      if (match == matkeys[i]) {
        hundred_ms_cnt = 0;
        return (keys[i]);
      }
    }
  }
#endif
#if (defined(ILI9341) || defined(ST7789)) && defined(USE_VGA)
  if (!menuOn) {
    retval = handleOskb(); 
  }  
#endif  
  return(retval);
}

unsigned char emu_ReadI2CKeyboard2(int row) {
  int retval=0;
#if ( defined(PICO2ZX) )
  retval = keymatrix[row];
#endif
  return retval;
}


void emu_InitJoysticks(void) { 

  // Second Joystick   
#ifdef PIN_JOY1_1
  gpio_init(PIN_JOY1_1);
  gpio_set_pulls(PIN_JOY1_1,true,false);
  gpio_set_dir(PIN_JOY1_1,GPIO_IN);  
#endif  
#ifdef PIN_JOY1_2
  gpio_init(PIN_JOY1_2);
  gpio_set_pulls(PIN_JOY1_2,true,false);
  gpio_set_dir(PIN_JOY1_2,GPIO_IN);  
#endif  
#ifdef PIN_JOY1_3
  gpio_init(PIN_JOY1_3);
  gpio_set_pulls(PIN_JOY1_3,true,false);
  gpio_set_dir(PIN_JOY1_3,GPIO_IN);  
#endif  
#ifdef PIN_JOY1_4
  gpio_init(PIN_JOY1_4);
  gpio_set_pulls(PIN_JOY1_4,true,false);
  gpio_set_dir(PIN_JOY1_4,GPIO_IN);  
#endif  
#ifdef PIN_JOY1_BTN
  gpio_init(PIN_JOY1_BTN);
  gpio_set_pulls(PIN_JOY1_BTN,true,false);
  gpio_set_dir(PIN_JOY1_BTN,GPIO_IN);  
#endif  

  // User keys   
#ifdef PIN_KEY_USER1
  gpio_init(PIN_KEY_USER1);
  gpio_set_pulls(PIN_KEY_USER1,true,false);
  gpio_set_dir(PIN_KEY_USER1,GPIO_IN);  
#endif  
#ifdef PIN_KEY_USER2
  gpio_init(PIN_KEY_USER2);
  gpio_set_dir(PIN_KEY_USER2,GPIO_IN);
  gpio_set_pulls(PIN_KEY_USER2,true,false);
#endif  
#ifdef PIN_KEY_USER3
  gpio_init(PIN_KEY_USER3);
  gpio_set_pulls(PIN_KEY_USER3,true,false);
  gpio_set_dir(PIN_KEY_USER3,GPIO_IN);  
#endif  
#ifdef PIN_KEY_USER4
  gpio_init(PIN_KEY_USER4);
  gpio_set_pulls(PIN_KEY_USER4,true,false);
  gpio_set_dir(PIN_KEY_USER4,GPIO_IN);  
#endif  

  // First Joystick   
#ifdef PIN_JOY2_1
  gpio_init(PIN_JOY2_1);
  gpio_set_pulls(PIN_JOY2_1,true,false);
  gpio_set_dir(PIN_JOY2_1,GPIO_IN);
  gpio_set_input_enabled(PIN_JOY2_1, true); // Force ADC as digital input        
#endif  
#ifdef PIN_JOY2_2
  gpio_init(PIN_JOY2_2);
  gpio_set_pulls(PIN_JOY2_2,true,false);
  gpio_set_dir(PIN_JOY2_2,GPIO_IN);  
  gpio_set_input_enabled(PIN_JOY2_2, true);  // Force ADC as digital input       
#endif  
#ifdef PIN_JOY2_3
  gpio_init(PIN_JOY2_3);
  gpio_set_pulls(PIN_JOY2_3,true,false);
  gpio_set_dir(PIN_JOY2_3,GPIO_IN);  
  gpio_set_input_enabled(PIN_JOY2_3, true);  // Force ADC as digital input        
#endif  
#ifdef PIN_JOY2_4
  gpio_init(PIN_JOY2_4);
  gpio_set_pulls(PIN_JOY2_4,true,false);
  gpio_set_dir(PIN_JOY2_4,GPIO_IN);  
#endif  
#ifdef PIN_JOY2_BTN
  gpio_init(PIN_JOY2_BTN);
  gpio_set_pulls(PIN_JOY2_BTN,true,false);
  gpio_set_dir(PIN_JOY2_BTN,GPIO_IN);  
#endif  
 
#ifdef PIN_JOY2_A1X
  adc_init(); 
  adc_gpio_init(PIN_JOY2_A1X);
  adc_gpio_init(PIN_JOY2_A2Y);
  xRef=0; yRef=0;
  for (int i=0; i<10; i++) {
    adc_select_input(0);  
    xRef += adc_read();
    adc_select_input(1);  
    yRef += adc_read();
    sleep_ms(20);
  }
#if INVX
  xRef = 4095 -xRef/10;
#else
  xRef /= 10;
#endif
#if INVY
  yRef = 4095 -yRef/10;
#else
  yRef /= 10;
#endif
#endif

#if ( defined(PICO2ZX) )
  gpio_init(RP_DAT);
  gpio_set_dir(RP_DAT, GPIO_OUT);
  gpio_init(RP_CLK);
  gpio_set_dir(RP_CLK, GPIO_OUT);
  gpio_put(RP_DAT, 1);
  gpio_put(RP_CLK, 0);
  sleep_ms(1);
  for(int i = 0; i < CN; i++) {
    gpio_put(RP_CLK, 1);
    sleep_ms(1);
    gpio_put(RP_CLK, 0);
    sleep_ms(1);
  }
  gpio_put(RP_DAT, 0);
  sleep_ms(1);
  gpio_put(RP_CLK, 1);
  sleep_ms(1);
  for(int i = 0; i < RN; ++i) {
    gpio_init(rows[i]);
    gpio_set_dir(rows[i], GPIO_IN);
    gpio_pull_up(rows[i]);
  }
#endif
}

int emu_setKeymap(int index) {
  return 0;
}



/********************************
 * Menu file loader UI
********************************/ 
#include "ff.h"
static FATFS fatfs;
static FIL file; 
extern "C" int sd_init_driver(void);

#ifdef FILEBROWSER
static int readNbFiles(char * rootdir) {
  int totalFiles = 0;

  DIR dir;
  FILINFO entry;
  FRESULT fr = f_findfirst(&dir, &entry, rootdir, "*");
  while ( (fr == FR_OK) && (entry.fname[0]) && (totalFiles<MAX_FILES) ) {  
    if (!entry.fname[0]) {
      // no more files
      break;
    }

    char * filename = entry.fname;
    if ( !(entry.fattrib & AM_DIR) ) {
      if (strcmp(filename,AUTORUN_FILENAME)) {
        strncpy(&files[totalFiles][0], filename, MAX_FILENAME_SIZE-1);
        totalFiles++;
      }  
    }
    else {
      if ( (strcmp(filename,".")) && (strcmp(filename,"..")) ) {
        strncpy(&files[totalFiles][0], filename, MAX_FILENAME_SIZE-1);
        totalFiles++;
      }
    }
    fr = f_findnext(&dir, &entry);  
  } 
  f_closedir(&dir);

  return totalFiles;  
}  



void backgroundMenu(void) {
    menuRedraw=true;  
    tft.fillScreenNoDma(RGBVAL16(0x00,0x00,0x00));
    tft.drawTextNoDma(0,0, TITLE, RGBVAL16(0x00,0xff,0xff), RGBVAL16(0x00,0x00,0xff), true);           
}


static void menuLeft(void)
{
#if (defined(ILI9341) || defined(ST7789)) && defined(USE_VGA)
  toggleOskb(true);  
#endif
}


bool menuActive(void) 
{
  return (menuOn);
}

void toggleMenu(bool on) {
  if (on) {
    menuOn = true;
    backgroundMenu();
  } else {
    tft.fillScreenNoDma(RGBVAL16(0x00,0x00,0x00));    
    menuOn = false;    
  }
}

int handleMenu(uint16_t bClick)
{
  if (autorun) {
      menuLeft();
      toggleMenu(false);
      menuRedraw=false;
      return (ACTION_RUN);
  }

  if ( (bClick & MASK_JOY2_BTN) || (bClick & MASK_KEY_USER1) || (bClick & MASK_KEY_USER3) ) {
    char newpath[MAX_FILENAME_PATH];
    strcpy(newpath, selection);
    strcat(newpath, "/");
    strcat(newpath, selected_filename);
    strcpy(selection,newpath);
    emu_printf("new filepath is");
    emu_printf(selection);
    FILINFO entry;
    FRESULT fr;
    fr = f_stat(selection, &entry);
    if ( (fr == FR_OK) && (entry.fattrib & AM_DIR) ) {
        curFile = 0;
        nbFiles = readNbFiles(selection);
        menuRedraw=true;
    }
    else
    {
#ifdef PICO2ZX
      if (bClick & MASK_KEY_USER3) {
        emu_writeConfig();
      }
#endif
      menuLeft();
      toggleMenu(false);
      menuRedraw=false;
#ifdef PICO2ZX
      if ( tft.getMode() != MODE_VGA_320x240) {   
        if ( (bClick & MASK_KEY_USER1) ) {
          tft.begin(MODE_VGA_320x240);
        }
      }
#endif
      return (ACTION_RUN);
    }
  }
  else if ( (bClick & MASK_JOY2_UP) || (bClick & MASK_JOY1_UP) ) {
    if (curFile!=0) {
      menuRedraw=true;
      curFile--;
    }
  }
  else if ( (bClick & MASK_JOY2_RIGHT) || (bClick & MASK_JOY1_RIGHT) ) {
    if ((curFile-9)>=0) {
      menuRedraw=true;
      curFile -= 9;
    } else if (curFile!=0) {
      menuRedraw=true;
      curFile--;
    }
  }  
  else if ( (bClick & MASK_JOY2_DOWN) || (bClick & MASK_JOY1_DOWN) )  {
    if ((curFile<(nbFiles-1)) && (nbFiles)) {
      curFile++;
      menuRedraw=true;
    }
  }
  else if ( (bClick & MASK_JOY2_LEFT) || (bClick & MASK_JOY1_LEFT) ) {
    if ((curFile<(nbFiles-9)) && (nbFiles)) {
      curFile += 9;
      menuRedraw=true;
    }
    else if ((curFile<(nbFiles-1)) && (nbFiles)) {
      curFile++;
      menuRedraw=true;
    }
  }
  else if ( (bClick & MASK_KEY_USER2) ) {
    emu_SwapJoysticks(0);
    menuRedraw=true;  
  } 

  if (menuRedraw && nbFiles) {
    int fileIndex = 0;
    tft.drawRectNoDma(MENU_FILE_XOFFSET,MENU_FILE_YOFFSET, MENU_FILE_W, MENU_FILE_H, MENU_FILE_BGCOLOR);
//    if (curFile <= (MAX_MENULINES/2-1)) topFile=0;
//    else topFile=curFile-(MAX_MENULINES/2);
    if (curFile <= (MAX_MENULINES-1)) topFile=0;
    else topFile=curFile-(MAX_MENULINES/2);

    int i=0;
    while (i<MAX_MENULINES) {
      if (fileIndex>=nbFiles) {
          // no more files
          break;
      }
      char * filename = &files[fileIndex][0];    
      if (fileIndex >= topFile) {              
        if ((i+topFile) < nbFiles ) {
          if ((i+topFile)==curFile) {
            tft.drawTextNoDma(MENU_FILE_XOFFSET,i*TEXT_HEIGHT+MENU_FILE_YOFFSET, filename, RGBVAL16(0xff,0xff,0x00), RGBVAL16(0xff,0x00,0x00), true);
            strcpy(selected_filename,filename);            
          }
          else {
            tft.drawTextNoDma(MENU_FILE_XOFFSET,i*TEXT_HEIGHT+MENU_FILE_YOFFSET, filename, RGBVAL16(0xff,0xff,0xff), MENU_FILE_BGCOLOR, true);      
          }
        }
        i++; 
      }
      fileIndex++;    
    }

     
    tft.drawTextNoDma(48,MENU_JOYS_YOFFSET+8, (emu_SwapJoysticks(1)?(char*)"SWAP=1":(char*)"SWAP=0"), RGBVAL16(0x00,0xff,0xff), RGBVAL16(0x00,0x00,0xff), false);
    menuRedraw=false;     
  }

  return (ACTION_NONE);  
}

char * menuSelection(void)
{
  return (selection);  
}
#endif



/********************************
 * USB keyboard
********************************/ 
#ifdef HAS_USBHOST

#ifdef KEYBOARD_ACTIVATED
static bool kbdasjoy = false;
#else
static bool kbdasjoy = true;
#endif

static void signal_joy (int code, int pressed, int flags) {
  if ( (code == KBD_KEY_DOWN) && (pressed) ) usbnavpad |= MASK_JOY2_DOWN;
  if ( (code == KBD_KEY_DOWN) && (!pressed) ) usbnavpad &= ~MASK_JOY2_DOWN;
  if ( (code == KBD_KEY_UP) && (pressed) ) usbnavpad |= MASK_JOY2_UP;
  if ( (code == KBD_KEY_UP) && (!pressed) ) usbnavpad &= ~MASK_JOY2_UP;
#if INVX
  if ( (code == KBD_KEY_RIGHT) && (pressed) ) usbnavpad |= MASK_JOY2_LEFT;
  if ( (code == KBD_KEY_RIGHT) && (!pressed) ) usbnavpad &= ~MASK_JOY2_LEFT;
  if ( (code == KBD_KEY_LEFT) && (pressed) ) usbnavpad |= MASK_JOY2_RIGHT;
  if ( (code == KBD_KEY_LEFT) && (!pressed) ) usbnavpad &= ~MASK_JOY2_RIGHT;
#else  
  if ( (code == KBD_KEY_LEFT) && (pressed) ) usbnavpad |= MASK_JOY2_LEFT;
  if ( (code == KBD_KEY_LEFT) && (!pressed) ) usbnavpad &= ~MASK_JOY2_LEFT;
  if ( (code == KBD_KEY_RIGHT) && (pressed) ) usbnavpad |= MASK_JOY2_RIGHT;
  if ( (code == KBD_KEY_RIGHT) && (!pressed) ) usbnavpad &= ~MASK_JOY2_RIGHT;
#endif
  if ( (code == '\t') && (pressed) ) usbnavpad |= MASK_JOY2_BTN; 
  if ( (code == '\t') && (!pressed) ) usbnavpad &= ~MASK_JOY2_BTN;
  if ( (code == '1') && (pressed) ) usbnavpad |= MASK_KEY_USER1; 
  if ( (code == '1') && (!pressed) ) usbnavpad &= ~MASK_KEY_USER1;
  if ( (code == '2') && (pressed) ) usbnavpad |= MASK_KEY_USER2; 
  if ( (code == '2') && (!pressed) ) usbnavpad &= ~MASK_KEY_USER2;
  //if ( (code == 'c') && (pressed) ) usbnavpad |= MASK_KEY_USER3; 
  //if ( (code == 'c') && (!pressed) ) usbnavpad &= ~MASK_KEY_USER3;
  if ( (code == KBD_KEY_CAPS) && (pressed) ) usbnavpad |= MASK_KEY_USER3; 
  if ( (code == KBD_KEY_CAPS) && (!pressed) ) usbnavpad &= ~MASK_KEY_USER3;
  if ( (code == ' ') && (pressed) ) usbnavpad |= MASK_KEY_USER4; 
  if ( (code == ' ') && (!pressed) ) usbnavpad &= ~MASK_KEY_USER4;
}

void kbd_signal_raw_key (int keycode, int code, int codeshifted, int flags, int pressed) {
  //printf("k %d\r\n", keycode); 
#ifdef FILEBROWSER
  if (menuActive())
  {
    signal_joy(code, pressed, flags);          
  }
  else  
#endif  
  {
    // LCTRL + LSHIFT + J => keyboard as joystick
    if ( ( ( (flags & (KBD_FLAG_LSHIFT + KBD_FLAG_LCONTROL)) == (KBD_FLAG_LSHIFT + KBD_FLAG_LCONTROL) ) && (!pressed) && (code == 'j') ) || ( (!pressed) && (keycode == 69) ) ) {
      if (kbdasjoy == true) kbdasjoy = false; 
      else kbdasjoy = true;
    }

    //keyboard as joystick?
    if (kbdasjoy == true) {
      signal_joy(code, pressed, flags);
    }
    else {
      if (pressed == KEY_PRESSED)
      {    
        //emu_printi(keycode);    
        //emu_printi(codeshifted);    
        emu_KeyboardOnDown(flags, codeshifted);
      }
      else    
      { 
        emu_KeyboardOnUp(flags, codeshifted);
      }
    }
  }
  return;
}
#endif


/********************************
 * File IO
********************************/ 
int emu_FileOpen(const char * filepath, const char * mode)
{
  int retval = 0;

  emu_printf("FileOpen...");
  emu_printf(filepath);
  if( !(f_open(&file, filepath, FA_READ)) ) {
    retval = 1;  
  }
  else {
    emu_printf("FileOpen failed");
  }
  return (retval);
}

int emu_FileRead(void * buf, int size, int handler)
{
  unsigned int retval=0; 
  f_read (&file, (void*)buf, size, &retval);
  return retval; 
}

int emu_FileGetc(int handler)
{
  unsigned char c;
  unsigned int retval=0;
  if( !(f_read (&file, &c, 1, &retval)) )
  if (retval != 1) {
    emu_printf("emu_FileGetc failed");
  }  
  return (int)c; 
}

void emu_FileClose(int handler)
{
  f_close(&file); 
}

int emu_FileSeek(int handler, int seek, int origin)
{
  f_lseek(&file, seek);
  return (seek);
}

int emu_FileTell(int handler)
{
  return (f_tell(&file));
}


unsigned int emu_FileSize(const char * filepath)
{
  int filesize=0;
  emu_printf("FileSize...");
  emu_printf(filepath);
  FILINFO entry;
  f_stat(filepath, &entry);
  filesize = entry.fsize; 
  return(filesize);    
}

unsigned int emu_LoadFile(const char * filepath, void * buf, int size)
{
  int filesize = 0;
    
  emu_printf("LoadFile...");
  emu_printf(filepath);
  if( !(f_open(&file, filepath, FA_READ)) ) {
    filesize = f_size(&file);
    emu_printf(filesize);
    if (size >= filesize)
    {
      unsigned int retval=0;
      if( (f_read (&file, buf, filesize, &retval)) ) {
        emu_printf("File read failed");        
      }
    }
    f_close(&file);
  }
 
  return(filesize);
}

static FIL outfile; 

static bool emu_writeGfxConfig(void)
{
  bool retval = false;
  if( !(f_open(&outfile, "/" GFX_CFG_FILENAME, FA_CREATE_NEW | FA_WRITE)) ) {
    f_close(&outfile);
    retval = true;
  } 
  return retval;   
}

static bool emu_readGfxConfig(void)
{
  bool retval = false;
  if( !(f_open(&outfile, "/" GFX_CFG_FILENAME, FA_READ)) ) {
    f_close(&outfile);
    retval = true;
  }  
  return retval;   
}

static bool emu_eraseGfxConfig(void)
{
  f_unlink ("/" GFX_CFG_FILENAME);
  return true;
}

static bool emu_writeConfig(void)
{
  bool retval = false;
  if( !(f_open(&outfile, ROMSDIR "/" AUTORUN_FILENAME, FA_CREATE_NEW | FA_WRITE)) ) {
    unsigned int sizeread=0;
    if( (f_write (&outfile, selection, strlen(selection), &sizeread)) ) {
      emu_printf("Config write failed");        
    }
    else {
      retval = true;
    }  
    f_close(&outfile);   
  } 
  return retval; 
}

#ifdef HAS_USBHOST          
static bool emu_readKbdConfig(void)
{
  bool retval = false;
  char scratchpad[64]={0};
  if( !(f_open(&outfile, "/" KBD_CFG_FILENAME , FA_READ)) ) {
    while (f_gets(scratchpad, 64, &outfile) != NULL)  {
      if (!strncmp(scratchpad, "keyboard=", 9)) {
        if ( ( scratchpad[9]=='u') && (scratchpad[10]=='k') ) {
          kbd_set_locale(KLAYOUT_UK);
        }
        else if ( ( scratchpad[9]=='b') && (scratchpad[10]=='e') ) {
          kbd_set_locale(KLAYOUT_BE);
        }       
      }  
    }
    f_close(&outfile);
    retval = true;
  }  
  return retval;   
}
#endif

static bool emu_readConfig(void)
{
  bool retval = false;
  if( !(f_open(&outfile, ROMSDIR "/" AUTORUN_FILENAME, FA_READ)) ) {
    unsigned int filesize = f_size(&outfile);
    unsigned int sizeread=0;
    if( (f_read (&outfile, selection, filesize, &sizeread)) ) {
      emu_printf("Config read failed");        
    }
    else {
      if (sizeread == filesize) {
        selection[filesize]=0;
        retval = true;
      }
    }  
    f_close(&outfile);   
  }  
  return retval; 
}

static bool emu_eraseConfig(void)
{
  f_unlink (ROMSDIR "/" AUTORUN_FILENAME);
  return true;
}


/********************************
 * Initialization
********************************/ 
void emu_init(void)
{
  //board_init();
  stdio_init_all();

  bool forceVga = false;

#ifdef HAS_USBHOST
  printf("Init USB...\n");
  tuh_init(BOARD_TUH_RHPORT);
#endif
#ifdef HAS_USBPIO  
  printf("USB D+/D- on GP%d and GP%d\r\n", PIO_USB_DP_PIN_DEFAULT, PIO_USB_DP_PIN_DEFAULT+1);
  printf("TinyUSB Host HID Controller Example\r\n");
#endif
  
#ifdef FILEBROWSER
//  sd_init_driver(); 

  int retry=5;
  FRESULT fr = FR_NO_FILESYSTEM;
  while ((retry > 0) && (fr != FR_OK)) {
    fr = f_mount(&fatfs, "", 0); 
    sleep_ms(500); 
    retry--; 
  }
  if (fr != FR_OK) {
    emu_printf("mount fail"); 
  }     

#ifdef HAS_USBHOST          
  emu_readKbdConfig();
#endif

  forceVga = emu_readGfxConfig();

  strcpy(selection,ROMSDIR);
  nbFiles = readNbFiles(selection); 

  emu_printf("SD initialized, files found: ");
  emu_printi(nbFiles);
#endif
  
  emu_InitJoysticks();
#ifdef SWAP_JOYSTICK
  joySwapped = true;   
#else
  joySwapped = false;   
#endif  

int keypressed = emu_ReadKeys();

#ifdef USE_VGA    
    tft.begin(MODE_VGA_320x240);
#else

#ifdef PICO2ZX    
  // Force VGA if LEFT/RIGHT pressed
  if (keypressed & MASK_JOY2_UP)
  {
    tft.begin(MODE_VGA_320x240);
#ifdef FILEBROWSER
    emu_writeGfxConfig();
#endif    
  }
  else
  {
    if ( (keypressed & MASK_JOY2_LEFT) || (keypressed & MASK_JOY2_RIGHT) )
    {
#ifdef FILEBROWSER
        emu_eraseGfxConfig();
#endif    
        forceVga = false;
    }
    if (forceVga) {
      tft.begin(MODE_VGA_320x240);
    }
    else
    {
      tft.begin(MODE_TFT_320x240);    
    }     
  }
#else /* end PICO2ZX */
  tft.begin(MODE_TFT_320x240);    
#endif 

#endif

  if ( (keypressed & MASK_JOY2_DOWN) ){
    tft.fillScreenNoDma( RGBVAL16(0xff,0x00,0x00) );
    tft.drawTextNoDma(64,48,    (char*)" AUTURUN file erased", RGBVAL16(0xff,0xff,0x00), RGBVAL16(0xff,0x00,0x00), true);
    tft.drawTextNoDma(64,48+24, (char*)"Please reset the board!", RGBVAL16(0xff,0xff,0x00), RGBVAL16(0xff,0x00,0x00), true);
    emu_eraseConfig();
  }
  else {
    if (emu_readConfig()) {
      autorun = true;
    }
  }  

#ifdef FILEBROWSER
  toggleMenu(true);
#endif  
}


void emu_start(void)
{
  usbnavpad = 0;

  keyMap = 0;
}
