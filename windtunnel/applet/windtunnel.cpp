#include <MsTimer2.h>
#include <FiniteStateMachine.h>
#include <Button.h>
#include <MemoryFree.h>

#define encoder0PinA  2
#define encoder0PinB  4
#define turbineTacho 3
#define motorControl 6
#define motorOn 7

//analog ins
#define TURBINE_VOLTAGE 2


#define GO_SWITCH_LED 10
#define GO_SWITCH 11
#define STOP_SWITCH_LED 12
#define STOP_SWITCH 9
#define SAFETY_SWITCH 8

#define STARTSPEED 80

//LED states
#define ON 1
#define OFF 2
#define FLASH 3

#include "WProgram.h"
void setup();
void loop();
void nullFunc();
void stopMotor();
void safe();
void unSafe();
void running();
void switchOn();
void switchOff();
void ledControl( int led, int state );
void doLed( int ledState, int ledPin );
void flash();
void doEncoder();
void doTacho();
void addToHistory( int hist[], int newValue );
void wipeHistory( int hist[] );
int movingAverage( int hist[] );
int greenLED = OFF;
int redLED = OFF;

//states: enter, update, exit
State Off = State( switchOff, nullFunc, nullFunc );
State On = State( switchOn, running, nullFunc );
State Unsafe = State( unSafe, nullFunc, nullFunc );
State Safe = State( safe, nullFunc, nullFunc );

FSM stateMachine = FSM( Unsafe );

Button onButton = Button( GO_SWITCH, PULLUP );
Button offButton = Button( STOP_SWITCH, PULLUP );
Button safetySwitch = Button( SAFETY_SWITCH, PULLUP );


//globals
int serialSend = 0;
int turbineSpeed = 0;
int turbineVoltage = 0;
const int N = 5;
volatile unsigned int encoder0Pos = 0;
volatile boolean movedEncoder = false;

boolean safeState = false;
boolean fanState = false;
boolean flashState = false;
int turbineVoltageHistory[N];
//int tachoPeriodHistory[N];


volatile byte tachoCount;
float rpm;
unsigned long timeold;

void setup() { 

  //encoder
  pinMode(encoder0PinA, INPUT); 
  digitalWrite(encoder0PinA, HIGH);       // turn on pullup resistor
  pinMode(encoder0PinB, INPUT); 
  digitalWrite(encoder0PinB, HIGH);       // turn on pullup resistor

  //plc control
  pinMode( motorOn, OUTPUT );
  digitalWrite( motorOn, LOW );

  //switches
  pinMode( GO_SWITCH_LED, OUTPUT );
  digitalWrite( GO_SWITCH_LED, HIGH );
  pinMode( STOP_SWITCH_LED, OUTPUT );
  digitalWrite( STOP_SWITCH_LED, HIGH );

  pinMode( GO_SWITCH, INPUT );
  digitalWrite( GO_SWITCH, HIGH );
  pinMode( STOP_SWITCH, INPUT );
  digitalWrite( STOP_SWITCH, HIGH );

  //safety switch is a reed swith normally open. Closed in the safe state
  pinMode( SAFETY_SWITCH, INPUT );
  digitalWrite( SAFETY_SWITCH, HIGH );

  ///tacho
  pinMode( turbineTacho, INPUT );
  digitalWrite( turbineTacho, HIGH );

  MsTimer2::set(500, flash); // 500ms period
  MsTimer2::start();

  attachInterrupt(0, doEncoder, CHANGE);  // encoder pin on interrupt 0 - pin 2
  attachInterrupt(1, doTacho, RISING);  // tacho pin on interrupt 1 - pin 3

  Serial.begin (115200);
  Serial.println("state:start");

} 

void loop(){

  if( onButton.uniquePress() && safeState == true )
  {
    stateMachine.transitionTo( On );
  }
  if( offButton.uniquePress() && stateMachine.isInState( On ) )
  {
    stateMachine.transitionTo( Off );
  }
  if( digitalRead( SAFETY_SWITCH ) == LOW and safeState == false )
  {
    //debounce switch
    delay( 50 );
    if( digitalRead( SAFETY_SWITCH ) == LOW )
      stateMachine.transitionTo( Safe );
  }
  if( digitalRead( SAFETY_SWITCH ) == HIGH and safeState == true )
  {
    //debounce switch
    delay( 50 );
    if( digitalRead( SAFETY_SWITCH ) == HIGH )
      stateMachine.transitionTo( Unsafe );
  }
  stateMachine.update();
}

void nullFunc()
{
}

