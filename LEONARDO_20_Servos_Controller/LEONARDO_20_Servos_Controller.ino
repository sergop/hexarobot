//////////////////////////////////////////////////////////////////////////////////////////////////////
// UNO_20_Servos_Controller.ino - High definition (15 bit), low jitter, 20 servo software for Atmega328P and Arduino UNO. Version 1.
// Jitter is typical 200-400 ns. 32000 steps resolution for 0-180 degrees. In 18 servos mode it can receive serial servo-move commands.
//
// Copyright (c) 2013 Arvid Mortensen.  All right reserved. 
// http://www.lamja.com
// 
// This software is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//
//////////////////////////////////////////////////////////////////////////////////////////////////////
//                              !!!!!!!!!!!!!!!!!!!
// This software will only work with ATMEGA328P (Arduino UNO and compatible. Or that is what I have tested it with anyways....  !!!!
//                              !!!!!!!!!!!!!!!!!!!
// How it works:
// 18 or 20 pins are used for the servos. These pins are all pre configured.
// All 18/20 servos are always active and updated. To change the servo position, change the
// values in the ServoPW[] array, use the serial commands (int 18 servos mode only) or use 
// ServoMove() function. The range of the ServoPW is 8320 to 39680. 8320=520us. 39680=2480us.
//
// Some formulas:
// micro second = ServoPW value / 16
// angle = (ServoPW value - 8000) / 177.77  (or there about)
// ServoPW value = angle * 177.77 + 8000
// ServoPW value = micro second * 16
//
// Channels are locked to these pins:
// Ch0=Pin2, Ch1=Pin3, Ch2=Pin4, Ch3=Pin5, Ch4=Pin6, Ch5=Pin7, Ch6=Pin8, Ch7=Pin9, Ch8=Pin10, Ch9=Pin11
// Ch10=Pin12, Ch11=Pin13, Ch12=PinA0, Ch13=PinA1, Ch14=PinA2, Ch15=PinA3, Ch16=PinA4, Ch17=PinA5, Ch18=Pin0, Ch19=Pin1
//
// Serial commands:
// # = Servo channel
// P = Pulse width in us
// p = Pulse width in 1/16 us
// S = Speed in us per second
// s = Speed in 1/16 us per second
// T = Time in ms
// PO = Pulse offset in us. -2500 to 2500 in us. Used to trim servo position.
// Po = Pulse offset in 1/16us -40000 to 40000 in 1/16 us
// I = Invert servo movements.
// N = Non-invert servo movements.
// Q = Query movement. Return "." if no servo moves and "+" if there are any servos moving.
// QP = Query servo pulse width. Return 20 bytes where each is from 50 to 250 in 10us resolution. 
//      So you need to multiply byte by 10 to get pulse width in us. First byte is servo 0 and last byte is servo 20.
// <cr> = Carrage return. ASCII value 13. Used to end command.
//
// Examples:
// #0 P1500 T1000<cr>                        - Move Servo 0 to 1500us in 1 second.
// #0 p24000 T1000<cr>                       - Move Servo 0 to 1500us in 1 second.
// #0 p40000 s1600<cr>                       - Move Servo 0 to 2500us in 100us/s speed
// #0 p40000 S100<cr>                        - Move Servo 0 to 2500us in 100us/s speed
// #0 P1000 #1 P2000 T2000<cr>               - Move Servo 0 and servo at the samt time from current pos to 1000us and 2000us in 2 second.
// #0 P2400 S100<cr>                         - Move servo 0 to 2400us at speed 100us/s
// #0 P1000 #1 P1200 S500 #2 P1400 T1000<cr> - Move servo 0, 1 and 2 at the same time, but the one that takes longes S500 or T1000 will be used.
// #0 PO100 #1 PO-100<cr>                    - Will set 100 us offset to servo 0 and -100 us ofset to servo 1
// #0 Po1600 #1 Po-1600<cr>                  - Will set 100 us offset to servo 0 and -100 us ofset to servo 1
// #0 I<cr>                                  - Will set servo 0 to move inverted from standard
// #0 N<cr>                                  - Will set servo 0 back to move non-inverted
// Q<cr>                                     - Will return "." if no servo moves and "+" if there are any servos moving
// QP<cr>                                    - Will retur 18 bytes (each 20ms apart) for position of servos 0 to 17
//
// 18 or 20 channels mode:
// #define HDServoMode 18           - This will set 18 channels mode so you can use serial in and out. Serial command interpreter is activated.
// #define HDServoMode 20           - This will set 20 channels mode, and you can not use serial. 
//                                    A demo will run in the loop() routine . Serial command interpreter is not active.
//                                    use ServoMove(int Channel, long PulseHD, long SpeedHD, long Time) to control servos.
//                                    one of SpeedHD or Time can be set to 0 to just use the other one for speed. If both are used,
//                                    the one that takes the longest time will be used. You can also change the values in the 
//                                    ServoPW[] array directly, but take care not to go under/over 8320/39680.
//
// #deefine SerialInterfaceSpeed 115200      - Serial interface Speed
//////////////////////////////////////////////////////////////////////////////////////////////////////
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <math.h>
#define HDServoMode 18
#define SerialInterfaceSpeed 115200    // Serial interface Speed

