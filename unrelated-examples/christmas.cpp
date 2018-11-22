/**
 * Modified version of https://gist.github.com/edalquist/debd5c83f02e1a08e891678b33f07d00
 * that compiles on particle.io hardware.
 */
#include <FastLED.h>
FASTLED_USING_NAMESPACE;

#define PARTICLE_NO_ARDUINO_COMPATIBILITY 1
#include "Particle.h"
#define FL_PROGMEM

#define NUM_LEDS 99
#define LED_TYPE WS2811
#define COLOR_ORDER NSFastLED::RGB
#define DATA_PIN D5
#define VOLTS 12
#define MAX_MA 4000

CRGB leds[NUM_LEDS];

// Overall twinkle speed.
// 0 (VERY slow) to 8 (VERY fast).
// 4, 5, and 6 are recommended, default is 4.
#define TWINKLE_SPEED 4

// Overall twinkle density.
// 0 (NONE lit) to 8 (ALL lit at once).
// Default is 5.
#define TWINKLE_DENSITY 5

// How often to change color palettes.
#define SECONDS_PER_PALETTE 30

// Background color for 'unlit' pixels
// Can be set to CRGB::Black if desired.
CRGB gBackgroundColor = CRGB::Black;

// If AUTO_SELECT_BACKGROUND_COLOR is set to 1,
// then for any palette where the first two entries
// are the same, a dimmed version of that color will
// automatically be used as the background color.
#define AUTO_SELECT_BACKGROUND_COLOR 0

// If COOL_LIKE_INCANDESCENT is set to 1, colors will
// fade out slighted 'reddened', similar to how
// incandescent bulbs change color as they get dim down.
#define COOL_LIKE_INCANDESCENT 1
#define MAX_ARGS 64

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

CRGBPalette16 gCurrentPalette;
CRGBPalette16 gTargetPalette;

SYSTEM_MODE(SEMI_AUTOMATIC)

int lightsOn(String input);
int lightsOff(String input);
int toggleLights(String args);
int setBright(String brightness);
void addGlitter(NSFastLED::fract8 chanceOfGlitter);
void rainbow();
int twitterMention(String args);
int nextPattern(String args);
int MerryXMAS(String args);

void setup()
{
  if (!Particle.connected())
  {
    Particle.connect();
    waitFor(Particle.connected, 10000);
  }

  // Publishing particle functions and vars.
  Particle.function("lightsOn", lightsOn);
  Particle.function("lightsOff", lightsOff);
  Particle.function("toggleLights", toggleLights);
  Particle.function("brightness", setBright);
  Particle.function("notify", twitterMention);
  Particle.function("nextPattern", nextPattern);
  Particle.function("MerryXMAS", MerryXMAS);

  delay(3000); //safety startup delay
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS)
      .setCorrection(TypicalLEDStrip);

  chooseNextColorPalette(gTargetPalette);
}

bool mentioned = false;
bool shouldChangePattern = false;
bool lightState = false;
bool currentState = false;
int lightBrightness = 100;
bool cyclePatterns = true;

void loop()
{

  // main loop
  if (currentState != lightState)
  {
    lightState = currentState;
  }

  if (shouldChangePattern == true)
  {
    chooseNextColorPalette(gTargetPalette);
    shouldChangePattern = false;
  }

  if (lightState == true || mentioned)
  {
    EVERY_N_SECONDS(SECONDS_PER_PALETTE)
    {
      chooseNextColorPalette(gTargetPalette);
    }

    EVERY_N_MILLISECONDS(10)
    {
      if (mentioned)
      {
        bool lightsAlreadyOn = currentState;
        if (!lightsAlreadyOn)
        {
          lightsOn("");
        }
        // Blink the tree lights 5 times if mentioned on twitter
        for (int i = 0; i < 5; i++)
        {
          FastLED.clear();
          FastLED.show();
          delay(250);
          rainbow();
          FastLED.show();
          delay(250);
        }
        if (!lightsAlreadyOn)
        {
          FastLED.clear();
          FastLED.show();
        }
        mentioned = false;
      }
      nblendPaletteTowardPalette(gCurrentPalette, gTargetPalette, 12);
    }

    drawTwinkles();

    FastLED.show();
  }
}

