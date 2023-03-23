#include <Arduino.h>
#include "numeric_lib.h"

/**
 * @brief Converts an integer to a string.
*/
void IntegerToString(uint32_t integer,char* stringPtr)
{
  if(integer == 0)
  {  
    stringPtr[0] = '0';
    return;
  }
  uint32_t integerCopy = integer;
  uint8_t numOfDigits = 0;

  while(integerCopy > 0)
  {
    integerCopy /= 10;
    numOfDigits++;
  }
  while(integer > 0)
  {
    stringPtr[numOfDigits - 1] = '0' + (integer % 10);
    integer /= 10;
    numOfDigits--;
  }
}

/**
 * @brief Converts a float to a string.
*/
void FloatToString(float floatPt,char* stringPtr,uint8_t decimalPlaces)
{
  uint32_t multiplier = 1;
  for(uint8_t i = 0; i < decimalPlaces; i++)
  {
    multiplier *= 10;
  }  
  uint32_t floatAsInt = lround(floatPt * multiplier);
  char quotientBuff[20] = {0};
  char remainderBuff[20] = {0};
  
  IntegerToString((floatAsInt / multiplier),quotientBuff);
  IntegerToString((floatAsInt % multiplier),remainderBuff);
  strcat(stringPtr,quotientBuff);
  strcat(stringPtr,".");
  uint8_t remainderLen = strlen(remainderBuff);
  while(remainderLen < decimalPlaces)
  {
    strcat(stringPtr,"0");
    remainderLen++;
  }  
  strcat(stringPtr,remainderBuff);
}

/**
 * @brief Converts a string to an integer.
 * e.g.
 * "932" to 932:
 * '9' - '0' = 9
 * '3' - '0' = 3
 * '2' - '0' = 2
 * 9 x 10^2 + 3 x 10^1 + 2 x 10^0 = 932.
*/
void StringToInteger(char* stringPtr,uint32_t* integerPtr)
{
  *integerPtr = 0;
  uint8_t len = strlen(stringPtr);
  uint32_t j = 1;
  for(uint8_t i = 0; i < len; i++)
  {
    *integerPtr += ((stringPtr[len - i - 1] - '0') * j);
    j *= 10;
  }
}

/**
 * @brief Converts a string to a float
*/
void StringToFloat(char* stringPtr,float* floatPtr)
{
  uint8_t decimalPointIndex = 255; //Invalid index
  uint8_t len = strlen(stringPtr);
  
  for(uint8_t i = 0; i < len; i++)
  {
    if(stringPtr[i] == '.')
    {
      decimalPointIndex = i;
      break;
    }
  }
  if(decimalPointIndex == 255)
  {
    uint32_t integer = 0;
    StringToInteger(stringPtr,&integer);
    *floatPtr = (float)integer;
  }
  else
  {
    char quotientBuff[20] = {0};
    char remainderBuff[20] = {0};
    uint32_t quotient = 0;
    uint32_t remainder = 0;
    uint32_t multiplier = 1;
    uint8_t multiplierLen = len - 1 - decimalPointIndex;

    for(uint8_t i = 0; i < multiplierLen; i++)
    {
      multiplier *= 10;
    }
    for(uint8_t i = 0; i < decimalPointIndex; i++)
    {
      quotientBuff[i] = stringPtr[i];
    }
    for(uint8_t i = decimalPointIndex + 1; i < len; i++)
    {
      remainderBuff[i - decimalPointIndex - 1] = stringPtr[i];
    }

    StringToInteger(quotientBuff,&quotient);
    StringToInteger(remainderBuff,&remainder);
    uint32_t floatAsInt = ((quotient * multiplier) + remainder);
    *floatPtr = (float)floatAsInt / multiplier;
  }
}


/**
 * @brief Set float to zero if it is not a number.
*/
void SetToZeroIfNaN(float* floatPtr)
{
  if(isnan(*floatPtr))
  {
    *floatPtr = 0.0;
  }
}
