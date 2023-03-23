#pragma once

extern void IntegerToString(uint32_t integer,char* stringPtr);
extern void FloatToString(float floatPt,char* stringPtr,uint8_t decimalPlaces);
extern void StringToInteger(char* stringPtr,uint32_t* integerPtr);
extern void StringToFloat(char* stringPtr,float* floatPtr);
extern void SetToZeroIfNaN(float* floatPtr);
