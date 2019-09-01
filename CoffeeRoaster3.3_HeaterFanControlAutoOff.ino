
// *******************************************
// Libraries
// *******************************************
#include <TimerOne.h>
#include <LiquidCrystal.h>
#include <SPI.h>
#include <string.h> //For Ocean Controls
#include <ctype.h> //For Ocean Controls
#include <EEPROM.h> //For Ocean Controls

// *******************************************
// Thermocouple Shield
// *******************************************
#define PINEN 7 //Mux Enable pin
#define PINA0 4 //Mux Address 0 pin
#define PINA1 5 //Mux Address 1 pin
#define PINA2 6 //Mux Address 2 pin
#define PINSO 12 //TCAmp Slave Out pin (MISO)
#define PINSC 13 //TCAmp Serial Clock (SCK)
#define PINCS 9  //TCAmp Chip Select Change this to match the position of the Chip Select Link

int Temp[8], SensorFail[8];
float floatTemp, floatInternalTemp;
char failMode[8];
int internalTemp, intTempFrac;

// *******************************************
// AC Phase Control
// *******************************************
volatile int i1 = 0;                          // Variable used as a counter for heater 1
volatile int i2 = 0;                          // Variable used as a counter for heater 2
volatile boolean zero_cross1 = 0;             // Boolean that switches when input waveform crosses zero
volatile boolean zero_cross2 = 0;

// Definition of physical pin for each heater
int AC_fan = 10; // AC Fan is connected to physical pin 10
int AC_heater = 3; // Heater is connected to physical pin 3
int dimFan = 0; // Initial dimming level of fan (0-64); 0 = on, 64 = off. // This means that we have 64 steps of dimming
int dimHeater = 0; // Initial dimming level of heater

// *******************************************
// Buttons/Controls
// *******************************************
const int buttonStartPin = 15;
const int StopPin = 14;
int buttonStateStart = 0;
//int buttonStateStop = 0;
int rate = 0;
float time;
float prevTime = 0;
float timeold;
float cycletime = 0;
float increment = 0;
float setTemp = 200;
int actualTemp;
int beanTemp;
int roastendTemp;
bool preheat = false;
bool profileRunning = false;
float runSeconds = 0;
unsigned long runMinutes = 0;
unsigned long runSecondsDisplay = 0;
float timeout = 0;

// *******************************************
// LCD Pins
// *******************************************
//(Uses Analog Pins 2-5 as Digital Pins)
LiquidCrystal lcd(11,8,16,17,18,19);

void setup()
{
  //initialized LCD
  lcd.begin(20, 4);

  // *******************************************
  // Ocean Control Thermocouple Shield
  // *******************************************
  

  pinMode(PINEN, OUTPUT);
  pinMode(PINA0, OUTPUT);
  pinMode(PINA1, OUTPUT);
  pinMode(PINA2, OUTPUT);
  pinMode(PINSO, INPUT);
  pinMode(PINCS, OUTPUT);
  pinMode(PINSC, OUTPUT);


  digitalWrite(PINEN, HIGH);// enable on
  digitalWrite(PINA0, LOW); // low, low, low = channel 1
  digitalWrite(PINA1, LOW);
  digitalWrite(PINA2, LOW);
  digitalWrite(PINSC, LOW); //put clock in low

  // *******************************************
  // AC Phase
  // *******************************************
  pinMode(AC_fan, OUTPUT); // Define pins as outputs to drive triacs
  pinMode(AC_heater, OUTPUT);
  Timer1.initialize(260);
  attachInterrupt(digitalPinToInterrupt(2), zero_cross_int, RISING); // Define pin 2 as the input signal of the zero-cross signal and call the function at the zero-cross
  Timer1.attachInterrupt(counter); // Define timer #3 to start the function “counter” every 131 us = 1/64th of the half sine-wave period 8.33ms


  // *******************************************
  // Buttons/Controls
  // *******************************************
  Serial.begin(9600);
  Serial.println("Reset");
  pinMode(buttonStartPin, INPUT);
  pinMode(StopPin, OUTPUT);

  digitalWrite(StopPin, HIGH);
}

