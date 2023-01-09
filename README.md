# Energy-monitoring-system
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

 
