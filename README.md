# ReefLight
I wrote a small python script that allows to control my aquarium lamp via mqtt protocoll.
The aquarium lamp is a cheap wifi enabled ESP8266 chip with the mighty
[ESPEasy](https://www.letscontrolit.com/wiki/index.php/ESPEasy) Firmware.
The Firmware can be used to generate multiple PWM signals using directly the pins of the ESP8266 
or by using the I2C PCA9685 board for example.

The python script can be controlled via a webinterface where the daily schedule of the different channels/colors of the lamp can be configured via drag and drop (ipadress:5000/schedule.html). Also each channel can be configured manually (ipadress:5000/).

The settings of the script are stored in the [settings.json](settings.json) json file.

It contains an array of the different **channel** objects if you want to use multiple channels
(for different light colors for example).
A channel object contains
* **name** of the channel for displaying in the webinterface
* **color** of the channel for displaying in the webinterface
* **pwm** PWM Value of the channel at last update (range 0..1)
* **manual**  
**true**: The **pwm** value is not automatically updated and can be changed in the webinterface  
**false**: the channel is either in **moolight** or not in **moonlight** (regular) mode.
* **moonlight**  
**true**: the script calculates at each update the brightness of the moon at your location that can be 
configured in **latitude** and **longitude** in the settings.json file. It uses the fractional phase of the moon and the height
of the moon at the sky.  
**false**: the channels is working as regular channel. At each update it takes the **data_points** array and interpolates the 
pwm value using the pwm values of the two data_points greater and smaller to the current time
* **data_points**
Array storing the lightschedule in normal mode (not **manual** and no **moonlight** mode). 
It consists of time value pairs that can be changed using the ip-address:5000/schedule.html

The json file also contains general settings
* **mqtt_broker**, **mqtt_client_name**, **mqtt_port**, **mqtt_qos**, **mqtt_topic** MQTT settings
* **seconds_between_updates** seconds between two successive updates
* **longitude**, **langitude** geographic location for the moonlight simulation

The script can be executed either
* directly via python with the required packages in [requirements.txt](requirements.txt)
or 
* via Docker  
Build the Docker image with
`docker build -t reeflight .` inside the git-repo and start a container with 
`docker run -dit --restart always  --net=host -v /etc/localtime:/etc/localtime reeflight`
