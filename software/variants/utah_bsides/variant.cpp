#include "../main.h"
#include "../configuration.h"
#include <Wire.h>
#include "variant.h"
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>

// I2C Expander (TCA9535)
#define TCA9535_ADDRESS 0x20
#define BUTTON_SDA 1
#define BUTTON_SCL 2
#define BUTTON_INT 15

extern graphics::Screen *screen;


/*-----------------------------------------------------------------------
Definitions and variables for LEDs
-----------------------------------------------------------------------*/
void handleButtonLedFlashing();
void handleChasingLedPatterns();
void handleColorArrayChange();

// Variables for fading button LEDs
unsigned long lastButtonFadeTime = 0;
const unsigned long fadeInterval = 150;  

// Variables for rainbow colors and fading logic
int currentColorIndex = 0;  
float fadeProgress[BUTTON_LED_COUNT] = {0};  
float fadeProgressEye[LOGO_EYE_LED_COUNT] = {0}; 
const float transitionSpeed = 0.01;  

// Variables for chasing eye LEDs
unsigned long lastChaseTime = 0;
const unsigned long chaseInterval = 150; 
int currentLedIndex = 0;  

// Initialize LED objects directly
Adafruit_NeoPixel button_leds(BUTTON_LED_COUNT, BUTTON_LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel logo_eye_leds(LOGO_EYE_LED_COUNT, LOGO_EYE_LED_PIN, NEO_GRB + NEO_KHZ800);

const int COLORSCOUNT = 7;

const uint32_t rainbowColors[COLORSCOUNT] = {
    0xFF0000,    // Red
    0xFF00FF,    // Magenta
    0xFFD700,    // Yellow
    0x006400,    // Green
    0x00FF00,    // Blue
    0x800080,    // Indigo
    0xFF1493     // Violet
};

const uint32_t blueWhiteColors[COLORSCOUNT] = {
    0x0000FF,    // Blue
    0xFFFFFF,    // White
    0x0000FF,    // Blue
    0xFFFFFF,    // White
    0x0000FF,    // Blue
    0xFFFFFF,    // White
    0x0000FF     // Blue
};

const uint32_t redMaroonColors[COLORSCOUNT] = {
    0xFF0000,    // Red
    0x800000,    // Maroon
    0xFF0000,    // Red
    0x800000,    // Maroon
    0xFF0000,    // Red
    0x800000,    // Maroon
    0xFF0000     // Red
};

const uint32_t pinkWhiteColors[COLORSCOUNT] = {
    0xFF00FF,    // Pink
    0xFFFFFF,    // White
    0xFF00FF,    // Pink
    0xFFFFFF,    // White
    0xFF00FF,    // Pinkc
    0xFFFFFF,    // White
    0xFF00FF     // Pink
};

const uint32_t pinkPurpleColors[COLORSCOUNT] = {
    0xFF00FF,    // Pink
    0x4B0082,    // Purple
    0xFF00FF,    // Pink
    0x4B0082,    // Purple
    0xFF00FF,    // Pink
    0x4B0082,    // Purple
    0xFF00FF     // Pink
};

const uint32_t greenBlueColors[COLORSCOUNT] = {
    0x00FF00,    // Green
    0x0000FF,    // Blue
    0x00FF00,    // Green
    0x0000FF,    // Blue
    0x00FF00,    // Green
    0x0000FF,    // Blue
    0x00FF00     // Green
};

const uint32_t ledsOff[COLORSCOUNT] = {
    0x000000,    // Black
    0x000000,    // Black
    0x000000,    // Black
    0x000000,    // Black
    0x000000,    // Black
    0x000000,    // Black
    0x000000     // Black
};

const int COLORARRAYCOUNT = 7;
const uint32_t* colorArrays[COLORARRAYCOUNT] = {rainbowColors, blueWhiteColors, redMaroonColors, pinkWhiteColors, pinkPurpleColors, greenBlueColors, ledsOff};

uint32_t activeColorArrayIndex = 0;

/*-----------------------------------------------------------------------
TCA9535 FUNCTIONS --- Keyboard
-----------------------------------------------------------------------*/

#define USE_KEYBOARD_INTERRUPT false

#define KEY(x) meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_##x

enum ButtonBit {
    BUTTON_BOTLEFT  = 1<<0,
    BUTTON_ZERO     = 1<<1,
    BUTTON_BOTRIGHT = 1<<2,
    BUTTON_NINE     = 1<<3,
    BUTTON_EIGHT    = 1<<4,
    BUTTON_SEVEN    = 1<<5,
    BUTTON_SIX      = 1<<6,
    BUTTON_FIVE     = 1<<7,
    BUTTON_FOUR     = 1<<8,
    BUTTON_THREE    = 1<<9,
    BUTTON_TWO      = 1<<10,
    BUTTON_ONE      = 1<<11,
    BUTTON_UP       = 1<<12,
    BUTTON_RIGHT    = 1<<13,
    BUTTON_DOWN     = 1<<14,
    BUTTON_LEFT     = 1<<15
};

char keys[][6] = {
    {'*',   0,   0,   0,   0,   0},
    {' ', '0',   0,   0,   0,   0},
    {'#',   0,   0,   0,   0,   0},
    {'w', 'x', 'y', 'z', '9',   0},
    {'t', 'u', 'v', '8',   0,   0},
    {'p', 'q', 'r', 's', '7',   0},
    {'m', 'n', 'o', '6',   0,   0},
    {'j', 'k', 'l', '5',   0,   0},
    {'g', 'h', 'i', '4',   0,   0},
    {'d', 'e', 'f', '3',   0,   0},
    {'a', 'b', 'c', '2',   0,   0},
    {'.', ',', '?', '!', '+', '1'},
    {KEY(UP),    0, 0, 0, 0, 0},
    {KEY(RIGHT), 0, 0, 0, 0, 0},
    {KEY(DOWN),  0, 0, 0, 0, 0},
    {KEY(LEFT),  0, 0, 0, 0, 0}
};

#define NUM_ELEMS(a) (sizeof(a)/sizeof a[0])
int konami_code[] = {BUTTON_UP, BUTTON_UP, BUTTON_DOWN, BUTTON_DOWN, BUTTON_LEFT, BUTTON_RIGHT, BUTTON_LEFT, BUTTON_RIGHT, BUTTON_TWO, BUTTON_TWO};
#define KONAMI_CODE_LEN NUM_ELEMS(konami_code)

// forward declaration for use in the keyboard class
void drawMenuScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

void handleKonamiCode() {
    screen->startAlert("Konami code:\nYou win!");
}

void initializeTCA9535() {
    Wire.beginTransmission(TCA9535_ADDRESS);
    Wire.write(0x06); // Set all pins as inputs
    Wire.endTransmission();
}

uint16_t readButtonState() {
    Wire.beginTransmission(TCA9535_ADDRESS);
    Wire.write(0x00); // Request input port 0
    Wire.endTransmission();
    Wire.requestFrom(TCA9535_ADDRESS, 2);
    uint16_t state = Wire.read() | (Wire.read() << 8);
    return ~state;
}

void intPressHandler();

class MyKeyboard : public Observable<const InputEvent *>, public concurrency::OSThread
{
    public:
    MyKeyboard(const char* name) : concurrency::OSThread(name) {
        _originName = name;
        inputBroker->registerSource(this);
        initializeTCA9535();

        if (USE_KEYBOARD_INTERRUPT) {
            // use the keyboard interrupt to detect changes
            attachInterrupt(BUTTON_INT, intPressHandler, FALLING);
        } else {
            // use polling to detect changes
            setIntervalFromNow(20);
        }
    }

    // this is an interrupt handler, so only schedule runOnce to be called.
    void onIntPress()
    {
        setIntervalFromNow(20);
    }

    uint16_t state() {
        return _prevButtonState;
    }

    protected:
    int32_t runOnce() {
        // process any button presses
        button_pressed();

        if (USE_KEYBOARD_INTERRUPT) {
            // don't call this method again
            return INT32_MAX;
        } else {
            // call this method again soon
            return 20;
        }
    }

    private:
    const char* _originName;
    uint16_t _prevButtonState = 0;
    int _konamiCodeIndex = 0;
    int _lastKeyPressed = 0;
    int _menuActivated = 0;
    int _konamiCodeActivated = 0;
    int _quickPress = 0;
    unsigned long _lastPressTime = 0;

    char _event = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;

    void emit(char key, char kbchar = 0) {
        InputEvent e;
        e.inputEvent = key;
        e.kbchar = kbchar;
        e.source = this->_originName;
        if (e.inputEvent != KEY(NONE)) {
            this->notifyObservers(&e);
        }
    }

    void button_pressed() {
        String buttonStateText = "Button State:\n";
        uint16_t button_state = readButtonState();
        uint16_t newly_pressed = button_state & (button_state ^ _prevButtonState);
        if (!button_state) {
            //Serial.println("no buttons.");
            goto cleanup;
        } else {
            Serial.println("A button is pressed!");
        }

        if (!newly_pressed) {
            // we've already detected and handled the button
            // so check for a long press action from BUTTON_BOTLEFT
            unsigned long currentTime = millis();
            // if it's been 3 seconds
            if (button_state == BUTTON_BOTLEFT && _lastPressTime + 3000 < currentTime) {
                if (!_menuActivated) {
                    _menuActivated = 1;
                    Serial.println("Menu Activated!");
                    erase();  // remove the emitted BOTLEFT character from the initial press
                    screen->startAlert(drawMenuScreen);
                }
            }

            // no new button pressed, exit the function
            goto cleanup;
        }

        if (_konamiCodeActivated) {
            _konamiCodeActivated = 0;
            screen->endAlert();
            return; // don't process the button
        }

        if (newly_pressed == konami_code[_konamiCodeIndex]) {
            _konamiCodeIndex += 1;
            if (_konamiCodeIndex == KONAMI_CODE_LEN) {
                _konamiCodeIndex = 0;
                Serial.println("KONAMI CODE ACTIVATED!");
                handleKonamiCode();
                _konamiCodeActivated = 1;
            }
        } else {
            _konamiCodeIndex = 0;
        }

        if (_menuActivated) {
            if (newly_pressed == BUTTON_ONE) {
                handleColorArrayChange();
            }
            else if (newly_pressed == BUTTON_TWO) {
                screen->endAlert();
                _menuActivated = 0;
            }
            // don't emit any keys while in our menu
            goto cleanup;
        }

        for (int i = 0; i < 16; i++) {
            if (newly_pressed & (1 << i)) {
                // if a non arrow key was pressed
                if (i < 12) {
                    // if the same key is pressed quickly
                    if (i == _lastKeyPressed && millis() - _lastPressTime < 500) {
                        // increment the key index used
                        _quickPress = (_quickPress + 1) % 6;
                        // but if we've run out of symbols for the key
                        if (!keys[i][_quickPress]) {
                            // go back to the beginning
                            _quickPress = 0;
                        }
                        erase();
                    } else {
                        _quickPress = 0;
                    }
                    emit(ANYKEY, keys[i][_quickPress]);
                } else {
                    if (i==14) {            // BUTTON_DOWN
                        erase();            //   erase the previous letter
                    } else if (i==12) {     // BUTTON_UP
                        emit(KEY(SELECT));  //   send the message
                    } else {
                        emit(keys[i][0]);   // send LEFT or RIGHT
                    }
                }
                _lastKeyPressed = i;
            }
        }
        
        _lastPressTime = millis();

    cleanup:
        _prevButtonState = button_state;
    }

    void erase()
    {
        emit(KEY(BACK), 0x08);
    }
};

// Our keyboard object
MyKeyboard *mkb = NULL;

// this function just calls the keyboard's interrupt handler,
// because the interrupt handler doesn't work with class methods.
void intPressHandler() {
    mkb->onIntPress();
}

// ../../.pio/libdeps/utah_bsides/ESP8266 and ESP32 OLED driver for SSD1306 displays/src/OLEDDisplay.h
void drawMenuScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    display->drawString(5, 5, "Press 1 to change light pattern");
    display->drawString(5, 15, "Press 2 to exit");
}