void stopMotor()
{
  digitalWrite( motorOn, LOW );
  //set the current fan speed to be 0
  encoder0Pos = 0;
  analogWrite( motorControl, 255 - encoder0Pos );
}

void safe()
{
  safeState = true;
  Serial.println( "state:safe" );
  ledControl( STOP_SWITCH_LED, OFF );
  ledControl( GO_SWITCH_LED, ON );
}

void unSafe()
{
  safeState = false;
  Serial.println( "state:unsafe" );
  //ensure motor is off
  stopMotor();
  ledControl( STOP_SWITCH_LED, FLASH );
  ledControl( GO_SWITCH_LED, OFF );
}

void running()
{
  analogWrite( motorControl, 255 - encoder0Pos );
  turbineVoltage = analogRead( TURBINE_VOLTAGE );


  addToHistory( turbineVoltageHistory, turbineVoltage );

  //http://www.arduino.cc/playground/Main/ReadingRPM
  if (tachoCount >= 10) { 
    //Update RPM every 20 counts, increase this for better RPM resolution,
    //decrease for faster update
   // float pulsePerMilli = ( millis() - timeold) * tachoCount;
   float period = millis() - timeold;
    rpm = 30*1000/period*tachoCount;
   // rpm *= (float)tachoCount;
   // Serial.println( pulsePerMilli );
   // rpm = 30*1000/pulsePerMilli;
    timeold = millis();
    tachoCount = 0;

  }
 /* 
     if (rpmcount >= 20) { 
     //Update RPM every 20 counts, increase this for better RPM resolution,
     //decrease for faster update
     rpm = 30*1000/(millis() - timeold)*rpmcount;
     timeold = millis();
     rpmcount = 0;
     Serial.println(rpm,DEC);
   }
*/

  if( serialSend ++ > 1000 )
  {
    serialSend = 0;
    //sample the data
    // Serial.print("freeMem: ");
    //   Serial.println( freeMemory() );  
    Serial.println( "state:running" ); 
    Serial.print( encoder0Pos );
    Serial.print( "," );
    Serial.print( rpm );
    Serial.print( "," );
    Serial.println( movingAverage( turbineVoltageHistory ) );
  }

}

void switchOn()
{
  Serial.println( "state:on" );
  wipeHistory( turbineVoltageHistory );
  //  wipeHistory( tachoPeriodHistory );

  ledControl( STOP_SWITCH_LED, ON );
  ledControl( GO_SWITCH_LED, OFF );

  //set the current fan speed to something
  encoder0Pos = STARTSPEED;
  //turn the motor on
  digitalWrite( motorOn, HIGH );

}

void switchOff()
{
  Serial.println( "state:off" );
  stopMotor();

  ledControl( STOP_SWITCH_LED, OFF );
  ledControl( GO_SWITCH_LED, ON );


}



//led crap
void ledControl( int led, int state )
{
  if( led == STOP_SWITCH_LED )
  {
    redLED = state;
  }
  else if( led == GO_SWITCH_LED )
  {
    greenLED = state;
  }
  doLed( state, led );
}

void doLed( int ledState, int ledPin )
{
  if( ledState == FLASH )
  {
    if( flashState )
    {
      digitalWrite( ledPin, HIGH );
    }
    else
    {
      digitalWrite( ledPin, LOW );
    }
  }
  else if( ledState == ON )
    digitalWrite( ledPin, HIGH );
  else if( ledState == OFF )
    digitalWrite( ledPin, LOW );
}        

void flash()
{
  if( flashState )
    flashState = false;
  else
    flashState = true;

  doLed( greenLED, GO_SWITCH_LED );
  doLed( redLED, STOP_SWITCH_LED );
}      

//ISRs
void doEncoder()
{
  movedEncoder = true;
  if (digitalRead(encoder0PinA) == digitalRead(encoder0PinB)) {
    if( encoder0Pos < 255 )
      encoder0Pos++;
  } 
  else {
    if( encoder0Pos > 0 )
      encoder0Pos--;
  }
}

void doTacho()
{
  tachoCount ++;

}

//moving average stuff (please replace with FIR!!
void addToHistory( int hist[], int newValue )
{
  for( int i = 0; i < N - 1; i ++ )
  {
    hist[i] = hist[i+1];
  }
  hist[N - 1] = newValue;
}

void wipeHistory( int hist[] )
{
  for (int i = 0; i<N; i++)
  {
    hist[i] = 0;
  }
}

int movingAverage( int hist[] )      
{
  int sum = 0.0;
  for (int i = 0; i<N; i++)
  {
    sum += hist[i];
  }
  return sum / N;
}






int main(void)
{
	init();

	setup();
    
	for (;;)
		loop();
        
	return 0;
}

