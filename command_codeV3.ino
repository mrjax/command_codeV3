/*  
 *  Command Node Code
 *  
 *  Before using, check:
 *  NUM_NODES, MY_NAME (should always be 0)
 *  Anchor node locations, x1, y1, x2, y2, x3, y3
 *
 */

#include <SPI.h>
#include "cc2500_REG_V2.h"
#include "cc2500_VAL_V2.h"
#include "cc2500init_V2.h"
#include "read_write.h"

//Number of nodes, including Command Node
const byte NUM_NODES = 4;

void setup(){
  init_CC2500_V2();
  pinMode(9,OUTPUT);
  Serial.begin(9600);

  //This just hardcodes some values into the Desired table, as bytes
  randomSeed(analogRead(3));
  int desired[NUM_NODES][2];
  for(int i = 0; i < NUM_NODES; i++){
    desired[i][0] = roundUp(random(30,230));  //roundUp((rand() % 200) + 30);  //gets a range of randoms from -100ish to 100ish
    desired[i][1] = roundUp(random(30,230)); //roundUp((rand() % 200) + 30);
  }
  desired[0][0] = byte(128);
  desired[0][0] = byte(128);
}


//State names
const int INIT = 0;
const int RECEIVE = 1;
const int CALCULATE = 2;
int state = INIT;


//Names of Node and nodes before it
//Determines when this node's turn is
const byte MY_NAME = 0; //Command node is always 0
const byte PREV_NODE = NUM_NODES - 1;
const byte PREV_PREV_NODE = NUM_NODES - 2;

//Initial conditions of three points, first is command node at origin
//stub convert to (-127)-128 before sending to serial
const int x1 = 0;  //128;  //0
const int y1 = 0;  //128;  //0
const int x2 = 12;  //129;  //1
const int y2 = 36;  //131;  //3
const int x3 = 36;  //127;  //turns into -1
const int y3 = -12;  //124;  //turns into -4

//How many data entries to take an average from, arbitrarily set to 15 stub
const int STRUCT_LENGTH = 15;

//The indexes of where each piece of data is (for readability of code)
const int SENDER = 0;
const int TARGET = 1;
const int DISTANCE = 2;
const int SENSOR_DATA = 3;
const int HOP = 4;
const int END_BYTE = 5;
const int RSSI_INDEX = 6;

//This is how many times to resend all data, for redundancy.  Arbitrarily set to 4
const int REDUNDANCY = 4; 

//Timer info
const unsigned long TIMEOUT = 2000; //??? check this timeout number stub

//Global variables
//These control timeouts
unsigned long currTime;
unsigned long lastTime;

//These are the structures that contain the data to be averaged
byte rssiData[NUM_NODES][STRUCT_LENGTH] = {
  0};

//Each contains a pointer for each of the nodes, indicating where to write in the above tables
int rssiPtr[NUM_NODES] = {
  0};

//Arrays of averages
byte rssiAvg[NUM_NODES] = {
  0};  

//The main matrices
byte distances[NUM_NODES][NUM_NODES] = {
  0};
byte allSensorData[NUM_NODES];    //Collected sensor data from all nodes
byte currLoc[NUM_NODES][2] = {
  0}; //The contents of the R calculations file will go here
byte desired[NUM_NODES][2] = {
  0};

//Flags for controlling getting new data every cycle or not
boolean wantNewMsg = true;
boolean gotNewMsg = false; 

//The current message, and storage for data restoration in case of bad packet
byte currMsg[PACKET_LENGTH] = {
  0};
byte oldMsg[PACKET_LENGTH] = {
  0};

//Current Round number
int roundNumber = 0;

//Temporary variable used for averaging
int temp = 0;
int goodMsg = 0;

//Helps coordinate timeout
byte lastHeardFrom;


/*
//This function converts Serial values which can be negative into positive bytes
 int byteToInt(byte input){
 if(input > 128){
 input = int(input - byte(128));
 return input;
 }else if(input < 128){
 input = int((byte(128) - input)) * -1;
 return input;
 }else return 0;
 }
 */

/*
//This function converts 
 int byteToInt(byte input){
 if(input > 127){
 return int(255 - input);
 }else return int(input);
 }
 */

//Round values up to nearest whole number, cast to byte
byte roundUp(int input){
  int output = input - (input%1);
  if(output == input) return byte(input);
  else return byte(output + 1);
}