// ../../.pio/libdeps/utah_bsides/ESP8266 and ESP32 OLED driver for SSD1306 displays/src/OLEDDisplay.h
void drawDebugScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    LOG_DEBUG("drawDebugScreen called");
    //display->setFont(FONT_SMALL);
    //String buttonStateText = "Button State:\n";
    uint16_t button_state = mkb->state();
    //for (int i = 15; i >= 0; i--) {  // Assuming 16 buttons
        //buttonStateText += (button_state & (1 << i)) ? "1 " : "0 ";
    //}
    //display->drawString(5, 5, buttonStateText);

    display->drawRect(0, 0, display->width(), display->height());

    for (int i = 15; i >= 0; i--) {
        if (button_state & (1 << i)) {
            display->drawRect(i*10, 0, 10, 10);
        }
    }
}

void checkForDebugMode() {
    uint16_t button_state = readButtonState();
    if (button_state & BUTTON_ZERO) {
        screen->startAlert("This is a test screen message");
    }
    if (button_state & BUTTON_ONE) {
        screen->startAlert(drawDebugScreen);
    }
}

void lateInitVariant()
{
    LOG_DEBUG("here we are!");
    mkb = new MyKeyboard("BSIDES-UTAH");

    checkForDebugMode();
}

/*-----------------------------------------------------------------------
LED Functions
-----------------------------------------------------------------------*/