void loop()
{
  // *******************************************
  // Roast Timer
  // *******************************************
  //if (millis() > (time + 1000))
    time = millis();
    cycletime = (time - prevTime);
    increment = cycletime * rate;
    prevTime = time;
    setTemp = increment / 60000 + setTemp; 
    
    // Read the latest temps from the thermocouples.
    ReadSensors();

    // Update Roast Profile time variables.
    if (profileRunning == true)
    {
      runSeconds = runSeconds + cycletime/1000;

    }
    else
    {
      runSeconds = 0;
    }
    runMinutes = runSeconds / 60;
    runSecondsDisplay = runSeconds - (runMinutes * 60);
    



  // *******************************************
  // Handle Start/Stop Buttons
  // *******************************************
  buttonStateStart = digitalRead(buttonStartPin);
  //buttonStateStop = digitalRead(buttonStopPin);

  if (buttonStateStart == HIGH)
  {
    // First press = preheat, 2nd press starts profile.
    if (profileRunning == false)
    {
       if (preheat == false)
       {
         preheat = true;
         digitalWrite(StopPin, HIGH);
       }
       else
       {
         profileRunning = true;
         digitalWrite(StopPin, HIGH);
       }
    }
    rate = 0;
    setTemp = 200;
  }

  // *******************************************
  // Manage Roast Profile
  // *******************************************
  
  roastendTemp = 415;
  
  switch (runMinutes)
  {
    case 0:
      rate = 0;
      dimFan = 0;
      break;
    case 1:
      rate = 40;
      dimFan = 0;
      break;
    case 5:
      rate = 25;
      dimFan = 10;
      break;
    case 8:
      rate = 15;
      dimFan = 15;
      break;
    case 12:
      rate = 0;
      dimFan = 20;
      //profileRunning = false;
      // Drive the complete pin high.  This can be used to trigger a web response (i.e. tweet, text, etc.)
      //digitalWrite(completePin, HIGH);
      break;
  }
  
  if (beanTemp > roastendTemp)
  {
    profileRunning = false;
    preheat = false;
    dimFan = 0;
    digitalWrite(StopPin, LOW);
  }

  if (beanTemp < 100 && preheat == false)
  {
    timeout = timeout + 1;
    if (timeout > 50)
    {
    dimFan = 128;
    digitalWrite(StopPin, LOW);
    }
  }
  
  // *******************************************
  // Manage the temperature ramp rate
  // *******************************************
 


  // *******************************************
  // Drive the heating elements
  // *******************************************
  // Convert the thermocouple feedback to F.
  actualTemp = Temp[1] * 0.45 + 32;
  beanTemp = Temp[0] * 0.45 + 32;
  
  
  // Drive the heater with a hysteresis band of 2 degrees F.
  DriveHeater((preheat || profileRunning), setTemp, actualTemp, 2);

  // Send Data to Logger
  SendDataToLogger();

  // Update LCD Display
  UpdateDisplay();
  
}





/****************************************************************************
 * Function: Zero Cross Detection
 * Args: None
 * Description:
 *  Function triggered by zero crossing
 ****************************************************************************/
void zero_cross_int() {
  zero_cross1 = true; // Set the Boolean to true to tell the dimming function that a delayed zero-crossing has actually occurred
  zero_cross2 = true;
  i1 = 0; // Initialize variable for dimming sequence ( heater 1)
  i2 = 0; // Initialize variable for dimming sequence ( heater 2)
  digitalWrite(AC_fan, LOW); // Turn heater 1 OFF, or leave it off if already off
  digitalWrite(AC_heater, LOW); // Turn heater 2 OFF, or leave it off if already off 
}





/****************************************************************************
 * Function: Dimmer Loop
 * Args: None
 * Description:
 *  This function varies the heater and fan power input via triac delay
 ****************************************************************************/
void counter() { // Start of the loop of the actual dimming. Each step is 1/64th of a half-cycle
  if (zero_cross1 == true | zero_cross2 == true) { // Check that the delayed zero-cross has occurred
    if (i1 >= dimFan && zero_cross1 == true) { // Check if heater 1 set-point has been reached
      digitalWrite(AC_fan, HIGH); // Turn heater 1 ON
      i1 = 0; // Reset time step counter of heater 1
      zero_cross1 = false; // Reset zero-cross Boolean
    }
    else { // If set-point has not been reached, continue
      i1++;
    }

    if (i2 >= dimHeater && zero_cross2 == true) { // For heater 2…
      digitalWrite(AC_heater, HIGH);
      i2 = 0;
      zero_cross2 = false;
    }
    else {
      i2++;
    }
  }
} 





