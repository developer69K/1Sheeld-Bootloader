/*

                           e Y8b    Y8b YV4.08P888 88e
                          d8b Y8b    Y8b Y888P 888 888D
                         d888b Y8b    Y8b Y8P  888 88"
                        d888WuHan8b    Y8b Y   888 b,
                       d8888888b Y8b    Y8P    888 88b,
           8888 8888       ,e,                                  888
           8888 888820088e  " Y8b Y888P ,e e, 888,8, dP"Y ,"Y88b888
           8888 8888888 88b888 Y8b Y8P d88 88b888 " C88b "8" 888888
           8888 8888888 888888  Y8b "  888   ,888    Y88D,ee 888888
           'Y88 88P'888 888888   Y8P    "YeeP"888   d,dP "88 888888
   888 88b,                    d8  888                     888
   888 88P' e88 88e  e88 88e  d88  888 e88 88e  ,"Y88b e88 888 ,e e, 888,8,
   888 8K  d888 888bd888 8Shaoziyang88d888 888b"8" 888d888 888d88 88b888 "
   888 88b,Y888 888PY888 888P 888  888Y888 888P,ee 888Y888 888888   ,888
   888 88P' "88 88"  "88 88"  888  888 "88 88" "88 888 "88 888 "YeeP"888


  Project:       AVR Universal BootLoader
  File:          bootldr.c
                 main program code
  Version:       4.0

  Compiler:      WinAVR 20071221 + AVR Studio 4.14.589

  Author:        Shaoziyang
                 Shaoziyang@gmail.com
                 http://avrubd.googlepages.com
                 http://sourceforge.net/projects/avrub

  Date:          2008.4

  See readme.htm to get more information.

*/
#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#ifndef BAUD
# define BAUD 9600
#endif

#include "bootcfg.h"
#include "bootldr.h"
//#include <stdio.h>
#include <util/setbaud.h>
#include <util/delay.h>

//user's application start address
#define PROG_START         0x0000

//receive buffer' size will not smaller than SPM_PAGESIZE
#if (BUFFERSIZE < SPM_PAGESIZE)
#define BUFSIZE SPM_PAGESIZE
#else
#define BUFSIZE BUFFERSIZE
#endif

//define receive buffer
unsigned char buf[BUFSIZE];

#if VERBOSE
//Verbose msgs
const char msg1[] = "1";//"waiting for password";
const char msg2[] = "2";//"timeout";
const char msg3[] = "3";//"waiting for data";
const char msg4[] = "4";//"update success";
const char msg5[] = "5";//"update fail";
const char msg6[] = "6";//"enter boot mode";
const char msg7[] = "7";//"execute user application";
const char msg8[]  = "8";//"get package number";
const char msg9[]  = "9";//"received a full data frame";
const char msg10[] = "10";//"get checksum";
const char msg11[] = "11";//"calculate checksum";
const char msg12[] = "12";//"avoid write to boot section";
const char msg13[] = "13";//"decrypt buffer";
const char msg14[] = "14";//"Flash page full, write flash page;otherwise receive next frame";
const char msg15[] = "15";//"write data to Flash";
const char msg16[] = "16";//"modify Flash page address 16";
const char msg17[] = "17";//"receive one frame, write multi pages";
const char msg18[] = "18";//"modify Flash page address 18";
const char msg19[] = "19";//"reset receive pointer";
const char msg20[] = "20";//"read flash, and compare with buffer's content";
const char msg21[] = "21";//"enable application section";
const char msg22[] = "22";//"clear error flag";
const char msg23[] = "23";//"set error flag";
const char msg24[] = "24";//"checksum equal, send ACK";
const char msg25[] = "25";//"checksum error, ask resend";
const char msg26[] = "26";//"don't need verify, send ACK directly";
const char msg27[] = "27";//"no verify, send ACK directly";
const char msg28[] = "28";//"clear watchdog";
const char msg29[] = "29";//"LED indicate update status";
const char msg30[] = "30";//"require resend";
const char msg31[] = "31";//"too many error, abort update";
const char msg32[] = "32";//"dead loop, wait watchdog reset";
const char msg33[] = "33";//"quit bootloader";
#endif
#if (BUFSIZE > 255)
unsigned int bufptr, pagptr;
#else
unsigned char bufptr, pagptr;
#endif

unsigned char ch, cl, SOHChar;

//Flash address
#if (FLASHEND > 0xFFFFUL)
unsigned long int FlashAddr;
#else
unsigned int FlashAddr;
#endif

//include decrypt subroutine file
#if Decrypt

//PC1 decrypt algorithm subroutine
#if (Algorithm == 0)||(Algorithm == 1)

#include "pc1crypt.c"

#else

#error "Unknow encrypt algorithm!"

#endif