void updateColorIndexPreference() {
    Preferences prefs;

    // save the new preference
    prefs.begin("utah", false);
    prefs.putInt("colorIndex", activeColorArrayIndex);
    prefs.end();
}

void updateLEDs() {
    // Set the initial color pattern for both button LEDs and logo eye LEDs
    for (int i = 0; i < BUTTON_LED_COUNT; i++) {
        button_leds.setPixelColor(i, colorArrays[activeColorArrayIndex][i % COLORSCOUNT]);
    }
    button_leds.show();

    for (int i = 0; i < LOGO_EYE_LED_COUNT; i++) {
        logo_eye_leds.setPixelColor(i, colorArrays[activeColorArrayIndex][i % COLORSCOUNT]);
    }
    logo_eye_leds.show();
}

void initLEDs() {
    bool update_prefs = false;
    Preferences prefs;
    prefs.begin("utah", false);
    if (prefs.isKey("colorIndex")) {
        activeColorArrayIndex = prefs.getInt("colorIndex");
        if (activeColorArrayIndex >= COLORARRAYCOUNT) {
            activeColorArrayIndex = 0;
            update_prefs = true;
        }
    } else {
        update_prefs = true;
    }
    prefs.end();

    if (update_prefs) {
        updateColorIndexPreference();
    }

    button_leds.begin();
    logo_eye_leds.begin();

    updateLEDs();
}