/*
//stub have this function passing array and only output status int
 int changeDesiredLocs(int want[][], int curr[][]){
 //This method is where the desired locations of the slave nodes is reset
 //currently, it only adds some randomness to all the points
 //to have the desired locations a little bit somewhere else.
 //(future) Have something more intelligent.
 
 for(int i = 0; i < NUM_NODES; i++){
 desired[i][0] = desired[i][0] + int((rand() % 3) - 2);  //adds -1, 0, or 1
 	    	desired[i][1] = desired[i][1] + int((rand() % 3) - 2);
 }
 return want;
 }
 */

void loop(){
  //This block picks up a new message if the state machine requires one this
  //cycle.  It also accommodates packets not arriving yet
  //It also sets gotNewMsg, which controls data collection later
  if(wantNewMsg){
    //Save old values in case can't pick up a new packet
    for(int i = 0; i < PACKET_LENGTH; i++){
      oldMsg[i] = currMsg[i];
    }

    //Get new values. If no packet available, goodMsg will be null
    goodMsg = listenForPacket(currMsg);

    //Check to see if packet is null. If so, put old values back
    if(goodMsg == 0){
      for(int i = 0; i < PACKET_LENGTH; i++){
        currMsg[i] = oldMsg[i];
      }
      gotNewMsg = false;
    }
    else{
      //..otherwise, the packet is good, and you got a new message successfully
      lastHeardFrom = currMsg[SENDER];
      gotNewMsg = true;
    }
  }


  //State machine controlling what to do with packet
  switch(state){
  case INIT:
    //Serial.println("Init state");
    //Send Startup message, with SENDER == 0, and TARGET == 0;  Sending 10 times arbitrarily
    for(int i = 0; i < 10; i++){
      sendPacket(0, 0, 0, 0, 0, 0);
    }
    sendPacket(0, 0, 0, 0, 0, 1);

    state = RECEIVE;
    wantNewMsg = true;
    break;

  case RECEIVE:
    //Serial.println("Receive state");
    digitalWrite(9, LOW);

    //If hear from Prev_Prev, and it's a valid message, "start" timeout timer
    if(currMsg[SENDER] == PREV_PREV_NODE && gotNewMsg){
      lastTime = millis();
    }

    //stub, this allows the case where prev_prev fails, and the node hasn't heard from prev_prev
    //so as soon as prev node says anything, the timer is checked and is of course active,
    //so we need another timer to see whether you've last heard anything from prev_prev, and a timer
    //for whether you've last heard anything from prev
    if(lastHeardFrom == PREV_NODE || lastHeardFrom == PREV_PREV_NODE){
      currTime = millis() - lastTime;
    }

  //Debug printing
    //Serial.print("l ");
    //Serial.println(lastTime);
    //Serial.print("c ");
    //Serial.println(currTime);

    //Serial.print("Msg from ");
    //Serial.print(currMsg[SENDER]);
    //Serial.print(" end ");
    //Serial.print(currMsg[END_BYTE]);
    //Serial.print(" rssi ");
    //Serial.println(currMsg[RSSI_INDEX]);

    //Serial.println(lastHeardFrom);

    //Prev node has finish sending
    if(currMsg[SENDER] == PREV_NODE && currMsg[END_BYTE] == byte(1) && lastHeardFrom == PREV_NODE){
      state = CALCULATE;
      wantNewMsg = false;
    } //...or timeout has occurred
    else if(currTime > TIMEOUT){
      //Serial.println("Timeout");
      state = CALCULATE;
      lastTime = millis();
      currTime = 0;
      wantNewMsg = false;
    }

    //Add data from packet to data structure
    distances[currMsg[SENDER]][currMsg[TARGET]] = currMsg[DISTANCE];
    allSensorData[currMsg[SENDER]] = currMsg[SENSOR_DATA];

    break;

  case CALCULATE:
    //Serial.println("Calculate state");
    digitalWrite(9, HIGH);

    //Assert "distance to self"s to 0 then
    //average upper and lower triangles of data
    for(int i = 0; i < NUM_NODES; i++){
      distances[i][i] = 0; 
      int h = i;
      for(h = h + 1; h < NUM_NODES; h++){
        distances[i][h] = roundUp((distances[i][h] + distances[h][i])/2);
        distances[h][i] = distances[i][h];
      }
    }

    //Transmit data through serial
    roundNumber = roundNumber + 1;
    //Hard-coded formatting that R knows to accept, starting with round number and number of nodes
    Serial.print("-3 ");
    Serial.println(roundNumber);
    Serial.print("-1 0 ");
    Serial.println(NUM_NODES);
    //Send anchor node values
    Serial.print("-1 1 ");
    Serial.println(x1);
    Serial.print("-1 2 ");
    Serial.println(y1);
    Serial.print("-1 3 ");
    Serial.println(x2);
    Serial.print("-1 4 ");
    Serial.println(y2);
    Serial.print("-1 5 ");
    Serial.println(x3);
    Serial.print("-1 6 ");
    Serial.println(y3);
    //Send current distance values
    for(int i = 0; i < NUM_NODES; i++){
      for(int j = 0; j < NUM_NODES; j++){
        Serial.print(i);
        Serial.print(" ");
        Serial.print(j);
        Serial.print(" ");
        Serial.println(distances[i][j]);
      }
    }
    //Send desired values
    for(int i = 0; i < NUM_NODES; i++){
      Serial.print("-2 ");
      Serial.print(i);
      Serial.print(" 0 ");
      Serial.println(desired[i][0]);

      Serial.print("-2 ");
      Serial.print(i);
      Serial.print(" 1 ");
      Serial.println(desired[i][1]);
    }
    //Send round number again, as "end of serial transmission" indicator
    Serial.print("-4 ");
    Serial.println(roundNumber);

    //Stub, read from Serial to detect results from R, then read in until
    //receive "-4 [roundNumber]" back (signifies end of transmission)
    //For now, just delay for a second
    delay(1000);

    //Parse returned calculated values
    //for(int i = 0; i < NUM_NODES; i++){
    //currLoc[i][0] = line[1];
    //currLoc[i][1] = line[2];
    //}

    //Compare current network locations to desired locations, decide to change desired locations
    //boolean closeEnough = true;
    //for(int i = 0; i < NUM_NODES; i++){
    //  if((abs(desired[i][0] - currLoc[i][0]) < 0) && (abs(desired[i][0] - currLoc[i][0]) > 2)) closeEnough = false;
    //  if((abs(desired[i][1] - currLoc[i][1]) < 0) && (abs(desired[i][1] - currLoc[i][1]) > 2)) closeEnough = false;
    //}
    //if(closeEnough){
    //	desired = changeDesiredLocs(desired, currLoc);
    //}

    //Send current locations and commands a few times
    for(int j = 0; j < REDUNDANCY; j++){
      for(int i = 0; i < NUM_NODES; i++){
        sendPacket(MY_NAME, i, currLoc[i][0], currLoc[i][1], 0, 0);
        sendPacket(MY_NAME, i, desired[i][0], desired[i][1], 1, 0);
      }
    }
    sendPacket(MY_NAME, NUM_NODES, desired[NUM_NODES][0], desired[NUM_NODES][1], 1, 1);

    lastHeardFrom = MY_NAME;

    //Turn is over, begin receiving again
    wantNewMsg = true;
    state = RECEIVE;
    break;
  }

  //Every received packet must have RSSI scraped off and added to calculations
  if(gotNewMsg){
    //Check if there is already an average, if so, do filter, if not just add data in appropriate position
    //(in both cases pointer must be incremented or looped
    if(rssiAvg[currMsg[SENDER]] != 0){
    //Filter RSSI based on +-10 around running average
      if(currMsg[RSSI_INDEX] < rssiAvg[currMsg[SENDER]] + 10 && currMsg[RSSI_INDEX] > rssiAvg[currMsg[SENDER]] - 10){
        if(rssiPtr[currMsg[SENDER]] == STRUCT_LENGTH - 1) rssiPtr[currMsg[SENDER]] = 0;  //Loop pointer around if it's reached the end of the array
        else rssiPtr[currMsg[SENDER]] += 1;                              //..otherwise just increment
        
        rssiData[currMsg[SENDER]][rssiPtr[currMsg[SENDER]]] = currMsg[RSSI_INDEX];
      }
    }
    else{
      if(rssiPtr[currMsg[SENDER]] == STRUCT_LENGTH - 1) rssiPtr[currMsg[SENDER]] = 0;
      else rssiPtr[currMsg[SENDER]] += 1;

      rssiData[currMsg[SENDER]][rssiPtr[currMsg[SENDER]]] = currMsg[RSSI_INDEX];
    }


    //If there's a full row of data to average, average it
    //(detects full row by checking last bit in row filled)
    if(rssiData[currMsg[SENDER]][STRUCT_LENGTH - 1] != 0){
      temp = 0;
      for(int i = 0; i < STRUCT_LENGTH; i++){
        temp += rssiData[currMsg[SENDER]][i];
      }
      rssiAvg[currMsg[SENDER]] = temp/STRUCT_LENGTH;
    }

    //Add new average to distance table
    distances[MY_NAME][currMsg[SENDER]] = roundUp(int((log(float(rssiAvg[currMsg[SENDER]])/95)/log(0.99))));

  }
}

