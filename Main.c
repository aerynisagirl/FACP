/************************************************************************
 *  Fire Alarm Panel - DIY Fire Alarm Control Panel for demonstrations  *
 *  Created by mikemadealarms on October 10, 2016 at 11:33 AM           *
 * -------------------------------------------------------------------- *
 *  Last modified by mikemadealarms on October 27, 2016 at 4:13 PM      *
 *  Last modification made was: Conditions will now restore if gone     *
 ************************************************************************/

/***********************
 *  MCU Configuration  *
 ***********************/

//CONFIG1 Register
#pragma config FOSC = INTRC_NOCLKOUT  //Configure the MCU to run on the internal RC-Oscillator with PORTA6 and PORTA7 as digital I/O
#pragma config WDTE = OFF             //Disable the bypass of the SWDTEN bit of WDTCON to enable the Watch-dog Timer to perform a software reset
#pragma config PWRTE = ON             //Enable the use of built in the power-up timer
#pragma config MCLRE = OFF             //Set PORTE3 to act as an external reset trigger to the MCU
#pragma config CP = OFF               //Disable the flash protection as it is completely unnecessary
#pragma config CPD = OFF              //Disable the RAM read protection as it is completely unnecessary
#pragma config BOREN = ON             //Enable resets to the MCU caused by the internal brown-out reset module
#pragma config IESO = ON              //Enable the internal/external switchover mode to enable the 2 speed start up mode
#pragma config FCMEN = ON            //Disable the fail-safe clock monitor as it is not needed
#pragma config LVP = OFF              //Set PORTB3 to act as digital I/O and use high voltage on MCLRE to program the MCU

//CONFIG2 Register
#pragma config BOR4V = BOR40V  //Set the brown-out reset threshold to 4V, anything below and the MCU will reset
#pragma config WRT = OFF       //Disable the flash memory self write protection as it is not needed

#include <xc.h>

/***************
 *  Variables  *
 ***************/

//Control Variables
unsigned char slcControl = 0x00;       //Determines if an SLC is disabled via software internally
unsigned char nacControl = 0xC0;       //First 4 bits determine the output state of the NAC, last 4 bits determine if it's disabled by software
unsigned char nacTypeControl = 0x0F;   //Determines the type of NAC, first 4 bits determines if it's silence-able, last 4 bits determine if it's used in pre-signal
unsigned char preAlarmControl = 0x00;  //Determines how the panel should behave in a pre-alarm condition, or if it should just throw a general alarm condition
unsigned char ledControl = 0x00;       //Controls the state of the LED's on the user interface, last bit are used for flashing the LED's
unsigned char coderControl = 0x03;     //Used to MUX coding patterns out to the NAC's, first 2 bits NAC 1 coding pattern, next 2 bits NAC 2 coding pattern, etc.

//Cause Tracking Variables
unsigned char preAlarmCause = 0x00;        //Tracks all the pre-alarm conditions that have occurred during an alarm session
unsigned char generalAlarmCause = 0x00;    //Tracks all the general alarm conditions that have occurred during an alarm session
unsigned char slcTroubleCause = 0x00;      //Tracks all the troubles related to the SLC's with no EOL resistor, if a bit is set, that trouble is present
unsigned char nacTroubleCause = 0x00;      //Tracks all the troubles related to the NAC's, first 4 bits are no EOL trouble conditions, the last 4 bits are a NAC disabled condition
unsigned char generalTroubleCause = 0x00;  //Tracks general trouble conditions that don't have a dedicated tracker, if a bit is set, that condition is present

//Interrupt Tracking Variables
unsigned char buttonTracker = 0x00;          //Tracks the previous and newest state of the buttons on the user interface, used by the software interrupt system
unsigned short slcAlarmTracker = 0x0000;     //Tracks the previous and newest state of alarm conditions on SLC's
unsigned short slcTroubleTracker = 0x0000;   //Tracks the previous and newest state of trouble conditions on the SLC's
unsigned char nacTroubleTracker = 0x00;      //Tracks the previous and newest state of trouble conditions on the NAC's
unsigned char generalTroubleTracker = 0x00;  //Tracks the previous and newest state of general trouble conditions

//Software Interrupts Variables
unsigned char generalInterrupt = 0x00;         //Used to track general interrupts within the software, primarily used to update the status LCD on the user interface/annunciator panel
unsigned char buttonInterrupt = 0x00;          //Used to track interrupts involving the user interface, a bit is set whenever a button is pressed
unsigned char slcAlarmInterrupt = 0x00;        //Used to track interrupts on SLC's, a bit is set whenever an interrupt involving an alarm on an SLC occurs
unsigned char slcTroubleInterrupt = 0x00;      //Used to track interrupts on SLC's, a bit is set whenever an interrupt involving a trouble on an SLC occurs
unsigned char nacTroubleInterrupt = 0x00;      //Used to track interrupts on NAC's, a bit is set whenever an interrupt involving a trouble on a NAC occurs
unsigned char generalTroubleInterrupt = 0x00;  //Used to track general trouble conditions on the system, a bit is set whenever a general trouble occurs

