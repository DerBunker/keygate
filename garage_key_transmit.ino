#include <SoftwareSerial.h>

#define TRANSMITTER_PIN 5
#define BUTTON_PIN 2
#define BRX_PIN 7
#define BTX_PIN 8

#define CHECKSUM_MAX_LENGTH 2
#define CHECKSUM_DELIMITER ':'
#define KEY_LENGTH 66
#define KEY_SEND_TIMES 20

#define TE 379

char receivedKey[KEY_LENGTH + 1] = "";
char receivedChecksum[CHECKSUM_MAX_LENGTH + 1] = "";
byte unsigned keyCounter = 0;
byte unsigned checksumCounter = -1;

SoftwareSerial mySerial(BRX_PIN, BTX_PIN); // RX, TX

void setup() {
  mySerial.begin(9600);
  Serial.begin(9600);
  while (!Serial) {
    ;
  }

  pinMode(TRANSMITTER_PIN, OUTPUT);
  digitalWrite(TRANSMITTER_PIN, LOW);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUTTON_PIN,INPUT_PULLUP);

  Serial.println("ready");
}

void loop() {
  if (dataCollected()) {
    if (isKeyValid()) {
      Serial.println(receivedKey);
      Serial.println(receivedChecksum);
      mySerial.write("received\r\n");
      delay(100);
      noInterrupts();
      
      for (int i = 0; i < KEY_SEND_TIMES; i++) {
        sendSignal();
      }

      interrupts();
      Serial.println("sent");
      mySerial.write("sent\r\n");
    } else {
      Serial.println("invalid");
      mySerial.write("invalid\r\n");
    }
  }
}

bool dataCollected() {
  char receivedChar;
  bool allDataHere = false;
  
  while (mySerial.available()) {
    receivedChar = mySerial.read();
    
    if (receivedChar == '\r' || receivedChar == '\n') {
      if (keyCounter == KEY_LENGTH && checksumCounter > 0) {
        receivedKey[keyCounter] = '\0';
        receivedChecksum[checksumCounter] = '\0';
        allDataHere = true;
      }
      
      keyCounter = 0;
      checksumCounter = -1;
    } else if (receivedChar == CHECKSUM_DELIMITER) {
      checksumCounter = 0;
    } else if (checksumCounter >= 0 && checksumCounter < CHECKSUM_MAX_LENGTH) {
      receivedChecksum[checksumCounter] = receivedChar;
      checksumCounter++;
    } else if (keyCounter < KEY_LENGTH) {
      receivedKey[keyCounter] = receivedChar;
      keyCounter++;
    }
    
  }

  return allDataHere;
}

bool isKeyValid() {
  byte checksum = 0;
  byte receivedChecksumInt = 0;
  bool allSymbolsValid = true;
  
  sscanf(receivedChecksum, "%d", &receivedChecksumInt);
  
  for (int i = 0; i < KEY_LENGTH; i++) {
    if (receivedKey[i] == '1') {
      checksum++;
    } else if (receivedKey[i] != '0') {
      allSymbolsValid = false;
    }
  }

  return allSymbolsValid && checksum == receivedChecksumInt;
}

void sendPreambula() {
  for (int i = 0; i < 12; i++) {
    digitalWrite(TRANSMITTER_PIN, HIGH);
    delayMicroseconds(TE);
    digitalWrite(TRANSMITTER_PIN, LOW);
    delayMicroseconds(TE);
  }

  delayMicroseconds(TE * 9);
}

void sendSignal() {
  sendPreambula();
  for (int i = 0; i < KEY_LENGTH; i++) {
    if (receivedKey[i] == '1') {
      digitalWrite(TRANSMITTER_PIN, HIGH);
      delayMicroseconds(TE * 2);
      digitalWrite(TRANSMITTER_PIN, LOW);
      delayMicroseconds(TE);
    } else {
      digitalWrite(TRANSMITTER_PIN, HIGH);
      delayMicroseconds(TE);
      digitalWrite(TRANSMITTER_PIN, LOW);
      delayMicroseconds(TE * 2);
    }
  }

  delayMicroseconds(TE * 40);
}
