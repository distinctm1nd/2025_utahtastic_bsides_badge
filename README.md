
# 2025 Utahtastic BSides Badge

![Utah BSides Badge front](hardware/utah_bsides_front.png)

![Utah BSides Badge front](hardware/utah_bsides_back.png)

Key Features:

    - ESP32-S3-WROOM Microcontroller
    - LoRa 
    - 25 Addressable LEDs
    - TFT Display
    - Power Options: Powered via Micro USB or 3 AA batteries (regulated at 3.3V).
    - Hackable and Programmable: Easily program with just a laptop and a Micro USB cable.
    - Expansion Ready: 3 minibadge slots for future add-ons.
    - Custom Meshtastic Firmware: Open-source firmware designed for mesh networking.
    - Convenient Controls – UART connection point, Reset and Flash buttons, and a power switch for toggling between USB and battery modes.
    - 4 GPIO pins available for custom dev work​

## Make sure you have the following items:

- Badge
- 6 Minibadge headers
- Battery holder
- Moon LED diffuser
- 3 AA Batteries
- Lanyard
- Minibadge kit
- Double sided foam tape

## Badge Assembly

The badge does not require further assembly to be functional by powering through the USB with an external battery. Make sure that the switch is at USB if powering with USB. 

To complete the assembly, first solder the minibadge headers to the badge. Be careful while soldering the header that is near the display. 

After this, solder the battery holder and apply foam tape to hold the end without solder connectors in place. 

Use hot glue to attach the moon diffuser to moon on the back of the badge. 

Attach the lanyard to the "post" by the raven. 

## Minibadge Assembly

![Utah BSides Badge front](hardware/sl_minibadge_front.png)

![Utah BSides Badge front](hardware/sl_minibadge_back.png)


- Solder the 68 ohm (1206) resistors to R1 and R2. These can be oriented any direction. 
- Solder the LEDs (yellow) to D1, D2, D3 and D4. Orientation on these matters. The green mark is on the cathode side of the LED so it needs to be oriented toward the cathode side on the PCB. The cathode side on the PCB is marked by a box around it. 

- Solder the headers to the four corners. 

## Keyboard Functionality

5-Way Switch
- Up: send message
- Down: erase/backspace
- Left: move cursor left
- Right: move cursor right
- Press: switch screens

## STL Files for joystick button cap and moon diffuser

[Joystick Button Cap](https://www.printables.com/model/1166931-joystick-cap-for-b-sides-utah-2025-badge)

[Moon Diffuser](https://www.printables.com/model/1166783-moon-led-diffuser-for-b-sides-utah-2025-badge)

## A few notes on usage

Don't transmit without the antenna attached as you could damage the badge. 

If you download the meshtastic app you can connect to your badge via bluetooth and send /receive messages through the app. You can also change many setting through the app. 

Make sure that you are on the Medium Fast channel for the conference. 

To change the moon LED color or brightness, in the app, go to:
- Radio Configuration -> Ambient Lighting

Once in the ambient lighting config you can change the brightness of the moon LEDs by changing "Current" to a different number. 255 is max brightness and anything less than that is dimmer. You can change the color codes for Red, Green and Blue to generate different colors. 

If you can't send or receive messages make sure that the LoRa channel is not on long fast. For the conference we'll be using medium fast. 

If the badge doesn't start up when turned on then push the reset button. 
