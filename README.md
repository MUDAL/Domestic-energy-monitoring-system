# Energy-monitoring-system

## Description  
This repository contains the code, schematics, videos, and documentation for a domestic energy monitoring system.   
The system is designed to measure and display current, voltage, power, and energy consumption at different   
points in a building, allowing users to monitor their energy usage in real-time.   

## Features   
1. Measurement of current, voltage, power, and energy consumed in a building.   
2. Periodic display of measured data on an LCD.   
3. Periodic logging of measured data to an SD card.   
4. Periodic uploading of data to the cloud (ThingSpeak Server and Google Sheets).  
5. Wi-Fi provisioning.   

## Hardware  
1. ESP32 development board  
2. PZEM-004T power module  
3. 16x2 LCD   
4. DS3231 RTC module  
5. SD card module   
6. Two Li-ion batteries  
7. 3-pin plug   

## Software  
The system was developed using the ESP-IDF FreeRTOS software platform. The ESP32 was programmed using C++.    
The code provides real-time measurement and display of current, voltage, power, and energy consumption,  
as well as data logging to an SD card and data upload to the ThingSpeak server and Google Sheets.  

## Software architecture  
![ss_sl drawio](https://user-images.githubusercontent.com/46250887/224770270-1bf60a7b-530a-4b28-9697-761c83392917.png)  

## Images of the prototype   
![20230214_095808](https://user-images.githubusercontent.com/46250887/218693188-4467e4f6-f67c-401f-bfd4-25fa2a50df3e.jpg)
![20230214_095804](https://user-images.githubusercontent.com/46250887/218693269-3f58c477-486f-4b24-bbf2-b4b3f9e1f41c.jpg)
![20230214_095738](https://user-images.githubusercontent.com/46250887/218693367-19334fc0-fde2-4a42-8139-cdb86ab65094.jpg)
![20230208_085155](https://user-images.githubusercontent.com/46250887/217480209-509b88e5-f881-40e4-9713-366d0af42f70.jpg)
![20230208_085224](https://user-images.githubusercontent.com/46250887/217482023-5dc77ed8-69c9-468b-a4b2-566806af0607.jpg)  
![20230208_085129](https://user-images.githubusercontent.com/46250887/217481476-54ddda4b-e33c-49e0-9621-62fcb409383a.jpg)       

## Credits  
1. https://randomnerdtutorials.com/esp32-esp8266-publish-sensor-readings-to-google-sheets/  
2. https://github.com/stechiez/iot_projects/blob/master/GoogleSpreadSheet_ESP32_IFTTT/ESP32_GoogleSpreadSheetIFTTT/ESP32_GoogleSpreadSheetIFTTT.ino  

## Notes(s)  
1. ``preferences.begin("T-Mon",false);``. I observed that a short string as the first argument    
of ``begin`` method helps in avoiding some issues with accessing the flash memory via the   
``Preferences`` library. ``T-Mon`` was used as a short form for ``Transformer-monitor``.  
The initial application of this project was transformer monitoring but the requirements changed  
and the system was designed to monitor electrical parameters in a residential building. ``T-Mon``  
was retained but the current application of the system is ``domestic energy monitoring``.  
 