static volatile uint8_t *OutPortTable[20] = {&PORTD,&PORTD, &PORTD,&PORTC,
											 &PORTD,&PORTE, &PORTB,&PORTB,
											 &PORTB,&PORTB, &PORTD,&PORTC,
											 &PORTF,&PORTF, &PORTF,&PORTF,
											 &PORTF,&PORTF};
static uint8_t OutBitTable[20] = {	2,1,16,64,
									128,64,16,32,
									64,128,64,128,
									128,64,32,16,
									2,1};
static unsigned int ServoPW[20] = {24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000};
static byte ServoInvert[20] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static byte Timer0Toggle;
static volatile uint8_t *OutPort1A = &PORTD;
static volatile uint8_t *OutPort1B = &PORTB;
static uint8_t OutBit1A = 4;
static uint8_t OutBit1B = 16;
static volatile uint8_t *OutPortNext1A = &PORTD;
static volatile uint8_t *OutPortNext1B = &PORTB;
static uint8_t OutBitNext1A = 4;
static uint8_t OutBitNext1B = 16;

static long ServoStepsHD[20] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static long ServoLastPos[20] = {24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000,24000};
static long StepsToGo[20] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
static int ChannelCount;

static long ServoGroupStepsToGo = 0;
static long ServoGroupServoLastPos[20];
static long ServoGroupStepsHD[20];
static int ServoGroupChannel[20];
static int ServoGroupNbOfChannels = 0;

static char SerialIn;
static char input;
static long SerialNumbers[10];
static int SerialNumbersLength = 0;
static boolean FirstSerialChannelAfterCR = 1;

static int SerialChannel = 0;
static long SerialPulseHD = 24000;
static long SerialPulseOffsetHD[20] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static long SerialPulseOffsetTempHD = 0;
static long SerialSpeedHD = 0;
static long SerialTime = 0;
static long SerialNegative = 1;
static boolean SerialNeedToMove = 0;
static char SerialCharToSend[50] = ".detratS slennahC 81 ovreSDH";
static int SerialNbOfCharToSend = 0;  //0= none, 1 = [0], 2 = [1] and so on...