/****************************************************************************
 * Function: UpdateDisplay
 * Args:
 *  setTemp - The temperature we are trying to achieve.
 *  actualTemp - Current temperature
 *  hysteresis - band applied above/below the setTemp.
 * Description:
 *  This function drives the heating element using basic hysteresis control
 ****************************************************************************/
 void UpdateDisplay()
 {

  // Set Temp
  lcd.setCursor(0, 0);
  lcd.print("SET");
  lcd.setCursor(4, 0);
  lcd.print(setTemp);
  delay(10);
  
    // Estimated Temp?
  lcd.setCursor(0, 1);
  lcd.print("ET");
  lcd.setCursor(4, 1);
  lcd.print(Temp[1] * 0.45 + 32);
  delay(10);

  // Bean Temp
  lcd.setCursor(0, 2);
  lcd.print("BT");
  lcd.setCursor(4, 2);
  lcd.print(Temp[0] * 0.45 + 32);
  delay(10);
  
   // Ambient temperature
  lcd.setCursor(0, 3);
  lcd.print("AT");
  lcd.setCursor(4, 3);
  lcd.print(floatInternalTemp * 1.8 + 32);
  delay(10);
  
  // Status
  lcd.setCursor(11, 0);
  lcd.print("STAT");
  lcd.setCursor(16, 0);
  if (profileRunning)
  {
    lcd.print("Run");
  }
  else if (preheat)
  {
    lcd.print("Pre");
  }
  else
  {
     lcd.print("Off");
  }
  delay(10); 
  
  // Time
  lcd.setCursor(11, 1);
  lcd.print("TIM");
  lcd.setCursor(16, 1);
  lcd.print(runMinutes);
  lcd.setCursor(17, 1);
  lcd.print(":");
  lcd.setCursor(18, 1);
  lcd.print(runSecondsDisplay);
  delay(10); 
  
  // Ramp Rate
  lcd.setCursor(11, 2);
  lcd.print("RATE");
  lcd.setCursor(16, 2);
  lcd.print(rate);
  delay(10);  

  // Fan Rate
  lcd.setCursor(11, 3);
  lcd.print("FAN");
  lcd.setCursor(16, 3);
  lcd.print(dimFan);
  delay(10);  
 }





/****************************************************************************
 * Function: DriveHeater
 * Args:
 *  setTemp - The temperature we are trying to achieve.
 *  actualTemp - Current temperature
 *  hysteresis - band applied above/below the setTemp.
 * Description:
 *  This function drives the heating element using basic hysteresis control
 ****************************************************************************/
void DriveHeater(bool Enabled, int SetTemp, int ActualTemp, int Hysteresis)
{
  const int HEATING = 0;
  const int COOLING = 1;
  static int tempMode = HEATING;

  if (Enabled)
  {
    switch (tempMode)
    {
      case HEATING:
        if (ActualTemp < (SetTemp + Hysteresis*5))
        {
          dimHeater = 0;
        }
        else
        {
          tempMode = COOLING;
        }
        break;
      case COOLING:
        if (ActualTemp > (SetTemp))
        {
          dimHeater = 64;
        }
        else
        {
          tempMode = HEATING;
        }
        break;
      default:
        tempMode = COOLING;
        dimHeater = 64;
        break;
    }
  }
  else
  {
    tempMode = COOLING;
    dimHeater = 64;
  }
}





/****************************************************************************
 * Function: ReadSensors
 * Args:
 *  None
 * Description:
 *  This was taken from sample code from the Mux thermocouple shield vendor.
 *  It reads however many sensors we are configured to use.
 ****************************************************************************/