#endif  //Decrypt


//write one Flash page
void write_one_page(unsigned char *buf)
{
  boot_page_erase(FlashAddr);                  //erase one Flash page
  boot_spm_busy_wait();
  for(pagptr = 0; pagptr < SPM_PAGESIZE; pagptr += 2) //fill data to Flash buffer
  {
    boot_page_fill(pagptr, buf[pagptr] + (buf[pagptr + 1] << 8));
  }
  boot_page_write(FlashAddr);                  //write buffer to one Flash page
  boot_spm_busy_wait();                        //wait Flash page write finish
}

//jump to user's application
void quit()
{
#if Decrypt
    DestroyKey();                              //delete decrypt key
#endif

  boot_rww_enable();                           //enable application section
  (*((void(*)(void))PROG_START))();            //jump
}

//send a byte to comport
void WriteCom(unsigned char dat)
{
#if RS485
  RS485Enable();
#endif

  UDRREG(COMPORTNo) = dat;
  //wait send finish
  while(!(UCSRAREG(COMPORTNo) & (1<<TXCBIT(COMPORTNo))));
  UCSRAREG(COMPORTNo) |= (1 << TXCBIT(COMPORTNo));

#if RS485
  RS485Disable();
#endif
}

//wait receive a data from comport
unsigned char WaitCom()
{
  while(!DataInCom());
  return ReadCom();
}

#if VERBOSE
//send a string to uart
void putstr(const char *str)
{
  while(*str)
    WriteCom(*str++);

  WriteCom(0x0D);
  WriteCom(0x0A);
}
#endif

//calculate CRC checksum
void crc16(unsigned char *buf)
{
#if (BUFSIZE > 255)
  unsigned int j;
#else
  unsigned char j;
#endif

#if (CRCMODE == 0)
  unsigned char i;
  unsigned int t;
#endif
  unsigned int crc;

  crc = 0;
  for(j = BUFFERSIZE; j > 0; j--)
  {
#if (CRCMODE == 0)
    //CRC1021 checksum
    crc = (crc ^ (((unsigned int) *buf) << 8));
    for(i = 8; i > 0; i--)
    {
      t = crc << 1;
      if(crc & 0x8000)
        t = t ^ 0x1021;
      crc = t;
    }
#elif (CRCMODE == 1)
    //word add up checksum
    crc += (unsigned int)(*buf);
#else
#error "Unknow CRCMODE!"
#endif
    buf++;
  }
  ch = crc / 256;
  cl = crc % 256;
}