//Utility Variables
unsigned char utilityCounter = 0x00;     //Counts up at a rate of 8Hz, used for various tasks needing delay
unsigned char coderCounter = 0x13;       //Internal NAC coder counter, first 4 bits are used to code the NAC's and the last 4 bits are used to control the temporal coding pattern
unsigned char activeADChannel = 0x00;    //Used by the ADC reading function to load the new value into the appropriate register, first 4 bits determine the active channel, last 4 bits determine the condition of the reading
unsigned char currentConditions = 0x00;  //Used by the user interface to track the types of conditions that are current and if they have been acknowledged
unsigned char resetCounter = 0x00;       //Used to create a delay for how long a system reset shall take
unsigned char LATA = 0x00;               //A fake LATA register, this MCU doesn't have one which is kind of annoying
unsigned char LATB = 0x00;               //A fake LATB register, this MCU doesn't have one which is kind of annoying
unsigned char LATC = 0x00;               //A fake LATC register, this MCU doesn't have one which is kind of annoying
unsigned char LATD = 0x00;               //A fake LATD register, this MCU doesn't have one which is kind of annoying

/****************
 *  Interrupts  *
 ****************/

//Hardware Interrupt ISR Function, called whenever an interrupt occurs on the MCU's builtin interrupt system
void interrupt hardwareInterruptISR() {
    //Internal timing counter, used for things that need delay such as smoke reset, user interface coding and NAC coding
    if ((INTCON & 0x04) == 0x04) {
        INTCON &= 0xFB;  //Clear the Timer 0 overflow interrupt flag to prevent false interrupts

        utilityCounter++;  //Increment the counter by 1

        //True every 1st count (8Hz)
        if ((utilityCounter & 0x01) == 0x01) {
            ledControl ^= 0x80;  //XOR the bit used for flashing the LED's on the user interface

            //Check to see if the reset counter is active
            if (resetCounter != 0x00) {
                resetCounter--;

                //If the reset counter is 0, than reset the MCU to perform a system reset
                if (resetCounter == 0x00) {
                    WDTCON = 0x01;  //Enable the watchdog timer to perform a reset on the MCU, causing the system to reset
                }
            }
        }

        //True every 2nd count (4Hz)
        if ((utilityCounter & 0x03) == 0x01) {
            coderCounter ^= 0x02;  //XOR the bit used by the coder to produce a 120 BPM March-Time pattern
        }

        //True every 4th count (2Hz)
        if ((utilityCounter & 0x07) == 0x01) {
            ledControl ^= 0x40;     //XOR the bit used to pulse the buzzer on the user interface to a 60 BPM March-Time pattern
            coderCounter ^= 0x04;   //XOR the bit used by the coder to produce a 60 BPM March-Time pattern
            coderCounter += 0x10;   //Increment the counter the coder uses to produce a temporal pattern by 1

            //Update the temporal output bit of the coder if the pattern hasn't reach 3 pulses yet
            if ((coderCounter & 0xF0) < 0x70) {
                coderCounter &= 0xF7;                           //Clear the bit that produces the temporal pattern
                coderCounter |= (coderCounter & 0x10) >> 0x01;  //Set the bit that produces the temporal pattern to the same state as the first bit in the counter
            }

            //Reset the temporal coder counter after the pattern has finished
            if ((coderCounter & 0xF0) == 0x80) {
                coderCounter &= 0x0F;  //Reset the counter used by the coder to produce another round of the temporal pattern
            }

            ADCON0 |= 0x02;  //Set the GO bit to start the conversion
        }
    }

    //Process any new readings from the ADC if any are available
    if ((PIR1 & 0x40) == 0x40) {
        PIR1 &= 0xBF;  //Clear the ADC read complete flag to prevent false interrupts

        //Handle the new reading, determine what condition the reading is
        activeADChannel &= 0x0F;  //Clear the condition flag bits

        //Check to see if the reading is considered an alarm condition
        if (ADRESL >= 0x55 && (ADRESH & 0x03) >= 0x03) {
            activeADChannel |= 0x10;  //Set the alarm condition flag bit
        }

        //Check to see if the reading is considered as a trouble condition for an SLC
        if (!(ADRESL >= 0xEF && (ADRESH & 0x03) == 0x01) && (ADRESL <= 0x03 && (ADRESH & 0x03) == 0x02)) {
        //if (!(ADRESL >= 0xEF && (ADRESH & 0x03) == 0x01)) {
            //activeADChannel |= 0x20;  //Set the SLC trouble condition flag bit
        }

        //Check to see if the reading is considered as a trouble condition for a NAC
        if (ADRESL <= 0x0F && (ADRESH & 0x03) == 0x00) { //(nacControl & 0x0F ^ 0x0F) & (0x01 << (activeADChannel & 0x03))
            //activeADChannel |= 0x40;  //Set the NAC trouble condition flag bit
        }

        //Determine if the selected ADC channel is an SLC or NAC and then update the interrupt trackers
        if ((activeADChannel & 0x0F) < 0x04) {
//            nacTroubleTracker &= 0xFF ^ (0x01 << (activeADChannel & 0x0F));                       //Clear the trouble tracker bit that belong to the NAC that the reading was taken from
//            nacTroubleTracker |= ((activeADChannel & 0x40) >> 0x06) << (activeADChannel & 0x0F);  //Set the trouble condition bit of the trouble tracker if the condition still exists
        } else if ((activeADChannel & 0x0F) >= 0x06) {
            slcAlarmTracker &= 0xFFFF ^ (0x01 << ((activeADChannel & 0x0F) - 0x06));                       //Clear the alarm tracker bit that belongs to the SLC that the reading was taken from
            slcAlarmTracker |= ((activeADChannel & 0x10) >> 0x04) << ((activeADChannel & 0x0F) - 0x06);    //Set the alarm condition bit of the alarm tracker if the condition still exists
            slcTroubleTracker &= 0xFFFF ^ (0x01 << ((activeADChannel & 0x0F) - 0x06));                     //Clear the trouble tracker bit that belongs to the SLC that the reading was taken from
            slcTroubleTracker |= ((activeADChannel & 0x20) >> 0x05) << ((activeADChannel & 0x0F) - 0x06);  //Set the trouble condition bit of the alarm tracker if the condition still exists
        }

        generalInterrupt |= 0x80;  //Set the start ADC conversion interrupts flag to start the ADC conversion
    }
}