// LUT
const uint16_t a_sin[1000] PROGMEM = 
{
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
		2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4,
		4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
		5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7,
		7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
		8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10,
		10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11,
		11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
		13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14,
		14, 14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
		15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17,
		17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
		18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20, 20, 20, 20,
		20, 20, 20, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
		22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23, 23, 23,
		23, 23, 23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 25, 25,
		25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26,
		26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28,
		28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
		30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 31, 31, 31, 31, 31, 31, 31, 31, 31,
		31, 31, 31, 31, 31, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 33, 33, 33, 33, 33,
		33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 35,
		35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
		36, 36, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 38, 38, 38, 38, 38, 38, 38, 38, 38,
		38, 38, 38, 38, 38, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 40, 40, 40, 40, 40, 40, 40,
		40, 40, 40, 40, 40, 40, 40, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 42, 42, 42, 42, 42,
		42, 42, 42, 42, 42, 42, 42, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 43, 44, 44, 44, 44, 44,
		44, 44, 44, 44, 44, 44, 44, 44, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 46, 46, 46, 46, 46,
		46, 46, 46, 46, 46, 46, 46, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 48, 48, 48, 48, 48, 48,
		48, 48, 48, 48, 48, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 50, 50, 50, 50, 50, 50, 50, 50,
		50, 50, 50, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 53,
		53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 55, 55, 55, 55, 55,
		55, 55, 55, 55, 55, 56, 56, 56, 56, 56, 56, 56, 56, 56, 57, 57, 57, 57, 57, 57, 57, 57, 57, 57, 58,
		58, 58, 58, 58, 58, 58, 58, 58, 59, 59, 59, 59, 59, 59, 59, 59, 59, 60, 60, 60, 60, 60, 60, 60, 60,
		61, 61, 61, 61, 61, 61, 61, 61, 62, 62, 62, 62, 62, 62, 62, 62, 62, 63, 63, 63, 63, 63, 63, 63, 64,
		64, 64, 64, 64, 64, 64, 64, 65, 65, 65, 65, 65, 65, 65, 66, 66, 66, 66, 66, 66, 66, 67, 67, 67, 67,
		67, 67, 67, 68, 68, 68, 68, 68, 68, 69, 69, 69, 69, 69, 69, 70, 70, 70, 70, 70, 70, 71, 71, 71, 71,
		71, 71, 72, 72, 72, 72, 72, 73, 73, 73, 73, 73, 74, 74, 74, 74, 75, 75, 75, 75, 75, 76, 76, 76, 76,
		77, 77, 77, 77, 78, 78, 78, 79, 79, 79, 80, 80, 80, 81, 81, 81, 82, 82, 83, 83, 84, 84, 85, 86, 87
};

const uint16_t a_cos[1000] PROGMEM = {
		90, 89, 89, 89, 89, 89, 89, 89, 89, 89, 89, 89, 89, 89, 89, 89, 89, 89, 88, 88, 88, 88, 88, 88, 88,
		88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 87, 87, 87, 87, 87, 87, 87, 87, 87, 87, 87, 87, 87, 87, 87,
		87, 87, 87, 86, 86, 86, 86, 86, 86, 86, 86, 86, 86, 86, 86, 86, 86, 86, 86, 86, 85, 85, 85, 85, 85,
		85, 85, 85, 85, 85, 85, 85, 85, 85, 85, 85, 85, 85, 84, 84, 84, 84, 84, 84, 84, 84, 84, 84, 84, 84,
		84, 84, 84, 84, 84, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 83, 82, 82, 82,
		82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 81, 81, 81, 81, 81, 81, 81, 81, 81, 81,
		81, 81, 81, 81, 81, 81, 81, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 79,
		79, 79, 79, 79, 79, 79, 79, 79, 79, 79, 79, 79, 79, 79, 79, 79, 78, 78, 78, 78, 78, 78, 78, 78, 78,
		78, 78, 78, 78, 78, 78, 78, 78, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
		76, 76, 76, 76, 76, 76, 76, 76, 76, 76, 76, 76, 76, 76, 76, 76, 76, 75, 75, 75, 75, 75, 75, 75, 75,
		75, 75, 75, 75, 75, 75, 75, 75, 75, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74,
		74, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73, 72, 72, 72, 72, 72, 72, 72,
		72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71,
		71, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 70, 69, 69, 69, 69, 69, 69, 69,
		69, 69, 69, 69, 69, 69, 69, 69, 69, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68,
		67, 67, 67, 67, 67, 67, 67, 67, 67, 67, 67, 67, 67, 67, 67, 67, 66, 66, 66, 66, 66, 66, 66, 66, 66,
		66, 66, 66, 66, 66, 66, 66, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 64, 64,
		64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63,
		63, 63, 63, 63, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 62, 61, 61, 61, 61, 61,
		61, 61, 61, 61, 61, 61, 61, 61, 61, 61, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60,
		60, 59, 59, 59, 59, 59, 59, 59, 59, 59, 59, 59, 59, 59, 59, 59, 58, 58, 58, 58, 58, 58, 58, 58, 58,
		58, 58, 58, 58, 58, 57, 57, 57, 57, 57, 57, 57, 57, 57, 57, 57, 57, 57, 57, 57, 56, 56, 56, 56, 56,
		56, 56, 56, 56, 56, 56, 56, 56, 56, 56, 55, 55, 55, 55, 55, 55, 55, 55, 55, 55, 55, 55, 55, 55, 54,
		54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 54, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53, 53,
		53, 53, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 51, 51, 51, 51, 51, 51, 51, 51, 51,
		51, 51, 51, 51, 51, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 49, 49, 49, 49, 49, 49, 49,
		49, 49, 49, 49, 49, 49, 49, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 47, 47, 47, 47, 47,
		47, 47, 47, 47, 47, 47, 47, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 45, 45, 45, 45, 45,
		45, 45, 45, 45, 45, 45, 45, 45, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 43, 43, 43, 43, 43,
		43, 43, 43, 43, 43, 43, 43, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 41, 41, 41, 41, 41, 41,
		41, 41, 41, 41, 41, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 39, 39, 39, 39, 39, 39, 39, 39,
		39, 39, 39, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 36,
		36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 34, 34, 34, 34, 34,
		34, 34, 34, 34, 34, 33, 33, 33, 33, 33, 33, 33, 33, 33, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 31,
		31, 31, 31, 31, 31, 31, 31, 31, 30, 30, 30, 30, 30, 30, 30, 30, 30, 29, 29, 29, 29, 29, 29, 29, 29,
		28, 28, 28, 28, 28, 28, 28, 28, 27, 27, 27, 27, 27, 27, 27, 27, 27, 26, 26, 26, 26, 26, 26, 26, 25,
		25, 25, 25, 25, 25, 25, 25, 24, 24, 24, 24, 24, 24, 24, 23, 23, 23, 23, 23, 23, 23, 22, 22, 22, 22,
		22, 22, 22, 21, 21, 21, 21, 21, 21, 20, 20, 20, 20, 20, 20, 19, 19, 19, 19, 19, 19, 18, 18, 18, 18,
		18, 18, 17, 17, 17, 17, 17, 16, 16, 16, 16, 16, 15, 15, 15, 15, 14, 14, 14, 14, 14, 13, 13, 13, 13,
		12, 12, 12, 12, 11, 11, 11, 10, 10, 10, 9, 9, 9, 8, 8, 8, 7, 7, 6, 6, 5, 5, 4, 3, 2
};

