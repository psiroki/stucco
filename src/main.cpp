#include <Arduino.h>

/*
 Stepper Motor Control via Bluetooth commands

 This program drives a unipolar or bipolar stepper motor.
 The motor is attached to digital pins 8 - 11 of the Arduino.

 A Bluetooth module may be connected to D2/D12 (TX, RX on
 Arduino side).

 Created 9 July 2017
 Modified 5 July 2019
 by Peter Siroki

 */

#include <SoftwareSerial.h>
#include <string.h>

SoftwareSerial btSerial(12, 2); // RX, TX

void moveStep(int eighth);

// the gear is 25792:405, which makes the full circle about 4077
const int stepsPerRevolution = 4077;  // change this to fit the number of steps per revolution
// for your motor

int lookup[8] = {0b01000, 0b01100, 0b00100, 0b00110, 0b00010, 0b00011, 0b00001, 0b01001};

const int IN1 = 8;    
const int IN2 = 9;
const int IN3 = 10;
const int IN4 = 11;

class Buffer {
  char buffer[256];
  uint8_t readPos;
  uint8_t writePos;
public:
  Buffer(): readPos(0), writePos(0) { }
  
  void write(char c) {
    buffer[writePos++] = c;
  }

  bool available() {
    return readPos != writePos;
  }

  char read() {
    char result = buffer[readPos];
    buffer[readPos] = 0;
    ++readPos;
    return result;
  }
} inputBuffer;
unsigned long lastByteMicros = 0;

char command[64];
uint8_t commandLength = 0;

struct Command {
  long numSteps;
  int duration;
  unsigned long microstep;
  bool quick;
//  unsigned long start;
  unsigned long nextStepTime;
  uint8_t state;

  static const uint8_t done = 0;
  static const uint8_t pending = 1;
  static const uint8_t active = 2;
};

Command commands[24];
uint8_t produceCommand = 0;
uint8_t consumeCommand = 0;
uint8_t numCommandsPending = 0;
bool executing = false;
const uint8_t maxCommands = sizeof(commands)/sizeof(*commands);

uint8_t freezeMode = 0;
const uint8_t freezeNone = 0;
const uint8_t freezeExec = 1;
const uint8_t freezeComm = 2;

uint8_t eighth = 0;

inline uint8_t stepCommand(uint8_t &c) {
  c = (c+1)%maxCommands;
  return c;
}

void clearCommands() {
  memset(commands, 0, sizeof(commands));
  produceCommand = 0;
  consumeCommand = 0;
  numCommandsPending = 0;
}

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(12, INPUT);
  pinMode(2, OUTPUT);
  clearCommands();
  // initialize the serial port:
  Serial.begin(9600);
  btSerial.begin(9600);
  moveStep(eighth);
  Serial.print("Max number of commands: ");
  Serial.println(maxCommands);
}

class StatusReporter {
  int8_t i;
public:
  StatusReporter() {
    i = -1;
  }

  void start() {
    i = 0;
  }

  void header1() {
    btSerial.println(executing ? "Currently executing" : "Currently idle");
  }

  void header2() {
    btSerial.print("There are ");
    btSerial.print(numCommandsPending);
    btSerial.println(" commands pending");
  }

  void command(int8_t index) {
    Command &c(commands[index]);
    if(c.state != Command::done) {
      btSerial.print("#");
      btSerial.print(index);
      btSerial.print(" ");
      btSerial.print(c.numSteps);
      btSerial.print(" steps left (");
      btSerial.print(c.numSteps*360/stepsPerRevolution);
      btSerial.print("deg) ");
      if(c.state == Command::active) {
        btSerial.print("time left: ");
        btSerial.print(c.microstep*c.numSteps/1000);
        btSerial.print("ms");
      } else {
        btSerial.print("duration: ");
        btSerial.print(c.duration);
        btSerial.print("s");
      }
      if(c.quick)
        btSerial.print(" (quick)");
      btSerial.println();
    }
  }

  bool step() {
    if(i < 0) return false;
    switch(i) {
    case 0:
      start();
      break;
    case 1:
      header1();
      break;
    case 2:
      header2();
      break;
    default:
      if(i >= 3 && i-3 < maxCommands)
        command(i-3);
      break;
    }
    ++i;
    if(i >= maxCommands+3)
      i = -1;
    return true;
  }
} statusReporter;

