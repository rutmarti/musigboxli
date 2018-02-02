/*  musigBoxli.ino
    copyright 2018 martin.rutschmann@gmail.com

    This scetch contains the firmware for the musigböxli hardware. To be able to
    compile this scetch the pcmPlay library as well as the teensyLc-i2s library
    is required.
*/

#include <pcmPlay.h>
#include <SPI.h>

//#define DEBUG_ENABLED //!< Enable debug output

#ifdef DEBUG_ENABLED
#define DEBUG_PRINT(fmt, args...)  Serial.printf(fmt, ## args)
#else
#define DEBUG_PRINT(fmt, args...)  // don't output anything for non debug builds
#endif

//==============================================================================
// Constants

//! Chip select for SD card on musigBoxli board
const int sdCardChipSelect = 16;
//! Sck on pin 14 on musigBoxli board
const int sdCardSck = 14;

//! Mapping between button index and pin number on musigBoxli board
const uint8_t buttonIxMap[] = { 6, 7, 8, 5, 4, 3, 2, 1, 0, 10, 15 };
//! Number of input buttons
const int numButtons = sizeof(buttonIxMap) / sizeof(buttonIxMap[0]);
//! Volume mapping lookup table, this maps the linear poti input to the actual volume
const uint16_t volTable[16] =
{ 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 17, 21, 26, 32, 40, 50 };

//! Index of backward button
const uint32_t backButtonIx = 9;
//! Index of forward button
const uint32_t fwdButtonIx = 10;

//! First album to be played after power up
const int firstAlbIx = 3;
//! First song of the album to be played after power up
const int firstSongIx = 0;

//==============================================================================
// Function prototypes

/*! Callback function for pcmPlay library

    This function is called in regular intervals when a pcm file is playing.
    The play call of the pcmPlay library is blocking and this callback provides
    the possibility to stop the track beeing played.
    Therefore the input pins are sampled in this call back to decide if there
    is a need to stop the current playback based on pressed pin.

    \param pVolume  Volume to be set by the pcmPlay library

    \return Function returns non zero if buttons have been pressed where the
            any bits set to one corresponds to button index which was pressed
*/
static uint32_t playCallBack(uint32_t *pVolume);

/*! Setup routing for gpio pins

    This routine sets configuration of the use GPIO pins. This includes mainly
    the pins connected to the input button as well.
*/
static void setupPin(void);

/*! Setup and initialize SD card

    This  routine ensures that the SPI peripheral is configured correctly and
    afterwards initalizes the SD card.

    \return true one success false otherwise
*/
static bool setupSdCard(void);


//==============================================================================
// Function definitions

static void setupPin(void)
{
    // first setup input button pins. If the buttons are not pressed the input
    // is floating. If the button are pressed the pin is driven to ground.
    // Therefore the internal pull up is used for all the input pins
    int num = numButtons;
    const uint8_t *pButton = buttonIxMap;
    while (num--)
    {
        pinMode(*pButton, INPUT_PULLUP);
        pButton++;
    }

    // drive the amplifier enable pin to high
    pinMode(20, OUTPUT);
    digitalWrite(20, HIGH);
}

static bool setupSdCard(void)
{
    // Adjust the default SCK pin
    SPI.setSCK(sdCardSck);

    DEBUG_PRINT("Initializing SD card...\n");

    // see if the card is present and can be initialized:
    if (!SD.begin(sdCardChipSelect))
    {
        DEBUG_PRINT("Card failed, or not present\n");
        // don't do anything more:
        return true;
    }
    DEBUG_PRINT("card initialized.\n");

    return false;
}