uint16_t sinTable16[] = { 
		0, 1144, 2287, 3430, 4571, 5712, 6850, 7987, 9121, 10252, 11380,
		12505, 13625, 14742, 15854, 16962, 18064, 19161, 20251, 21336, 22414,
		23486, 24550, 25607, 26655, 27696, 28729, 29752, 30767, 31772, 32768,

		33753, 34728, 35693, 36647, 37589, 38521, 39440, 40347, 41243, 42125,
		42995, 43851, 44695, 45524, 46340, 47142, 47929, 48702, 49460, 50203,
		50930, 51642, 52339, 53019, 53683, 54331, 54962, 55577, 56174, 56755,

		57318, 57864, 58392, 58902, 59395, 59869, 60325, 60763, 61182, 61583,
		61965, 62327, 62671, 62996, 63302, 63588, 63855, 64103, 64331, 64539,
		64728, 64897, 65047, 65176, 65286, 65375, 65445, 65495, 65525, 65535,
};


// Fast trigonometry
int fast_sin(int x)
{
	boolean pos = true;  // positive - keeps an eye on the sign.
	uint8_t idx;
	// remove next 6 lines for fastestl!
	if (x < 0)
	{
		x = -x;
		pos = !pos;
	}
	if (x >= 360) x %= 360;
	if (x > 180)
	{
		idx = x - 180;
		pos = !pos;
	}
	else idx = x;
	if (idx > 90) idx = 180 - idx;
	if (pos) return sinTable16[idx]/2 ;
	return -(sinTable16[idx]/2);
};

int fast_cos(long x)
{
	return fast_sin(x+90);
};

int fast_atan2(int opp, int adj) {
	float hypt = sqrt(adj * adj + opp * opp);
	unsigned int idx = adj / hypt * 1000;
	int res = pgm_read_word_near(a_cos + idx);
	if(opp < 0) {
		res = -res;
	}
	return res;
}

// leg and body measurements in mm

static const uint8_t COXA_LENGTH = 13;
static const uint8_t FEMUR_LENGTH = 53;
static const uint8_t TIBIA_LENGTH = 90;
static const uint16_t COXA_LENGTH_SQ = COXA_LENGTH*COXA_LENGTH;
static const uint16_t FEMUR_LENGTH_SQ = FEMUR_LENGTH*FEMUR_LENGTH;
static const uint16_t TIBIA_LENGTH_SQ = TIBIA_LENGTH*TIBIA_LENGTH;

