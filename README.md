# Energy-monitoring-system

## Description  
This repository contains the code, schematics, videos, and documentation for a domestic energy monitoring system.   
The system is designed to measure and display current, voltage, power, and energy consumption at different   
points in a building, allowing users to monitor their energy usage in real-time.   

## Features   
1. Measurement of current, voltage, power, and energy consumed in a building.
2. Periodic display of measured data on an LCD.
3. Periodic logging of measured data on an SD card.
4. Periodic data upload to the cloud (ThingSpeak Server and Google Sheets).
5. Wi-Fi provisioning: To configure the SSID and password of the Wi-Fi network required by the system via a captive portal.
The captive portal also configures voltage, current, power, and energy limits.
6. The device sends notifications to the user (via an MQTT mobile application) whenever the measured parameters exceed their limits.    

## Hardware  
1. ESP32 development board  
2. PZEM-004T power module  
3. 16x2 LCD   
4. DS3231 RTC module  
5. SD card module   
6. 2GB SD card  
7. Two 18650 Li-ion batteries (deprecated)   
8. 18650 battery holder (deprecated)  
9. AC power socket
10. 3-pin AC power plug   
11. Jumper wires  
12. LM7805 voltage regulator  
13. LM2596 buck converter module  
14. Logic level shifter  
15. SPDT switch
16. 9V AC adapter (replacement for the batteries)   

## Software  
The system was developed using the ESP-IDF FreeRTOS software platform. The ESP32 was programmed using C++.    
The code provides real-time measurement and display of current, voltage, power, and energy consumption,  
as well as data logging to an SD card and data upload to the ThingSpeak server and Google Sheets.  

The software tools used in this project include:
1. Arduino IDE  
2. ESP-IDF FreeRTOS: To execute different tasks concurrently  
3. ThingSpeak: To visualize data  
4. IFTTT: To upload data to Google Sheets   
5. MQTT Alert: A mobile app to notify the user when energy limits have been exceeded.    
The smart energy monitor sends notifications to an MQTT broker (HiveMQ was used).    
The mobile app receives these notifications from the broker and alerts the user.  

Link to download the ``MQTT Alert`` application: https://play.google.com/store/apps/details?id=gigiosoft.MQTTAlert  

## Software architecture  
![ss_sl drawio](https://user-images.githubusercontent.com/46250887/224770270-1bf60a7b-530a-4b28-9697-761c83392917.png)  

## Images of the prototype   
![20230623_193620](https://github.com/MUDAL/Domestic-energy-monitoring-system/assets/46250887/d942988b-29da-4e9d-bdbf-2139b8d0b342)
![Screenshot (380)](https://github.com/MUDAL/Domestic-energy-monitoring-system/assets/46250887/c881b5d6-91a8-4877-9cd6-7a5c536c39aa)
![Screenshot (382)](https://github.com/MUDAL/Domestic-energy-monitoring-system/assets/46250887/8b9c3171-f767-42b2-917e-9f816123e2fd)  
![Screenshot (378)](https://github.com/MUDAL/Domestic-energy-monitoring-system/assets/46250887/e676d6f2-1338-433c-b834-7ae8c36190cf)  
![yy](https://github.com/MUDAL/Domestic-energy-monitoring-system/assets/46250887/6204bc34-9261-4423-b606-3d0598d70f37)  
![20230314_154550](https://user-images.githubusercontent.com/46250887/225056313-a61fb779-47ac-4ccb-96ba-5e7eebf49878.jpg)    
![20230214_095808](https://user-images.githubusercontent.com/46250887/218693188-4467e4f6-f67c-401f-bfd4-25fa2a50df3e.jpg)
![20230214_095804](https://user-images.githubusercontent.com/46250887/218693269-3f58c477-486f-4b24-bbf2-b4b3f9e1f41c.jpg)
![20230214_095738](https://user-images.githubusercontent.com/46250887/218693367-19334fc0-fde2-4a42-8139-cdb86ab65094.jpg)     
![20230214_095745](https://user-images.githubusercontent.com/46250887/227372889-d035e268-7342-4ddf-9302-02d17e1a97ad.jpg)   

## Credits  
1. https://randomnerdtutorials.com/esp32-esp8266-publish-sensor-readings-to-google-sheets/  
2. https://github.com/stechiez/iot_projects/blob/master/GoogleSpreadSheet_ESP32_IFTTT/ESP32_GoogleSpreadSheetIFTTT/ESP32_GoogleSpreadSheetIFTTT.ino  
3. PZEM-004T sample code and connections: https://www.nn-digital.com/en/blog/2019/11/04/example-of-the-pzem-004t-v3-v3-0-interfacing-program-using-arduino/  

## Note(s)  
1. ``preferences.begin("T-Mon",false);``. I observed that a short string as the first argument    
of ``begin`` method helps in avoiding some issues with accessing the flash memory via the   
``Preferences`` library. ``T-Mon`` was used as a short form for ``Transformer-monitor``.  
The initial application of this project was transformer monitoring but the requirements changed  
and the system was designed to monitor electrical parameters in a residential building. ``T-Mon``  
was retained but the current application of the system is ``domestic energy monitoring``.  
 
## Recommendation(s)  
1. Add a feature to prevent users from removing the SD card each time they need to view their logged data.  
One way this could be achieved is the use of ``FTP`` for file transfer from the system to a connected device  
(e.g. a PC) which will run an FTP server software.  
Another option is to set the system up as a local server that will enable other devices to download the file  
containing the logged data through a web browser.  

## Improvements made  
1. Display of ``0`` instead of ``NAN`` whenever the system isn't plugged.  
2. Replacement of code utilizing dynamic memory allocation with code utilizing static memory allocation.  
3. Replacement of batteries with an AC adapter for power supply (final modification).  
