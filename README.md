# Energy-monitoring-system

![20230208_085155](https://user-images.githubusercontent.com/46250887/217480209-509b88e5-f881-40e4-9713-366d0af42f70.jpg)
![20230208_085224](https://user-images.githubusercontent.com/46250887/217482023-5dc77ed8-69c9-468b-a4b2-566806af0607.jpg)  
![20230208_085129](https://user-images.githubusercontent.com/46250887/217481476-54ddda4b-e33c-49e0-9621-62fcb409383a.jpg)  

Client: Mary  
Monitors electrical parameters (voltage,current,power, and energy), logs the data (to an SD card),  
and uploads to the cloud (Google Sheets and ThingSpeak).     

## Credits  
1. https://randomnerdtutorials.com/esp32-esp8266-publish-sensor-readings-to-google-sheets/  
2. https://github.com/stechiez/iot_projects/blob/master/GoogleSpreadSheet_ESP32_IFTTT/ESP32_GoogleSpreadSheetIFTTT/ESP32_GoogleSpreadSheetIFTTT.ino  

## Precaution(s)  
1. ``preferences.begin("T-Mon",false);``. I observed that a short string as the first argument    
of ``begin`` method helps in avoiding some issues with accessing the flash memory via the   
``Preferences`` library. ``T-Mon`` was used as a short form for ``Transformer-monitor``.  
The initial application of this project was transformer monitoring but the requirements changed  
and the system was designed to monitor electrical parameters in a residential building. ``T-Mon``  
was retained but the current application of the system is ``domestic energy monitoring``.  
 