// Servo and servo angle arrays
static double servoAnglesOld[18] = {101,129,30,   93,128,33,   108,109,38,   102,60,143,   92,57,148,   90,50,150};
static double servoAngles[18] = {0,43,-36,
								0,42,-36,
								0,38,-32,
								0,-31,21,
								0,-29,24,
								0,-30,26};
static const double servoZeroAngles[18] = 	{101,86,66,
											93,86,69,
											108,71,70,
											102,91,122,
											92,86,124,
											90,80,124};

static const double coxaAngOffset[6] = {45,90,135,-135,-90,-45};

static const char tibia1[3] = {2,7,13};
static const char tibia1dir[3] = {1,1,-1};

static const char tibia2[3] = {4,10,16};
static const char tibia2dir[3] = {1,-1,-1};

static const char femur1[3] = {1,7,13};
static const char femur1dir[3] = {1,1,-1};

static const char femur2[3] = {4,10,16};
static const char femur2dir[3] = {1,-1,-1};

static const char coxa1[3] = {0,6,12};
static const char coxa1dir[3] = {1,1,-1};

static const char coxa2[3] = {3,9,15};
static const char coxa2dir[3] = {1,-1,-1};


// TEST
static int dir = 1;
static int stepTime = 2500;


// Startpositions for legs
static int pos_x[6] = {-50,-70,-50,50,70,50};
static int pos_y[6] = {38,0,-38,-38,0,38};
static int pos_z[6] = {-80,-80,-80,-80,-80,-80};

// movement patterns: walk - 4 iterations for 2 sides
// it1: set1 forward and up, set2 back
// it2: set1 down
// it3: set1 back, set2 forward and up
// it4: set2 down
static int walk_x[4][2] = {{0,0},
		{0,0},
		{0,0},
		{0,0}};

static int walk_y[4][2] = {{20,-20},	// set1 forward, set2 back
		{0,0},		// wait
		{-20,20},	// set1 back, set2 forward
		{0,0}};		// wait

static int walk_z[4][2] = {{10,0},
		{-10,0},
		{0,10},
		{0,-10}};

// other constants
static const int SET_1 = 0;
static const int SET_2 = 1;

// Formulas
double leg_length(int x, int y) {
	return sqrt(x*x+y*y);
}

double hf(double leg_length, int z) {
	return sqrt((leg_length-COXA_LENGTH)*(leg_length-COXA_LENGTH)+z*z);
}

double a1(double leg_length, int z) {
	return atan((leg_length-COXA_LENGTH)*1.0/abs(z)) * 180.0 / M_PI;
}

double a2(double hf) {
	double idx = (FEMUR_LENGTH_SQ + hf * hf - TIBIA_LENGTH_SQ) / (2.0 * FEMUR_LENGTH * hf);
	return acos(idx) * (180.0 / M_PI);
}

double femur_angle(double a1, double a2) {
	return a1+a2-90.0;
}

double b1(double hf) {
	double idx = (TIBIA_LENGTH_SQ + FEMUR_LENGTH_SQ - hf * hf) / (2.0 * FEMUR_LENGTH * TIBIA_LENGTH);
	return acos(idx) * (180.0 / M_PI);
}

double tibia_angle(double b1) {
	return 90-b1;
}

double coxa_angle(double y, double x) {
	return atan2(y, x) * (180.0 / M_PI);
	// return fast_atan2(y, x);
}

// interpolation
// TODO later


void ServoMove(int Channel, long PulseHD, long SpeedHD, long Time)
{
	// Use ServoMove(int Channel, long PulseHD, long SpeedHD, long Time) to control servos.
	// One of th SpeedHD or Time can be set to 0 to only  use the other one for speed. If both are used,
	// the one that takes the longest time, will be used
	ServoGroupMove(Channel, CheckRange(PulseHD), SpeedHD, Time);
	ServoGroupMoveActivate();
}

void ServoMoveAngle(int Channel, double angle, long Time)
{
	long pulse = angle * 177.77 + 8000;
	Serial.print("Servo: ");
	Serial.print(Channel);
	Serial.print(", Angle: ");
	Serial.println(angle);
	ServoGroupMove(Channel, CheckRange(pulse), 0, Time);
	ServoGroupMoveActivate();
}