//Software ISR Function, processes all interrupts that are controlled within software
void softwareISR() {
    //Process interrupts related to SLC's
    //Check to see if an SLC has detected any new alarm conditions, and then process them accordingly
    if (slcAlarmInterrupt != 0x00 && slcAlarmInterrupt != ((generalAlarmCause | preAlarmCause) & slcAlarmInterrupt)) {
        //Check to see if any general alarm conditions have occurred yet before putting the panel in pre-alarm if applicable
        if (preAlarmCause == 0x00 && (preAlarmControl & 0x01) == 0x01) {
            preAlarmCause |= slcAlarmInterrupt;  //Set the pre-alarm cause to the SLC that the pre-alarm condition occurred on
            generalInterrupt |= 0x01;            //Set the pre-alarm condition occurred flag bit of the general interrupt variable
        } else {
            generalAlarmCause |= slcAlarmInterrupt;  //Set the general alarm cause to the SLC that the general alarm condition occurred on
            generalInterrupt |= 0x02;                //Set the general alarm condition occurred flag bit of the general interrupt variable
        }

        slcAlarmInterrupt &= slcAlarmInterrupt ^ 0xFF;  //Clear the bit(s) that triggered the SLC alarm condition interrupt to prevent false interrupts from occurring
    }

    //Check to see if any alarm conditions have been restored, and then process them accordingly
    if (slcAlarmInterrupt != 0x00 && slcAlarmInterrupt == ((generalAlarmCause) & slcAlarmInterrupt)) {
//         generalAlarmCause &= 0xFF ^ slcAlarmInterrupt;  //Clear the bit(s) that corresponds to the SLC that the alarm condition is no longer present on, as the alarm condition no longer exists

        slcAlarmInterrupt &= slcAlarmInterrupt ^ 0xFF;  //Clear the bit(s) that triggered the SLC alarm condition interrupt to prevent false interrupts from occurring
    }

    //Check to see if an SLC has detected any new trouble conditions, and then process them accordingly
    if (slcTroubleInterrupt != 0x00 && slcTroubleInterrupt != (slcTroubleCause & slcTroubleInterrupt)) {
        slcTroubleCause |= slcTroubleInterrupt;  //Set the cause of the trouble to the SLC that caused the trouble condition
        generalInterrupt |= 0x04;                //Set the trouble condition occurred flag bit of the general interrupt variable

        slcTroubleInterrupt &= slcTroubleInterrupt ^ 0xFF;  //Clear the bit(s) that triggered the SLC trouble condition interrupt to prevent false interrupts from occurring
    }

    //Check to see if any trouble conditions have been restored, and then process them accordingly
    if (slcTroubleInterrupt != 0x00 && slcTroubleInterrupt != (slcTroubleCause & slcTroubleInterrupt)) {
        slcTroubleInterrupt &= 0xFF ^ slcTroubleInterrupt;  //Clear the bit(s) that corresponds to the SLC that the trouble condition is no longer present on, as the the trouble condition no longer exists

        slcTroubleInterrupt &= slcTroubleInterrupt ^ 0xFF;  //Clear the bit(s) that triggered the SLC trouble condition interrupt to prevent false interrupts from occurring
    }


    //Process interrupts related to NAC's
    //Check to see if a NAC has detected any new trouble conditions, and then process them accordingly
    if (nacTroubleInterrupt != 0x00 && nacTroubleInterrupt != (nacTroubleCause & nacTroubleInterrupt)) {
        nacTroubleCause |= nacTroubleInterrupt;  //Set the cause of the trouble to the NAC that caused the trouble condition
        generalInterrupt |= 0x04;                //Set the trouble condition occurred flag bit of the general interrupts variable

        nacTroubleInterrupt &= nacTroubleInterrupt ^ 0xFF;  //Clear the bit(s) that triggered the NAC trouble condition to prevent false interrupts from occurring
    }

    //Check to see if any trouble conditions have been restored, and then process them accordingly
    if (nacTroubleInterrupt != 0x00 & nacTroubleInterrupt == (nacTroubleCause & nacTroubleInterrupt)) {
        nacTroubleCause &= 0xFF ^ nacTroubleInterrupt;  //Clear the bit(s) that corresponds to the SLC that the trouble condition is no longer present on, as the trouble condition no longer exists

        nacTroubleInterrupt &= nacTroubleInterrupt ^ 0xFF;  //Clear the bit(s) that triggered the NAC trouble condition to prevent false interrupts from occurring
    }


    //Process interrupts related to general troubles
    //Check to see if there is a loss of AC power
    if ((generalTroubleInterrupt & 0x01) == 0x01) {
        generalTroubleInterrupt &= 0xFE;  //Clear the AC Power Loss trouble flag to prevent false interrupts

//        generalTroubleCause |= 0x01;  //Set the AC Power Loss bit to indicate that the trouble condition was caused by loss of AC power
//        ledControl &= 0xF7;           //Turn off the power LED on the user interface as AC Power has been lost
//        generalInterrupt |= 0x04;     //Set the general trouble interrupt bit to cause a general trouble condition on the panel
    }


    //Process general interrupts that are used to control the basic state of the panel and control the data sent out to the user interface
    //Check to see if a pre-alarm condition has occurred
    if ((generalInterrupt & 0x01) == 0x01) {
        generalInterrupt &= 0xFE;  //Clear the pre-alarm condition interrupt flag to prevent false interrupts

        nacControl |= ((nacControl & 0xF0 ^ 0xF0) & (nacTypeControl & 0xF0)) >> 0x04;  //Activate the enabled NAC's used during a pre-alarm condition
        currentConditions |= 0x01;                                                     //Indicate that there is an unacknowledged pre-alarm condition
        ledControl &= 0xEC;                                                            //Clear the flash disable bit for the alarm LED in the led control variable to start flashing the LED
        ledControl |= 0x03;                                                            //Turn on the alarm LED flasher and the buzzer to indicate an un-acknowledged pre-alarm condition on the user interface
    }

    //Check to see if a general alarm condition has occurred
    if ((generalInterrupt & 0x02) == 0x02) {
        generalInterrupt &= 0xFD;  //Clear the general alarm condition interrupt flag to prevent false interrupts

        nacControl |= (nacControl & 0xF0 ^ 0xF0) >> 0x04;  //Activate all the enabled NAC's
        currentConditions |= 0x02;                         //Indicate that there is an un-acknowledged general alarm condition
        ledControl &= 0xE4;                                //Clear the flash disable bit for the alarm LED in the led control variable to start flashing the alarm LED
        ledControl |= 0x03;                                //Turn on the alarm LED flasher and the buzzer to indicate an un-acknowledged general alarm condition on the user interface
    }

    //Check to see if a general trouble condition has occurred
    if ((generalInterrupt & 0x04) == 0x04) {
        generalInterrupt &= 0xFB;  //Clear the general trouble condition interrupt flag to prevent false interrupts

        currentConditions |= 0x04;  //Indicate that there is an un-acknowledged general trouble condition
        ledControl &= 0xDA;         //Clear the flash disable bit for the trouble LED in the led control variable to start flashing the LED
        ledControl |= 0x05;         //Turn on the trouble LED flasher and the buzzer to indicate an un-acknowledged trouble condition on the user interface
    }

    //Check to see if it's time to start another conversion on the ADC
    if ((generalInterrupt & 0x80) == 0x80) {
        generalInterrupt &= 0x7F;  //Clear the start ADC conversion interrupt flag to prevent false interrupts

        activeADChannel++;  //Increment the counter used to read all the ADC channels by 1

        //Check to see if all the ADC channels have been read
        if ((activeADChannel & 0x0F) > 0x0D) {
            activeADChannel &= 0xF0;  //Reset the ADC selection bits back to 0 to cycle through all the channels again

            //Update the interrupt trackers used for detecting interrupts from the SLC's
            //Cause an alarm interrupt if an alarm condition has been detected
            slcAlarmTracker ^= 0xFFFF;                                                                           //Invert the output of the tracker to trigger rising edge interrupts
            slcAlarmInterrupt |= ((slcAlarmTracker & 0x0001 ^ 0x0001) & ((slcAlarmTracker & 0x0100) >> 0x08)) |  //Update the interrupt tracker and set the bits if an alarm condition has come in
                                 ((slcAlarmTracker & 0x0002 ^ 0x0002) & ((slcAlarmTracker & 0x0200) >> 0x08)) |
                                 ((slcAlarmTracker & 0x0004 ^ 0x0004) & ((slcAlarmTracker & 0x0400) >> 0x08)) |
                                 ((slcAlarmTracker & 0x0008 ^ 0x0008) & ((slcAlarmTracker & 0x0800) >> 0x08)) |
                                 ((slcAlarmTracker & 0x0010 ^ 0x0010) & ((slcAlarmTracker & 0x1000) >> 0x08)) |
                                 ((slcAlarmTracker & 0x0020 ^ 0x0020) & ((slcAlarmTracker & 0x2000) >> 0x08)) |
                                 ((slcAlarmTracker & 0x0040 ^ 0x0040) & ((slcAlarmTracker & 0x4000) >> 0x08)) |
                                 ((slcAlarmTracker & 0x0080 ^ 0x0080) & ((slcAlarmTracker & 0x8000) >> 0x08));

            //Cause an alarm interrupt if an alarm condition has been restored
            //slcAlarmTracker ^= 0xFFFF;                                                                           //Invert the output of the tracker to trigger falling edge interrupts
            slcAlarmInterrupt |= ((slcAlarmTracker & 0x0001 ^ 0x0001) & ((slcAlarmTracker & 0x0100) >> 0x08)) |  //Update the interrupt tracker and set the bits if an alarm condition has been restored
                                 ((slcAlarmTracker & 0x0002 ^ 0x0002) & ((slcAlarmTracker & 0x0200) >> 0x08)) |
                                 ((slcAlarmTracker & 0x0004 ^ 0x0004) & ((slcAlarmTracker & 0x0400) >> 0x08)) |
                                 ((slcAlarmTracker & 0x0008 ^ 0x0008) & ((slcAlarmTracker & 0x0800) >> 0x08)) |
                                 ((slcAlarmTracker & 0x0010 ^ 0x0010) & ((slcAlarmTracker & 0x1000) >> 0x08)) |
                                 ((slcAlarmTracker & 0x0020 ^ 0x0020) & ((slcAlarmTracker & 0x2000) >> 0x08)) |
                                 ((slcAlarmTracker & 0x0040 ^ 0x0040) & ((slcAlarmTracker & 0x4000) >> 0x08)) |
                                 ((slcAlarmTracker & 0x0080 ^ 0x0080) & ((slcAlarmTracker & 0x8000) >> 0x08));
            slcAlarmTracker = (slcAlarmTracker & 0x00FF) << 0x08;                                       //Shift the SLC alarm states from the old section to the new section

            //Cause a trouble interrupt if a trouble condition has been detected
            slcTroubleTracker ^= 0xFFFF;                                                                               //Invert the output of the tracker to trigger rising edge interrupts
            slcTroubleInterrupt |= ((slcTroubleTracker & 0x0001 ^ 0x0001) & ((slcTroubleTracker & 0x0100) >> 0x08)) |  //Update the interrupt tracker and set the bits if an trouble condition has come in
                                   ((slcTroubleTracker & 0x0002 ^ 0x0002) & ((slcTroubleTracker & 0x0200) >> 0x08)) |
                                   ((slcTroubleTracker & 0x0004 ^ 0x0004) & ((slcTroubleTracker & 0x0400) >> 0x08)) |
                                   ((slcTroubleTracker & 0x0008 ^ 0x0008) & ((slcTroubleTracker & 0x0800) >> 0x08)) |
                                   ((slcTroubleTracker & 0x0010 ^ 0x0010) & ((slcTroubleTracker & 0x1000) >> 0x08)) |
                                   ((slcTroubleTracker & 0x0020 ^ 0x0020) & ((slcTroubleTracker & 0x2000) >> 0x08)) |
                                   ((slcTroubleTracker & 0x0040 ^ 0x0040) & ((slcTroubleTracker & 0x4000) >> 0x08)) |
                                   ((slcTroubleTracker & 0x0080 ^ 0x0080) & ((slcTroubleTracker & 0x8000) >> 0x08));

            //Cause a trouble interrupt if a trouble condition has been restored
            slcTroubleTracker ^= 0xFFFF;                                                                               //Invert the output of the tracker to trigger falling edge interrupts
            slcTroubleInterrupt |= ((slcTroubleTracker & 0x0001 ^ 0x0001) & ((slcTroubleTracker & 0x0100) >> 0x08)) |  //Update the interrupt tracker and set the bits if an trouble condition has been restored
                                   ((slcTroubleTracker & 0x0002 ^ 0x0002) & ((slcTroubleTracker & 0x0200) >> 0x08)) |
                                   ((slcTroubleTracker & 0x0004 ^ 0x0004) & ((slcTroubleTracker & 0x0400) >> 0x08)) |
                                   ((slcTroubleTracker & 0x0008 ^ 0x0008) & ((slcTroubleTracker & 0x0800) >> 0x08)) |
                                   ((slcTroubleTracker & 0x0010 ^ 0x0010) & ((slcTroubleTracker & 0x1000) >> 0x08)) |
                                   ((slcTroubleTracker & 0x0020 ^ 0x0020) & ((slcTroubleTracker & 0x2000) >> 0x08)) |
                                   ((slcTroubleTracker & 0x0040 ^ 0x0040) & ((slcTroubleTracker & 0x4000) >> 0x08)) |
                                   ((slcTroubleTracker & 0x0080 ^ 0x0080) & ((slcTroubleTracker & 0x8000) >> 0x08));
            slcTroubleTracker = (slcTroubleTracker & 0x00FF) << 0x08;                                                  //Shift the SLC trouble states from the old section to the new section

            //Update the interrupt trackers used for detecting interrupts from the NAC's
            //Cause a trouble interrupt if a trouble condition has been detected
            nacTroubleTracker ^= 0x0F;                                                                           //Invert the output of the tracker to trigger rising edge interrupts
            nacTroubleInterrupt |= ((nacTroubleTracker & 0x01 ^ 0x01) & ((nacTroubleTracker & 0x10) >> 0x04)) |  //Update the interrupt tracker and set the bits if an trouble condition has come in
                                   ((nacTroubleTracker & 0x02 ^ 0x02) & ((nacTroubleTracker & 0x20) >> 0x04)) |
                                   ((nacTroubleTracker & 0x04 ^ 0x04) & ((nacTroubleTracker & 0x40) >> 0x04)) |
                                   ((nacTroubleTracker & 0x08 ^ 0x08) & ((nacTroubleTracker & 0x80) >> 0x04));

            //Cause a trouble interrupt if a trouble condition has been restored
            nacTroubleTracker ^= 0x0F;                                                                           //Invert the output of the tracker to trigger falling edge interrupts
            nacTroubleInterrupt |= ((nacTroubleTracker & 0x01 ^ 0x01) & ((nacTroubleTracker & 0x10) >> 0x04)) |  //Update the interrupt tracker and set the bits if an trouble condition has been restored
                                   ((nacTroubleTracker & 0x02 ^ 0x02) & ((nacTroubleTracker & 0x20) >> 0x04)) |
                                   ((nacTroubleTracker & 0x04 ^ 0x04) & ((nacTroubleTracker & 0x40) >> 0x04)) |
                                   ((nacTroubleTracker & 0x08 ^ 0x08) & ((nacTroubleTracker & 0x80) >> 0x04));
            nacTroubleTracker = (nacTroubleTracker & 0x0F) << 0x04;                                              //Shift the NAC trouble states from the old section to the new section
        }

        //Select the new ADC channel and start the conversion
        ADCON0 &= 0xC1;                              //Clear the ADC channel selection bits to write the next channel
        ADCON0 |= (activeADChannel & 0x0F) << 0x02;  //Set ADCON0 to the new ADC channel
    
        //Create a 2 microsecond delay using the no operation macro (compiles as a nop instruction in assembly)
        __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop();
        __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop();
        __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop();
        __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop();
        __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop();
        ADCON0 |= 0x02;  //Start the conversion, an interrupt will be created once the conversion is done
    }


    //Process interrupts related to the user interface
    //Check to see if the interrupt was caused by the reset button being pushed
    if ((buttonInterrupt & 0x01) == 0x01) {
        buttonInterrupt &= 0xFE;  //Clear the reset button pushed interrupt flag to prevent false interrupts

        resetCounter = 0x7F;  //Start the reset counter by setting all the bits in the register
    }

    //Check to see if the interrupt was caused by the acknowledge button being pushed
    if ((buttonInterrupt & 0x02) == 0x02) {
        buttonInterrupt &= 0xFD;  //Clear the acknowledge button pushed interrupt flag to prevent false interrupts

        //Run through and acknowledge the first condition that is top priority
        if ((currentConditions & 0x01) == 0x01) {
            currentConditions &= 0xFE;  //Clear the un-acknowledged pre-alarm condition flag as the condition is now acknowledged

            ledControl |= 0x10;  //Set the alarm LED override bit on to indicate the condition has been acknowledged on the user interface
        } else if ((currentConditions & 0x02) == 0x02) {
            currentConditions &= 0xFD;  //Clear the un-acknowledged general alarm condition flag as the condition is now acknowledged

            ledControl |= 0x10;  //Set the alarm LED override bit on to indicate the condition has been acknowledged on the user interface
        } else if ((currentConditions & 0x04) == 0x04) {
            currentConditions &= 0xFB;  //Clear the un-acknowledged general trouble condition flag as the condition is now acknowledged

            ledControl |= 0x20;  //Set the trouble LED override bit on to indicate the condition has been acknowledged on the user interface 
        }

        //If no more un-acknowledged conditions are present than turn off the buzzer on the user interface
        if (currentConditions == 0x00) {
            ledControl &= 0xFE;  //Turn off the buzzer as all the un-acknowledged conditions are now acknowledged
        }
    }

    //Check to see if the interrupt was caused by the silence button being pushed
    if ((buttonInterrupt & 0x04) == 0x04) {
        buttonInterrupt &= 0xFB;  //Clear the silence button pushed interrupt flag to prevent false interrupts

        //Check to see if a pre-alarm condition is present
        if (preAlarmCause != 0x00 && generalAlarmCause == 0x00 && (nacTypeControl & 0x0F) != 0x00) {
            nacControl ^= (((nacControl & 0xF0 ^ 0xF0) & (nacTypeControl & 0xF0)) >> 0x04) & (nacTypeControl & 0x0F);  //Activate/De-Activate any NAC used for during a pre-alarm condition that is not disabled and is silence-able
            ledControl ^= 0x08;                                                                                        //Toggle the silenced LED to indicate the state of the silenced NAC's
        } else if (generalAlarmCause != 0x00 && (nacTypeControl & 0x0F) != 0x00){
            nacControl ^= ((nacControl & 0xF0 ^ 0xF0) >> 0x04) & (nacTypeControl & 0x0F);  //Activate/De-Activate any NAC that is not disabled and is silence-able
            ledControl ^= 0x08;                                                            //Toggle the silenced LED to indicate the state of the silenced NAC's
        }
    }

    //Check to see if the interrupt was caused by the function button being pushed
    if ((buttonInterrupt & 0x08) == 0x08) {
        buttonInterrupt &= 0xF7;  //Clear the function button pushed interrupt flag to prevent false interrupts
    }
}