void drawTwinkles()
{
  uint16_t PRNG16 = 11337;
  uint32_t clock32 = millis();

  CRGB bg;
  if ((AUTO_SELECT_BACKGROUND_COLOR == 1) &&
      (gCurrentPalette[0] == gCurrentPalette[1]))
  {
    bg = gCurrentPalette[0];
    uint8_t bglight = bg.getAverageLight();
    if (bglight > 64)
    {
      bg.nscale8_video(16); // very bright, so scale to 1/16th
    }
    else if (bglight > 16)
    {
      bg.nscale8_video(64); // not that bright, so scale to 1/4th
    }
    else
    {
      bg.nscale8_video(86); // dim, scale to 1/3rd.
    }
  }
  else
  {
    bg = gBackgroundColor; // just use the explicitly defined background color
  }

  uint8_t backgroundBrightness = bg.getAverageLight();

  // for( CRGB& pixel: L) {
  for (auto i = 0; i < NUM_LEDS; i++)
  {
    CRGB &pixel = leds[i];

    PRNG16 = (uint16_t)(PRNG16 * 2053) + 1384; // next 'random' number
    uint16_t myclockoffset16 = PRNG16;         // use that number as clock offset
    PRNG16 = (uint16_t)(PRNG16 * 2053) + 1384; // next 'random' number
    // use that number as clock speed adjustment factor (in 8ths, from 8/8ths to 23/8ths)
    uint8_t myspeedmultiplierQ5_3 = ((((PRNG16 & 0xFF) >> 4) + (PRNG16 & 0x0F)) & 0x0F) + 0x08;
    uint32_t myclock30 = (uint32_t)((clock32 * myspeedmultiplierQ5_3) >> 3) + myclockoffset16;
    uint8_t myunique8 = PRNG16 >> 8; // get 'salt' value for this pixel

    // We now have the adjusted 'clock' for this pixel, now we call
    // the function that computes what color the pixel should be based
    // on the "brightness = f( time )" idea.
    CRGB c = computeOneTwinkle(myclock30, myunique8);

    uint8_t cbright = c.getAverageLight();
    int16_t deltabright = cbright - backgroundBrightness;
    if (deltabright >= 32 || (!bg))
    {
      // If the new pixel is significantly brighter than the background color,
      // use the new color.
      pixel = c;
    }
    else if (deltabright > 0)
    {
      // If the new pixel is just slightly brighter than the background color,
      // mix a blend of the new color and the background color
      pixel = blend(bg, c, deltabright * 8);
    }
    else
    {
      // if the new pixel is not at all brighter than the background color,
      // just use the background color.
      pixel = bg;
    }
  }
}

CRGB computeOneTwinkle(uint32_t ms, uint8_t salt)
{
  uint16_t ticks = ms >> (8 - TWINKLE_SPEED);
  uint8_t fastcycle8 = ticks;
  uint16_t slowcycle16 = (ticks >> 8) + salt;
  slowcycle16 += sin8(slowcycle16);
  slowcycle16 = (slowcycle16 * 2053) + 1384;
  uint8_t slowcycle8 = (slowcycle16 & 0xFF) + (slowcycle16 >> 8);

  uint8_t bright = 0;
  if (((slowcycle8 & 0x0E) / 2) < TWINKLE_DENSITY)
  {
    bright = attackDecayWave8(fastcycle8);
  }

  uint8_t hue = slowcycle8 - salt;
  CRGB c;
  if (bright > 0)
  {
    c = ColorFromPalette(gCurrentPalette, hue, bright, NOBLEND);
    if (COOL_LIKE_INCANDESCENT == 1)
    {
      coolLikeIncandescent(c, fastcycle8);
    }
  }
  else
  {
    c = CRGB::Black;
  }
  return c;
}