void loop() {
  bool shouldComm = freezeMode != freezeComm || !executing;
  if(shouldComm && !statusReporter.step()) {
    bool written = false;
    while(btSerial.available()) {
      inputBuffer.write(btSerial.read());
      written = true;
    }
    while(Serial.available()) {
      inputBuffer.write(Serial.read());
      written = true;
    }
    if(written)
      lastByteMicros = micros();
  }
  shouldComm = micros()-lastByteMicros > 10000;
  while(shouldComm && inputBuffer.available()) {
    char c = inputBuffer.read();
    if(c == '\n' || c == '\r' || c == ';') {
      if(commandLength <= 0)
        continue;
      command[commandLength] = 0;
      Serial.write('#');
      Serial.print((int) produceCommand);
      Serial.print(' ');
      Serial.println(command);
      if(command[0] == 's') {
        // status
        statusReporter.start();
      } else
      if(command[0] == 'f') {
        // freeze
        char *space = strchr(command, 32);
        if(!space) {
          switch(freezeMode) {
          case freezeNone:
            btSerial.println("freezeNone");
            break;
          case freezeExec:
            btSerial.println("freezeExec");
            break;
          case freezeComm:
            btSerial.println("freezeComm");
            break;
          default:
            btSerial.print("Freeze what??? (");
            btSerial.print(freezeMode);
            btSerial.println(")");
          }
          btSerial.println("Valid values are n(one), e(xecution), c(ommunication)");
        } else {
          switch(space[1]) {
          case 'n':
            freezeMode = freezeNone;
            btSerial.println("Unfreeze");
            break;
          case 'e':
            freezeMode = freezeExec;
            btSerial.println("Freezing execution");
            break;
          case 'c':
            freezeMode = freezeComm;
            btSerial.println("Freezing communication");
            break;
          default:
            btSerial.print("Freeze what? (");
            btSerial.print(space+1);
            btSerial.println(")");
          }
        }
      } else
      if(command[0] == 'r' || command[0] == 'l') {
        // left/right
        char *space = strchr(command, 32);
        bool flip = command[0] == 'r';
        long values[2] = { 1020, 0 };
        int numValues = 0;
        char *start = space ? space+1 : 0;
        while(start && numValues < 2) {
          char *pos = strchr(start, 32);
          char *end = pos;
          if(!end) end = command+commandLength;
          bool deg = false;
          if(end[-1] == 'd') {
            deg = true;
            --end;
          }
          *end = 0;
          long val = atol(start);
          if(deg)
            val = ((long) val)*stepsPerRevolution/360;
          values[numValues++] = val;
          if(!pos)
            break;
          else
            start = pos+1;
        }
        if(flip)
          values[0] = -values[0];
        int index = produceCommand;
        Command &c(commands[produceCommand]);
        if(c.state != Command::done) {
          btSerial.println("Too many commands, try 'clear'");
        } else {
          c.numSteps = values[0];
          c.duration = values[1];
          float microsBetweenSteps = c.duration*1.0e6f/labs(c.numSteps);
          c.microstep = microsBetweenSteps;
          if(c.microstep < 1000)
            c.microstep = 1000;
          c.quick = c.microstep < 2000;
          if(c.quick)
            c.microstep *= 2;
          c.state = Command::pending;
          stepCommand(produceCommand);
          ++numCommandsPending;
          btSerial.print("#");
          btSerial.print(index);
          btSerial.print(c.numSteps < 0 ? " Going to turn right about " :  " Going to turn left about ");
          btSerial.print(labs(c.numSteps));
          btSerial.print(" steps with ");
          btSerial.print(c.microstep);
          btSerial.println(" microseconds between steps");
        }
      } else {
        if(command[0] == 'c') {
          clearCommands();
        }
      }
      commandLength = 0;
    } else if(commandLength < 63 && (c != ' ' || commandLength > 0)) {
      command[commandLength] = c;
      ++commandLength;
    }
  }
  executing = false;
  if(commands[consumeCommand].state == Command::active) {
    executing = true;
    Command &c(commands[consumeCommand]);
    if(c.numSteps == 0) {
      c.state = Command::done;
      Serial.print("#");
      Serial.print(consumeCommand);
      Serial.println(" Finished");
      executing = false;
    } else if(freezeMode != freezeExec) {
      unsigned long now = micros();
      if(now >= c.nextStepTime) {
        moveStep(eighth);
        uint8_t steps = c.quick ? 2 : 1;
        for(uint8_t i=0; i<steps; ++i) {
          if(c.numSteps < 0) {
            ++c.numSteps;
            --eighth;
          }
          if(c.numSteps > 0) {
            --c.numSteps;
            ++eighth;
          }
        }
        c.nextStepTime += c.microstep;
      }
    }
  } else if(numCommandsPending > 0 && freezeMode != freezeExec) {
    if(commands[consumeCommand].state != Command::pending)
      stepCommand(consumeCommand);
    if(commands[consumeCommand].state == Command::pending) {
      Command &c(commands[consumeCommand]);
      Serial.print("#");
      Serial.print(consumeCommand);
      Serial.print(" Starting rotating ");
      Serial.print(c.numSteps);
      Serial.println(" steps");
      if(c.quick)
        eighth |= 1;
      c.state = Command::active;
      //c.start = micros();
      c.nextStepTime = micros();//c.start;
      --numCommandsPending;
      executing = true;
    }
  }
}

void moveStepAndWait(int eighth) {
  moveStep(eighth);

  //delay(2);
  delayMicroseconds(1800);
}

// This function will move the motor one step in either direction (depending on which function called this function)
void moveStep(int eighth)
{
  uint8_t val = lookup[eighth&7];
  digitalWrite(IN1, bitRead(val, 0));
  digitalWrite(IN2, bitRead(val, 1));
  digitalWrite(IN3, bitRead(val, 2));
  digitalWrite(IN4, bitRead(val, 3));

// Below is debug: writes the bits to the serial monitor  
/*  Serial.print(bitRead(lookup[eighth], 0));  
  Serial.print(bitRead(lookup[eighth], 1));  
  Serial.print(bitRead(lookup[eighth], 2));  
  Serial.print(bitRead(lookup[eighth], 3));  
  Serial.print("\t");*/
}
