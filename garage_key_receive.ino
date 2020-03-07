#define HCS_RECIEVER_PIN  2
#define RELAY_POWER_PIN  4
#define RELAY_SWITCH_PIN  3
#define RELAY_TIMEOUT 10000

#define OSC_LENGTH  12
#define DATA_LENGTH  66
#define HEADER  3880
#define TE 382
#define TOLERANCE 80

#define MODE_OSC_SEARCH  0
#define MODE_DATA_READ  1

int lowSigDuration = 0;
int highSigDuration = 0;
byte oscCounter = 0;
byte dataCounter = 0;
uint32_t lastChangeTimestamp = 0;
char data[DATA_LENGTH+3];
short int dataSequence[132];
bool putToSerial = false;

uint32_t signalStartTimestamp = 0;

byte currentMode;

void setup() {
  Serial.begin(115200); 
  
  attachInterrupt(0, HCS_interrupt, CHANGE);
  pinMode(HCS_RECIEVER_PIN, INPUT);
  lastChangeTimestamp = micros();
  setModeOscSearch();

  pinMode(RELAY_POWER_PIN, OUTPUT);
  digitalWrite(RELAY_POWER_PIN, HIGH);
  pinMode(RELAY_SWITCH_PIN, OUTPUT);
  digitalWrite(RELAY_SWITCH_PIN, HIGH);

  Serial.println("r"); //ready
}

void loop() {
  if (Serial.read() > 0) {
    Serial.flush();
    relayOn();
  }

  if (putToSerial) {
    noInterrupts();
    Serial.println(data);
    putToSerial = false;
    relayOff();
    delay(100);//to generate new key
    interrupts();
  }

}

void relayOn()
{
  interrupts();
  digitalWrite(RELAY_SWITCH_PIN, LOW);
}

void relayOff()
{
  noInterrupts();
  digitalWrite(RELAY_SWITCH_PIN, HIGH);
}

void HCS_interrupt(){
  uint8_t currentState = digitalRead(HCS_RECIEVER_PIN);
  uint32_t currentTimestamp = micros();
  
  if (currentState == LOW) {
    highSigDuration = currentTimestamp - lastChangeTimestamp;
  } 
  
  if (currentState == HIGH) {
    lowSigDuration = currentTimestamp - lastChangeTimestamp;
  }

  lastChangeTimestamp = currentTimestamp;

  if (isModeOscSearch()) {
    processOscSearchModeSignal(currentState, currentTimestamp);
  } else {
    processModeDataReadSignal(currentState, currentTimestamp);
  }

}

void processOscSearchModeSignal(uint8_t currentState, uint32_t currentTimestamp)
{
  if (currentState == LOW) {
    if (isValidSignalDuration(highSigDuration)
      && (isValidSignalDuration(lowSigDuration) || oscCounter == 0)
    ) {
      if (signalStartTimestamp == 0) {
        signalStartTimestamp = currentTimestamp;
      }
      oscCounter++;
    
      if (oscCounter == OSC_LENGTH) {
        //signalStartTimestamp = currentTimestamp - signalStartTimestamp;
        setModeDataRead();
      }
    } else {
      signalStartTimestamp = 0;
      oscCounter = 0;
    }
  }

  if (oscCounter && currentState == HIGH && !isValidSignalDuration(lowSigDuration)) {
    oscCounter = 0;
    signalStartTimestamp = 0;
  }
}

void processModeDataReadSignal(uint8_t currentState, uint32_t currentTimestamp) 
{
  if (currentState == LOW) {
    if (isValidSignalDuration(highSigDuration)
      && (isValidSignalDuration(lowSigDuration) || dataCounter == 0)
    ) {
      dataCounter++;
      
      byte bitVal = getBitValue();

      if (bitVal >= 0) {
        data[dataCounter - 1] = bitVal == 0 ? '0' : '1';
      } else {
        setModeOscSearch();
      }
      
      if (dataCounter == DATA_LENGTH) {
        putToSerial = true;
        signalStartTimestamp = currentTimestamp - signalStartTimestamp;
        setModeOscSearch();
      }
    } else {
      dataCounter = 0;
      setModeOscSearch();
    }
  }
}

void setModeOscSearch()
{
  currentMode = MODE_OSC_SEARCH;
}

bool isModeOscSearch()
{
  return currentMode == MODE_OSC_SEARCH;
}

void setModeDataRead()
{
  currentMode = MODE_DATA_READ;
}

bool isModeDataRead()
{
  return currentMode == MODE_DATA_READ;
}

bool isValidSignalDuration(short int duration)
{
  if (isModeOscSearch()) {
    return tolerantEquals(duration, TE);
  } else {
    return tolerantEquals(duration, TE) || tolerantEquals(duration, 2 * TE);
  }
}

byte getBitValue() 
{
  if (isModeDataRead()) {
    if (tolerantEquals(highSigDuration, 2 * TE)) {
      return 1;
    }
  
    if (tolerantEquals(highSigDuration, TE)) {
      return 0;
    }
  }

  return -1;
}

bool tolerantEquals(short int duration1, short int duration2) 
{
  return abs(duration1 - duration2) < TOLERANCE;
}
