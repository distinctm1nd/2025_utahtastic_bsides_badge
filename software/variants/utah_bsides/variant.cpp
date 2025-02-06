#include "../main.h"
#include "../configuration.h"
#include <Wire.h>
#include "variant.h"
#include <Adafruit_NeoPixel.h>

// I2C Expander (TCA9535)
#define TCA9535_ADDRESS 0x20
#define BUTTON_SDA 1
#define BUTTON_SCL 2
#define BUTTON_INT 15

extern graphics::Screen *screen;


#define KEY(x) meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_##x

enum ButtonBit {
    BUTTON_BL       = 1<<0,
    BUTTON_ZERO     = 1<<1,
    BUTTON_BR       = 1<<2,
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
    {' ', ' ', ' ', ' ', ' ', ' '},
    {' ', '0', 0, 0, 0, 0},
    {' ', ' ', ' ', ' ', ' ', ' '},
    {'w', 'x', 'y', 'z', '9', 0},
    {'t', 'u', 'v', '8', 0, 0},
    {'p', 'q', 'r', 's', '7', 0},
    {'m', 'n', 'o', '6', 0, 0},
    {'j', 'k', 'l', '5', 0, 0},
    {'g', 'h', 'i', '4', 0, 0},
    {'d', 'e', 'f', '3', 0, 0},
    {'a', 'b', 'c', '2', 0, 0},
    {'.', ',', '?', '!', '+', '1'},
    {KEY(UP), 0, 0, 0, 0, 0},
    {KEY(RIGHT), 0, 0, 0, 0, 0},
    {KEY(DOWN), 0, 0, 0, 0, 0},
    {KEY(LEFT), 0, 0, 0, 0, 0}
};

#define NUM_ELEMS(a) (sizeof(a)/sizeof a[0])
int konami_code[] = {BUTTON_UP, BUTTON_UP, BUTTON_DOWN, BUTTON_DOWN, BUTTON_LEFT, BUTTON_RIGHT, BUTTON_LEFT, BUTTON_RIGHT, BUTTON_ONE, BUTTON_ONE};
#define KONAMI_CODE_LEN NUM_ELEMS(konami_code)

void handleKonamiCode() {
    screen->startAlert("Konami code:\nYou win!");
}

/*-----------------------------------------------------------------------
Definitions and variables for LEDs
-----------------------------------------------------------------------*/
void handleButtonLedFlashing();
void handleChasingLedPatterns();
void handleButtonPress();

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

const uint32_t rainbowColors[] = {
    0xFF0000,    // Red
    0xFF00FF,    // Magenta
    0xFFD700,    // Yellow
    0x006400,    // Green
    0x00FF00,    // Blue
    0x800080,    // Indigo
    0xFF1493     // Violet
};

const uint32_t blueWhiteColors[] = {
    0x0000FF,    // Blue
    0xFFFFFF,    // White
    0x0000FF,    // Blue
    0xFFFFFF,    // White
    0x0000FF,    // Blue
    0xFFFFFF,    // White
    0x0000FF     // Blue
};

const uint32_t redMaroonColors[] = {
    0xFF0000,    // Red
    0x800000,    // Maroon
    0xFF0000,    // Red
    0x800000,    // Maroon
    0xFF0000,    // Red
    0x800000,    // Maroon
    0xFF0000     // Red
};

const uint32_t pinkWhiteColors[] = {
    0xFF00FF,    // Pink
    0xFFFFFF,    // White
    0xFF00FF,    // Pink
    0xFFFFFF,    // White
    0xFF00FF,    // Pinkc
    0xFFFFFF,    // White
    0xFF00FF     // Pink
};

const uint32_t pinkPurpleColors[] = {
    0xFF00FF,    // Pink
    0x4B0082,    // Purple
    0xFF00FF,    // Pink
    0x4B0082,    // Purple
    0xFF00FF,    // Pink
    0x4B0082,    // Purple
    0xFF00FF     // Pink
};

const uint32_t greenBlueColors[] = {
    0x00FF00,    // Green
    0x0000FF,    // Blue
    0x00FF00,    // Green
    0x0000FF,    // Blue
    0x00FF00,    // Green
    0x0000FF,    // Blue
    0x00FF00     // Green
};

const uint32_t ledsOff[] = {
    0x000000,    // Black
    0x000000,    // Black
    0x000000,    // Black
    0x000000,    // Black
    0x000000,    // Black
    0x000000,    // Black
    0x000000     // Black
};

const int colorsCount = 7;

const uint32_t* activeColorArray = rainbowColors;

/*-----------------------------------------------------------------------
TCA9535 FUNCTIONS
-----------------------------------------------------------------------*/
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

enum MyKeyboardStateType { KEYBOARD_EVENT_OCCURRED, KEYBOARD_EVENT_CLEARED };
enum MyKeyboardActionType { KEYBOARD_ACTION_NONE, KEYBOARD_ACTION_PRESSED };

class MyKeyboard : public Observable<const InputEvent *>, public concurrency::OSThread
{
    public:
    MyKeyboard(const char* name) : concurrency::OSThread(name) {
        _originName = name;
        inputBroker->registerSource(this);
        //Wire.begin(BUTTON_SDA, BUTTON_SCL);
        initializeTCA9535();
        //Wire.end();
        attachInterrupt(BUTTON_INT, intPressHandler, FALLING);
    }