void handleButtonLedFlashing() {
    unsigned long currentMillis = millis();  

    static unsigned long lastTransitionTime = 0;

    if (currentMillis - lastTransitionTime >= fadeInterval) {  
        lastTransitionTime = currentMillis;

        // Apply a smooth transition to each button LED by updating the color gradually
        for (int i = 0; i < BUTTON_LED_COUNT; i++) {
            // Get the current color index for the LED
            int colorIndex = (currentColorIndex + i) % COLORSCOUNT;
            uint32_t targetColor = colorArrays[activeColorArrayIndex][colorIndex];

            // Extract RGB components of the new target color
            uint8_t targetR = (targetColor >> 16) & 0xFF;
            uint8_t targetG = (targetColor >> 8) & 0xFF;
            uint8_t targetB = targetColor & 0xFF;

            // Get the current color of the button LED
            uint32_t currentPixelColor = button_leds.getPixelColor(i);
            uint8_t currentR = (currentPixelColor >> 16) & 0xFF;
            uint8_t currentG = (currentPixelColor >> 8) & 0xFF;
            uint8_t currentB = currentPixelColor & 0xFF;

            // Apply smooth transition by interpolating colors (fading from one to the next)
            if (fadeProgress[i] < 1.0f) {
                fadeProgress[i] += transitionSpeed;  // Increase the fade progress
                fadeProgress[i] = constrain(fadeProgress[i], 0.0f, 1.0f);  // Ensure it doesn't go past 1.0
            }

            uint8_t newR = currentR + (targetR - currentR) * fadeProgress[i];
            uint8_t newG = currentG + (targetG - currentG) * fadeProgress[i];
            uint8_t newB = currentB + (targetB - currentB) * fadeProgress[i];

            button_leds.setPixelColor(i, button_leds.Color(newR, newG, newB));
        }

        button_leds.show();

        currentColorIndex = (currentColorIndex + 1) % COLORSCOUNT;
    }
}

