# ReefLight
I wrote a small python script that allows to control my aquarium lamp via mqtt.
The aquarium lamp is a cheap wifi enabled ESP8266 chip with the mighty
[ESPEasy](https://www.letscontrolit.com/wiki/index.php/ESPEasy) Firmware.
The Firmware can be used to generate multiple PWM signals using directly the pins of the ESP8266 
or by using the I2C PCA9685 board for example.

The settings of the script are stored in the [settings.json](reeflight_control_script/settings.json) json file.

It contains an array of the different **channel** objects if you want to use multiple channels
(for different light colors for example).
A channel object contains
1. **name** of the channel for displaying in the webinterface
2. **color** of the channel for displaying in the webinterface
3. **percentage** percentage of the channel at last update
4. **max_pwm_value** pwm_value corresponding to a duty cylcle of 100% (1023 by default)
5. **pwm_value** pwm_value of the channel at last update (corresponds to int(percentage)*max_pwm_value)
6. **manual** can bei either true or false. If true the **percentage** and **pwm_value** are not 
updated and can be changed manually in the webinterface. If false the channel is either in 
**moolight** or not in **moonlight** (regular) mode.
7. **moonlight**
if true the script calculates at each update the brightness of the moon at your location that can be 
configured in **latitude** and **longitude** in the settings.json file. It uses the fractional phase of the moon and the height
of the moon at the sky.
If false the channels is working as normal channel. At each update it takes the **data_points** array and interpolates the 
percentage using the percentage of the two data_points greater and smaller to the current time
new percentage and pwm_value is calculated as the linear interpolation between the 
8. **data_points**
Array storing the lightschedule in normal (no **manual** and no **moonlight** mode). 
It consists of time value pairs that can be changed using the ip-address:5000/schedule.html
