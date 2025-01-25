//////////////////////////////////////////////////////////////////////////////////
//                                                                              //
//   Utah BSides Badge    //
//                                                                              //
//////////////////////////////////////////////////////////////////////////////////

#define HAS_WIFI 1
#define HAS_BLUETOOTH 1

#define HAS_RADIO 1

#define HAS_GPS 0

#define CANNED_MESSAGE_MODULE_ENABLE 1

//LORA MODULE
#define USE_LLCC68 

//LORA SPI
#define LORA_SCK 3              
#define LORA_MOSI 10                 
#define LORA_MISO 9 
#define LORA_CS 11

//LORA CONFIG
#define SX126X_CS LORA_CS         // module's NSS pin                
#define SX126X_BUSY 14                
#define SX126X_DIO1 13  
#define SX126X_RESET 21      

#define LORA_FREQUENCY 915000000


// Display

#define HAS_SCREEN 1 

// ST7735S TFT LCD
#define ST7735S 1 // there are different (sub-)versions of ST7735
#define ST7735_CS 38
#define ST7735_RS 35  // DC
#define ST7735_SDA 37 // MOSI
#define ST7735_SCK 36
#define ST7735_RESET 48
#define ST7735_MISO -1
#define ST7735_BUSY -1
#define TFT_BL 47
#define ST7735_SPI_HOST SPI3_HOST 
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 160
#define TFT_WIDTH 128
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_INVERT false
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 5 // fps
#define DISPLAY_FORCE_SMALL_FONTS
#define TFT_BACKLIGHT_ON HIGH


//#define 

#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 6                     // How many neopixels are connected
#define NEOPIXEL_DATA 16                      // gpio pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // type of neopixels in use
#define ENABLE_AMBIENTLIGHTING               // Turn on Ambient Lighting


// All buttons including 5 way nav use the TCA9535 GPIO Expander 
//EXCEPT center NAV button is on following pin GPIO 7
//
//I2C
#define I2C_SDA 1
#define I2C_SCL 2



#define ENABLE_AMBIENTLIGHTING               // Turn on Ambient Lighting
// #define BUTTON1_COLOR 0xFF0000               // Background light for Button 1 in HEX RGB Color (RadioMaster Bandit only).
// #define BUTTON1_COLOR_INDEX 0                // NeoPixel Index ID for Button 1
// #define BUTTON2_COLOR 0x0000FF               // Background light for Button 2 in HEX RGB Color (RadioMaster Bandit only).
// #define BUTTON2_COLOR_INDEX 1                // NeoPixel Index ID for Button 2

/*
  It has 1 x five-way and 2 x normal buttons.

  Button    GPIO    RGB Index
  ---------------------------
  Five-way  39      -
  Button 1  34      0
  Button 2  35      1

  Five way button when using ADC.
  2.632V, 2.177V, 1.598V, 1.055V, 0V

  ADC Values:
  { UP, DOWN, LEFT, RIGHT, ENTER, IDLE }
  3227, 0 ,1961, 2668, 1290, 4095

  Five way button when using ADC.
  https://github.com/ExpressLRS/targets/blob/f3215b5ec891108db1a13523e4163950cfcadaac/TX/Radiomaster%20Bandit.json#L41

*/
//#define INPUTBROKER_EXPRESSLRSFIVEWAY_TYPE
//#define PIN_JOYSTICK 7
//#define JOYSTICK_ADC_VALS /*UP*/ 3227, /*DOWN*/ 0, /*LEFT*/ 1961, /*RIGHT*/ 2668, /*OK*/ 1290, /*IDLE*/ 4095


// keyboard
/*#define INPUTBROKER_SERIAL_TYPE 1
#define KB_LOAD 21 // load values from the switch and store in shift register
#define KB_CLK 22  // clock pin for serial data out
#define KB_DATA 23 // data pin
#define CANNED_MESSAGE_MODULE_ENABLE 1
*/

/*
 Normal Button Pin setup.
*/
#define BUTTON_PIN 7
//#define BUTTON_NEED_PULLUP

//Buzzer
//#define PIN_BUZZER  //Add pin number for buzzer. 39, 40, 41, 42


#define UART_TX 43
#define UART_RX 44