void moveAllServos(long time)
{
	for(int i = 0; i < 18 ; i++)
	{
		ServoMoveAngle(i, servoAngles[i]+servoZeroAngles[i], time);
	}
	delay(time);
}

void setup()
{
	ServoSetup();				//Initiate timers and misc.

	for(int i = 0; i < 18 ; i++)
	{
		servoAngles[i] = 0;
	}
//	moveAllServos(500);
//	delay(1000);

	for(int i = 0; i < 2 ; i++)
	{
//		moveLegSet(i, SET_1, true);
	}
//	moveAllServos(stepTime);
}

void loop()
{
	if(Serial.available() > 0)
	{
		OneByOne(Serial.read());
	}
	for(int i = 0; i < 4 ; i++) // 4 steps per cycle
	{
//		moveLegSet(i+2, SET_1, true);
//		moveLegSet(i, SET_2, true);
//		moveAllServos(stepTime);
	}
	for(int i = 0; i < 18 ; i++)
	{
		ServoMoveAngle(i, servoAnglesOld[i], 500);
	}
	delay(500);
	for(int i = 0; i < 18 ; i++)
	{
		ServoMoveAngle(i, servoAngles[i], 500);
	}
	delay(500);
}

void moveLegSet(int stepNum, int side, int walk)
{
	stepNum = (sizeof(walk_x) + stepNum) % sizeof(walk_x);
	Serial.print("stepNum: "); Serial.println(stepNum);
	for(int i = 0; i < 3 ; i++)
	{
		if(walk) {
			pos_x[i*2+side] = pos_x[i*2+side] + walk_x[stepNum][side];
			pos_y[i*2+side] = pos_y[i*2+side] + walk_y[stepNum][side];
			pos_z[i*2+side] = pos_z[i*2+side] + walk_z[stepNum][side];
		}
//		printPositions(i, side);
		double leg_l = leg_length(pos_x[i*2+side], pos_y[i*2+side]);
		double h_f = hf(leg_l, pos_z[i*2+side]);
		double a_1 = a1(leg_l, pos_z[i*2+side]);
		double a_2 = a2(h_f);
		double f_ang = 90.0-(a_1+a_2);
		double b_1 = b1(h_f);
		double t_ang = 90.0-b_1;
		double c_ang = (atan2(pos_x[i*2+side],pos_y[i*2+side]) * 180.0 / M_PI) + coxaAngOffset[i*2+side];
//		printAngles(leg_l, h_f, a_1, a_2, f_ang, b_1, t_ang, c_ang);
		servoAngles[i*6+side*3] = c_ang;
		servoAngles[i*6+side*3+1] = f_ang;
		servoAngles[i*6+side*3+2] = t_ang;
	}
	Serial.println();
	Serial.println();
}

void printAngles(double leg_l, double h_f, double a_1, double a_2, double f_ang, double b_1, double t_ang, double c_ang) {
	Serial.print("leg_l: ");
	Serial.println(leg_l);
	Serial.print("h_f: ");
	Serial.println(h_f);
	Serial.print("a_1: ");
	Serial.println(a_1);
	Serial.print("a_2: ");
	Serial.println(a_2);
	Serial.print("f_ang: ");
	Serial.println(f_ang);
	Serial.print("b_1: ");
	Serial.println(b_1);
	Serial.print("t_ang: ");
	Serial.println(t_ang);
	Serial.print("c_ang: ");
	Serial.println(c_ang);
}

void printPositions(int i, int side) {
	Serial.print("posX-");
	Serial.print(i*2+side);
	Serial.print(": ");
	Serial.println(pos_x[i*2+side]);
	Serial.print("posY-");
	Serial.print(i*2+side);
	Serial.print(": ");
	Serial.println(pos_y[i*2+side]);
	Serial.print("posZ-");
	Serial.print(i*2+side);
	Serial.print(": ");
	Serial.println(pos_z[i*2+side]);
}