//Main routine
int main(void)
{
  unsigned char cnt;
  unsigned char packNO;
  #if   (CRCMODE == 0)
	    unsigned char crch, crcl; 
  #elif (CRCMODE == 1)
	     unsigned char checksum;
  #endif
#if (InitDelay > 0)
#if (InitDelay > 255)
  unsigned int di;
#else
  unsigned char di;
#endif
#endif

#if (BUFFERSIZE > 255)
  unsigned int li;
#else
  unsigned char li;
#endif

  //disable interrupt
  cli();

#if WDGEn
  //if enable watchdog, setup timeout
  wdt_enable(WDTO_1S);
#else
  //disable watchdog
  MCUCSR = 0;
  wdt_disable();
#endif

  //initialize timer1, CTC mode
  TimerInit();

#if RS485
  //initialize RS485 port
  DDRREG(RS485PORT) |= (1 << RS485TXEn);
  RS485Disable();
#endif

#if LED_En
  //set LED control port to output
  DDRREG(LEDPORT) = (1 << LEDPORTNo);
#endif

  //initialize comport with special config value
  //ComInit();
  ////////////////////////Mohamed Samy////////////////////////////
  UBRR0H = (51>>8);                                           ////
  UBRR0L = 51;                                                ////
  UCSR0C = UCSR0C | (1<<URSEL0) | (1 << UCSZ00)|(1 << UCSZ01);////
  UCSR0B = UCSR0B | (1 << RXEN0) | (1 << TXEN0);              ////
  ///////////////////////Mohamed Samy/////////////////////////////

#if (InitDelay > 0)
  //some kind of avr mcu need special delay after comport initialization
  for(di = InitDelay; di > 0; di--)
    __asm__ __volatile__ ("nop": : );
#endif

#if LEVELMODE
  //according port level to enter bootloader
  //set port to input
  DDRREG(LEVELPORT) &= ~(1 << LEVELPIN);
#if PINLEVEL
  if(PINREG(LEVELPORT) & (1 << LEVELPIN))
#else
  if(!(PINREG(LEVELPORT) & (1 << LEVELPIN)))
#endif
  {
#if VERBOSE
    //prompt enter boot mode
    putstr(msg6);
#endif
  }
  else
  {
#if VERBOSE
    //prompt execute user application
    putstr(msg7);
#endif

    quit();
  }

#else
  //comport launch boot

#if VERBOSE
  //prompt waiting for password
  putstr(msg1);
#endif

  cnt = TimeOutCnt;
  cl = 0;
  while(1)
  {
	  
#if WDG_En
    //clear watchdog
    wdt_reset();
#endif

    if(TIFRREG & (1<<OCF1A))    //T1 overflow
    {
      TIFRREG |= (1 << OCF1A);

      if(cl == CONNECTCNT)      //determine Connect Key
        break;

#if LED_En
      LEDAlt();                 //toggle LED
#endif

      cnt--;
      if(cnt == 0)              //connect timeout
      {

#if VERBOSE
        putstr(msg2);           //prompt timeout
#endif
        quit();                 //quit bootloader
      }
    }

    if(DataInCom())             //receive connect key
    {
      if(ReadCom() == KEY[cl])  //compare ConnectKey
        cl++;
      else
        cl = 0;
    }
  }

#endif  //LEVELMODE

#if VERBOSE
  putstr(msg3);                 //prompt waiting for data
#endif

  //every interval send a "C",waiting XMODEM control command <soh>
  cnt = TimeOutCntC;
  while(1)
  {
    if(TIFRREG & (1 << OCF1A))  //T1 overflow
    {
      TIFRREG |= (1 << OCF1A);
      WriteCom(XMODEM_RWC) ;    //send "C"

#if LED_En
      LEDAlt();                 //toggle LED
#endif

      cnt--;
      if(cnt == 0)              //timeout
      {
#if VERBOSE
        putstr(msg2);           //prompt timeout
#endif
        quit();                 //quit bootloader
      }
    }

#if WDG_En
    wdt_reset();                //clear watchdog
#endif

    if(DataInCom())
    {
      if(ReadCom() == XMODEM_SOH)  //XMODEM command <soh>
        break;
    }
  }

  TCCR1B = 0;                   //close timer1

#if Decrypt
  DecryptInit();
#endif

  //begin to receive data
  packNO = 0;
  bufptr = 0;
  cnt = 0;
  FlashAddr = 0;
  do
  {
    packNO++;
    //#if VERBOSE
	//    putstr(msg8);					      //get package number	
	//#endif
	ch =  WaitCom();                          //get package number
    cl = ~WaitCom();
    if ((packNO == ch) && (packNO == cl))
    {
	//  #if VERBOSE	
	 //     putstr(msg9);                       //received a full data frame	
     // #endif
      for(li = 0; li < BUFFERSIZE; li++)      //receive a full data frame
      {
        buf[bufptr] = WaitCom();
        bufptr++;
        //putstr("received ");
		//WriteCom(bufaccess);
      }
	//  #if VERBOSE
	//      putstr(msg10);                      //get checksum
     // #endif
      #if    (CRCMODE == 0)
             crch = WaitCom();                       //get checksum
             crcl = WaitCom();
	  #elif  (CRCMODE == 1)
             checksum =  WaitCom();
      #endif
      //#if VERBOSE
	  //    putstr(msg11);                      //calculate checksum
     // #endif
	  crc16(&buf[bufptr - BUFFERSIZE]);       //calculate checksum
      #if   (CRCMODE  == 0)
      if((crch == ch) && (crcl == cl))
      #elif (CRCMODE == 1)
      if(checksum == cl) 
      #endif
	  {
#if BootStart
       // #if VERBOSE
       //     putstr(msg12);                    //avoid write to boot section
       // #endif
		if(FlashAddr < BootStart)             //avoid write to boot section
        {
#endif

#if Decrypt
         // #if VERBOSE
         //     putstr(msg13);                                   //decrypt buffer
         // #endif
		  DecryptBlock(&buf[bufptr - BUFFERSIZE], BUFFERSIZE); //decrypt buffer
#endif

#if (BUFFERSIZE <= SPM_PAGESIZE)
          
		  if(bufptr >= SPM_PAGESIZE)          //Flash page full, write flash page;otherwise receive next frame
          {                                   //receive multi frames, write one page
         //   #if VERBOSE
          //      putstr(msg14);                  //Flash page full, write flash page;otherwise receive next frame
           // #endif
           // #if VERBOSE
		   //     putstr(msg15);                //write data to Flash
		//	#endif
			write_one_page(buf);              //write data to Flash
		//	#if VERBOSE
		//	    putstr(msg16);                //modify Flash page address
         //   #endif
			FlashAddr += SPM_PAGESIZE;        //modify Flash page address
            bufptr = 0;
          }
#else
        //  #if VERBOSE
		 //     putstr(msg17);                  //receive one frame, write multi pages
         // #endif
		  while(bufptr > 0)                   //receive one frame, write multi pages
          {
            write_one_page(&buf[BUFSIZE - bufptr]);
		//	#if VERBOSE
	//			putstr(msg18);                //modify Flash page address
     //       #endif
			FlashAddr += SPM_PAGESIZE;        //modify Flash page address
            bufptr -= SPM_PAGESIZE;
          }
#endif

#if BootStart
        }
        else                                  //ignore flash write when Flash address exceed BootStart
        {
		//  #if VERBOSE	
	//		  putstr(msg19);                  //reset receive pointer	
     //     #endif
		  bufptr = 0;                         //reset receive pointer
        }
#endif
//#if VERBOSE
//	putstr(msg20);                            //read flash, and compare with buffer's content
//#endif
// read flash, and compare with buffer's content
#if (ChipCheck > 0) && (BootStart > 0)
#if (BUFFERSIZE < SPM_PAGESIZE)
        if((bufptr == 0) && (FlashAddr < BootStart))
#else
        if(FlashAddr < BootStart)
#endif
        {
//		  #if VERBOSE	
//			  putstr(msg21);	                  //enable application section 
//          #endif
		  boot_rww_enable();                  //enable application section
//		  #if VERBOSE
//			  putstr(msg22);                     //clear error flag
//          #endif
		  cl = 1;                             //clear error flag
          for(pagptr = 0; pagptr < BUFSIZE; pagptr++)
          {
#if (FLASHEND > 0xFFFFUL)
            if(pgm_read_byte_far(FlashAddr - BUFSIZE + pagptr) != buf[pagptr])
#else
            if(pgm_read_byte(FlashAddr - BUFSIZE + pagptr) != buf[pagptr])
#endif
            {
//			  #if VERBOSE	
///				  putstr(msg23);              //set error flag	
  //            #endif
			  cl = 0;                         //set error flag
			  //putstr("error at ");
			  //WriteCom(pagptr);
              break;
            }
          }
          if(cl)                              //checksum equal, send ACK
          {
	//		#if VERBOSE  
	//			putstr(msg24);                //checksum equal, send ACK  
     //       #endif
			WriteCom(XMODEM_ACK);
            cnt = 0;
          }
          else
          {
	//		#if VERBOSE  
	//			putstr(msg25);                //checksum error, ask resend  
     //       #endif
			WriteCom(XMODEM_NAK);             //checksum error, ask resend
            cnt++;                            //increase error counter
            FlashAddr -= BUFSIZE;             //modify Flash page address
          }
        }
        else                                  //don't need verify, send ACK directly
        {
		//  #if VERBOSE
		//	  putstr(msg26);                  //don't need verify, send ACK directly	
         // #endif
		  WriteCom(XMODEM_ACK);
          cnt = 0;
        }
#else
       // #if VERBOSE
	//		putstr(27);                       //no verify, send ACK directly
     //   #endif
		WriteCom(XMODEM_ACK);                 //no verify, send ACK directly
        cnt = 0;
#endif

#if WDG_En
      ///  #if VERBOSE
		//	putstr(28);                       //clear watchdog
        //#endif
		wdt_reset();                          //clear watchdog
#endif

#if LED_En
	    //#if VERBOSE
	//		putstr(msg29);                    //LED indicate update status
     //   #endif
		LEDAlt();                             //LED indicate update status
#endif
      }
      else //CRC
      {
//		#if VERBOSE  
//		    putstr(msg30);                    //require resend
 //       #endif
		WriteCom(XMODEM_NAK);                 //require resend
        cnt++;
      }
    }
    else //PackNo
    {
//	  #if VERBOSE	
//	      putstr(msg30);                      //require resend	
 //     #endif
	  WriteCom(XMODEM_NAK);                   //require resend
      cnt++;
    }
	
	if(cnt > 3)                               //too many error, abort update
    {
//        #if VERBOSE
 //           putstr(msg31);				          //too many error, abort update
  //      #endif
        break;
    }  
      
  }
  while(WaitCom() != XMODEM_EOT);
  //_delay_ms(10000);
  WriteCom(XMODEM_ACK);


#if VERBOSE
  if(cnt == 0)
  {
    //update success
    putstr(msg4);                             //prompt update success
  }
  else
  {
    // update fail
    putstr(msg5);                             //prompt update fail

#if WDG_En
   // #if VERBOSE
//		putstr(32);                          //dead loop, wait watchdog reset
 //   #endif
	while(1);                                //dead loop, wait watchdog reset
#endif

  }

#else

#if WDG_En
  if(cnt > 0)
    while(1);                                //when update fail, use dead loop wait watchdog reset
#endif

#endif
  #if VERBOSE
	  putstr(msg33);                          //quit bootloader
  #endif
  quit();                                     //quit bootloader
  return 0;
}

//End of file: bootldr.c