/*********************
 *  Core Processing  *
 *********************/

//Main Function, called upon reset of the MCU, or whenever the panel is hard reset
void main() {
    //Initialize the MCU by setting the appropriate registers with the appropriate values

    //Timing Related Registers
    OSCCON = 0x60;      //Set the internal RC-Oscillator to run at 8MHz
    OPTION_REG = 0xD7;  //Disable the internal pull-up resistors on PORTB and enable Timer 0 to run on the internal RC-Oscillator with a pre-scale of 256
    WDTCON = 0x00;      //Disable the watchdog timer and set the pre-scale value to 32

    //IO Related Registers
    TRISA = 0x0F;   //Set TRISA0 to TRISA3 to inputs and clear the rest as outputs
    TRISB = 0xBF;   //Set all of TRISB to inputs
    TRISC = 0xFF;   //Set all of TRISC to inputs
    TRISD = 0x0F;   //Set TRISD0 to TRISD3 to inputs and clear the rest as outputs
    TRISE = 0x0F;   //Set all of TRISE to inputs
    ANSEL = 0x0F;   //Set ANSEL0 to ANSEL3 to allow the built-in ADC to read from PORTA0 to PORTA3
    ANSELH = 0x3F;  //Set ANSEL8 to ANSEL13 to allow the built-in ADC to read from PORTB0 to PORTB6
    LATA = 0x00;    //Clear the fake LATA register
    LATB = 0x00;    //Clear the fake LATB register
    LATC = 0x00;    //Clear the fake LATC register
    LATD = 0x00;    //Clear the fake LATD register
    PORTA = LATA;   //Write the value of LATA to PORTA
    PORTB = LATB;   //Write the value of LATB to PORTB
    PORTC = LATC;   //Write the value of LATC to PORTC
    PORTD = LATD;   //Write the value of LATD to PORTD
    PORTE = 0x00;   //Clear all of PORTE to logic LOW

    //Interrupt Related Registers
    INTCON = 0xE0;  //Enable global interrupts, peripheral interrupts and the Timer 0 overflow interrupt
    PIE1 = 0x40;    //Enable the ADC read complete interrupt

    //ADC Related Registers
    ADCON1 = 0x80;  //Set the output format to be the lowest 8 bits of the 10 bit result to be shifted into the lower end of the 16 bit register
    ADCON0 = 0x41;  //Enable the internal ADC to run using the internal RC-Oscillator frequency divided by 8

    generalTroubleCause = 0x00;

    //Run in a continuous loop till the end of time
    while (0x01) {
        //Update the interrupt trackers used for detecting interrupts from the user interface buttons
        buttonTracker = (buttonTracker & 0x0F) << 0x04;                                          //Shift the button states from the new section to the old section
        buttonTracker |= PORTD & 0x0F ^ 0x0F;                                                    //Read the first 4 bits from PORTD to the first 4 bits of buttonTracker
        buttonInterrupt |= ((buttonTracker & 0x01 ^ 0x01) & ((buttonTracker & 0x10) >> 0x04)) |  //Update the interrupt tracker and set bits if a button has been pressed
                           ((buttonTracker & 0x02 ^ 0x02) & ((buttonTracker & 0x20) >> 0x04)) |
                           ((buttonTracker & 0x04 ^ 0x04) & ((buttonTracker & 0x40) >> 0x04)) |
                           ((buttonTracker & 0x08 ^ 0x08) & ((buttonTracker & 0x80) >> 0x04));

        //Update the interrupt trackers used for detecting general trouble conditions on the panel
        generalTroubleTracker = (generalTroubleTracker & 0x0F) << 0x04;                                                  //Shift the general trouble states from the new section to the old section
        generalTroubleTracker |= ((PORTB & 0x80 ^ 0x80) >> 0x07);                                                        //Set the appropriate bits if a trouble condition is present
        generalTroubleInterrupt |= ((generalTroubleTracker & 0x01 ^ 0x01) & ((generalTroubleTracker & 0x10) >> 0x04));   //Update the interrupt tracker and set bits if a general trouble interrupt has occurred

        softwareISR();  //Process all software based interrupts

        //Update the LED's on the user interface
        LATD &= 0x0F;                                                                                           //Clear the last 4 bits of LATD
        LATD |= (generalTroubleCause & 0x01 ^ 0x01) << 0x04;                                                    //Write the output state of the Power LED to LATD
        LATD |= ((ledControl & 0x80) >> 0x02) & ((ledControl & 0x02) << 0x04) | ((ledControl & 0x10) << 0x01);  //Write the output state of the Alarm LED to LATD
        LATD |= ((ledControl & 0x80) >> 0x01) & ((ledControl & 0x04) << 0x04) | ((ledControl & 0x20) << 0x01);  //Write the output state of the Trouble LED to LATD
        LATD |= (ledControl & 0x08) << 0x04;                                                                    //Write the output state of the Silence LED to LATD
        PORTD = LATD;                                                                                           //Write the value of LATD to PORTD

        //Update the state of the buzzer on the user interface
        LATB &= 0xBF;                                                 //Clear the last 2 bits of LATB
        LATB |= ((ledControl & 0x01) << 0x06) & (ledControl & 0x40);  //Write the output state of the buzzer to LATB
        PORTB = LATB;                                                 //Write the value of LATB to PORTB

        //Update the output state of the NAC's
        LATA &= 0x0F;                                                                                                                                          //Clear the last 4 bits of LATA
        LATA |= ((((coderCounter & (0x01 << (coderControl & 0x03))) >> (coderControl & 0x03)) & (nacControl & 0x01)) << 0x04) |                                //Write the output state of the NAC's to LATA
                ((((coderCounter & (0x01 << ((coderControl & 0x0C) >> 0x02))) >> ((coderControl & 0x0C) >> 0x02)) & ((nacControl & 0x02) >> 0x01)) << 0x05) |
                ((((coderCounter & (0x01 << ((coderControl & 0x30) >> 0x04))) >> ((coderControl & 0x30) >> 0x04)) & ((nacControl & 0x04) >> 0x02)) << 0x06) |
                ((((coderCounter & (0x01 << ((coderControl & 0xC0) >> 0x06))) >> ((coderControl & 0xC0) >> 0x06)) & ((nacControl & 0x08) >> 0x03)) << 0x07);
        PORTA = LATA;  //Write the value of LATA to PORTA
    }
}