void ReadSensors()
{
  static unsigned int Mask;
  static char i, j = 0;
  static char NumSensors = 2;

  if (j < (NumSensors - 1))
  {
    j++;
  }
  else
  {
    j = 0;
  }

  switch (j) //select channel
  {
    case 0:
      digitalWrite(PINA0, LOW);
      digitalWrite(PINA1, LOW);
      digitalWrite(PINA2, LOW);
      break;
    case 1:
      digitalWrite(PINA0, HIGH);
      digitalWrite(PINA1, LOW);
      digitalWrite(PINA2, LOW);
      break;
    case 2:
      digitalWrite(PINA0, LOW);
      digitalWrite(PINA1, HIGH);
      digitalWrite(PINA2, LOW);
      break;
    case 3:
      digitalWrite(PINA0, HIGH);
      digitalWrite(PINA1, HIGH);
      digitalWrite(PINA2, LOW);
      break;
    case 4:
      digitalWrite(PINA0, LOW);
      digitalWrite(PINA1, LOW);
      digitalWrite(PINA2, HIGH);
      break;
    case 5:
      digitalWrite(PINA0, HIGH);
      digitalWrite(PINA1, LOW);
      digitalWrite(PINA2, HIGH);
      break;
    case 6:
      digitalWrite(PINA0, LOW);
      digitalWrite(PINA1, HIGH);
      digitalWrite(PINA2, HIGH);
      break;
    case 7:
      digitalWrite(PINA0, HIGH);
      digitalWrite(PINA1, HIGH);
      digitalWrite(PINA2, HIGH);
      break;
  }

  delay(5);
  digitalWrite(PINCS, LOW); //stop conversion
  delay(5);
  digitalWrite(PINCS, HIGH); //begin conversion
  delay(100);  //wait 100 ms for conversion to complete
  digitalWrite(PINCS, LOW); //stop conversion, start serial interface
  delay(1);

  Temp[j] = 0;
  failMode[j] = 0;
  SensorFail[j] = 0;
  internalTemp = 0;
  for (i = 31; i >= 0; i--)
  {
    digitalWrite(PINSC, HIGH);
    delay(1);

    if ((i <= 31) && (i >= 18)) //thermocouple temperature data, 31 = sign, 30 = MSB (2^10), 18 = LSB (2^-2)
    {
      Mask = 1 << (i - 18);
      if (digitalRead(PINSO) == 1)
      {
        if (i == 31)
        {
          Temp[j] += (0b11 << 14); //pad the temp with the bit 31 value so we can read negative values correctly
        }
        Temp[j] += Mask;
      }
      else
      {
      }
    }

    //bit 17 is reserved

    if (i == 16) //bit 16 is sensor fault
    {
      SensorFail[j] = digitalRead(PINSO);
    }

    if ((i <= 15) && (i >= 4)) //internal temperature, 15 = sign, 14 = MSB (2^6), 4 = LSB (2^-4)
    {
      Mask = 1 << (i - 4);
      if (digitalRead(PINSO) == 1)
      {
        if (i == 15)
        {
          internalTemp += (0b1111 << 12); //pad the temp with the bit 31 value so we can read negative values correctly
        }
        internalTemp += Mask;//should probably pad the temp with the bit 15 value so we can read negative values correctly
      }
      else
      {
      }

    }
    //bit 3 is reserved
    //bit 2 is shorted to VCC
    //bit 1 is shorted to GND
    //bit 0 is open circuit

    digitalWrite(PINSC, LOW);
    delay(1);
  }

  if (SensorFail[j] == 1)
  {
    //code to say how sensor fails
  }
  else
  {
    floatTemp = (float)Temp[j] * 0.25;
  }

  floatInternalTemp = (float)internalTemp * 0.0625;
}





/****************************************************************************
 * Send data to computer once every second.  Data such as temperatures,
 * PID setting etc.
 * This allows current settings to be checked by the controlling program
 * and changed if, and only if, necessary.
 * This is quicker that resending data from the controller each second
 * and the Arduino having to read and interpret the results.
 ****************************************************************************/
void SendDataToLogger()
{
  //send data to logger

  float tt1;
  float tt2;

#ifdef CELSIUS
  tt1 = Temp[0] * 0.25;
  tt2 = Temp[1] * 0.25;
#else
  tt1 = (Temp[0] * 0.45) + 32;
  tt2 = (Temp[1] * 0.45) + 32;
#endif

  Serial.print("t1=");
  //delay(50);
  Serial.println(tt1, 1);
  //delay(50);
  Serial.print("t2=");
  //delay(50);
  Serial.println(tt2, 1);
  //delay(50);
}