uint8_t attackDecayWave8(uint8_t i)
{
  if (i < 86)
  {
    return i * 3;
  }
  else
  {
    i -= 86;
    return 255 - (i + (i / 2));
  }
}

void coolLikeIncandescent(CRGB &c, uint8_t phase)
{
  if (phase < 128)
    return;

  uint8_t cooling = (phase - 128) >> 4;
  c.g = qsub8(c.g, cooling);
  c.b = qsub8(c.b, cooling * 2);
}

// A mostly red palette with green accents and white trim.
// "CRGB::Gray" is used as white to keep the brightness more uniform.
const TProgmemRGBPalette16 RedGreenWhite_p FL_PROGMEM =
    {CRGB::Red, CRGB::Red, CRGB::Red, CRGB::Red,
     CRGB::Red, CRGB::Red, CRGB::Red, CRGB::Red,
     CRGB::Red, CRGB::Red, CRGB::Gray, CRGB::Gray,
     CRGB::Green, CRGB::Green, CRGB::Green, CRGB::Green};

// A mostly (dark) green palette with red berries.
#define Holly_Green 0x00580c
#define Holly_Red 0xB00402
const TProgmemRGBPalette16 Holly_p FL_PROGMEM =
    {Holly_Green, Holly_Green, Holly_Green, Holly_Green,
     Holly_Green, Holly_Green, Holly_Green, Holly_Green,
     Holly_Green, Holly_Green, Holly_Green, Holly_Green,
     Holly_Green, Holly_Green, Holly_Green, Holly_Red};

// A red and white striped palette
// "CRGB::Gray" is used as white to keep the brightness more uniform.
const TProgmemRGBPalette16 RedWhite_p FL_PROGMEM =
    {CRGB::Red, CRGB::Red, CRGB::Red, CRGB::Red,
     CRGB::Gray, CRGB::Gray, CRGB::Gray, CRGB::Gray,
     CRGB::Red, CRGB::Red, CRGB::Red, CRGB::Red,
     CRGB::Gray, CRGB::Gray, CRGB::Gray, CRGB::Gray};

// A mostly blue palette with white accents.
// "CRGB::Gray" is used as white to keep the brightness more uniform.
const TProgmemRGBPalette16 BlueWhite_p FL_PROGMEM =
    {CRGB::Blue, CRGB::Blue, CRGB::Blue, CRGB::Blue,
     CRGB::Blue, CRGB::Blue, CRGB::Blue, CRGB::Blue,
     CRGB::Blue, CRGB::Blue, CRGB::Blue, CRGB::Blue,
     CRGB::Blue, CRGB::Gray, CRGB::Gray, CRGB::Gray};

// A pure "fairy light" palette with some brightness variations
#define HALFFAIRY ((CRGB::FairyLight & 0xFEFEFE) / 2)
#define QUARTERFAIRY ((CRGB::FairyLight & 0xFCFCFC) / 4)
const TProgmemRGBPalette16 FairyLight_p FL_PROGMEM =
    {CRGB::FairyLight, CRGB::FairyLight, CRGB::FairyLight, CRGB::FairyLight,
     HALFFAIRY, HALFFAIRY, CRGB::FairyLight, CRGB::FairyLight,
     QUARTERFAIRY, QUARTERFAIRY, CRGB::FairyLight, CRGB::FairyLight,
     CRGB::FairyLight, CRGB::FairyLight, CRGB::FairyLight, CRGB::FairyLight};

// A palette of soft snowflakes with the occasional bright one
const TProgmemRGBPalette16 Snow_p FL_PROGMEM =
    {0x304048, 0x304048, 0x304048, 0x304048,
     0x304048, 0x304048, 0x304048, 0x304048,
     0x304048, 0x304048, 0x304048, 0x304048,
     0x304048, 0x304048, 0x304048, 0xE0F0FF};