static uint32_t playCallBack(uint32_t *pVolume)
{
    // persistent variable between function calls
    // bit mask with all buttons which are pressed at the moment set to 1
    static uint32_t buttonPressed = 0;
    // bit mask with all buttons which are pressed at the moment and were
    // already reported set to 1
    static uint32_t buttonPressedReleased = 0;

    // return value of the function
    uint32_t rv = 0;
    // loop variable used for looping over all input buttons
    int numBut = numButtons;
    // pointer to first index of button map, will be increased inside the loop
    const uint8_t *pButMap = buttonIxMap;
    // used bit mask for current buttonIx, this will be shifted by 1 after every
    // iteration
    uint16_t bitMask = 1;

    // loop over all buttons and check which buttons were pressed or released
    while (numBut--)
    {
        int buttonPin = *pButMap;
        // if digitalRead is 1, button is not pressed
        if (digitalRead(buttonPin))
        {
            // button is not pressed, clear the corresponding bit in
            // buttonPressed and buttonPressedReleased masks
            buttonPressed &= ~bitMask;
            buttonPressedReleased &= ~bitMask;
        }
        else
        {
            if (buttonPressed & bitMask & ~buttonPressedReleased)
            {
                rv |= bitMask;
            }
            else
            {
                buttonPressed |= bitMask;
            }
        }
        pButMap++;
        bitMask = bitMask << 1;
    }

    // read potentiometer voltage
    uint32_t ain = analogRead(A7);
    // resolution of adc is 10 bit
    // first we reduce this to 4 bit (value between 0..15)
    int32_t volNowIx = (ain >> 6);
    // use lookup table to map this into range from 0..255, the lookup table
    // allows us to use any mapping between linear potentiometer to the volume
    // we would like to set.
    int32_t volNow = volTable[volNowIx];
    // set current volume
    *pVolume = volNow;

    // remember for which buttons we reported the pressed state already
    buttonPressedReleased |= rv;
    return rv;
}


//==============================================================================
// Arduino scetch entrance functions


void setup()
{
    // put your setup code here, to run once:

    // first setup button pins
    setupPin();

    // setup and initialize sd card.
    // todo handle the case when sd card could not be found/initalized
    setupSdCard();

    // There's one global instance of the pcmPlay class
    pcmPlay.init();
}


void loop()
{
    // put your main code here, to run repeatedly:

    // index of current file to be played
    static int idx = firstSongIx;
    static int alb = firstAlbIx;

    uint32_t callBackVal;
    char fileName[10];

    // get the filename based on current idx and alb
    sprintf(fileName, "%d/%d.WAV", alb, idx);

    // play file
    DEBUG_PRINT("Play file %s\n", fileName);
    PCM_PLAY_ERROR_t rtrn = pcmPlay.play(fileName, &playCallBack, &callBackVal);
    DEBUG_PRINT("played, rv = %d, cbv = %d\n", rtrn, callBackVal);

    // based on return value decide what to do next
    if (rtrn == PCM_PLAY_ERROR_NONE)
    {
        if (callBackVal)
        {
            int buttonIx = 0;
            // loop over button index and break at the first pressed button
            while (buttonIx < numButtons)
            {
                if ((1 << buttonIx) & callBackVal)
                {
                    break;
                }
                buttonIx++;
            }
            DEBUG_PRINT("buttonIx = %d\n", buttonIx);

            // based on the pressed button, decide what to do next
            switch (buttonIx)
            {
            case backButtonIx:
                // go to previous song
                idx = (idx > 0) ? idx - 1 : 0;
                break;
            case fwdButtonIx:
                // go to next song
                idx += 1;
                break;
            default:
                if (alb == buttonIx)
                {
                    // if the current playlist button is pressed, play next song
                    idx += 1;
                }
                else
                {
                    // if not current playlist play the first song of the chosen
                    // playlist
                    idx = 0;
                    alb = buttonIx;
                }
            }
        }
        else
        {
            // no button was pressed, play next song of playlist
            idx += 1;
        }
    }
    else
    {
        // at the moment whenever there is an error playing a file we either
        // start from the beginning of the playlist or in the case where the
        // error happened on the first song of the album we go to the beginning
        // of the next album
        if (idx == 0)
        {
            alb = (alb < 8) ? (alb + 1) : 0;
        }
        idx = 0;
    }
}