void OneByOne(char input)
{
	if(input == 'p')
	{
		dir = 1;
	}
	else if(input == 'l')
	{
		dir = -1;
	}
	else if(input == '1')
	{
		servoAngles[0] = servoAngles[0] + dir;
	}
	else if(input == '2')
	{
		servoAngles[1] = servoAngles[1] + dir;
	}
	else if(input == '3')
	{
		servoAngles[2] = servoAngles[2] + dir;
	}
	else if(input == '4')
	{
		servoAngles[3] = servoAngles[3] + dir;
	}
	else if(input == '5')
	{
		servoAngles[4] = servoAngles[4] + dir;
	}
	else if(input == '6')
	{
		servoAngles[5] = servoAngles[5] + dir;
	}
	else if(input == '7')
	{
		servoAngles[6] = servoAngles[6] + dir;
	}
	else if(input == '8')
	{
		servoAngles[7] = servoAngles[7] + dir;
	}
	else if(input == '9')
	{
		servoAngles[8] = servoAngles[8] + dir;
	}
	else if(input == 'q')
	{
		servoAngles[9] = servoAngles[9] + dir;
	}
	else if(input == 'w')
	{
		servoAngles[10] = servoAngles[10] + dir;
	}
	else if(input == 'e')
	{
		servoAngles[11] = servoAngles[11] + dir;
	}
	else if(input == 'r')
	{
		servoAngles[12] = servoAngles[12] + dir;
	}
	else if(input == 't')
	{
		servoAngles[13] = servoAngles[13] + dir;
	}
	else if(input == 'y')
	{
		servoAngles[14] = servoAngles[14] + dir;
	}
	else if(input == 'u')
	{
		servoAngles[15] = servoAngles[15] + dir;
	}
	else if(input == 'i')
	{
		servoAngles[16] = servoAngles[16] + dir;
	}
	else if(input == 'o')
	{
		servoAngles[17] = servoAngles[17] + dir;
	}
	for(int i = 0; i < 18 ; i++)
	{
		ServoMoveAngle(i, servoAngles[i], 100);
	}
}


long CheckRange(long PulseHDValue)
{
	if(PulseHDValue > 39680) return 39680;
	else if(PulseHDValue < 8320) return 8320;
	else return PulseHDValue;
}

long CheckChannelRange(long CheckChannel)
{
	if(CheckChannel >= HDServoMode) return (HDServoMode-1);
	else if(CheckChannel < 0) return 0;
	else return CheckChannel;
}

void ServoGroupMove(int Channel, long PulseHD, long SpeedHD, long Time)    //ServoMove used by serial command interpreter
{
	long StepsToGoSpeed=0;
	long StepsToGoTime=0;

	ServoGroupChannel[ServoGroupNbOfChannels] = Channel;
	if(SpeedHD < 1) SpeedHD = 3200000;
	StepsToGoSpeed = abs((PulseHD - ServoPW[Channel]) / (SpeedHD / 50));
	StepsToGoTime = Time / 20;
	if(StepsToGoSpeed > ServoGroupStepsToGo) ServoGroupStepsToGo = StepsToGoSpeed;
	if(StepsToGoTime > ServoGroupStepsToGo) ServoGroupStepsToGo = StepsToGoTime;
	ServoGroupChannel[ServoGroupNbOfChannels] = Channel;
	ServoGroupServoLastPos[ServoGroupNbOfChannels] = PulseHD;
	ServoGroupNbOfChannels++;
}

void ServoGroupMoveActivate()                       //ServoMove used by serial command interpreter
{
	int ServoCount = 0;

	for(ServoCount = 0 ; ServoCount < ServoGroupNbOfChannels ; ServoCount++)
	{
		ServoStepsHD[ServoGroupChannel[ServoCount]] = (ServoGroupServoLastPos[ServoCount] - ServoPW[ServoGroupChannel[ServoCount]]) / ServoGroupStepsToGo;
		StepsToGo[ServoGroupChannel[ServoCount]] =ServoGroupStepsToGo;
		ServoLastPos[ServoGroupChannel[ServoCount]] = ServoGroupServoLastPos[ServoCount];
	}
	ServoGroupNbOfChannels = 0;
	ServoGroupStepsToGo = 0;
}

void RealTime50Hz() //Move servos every 20ms to the desired position.
{
	if(SerialNbOfCharToSend) { SerialNbOfCharToSend--; Serial.print(SerialCharToSend[SerialNbOfCharToSend]);}
	for(ChannelCount = 0; ChannelCount < 20; ChannelCount++)
	{
		if(StepsToGo[ChannelCount] > 0)
		{
			ServoPW[ChannelCount] += ServoStepsHD[ChannelCount];
			StepsToGo[ChannelCount] --;
		}
		else if(StepsToGo[ChannelCount] == 0)
		{
			ServoPW[ChannelCount] = ServoLastPos[ChannelCount];
			StepsToGo[ChannelCount] --;
		}
	}
}

