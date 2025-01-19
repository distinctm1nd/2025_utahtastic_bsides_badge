#include "../main.h"
#include "../configuration.h"
#include <Wire.h>

// I2C Expander (TCA9535) 
#define TCA9535_ADDRESS 0x20  
#define BUTTON_SDA 1
#define BUTTON_SCL 2
#define BUTTON_INT 15


#define KEY(x) meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_##x

/*-----------------------------------------------------------------------
TCA9535 FUNCTIONS
-----------------------------------------------------------------------*/
void initializeTCA9535() {
    Wire.beginTransmission(TCA9535_ADDRESS);
    Wire.write(0x06); // Set all pins as inputs
    Wire.endTransmission();
}

uint16_t readButtonStates() {
    Wire.beginTransmission(TCA9535_ADDRESS);
    Wire.write(0x00); // Request input port 0
    Wire.endTransmission();
    Wire.requestFrom(TCA9535_ADDRESS, 2);
    uint16_t states = Wire.read() | (Wire.read() << 8);
    return states;
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

    char _event = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
    volatile MyKeyboardStateType _state = KEYBOARD_EVENT_CLEARED;
    volatile MyKeyboardActionType _action = KEYBOARD_ACTION_NONE;

    //void emit(_meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar key, char kbchar = 0) {
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
        LOG_DEBUG("A button was pressed!");
        uint16_t button_states = ~readButtonStates();
        if (!button_states) {
            LOG_DEBUG("no buttons.");
            return;
        }
        // Display button states in the Serial console
        String buttonStateText = "Button States: ";
        for (int i = 0; i < 16; i++) {  // Assuming 16 buttons
            buttonStateText += (button_states & (1 << i)) ? "1 " : "0 ";
            int b = button_states & (1 << i);
            if (b && _prevButtonState) {
                LOG_DEBUG("Index %d pressed", i);
                if (i == 0) {
                    emit(ANYKEY, INPUT_BROKER_MSG_BRIGHTNESS_UP);
                }
                if (i == 1) {
                    emit(ANYKEY, INPUT_BROKER_MSG_BLUETOOTH_TOGGLE);
                }
                if (i == 2) {
                    emit(ANYKEY, INPUT_BROKER_MSG_BRIGHTNESS_DOWN);
                }
            }
        }
        Serial.println(buttonStateText);  
        _prevButtonState = button_states;
    }
};

MyKeyboard *mkb = NULL;

void intPressHandler() {
    mkb->onIntPress();
}

void lateInitVariant()
{
    LOG_DEBUG("here we are!");
    mkb = new MyKeyboard("BSIDES-UTAH");
}