    void onIntPress()
    {
        this->_action = KEYBOARD_ACTION_PRESSED;
        setIntervalFromNow(20);
    }

    uint16_t state() {
        return _prevButtonState;
    }

    protected:
    int32_t runOnce() {
        if (_action == KEYBOARD_ACTION_PRESSED) {
            button_pressed();
            this->_action = KEYBOARD_ACTION_NONE;
        }

        return INT32_MAX;
    }

    private:
    const char* _originName;
    uint16_t _prevButtonState = 0;
    int _konamiCodeIndex = 0;
    int _lastKeyPressed = 0;
    int _quickPress = 0;
    unsigned long _lastPressTime = 0;

    char _event = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
    volatile MyKeyboardStateType _state = KEYBOARD_EVENT_CLEARED;
    volatile MyKeyboardActionType _action = KEYBOARD_ACTION_NONE;

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
        uint16_t newly_pressed = 0;
        if (!button_state) {
            //LOG_DEBUG("no buttons.");
            goto cleanup;
        }

        //LOG_DEBUG("A button was pressed!");
        if (button_state == konami_code[_konamiCodeIndex]) {
            _konamiCodeIndex += 1;
            if (_konamiCodeIndex == KONAMI_CODE_LEN) {
                _konamiCodeIndex = 0;
                handleKonamiCode();
            }
        } else {
            _konamiCodeIndex = 0;
        }

        newly_pressed = button_state & (button_state ^ _prevButtonState);

        // Display button state in the Serial console
        for (int i = 15; i >= 0; i--) {  // Assuming 16 buttons
            buttonStateText += (button_state & (1 << i)) ? "1 " : "0 ";
        }
        Serial.println(buttonStateText);

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
                    emit(keys[i][0]);
                }
                _lastKeyPressed = i;
            }
        }
        
    cleanup:
        _lastPressTime = millis();
        _prevButtonState = button_state;
    }

    void erase()
    {
        emit(KEY(BACK), 0x08);
    }
};

MyKeyboard *mkb = NULL;

void intPressHandler() {
    mkb->onIntPress();
}

void drawDebugScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    LOG_DEBUG("drawDebugScreen called");
    display->setFont(FONT_SMALL);
    String buttonStateText = "Button State:";
    uint16_t button_state = mkb->state();
    for (int i = 15; i >= 0; i--) {  // Assuming 16 buttons
        buttonStateText += (button_state & (1 << i)) ? "1 " : "0 ";
    }
    display->drawString(5, 5, buttonStateText);
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
void initLEDs() {
    button_leds.begin();
    logo_eye_leds.begin();

    // Set the initial color pattern for both button LEDs and logo eye LEDs
    for (int i = 0; i < BUTTON_LED_COUNT; i++) {
        button_leds.setPixelColor(i, activeColorArray[i % colorsCount]);
    }
    button_leds.show();

    for (int i = 0; i < LOGO_EYE_LED_COUNT; i++) {
        logo_eye_leds.setPixelColor(i, activeColorArray[i % colorsCount]);
    }
    logo_eye_leds.show();
}

void handleButtonLedFlashing() {
    unsigned long currentMillis = millis();  

    static unsigned long lastTransitionTime = 0;

    if (currentMillis - lastTransitionTime >= fadeInterval) {  
        lastTransitionTime = currentMillis;

        // Apply a smooth transition to each button LED by updating the color gradually
        for (int i = 0; i < BUTTON_LED_COUNT; i++) {
            // Get the current color index for the LED
            int colorIndex = (currentColorIndex + i) % colorsCount;
            uint32_t targetColor = activeColorArray[colorIndex];

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

        currentColorIndex = (currentColorIndex + 1) % colorsCount;
    }
}

// Function to handle smooth flowing color transition for the eye LEDs
void handleChasingLedPatterns() {
    unsigned long currentMillis = millis();  

    static unsigned long lastTransitionTime = 0;

    if (currentMillis - lastTransitionTime >= chaseInterval) {  
        lastTransitionTime = currentMillis;

        for (int i = 0; i < LOGO_EYE_LED_COUNT; i++) {
            int colorIndex = (currentLedIndex + i) % colorsCount;
            uint32_t targetColor = activeColorArray[colorIndex];

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

void handleButtonPress() {
    static bool lastButtonState = LOW;
    bool buttonState = digitalRead(BUTTON_PIN);
    static unsigned long lastDebounceTime = 0;
    unsigned long debounceDelay = 50; 

    if (buttonState == HIGH && lastButtonState == LOW && (millis() - lastDebounceTime) > debounceDelay) {
        if (activeColorArray == rainbowColors) {
            activeColorArray = blueWhiteColors;  
        } else if (activeColorArray == blueWhiteColors) {
            activeColorArray = redMaroonColors;  
        } else if (activeColorArray == redMaroonColors) {
            activeColorArray = pinkWhiteColors;  
        } else if (activeColorArray == pinkWhiteColors) {
            activeColorArray = pinkPurpleColors;  
        } else if (activeColorArray == greenBlueColors) {
            activeColorArray = greenBlueColors;  
        }  else if (activeColorArray == ledsOff) {
            activeColorArray = ledsOff;
        }
        else {
            activeColorArray = rainbowColors;  
        }
        lastDebounceTime = millis(); 
    }

    lastButtonState = buttonState;
}

void initVariant() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);  
    initLEDs();  
}