ISR(TIMER1_COMPA_vect) // Interrupt routine for timer 1 compare A. Used for timing each pulse width for the servo PWM.
{
	*OutPort1A &= ~OutBit1A;                //Pulse A finished. Set to low
}

ISR(TIMER1_COMPB_vect) // Interrupt routine for timer 1 compare A. Used for timing each pulse width for the servo PWM.
{
	*OutPort1B &= ~OutBit1B;                //Pulse B finished. Set to low
}

ISR(TIMER3_COMPA_vect) // Interrupt routine for timer 0 compare A. Used for timing 50Hz for each servo.
{
	*OutPortNext1A |= OutBitNext1A;         // Start new pulse on next servo. Write pin HIGH
	*OutPortNext1B |= OutBitNext1B;         // Start new pulse on next servo. Write pin HIGH
}

ISR(TIMER3_COMPB_vect) // Interrupt routine for timer 0 compare A. Used for timing 50Hz for each servo.
{
	TIFR1 = 255;                                       // Clear  pending interrupts
	TCNT1 = 0;                                         // Restart counter for timer1
	TCNT3 = 0;                                         // Restart counter for timer0
	sei();
	*OutPort1A &= ~OutBit1A;                           // Set pulse low to if not done already
	*OutPort1B &= ~OutBit1B;                           // Set pulse low to if not done already
	OutPort1A = OutPortTable[Timer0Toggle];            // Temp port for COMP1A
	OutBit1A = OutBitTable[Timer0Toggle];              // Temp bitmask for COMP1A
	OutPort1B = OutPortTable[Timer0Toggle+10];         // Temp port for COMP1B
	OutBit1B = OutBitTable[Timer0Toggle+10];           // Temp bitmask for COMP1B
	OCR1A = ServoPW[Timer0Toggle]-7980;
	OCR1B = ServoPW[Timer0Toggle+10]-7965;
	Timer0Toggle++;                                    // Next servo in line.
	if(Timer0Toggle==10)
	{
		Timer0Toggle = 0;                                // If next servo is grater than 9, start on 0 again.
		RealTime50Hz();                                  // Do servo management
	}
	OutPortNext1A = OutPortTable[Timer0Toggle];        // Next Temp port for COMP1A
	OutBitNext1A = OutBitTable[Timer0Toggle];          // Next Temp bitmask for COMP1A
	OutPortNext1B = OutPortTable[Timer0Toggle+10];     // Next Temp port for COMP1B
	OutBitNext1B = OutBitTable[Timer0Toggle+10];       // Next Temp bitmask for COMP1B
}

void ServoSetup()
{
	// Timer 1 setup(16 bit):
	TCCR1A = 0;                     // Normal counting mode
	TCCR1B = 1;                     // Set prescaler to 1
	TCNT1 = 0;                      // Clear timer count
	TIFR1 = 255;                    // Clear  pending interrupts
	TIMSK1 = 6;                     // Enable the output compare A and B interrupt
	// Timer 0 setup(8 bit):
	TCCR3A = 0;                     // Normal counting mode
	TCCR3B = 4;                     // Set prescaler to 256
	TCNT3 = 0;                      // Clear timer count
	TIFR3 = 255;                    // Clear pending interrupts
	TIMSK3 = 6;                     // Enable the output compare A and B interrupt
	OCR3A = 93;                     // Set counter A for about 500us before counter B below;
	OCR3B = 124;                    // Set counter B for about 2000us (20ms/10, where 20ms is 50Hz);

	for(int iCount=2;iCount<14;iCount++) pinMode(iCount, OUTPUT);    // Set all pins used to output:
	OutPortTable[18] = &PORTC;    // In 18 channel mode set channel 18 and 19 to a dummy pin that does not exist.
	OutPortTable[19] = &PORTC;
	OutBitTable[18] = 128;
	OutBitTable[19] = 128;
	Serial.begin(SerialInterfaceSpeed);
	SerialNbOfCharToSend = 28;
	DDRF = 63;                      //Set analog pins A0 - A5 as digital output also.
}