// A palette reminiscent of large 'old-school' C9-size tree lights
// in the five classic colors: red, orange, green, blue, and white.
#define C9_Red 0xB80400
#define C9_Orange 0x902C02
#define C9_Green 0x046002
#define C9_Blue 0x070758
#define C9_White 0x606820
const TProgmemRGBPalette16 RetroC9_p FL_PROGMEM =
    {C9_Red, C9_Orange, C9_Red, C9_Orange,
     C9_Orange, C9_Red, C9_Orange, C9_Red,
     C9_Green, C9_Green, C9_Green, C9_Green,
     C9_Blue, C9_Blue, C9_Blue,
     C9_White};

// A cold, icy pale blue palette
#define Ice_Blue1 0x0C1040
#define Ice_Blue2 0x182080
#define Ice_Blue3 0x5080C0
const TProgmemRGBPalette16 Ice_p FL_PROGMEM =
    {
        Ice_Blue1, Ice_Blue1, Ice_Blue1, Ice_Blue1,
        Ice_Blue1, Ice_Blue1, Ice_Blue1, Ice_Blue1,
        Ice_Blue1, Ice_Blue1, Ice_Blue1, Ice_Blue1,
        Ice_Blue2, Ice_Blue2, Ice_Blue2, Ice_Blue3};

// Add or remove palette names from this list to control which color
// palettes are used, and in what order.
const TProgmemRGBPalette16 *ActivePaletteList[] = {
    &Snow_p,
    &FairyLight_p,
    &Holly_p,
    &RedGreenWhite_p,
    &RedWhite_p,
    //   &RetroC9_p,
    //   &RainbowColors_p,
    //   &BlueWhite_p,
    //   &PartyColors_p,
    //   &Ice_p
};

// Advance to the next color palette in the list (above).
void chooseNextColorPalette(CRGBPalette16 &pal)
{
  if (cyclePatterns)
  {
    const uint8_t numberOfPalettes = sizeof(ActivePaletteList) / sizeof(ActivePaletteList[0]);
    static uint8_t whichPalette = -1;
    whichPalette = addmod8(whichPalette, 1, numberOfPalettes);

    pal = *(ActivePaletteList[whichPalette]);
  }
}

int setBright(String brightness)
{
  lightBrightness = brightness.toInt();
  FastLED.setBrightness(lightBrightness);
  return 0;
}

int toggleLights(String args)
{
  int index = args.toInt();
  int value;
  char szArgs[MAX_ARGS];

  args.toCharArray(szArgs, MAX_ARGS);

  sscanf(szArgs, "%d=%d", &index, &value);

  if (value == 1)
  {
    lightsOn("");
  }
  else if (value == 0)
  {
    lightsOff("");
  }
  return 1;
}

int lightsOn(String input)
{
  currentState = true;
  FastLED.show();
  return 0;
}

int lightsOff(String input)
{
  currentState = false;
  FastLED.clear();
  FastLED.show();

  return 0;
}

int twitterMention(String args)
{
  mentioned = true;
  return 0;
}

int nextPattern(String args)
{
  shouldChangePattern = true;
  return 0;
}

void rainbow()
{
  // FastLED's built-in rainbow generator
  fill_rainbow(leds, NUM_LEDS, 0, 7);
}

int MerryXMAS(String args)
{
  bool lightsAlreadyOn = lightState;
  if (!lightsAlreadyOn)
  {
    lightsOn("");
  }
  cyclePatterns = false;
  gCurrentPalette = RedGreenWhite_p;

  for (int i = 0; i < 2000; i++)
  {
    drawTwinkles();
    FastLED.show();
    delay(10);
  }

  cyclePatterns = true;
  if (!lightsAlreadyOn)
  {
    lightsOff("");
  }
  return 1;
}