// Function to handle smooth flowing color transition for the eye LEDs
void handleChasingLedPatterns() {
    unsigned long currentMillis = millis();  

    static unsigned long lastTransitionTime = 0;

    if (currentMillis - lastTransitionTime >= chaseInterval) {  
        lastTransitionTime = currentMillis;

        for (int i = 0; i < LOGO_EYE_LED_COUNT; i++) {
            int colorIndex = (currentLedIndex + i) % COLORSCOUNT;
            uint32_t targetColor = colorArrays[activeColorArrayIndex][colorIndex];

            uint8_t targetR = (targetColor >> 16) & 0xFF;
            uint8_t targetG = (targetColor >> 8) & 0xFF;
            uint8_t targetB = targetColor & 0xFF;

            uint32_t currentPixelColor = logo_eye_leds.getPixelColor(i);
            uint8_t currentR = (currentPixelColor >> 16) & 0xFF;
            uint8_t currentG = (currentPixelColor >> 8) & 0xFF;
            uint8_t currentB = currentPixelColor & 0xFF;

            if (fadeProgressEye[i] < 1.0f) {
                fadeProgressEye[i] += transitionSpeed;
                fadeProgressEye[i] = constrain(fadeProgressEye[i], 0.0f, 1.0f);
            }

            uint8_t newR = currentR + (targetR - currentR) * fadeProgressEye[i];
            uint8_t newG = currentG + (targetG - currentG) * fadeProgressEye[i];
            uint8_t newB = currentB + (targetB - currentB) * fadeProgressEye[i];

            logo_eye_leds.setPixelColor(i, logo_eye_leds.Color(newR, newG, newB));
        }

        logo_eye_leds.show();

        currentLedIndex = (currentLedIndex + 1) % LOGO_EYE_LED_COUNT;
    }
}

void handleColorArrayChange() {
    activeColorArrayIndex = (activeColorArrayIndex + 1) % COLORARRAYCOUNT;
    updateLEDs(); //update the colors
    updateColorIndexPreference(); //persist
}

void minibadgeClk(void * pvParameters){ 
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();

    pinMode(MB_CLOCK_H, OUTPUT);
    pinMode(MB_CLOCK_L, OUTPUT);

    while (1){ 
        //Update Minibadge CLK pin
        if (digitalRead(MB_CLOCK_H)) {
            digitalWrite(MB_CLOCK_H, LOW);
            digitalWrite(MB_CLOCK_L, HIGH);
        } else {
            digitalWrite(MB_CLOCK_H, HIGH);
            digitalWrite(MB_CLOCK_L, LOW);
        }

        //Wait for the next 1 second interval
        xTaskDelayUntil(&xLastWakeTime, 1000);
    }
}

void initVariant() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);  
    initLEDs();  
    TaskHandle_t periodicTaskHandle = NULL;
    xTaskCreateUniversal(minibadgeClk, "periodic", 8192, NULL, 1, &periodicTaskHandle, ARDUINO_RUNNING_CORE);
}
