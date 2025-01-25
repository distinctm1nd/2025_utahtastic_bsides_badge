#include "../main.h"
#include "../configuration.h"
#include <Wire.h>

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

#define NUM_ELEMS(a) (sizeof(a)/sizeof a[0])
int konami_code[] = {BUTTON_UP, BUTTON_UP, BUTTON_DOWN, BUTTON_DOWN, BUTTON_LEFT, BUTTON_RIGHT, BUTTON_LEFT, BUTTON_RIGHT, BUTTON_ONE, BUTTON_ONE};
#define KONAMI_CODE_LEN NUM_ELEMS(konami_code)

void handleKonamiCode() {
    screen->startAlert("Konami code:\nYou win!");
}

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
        _prevButtonState = 0;
        _konamiCodeIndex = 0;
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
    uint16_t _prevButtonState;
    int _konamiCodeIndex;

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
        uint16_t button_state = readButtonState();
        if (!button_state) {
            //LOG_DEBUG("no buttons.");
            return;
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

        // Display button state in the Serial console
        String buttonStateText = "Button State: ";
        for (int i = 15; i > 0; i--) {  // Assuming 16 buttons
            buttonStateText += (button_state & (1 << i)) ? "1 " : "0 ";
        }
        Serial.println(buttonStateText);

        if (button_state & BUTTON_UP) {
            LOG_DEBUG("BUTTON_UP pressed");
            emit(KEY(UP));
        }
        if (button_state & BUTTON_DOWN) {
            LOG_DEBUG("BUTTON_DOWN pressed");
            emit(KEY(DOWN));
        }
        if (button_state & BUTTON_LEFT) {
            LOG_DEBUG("BUTTON_LEFT pressed");
            emit(KEY(LEFT));
        }
        if (button_state & BUTTON_RIGHT) {
            LOG_DEBUG("BUTTON_RIGHT pressed");
            emit(KEY(RIGHT));
        }
        if (button_state & BUTTON_BL) {
            LOG_DEBUG("BUTTON_BL pressed");
            emit(ANYKEY, INPUT_BROKER_MSG_BRIGHTNESS_DOWN);
        }
        if (button_state & BUTTON_BR) {
            LOG_DEBUG("BUTTON_BR pressed");
            emit(ANYKEY, INPUT_BROKER_MSG_BLUETOOTH_TOGGLE);
        }
        if (button_state & BUTTON_ZERO) {
            LOG_DEBUG("BUTTON_ZERO pressed");
            emit(ANYKEY, '0');
        }
        if (button_state & BUTTON_ONE) {
            LOG_DEBUG("BUTTON_ONE pressed");
            emit(ANYKEY, '1');
        }
        if (button_state & BUTTON_TWO) {
            LOG_DEBUG("BUTTON_TWO pressed");
            emit(ANYKEY, '2');
        }
        if (button_state & BUTTON_THREE) {
            LOG_DEBUG("BUTTON_THREE pressed");
            emit(ANYKEY, '3');
        }
        if (button_state & BUTTON_FOUR) {
            LOG_DEBUG("BUTTON_FOUR pressed");
            emit(ANYKEY, '4');
        }
        if (button_state & BUTTON_FIVE) {
            LOG_DEBUG("BUTTON_FIVE pressed");
            emit(ANYKEY, '5');
        }
        if (button_state & BUTTON_SIX) {
            LOG_DEBUG("BUTTON_SIX pressed");
            emit(ANYKEY, '6');
        }
        if (button_state & BUTTON_SEVEN) {
            LOG_DEBUG("BUTTON_SEVEN pressed");
            emit(ANYKEY, '7');
        }
        if (button_state & BUTTON_EIGHT) {
            LOG_DEBUG("BUTTON_EIGHT pressed");
            emit(ANYKEY, '8');
        }
        if (button_state & BUTTON_NINE) {
            LOG_DEBUG("BUTTON_NINE pressed");
            emit(ANYKEY, '9');
        }
        _prevButtonState = button_state;
    }
};

MyKeyboard *mkb = NULL;

void intPressHandler() {
    mkb->onIntPress();
}

void drawDebugScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    LOG_DEBUG("drawDebugScreen called");
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