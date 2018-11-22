#define FASTLED_INTERRUPT_RETRY_COUNT 0
#define FASTLED_ALLOW_INTERRUPTS 0

#include "lib/FastLED/src/FastLED.h"
FASTLED_USING_NAMESPACE;

#define PARTICLE_NO_ARDUINO_COMPATIBILITY 1
#define FL_PROGMEM

#include "Particle.h"
#include <palettes.h>
#include <main.h>

#define MAX_ARGS 64
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

#define NUM_LEDS 99
#define LED_TYPE WS2811
#define COLOR_ORDER NSFastLED::RGB
#define DATA_PIN D5
#define MAX_BRIGHTNESS 255

#define TWINKLE_SPEED 3
#define TWINKLE_DENSITY 5
#define SECONDS_PER_PALETTE 20
#define FRAMES_PER_SECOND 120
// If COOL_LIKE_INCANDESCENT is set to 1, colors will
// fade out slighted 'reddened', similar to how
// incandescent bulbs change color as they get dim down.
#define COOL_LIKE_INCANDESCENT 0

SYSTEM_MODE(SEMI_AUTOMATIC);

CRGB leds[NUM_LEDS];
CRGB gBackgroundColor = CRGB::Black;
CRGBPalette16 gCurrentPalette;
CRGBPalette16 gTargetPalette;

int lightBrightness = 100;
bool shouldNotify = false;
bool shouldChangePattern = false;
bool lightState = false;
bool currentState = true;
bool cyclePatterns = true;

void setup() {
  if (!Particle.connected()) {
    Particle.connect();
    waitFor(Particle.connected, 10000);
  }
  
  delay(3000);  // safety startup delay
  PublishParticleAttributes();

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS)
      .setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(lightBrightness);
  ChooseNextColorPalette(gTargetPalette);
}

void loop() {
  if (currentState != lightState) {
    lightState = currentState;
  }

  if (shouldChangePattern) {
    ChooseNextColorPalette(gTargetPalette);
    shouldChangePattern = false;
  }

  if (lightState || shouldNotify) {
    EVERY_N_SECONDS(SECONDS_PER_PALETTE) {
      ChooseNextColorPalette(gTargetPalette);
    }
    EVERY_N_MILLISECONDS(10) {
      if (shouldNotify) {
        BlinkRainbow();
      }
      nblendPaletteTowardPalette(gCurrentPalette, gTargetPalette, 12);
    }
    DrawTwinkles();
    FastLED.show();
  }

  if (!lightState) {
    fill_solid(leds, NUM_LEDS, CRGB(0, 0, 0));
    FastLED.clear();
    FastLED.show();
  }
}

void TurnLightsOn() { ParticleTurnLightsOn(""); }

void TurnLightsOff() { ParticleTurnLightsOff(""); }

void Rainbow() { fill_rainbow(leds, NUM_LEDS, 0, 7); }

void BlinkRainbow() {
  bool originalLightState = currentState;
  currentState = true;
  for (int i = 0; i < 5; i++) {
    FastLED.clear();
    FastLED.show();
    delay(250);
    Rainbow();
    FastLED.show();
    delay(250);
  }
  currentState = originalLightState;
  shouldNotify = false;
}

void DrawTwinkles() {
  uint16_t PRNG16 = 11337;
  uint32_t clock32 = millis();

  CRGB bg = CRGB::Black;
  uint8_t backgroundBrightness = bg.getAverageLight();

  for (auto i = 0; i < NUM_LEDS; i++) {
    CRGB &pixel = leds[i];
    PRNG16 = (uint16_t)(PRNG16 * 2053) + 1384;  // next 'random' number
    uint16_t myclockoffset16 = PRNG16;  // use that number as clock offset
    PRNG16 = (uint16_t)(PRNG16 * 2053) + 1384;  // next 'random' number
    // use that number as clock speed adjustment factor (in 8ths, from 8/8ths to
    // 23/8ths)
    uint8_t myspeedmultiplierQ5_3 =
        ((((PRNG16 & 0xFF) >> 4) + (PRNG16 & 0x0F)) & 0x0F) + 0x08;
    uint32_t myclock30 =
        (uint32_t)((clock32 * myspeedmultiplierQ5_3) >> 3) + myclockoffset16;
    uint8_t myunique8 = PRNG16 >> 8;  // get 'salt' value for this pixel

    // We now have the adjusted 'clock' for this pixel, now we call
    // the function that computes what color the pixel should be based
    // on the "brightness = f( time )" idea.
    CRGB c = ComputeOneTwinkle(myclock30, myunique8);
    uint8_t cbright = c.getAverageLight();
    int16_t deltabright = cbright - backgroundBrightness;
    if (deltabright >= 32 || (!bg)) {
      // If the new pixel is significantly brighter than the background color,
      // use the new color.
      pixel = c;
    } else if (deltabright > 0) {
      // If the new pixel is just slightly brighter than the background color,
      // mix a blend of the new color and the background color
      pixel = blend(bg, c, deltabright * 8);
    } else {
      // if the new pixel is not at all brighter than the background color,
      // just use the background color.
      pixel = bg;
    }
  }
}

CRGB ComputeOneTwinkle(uint32_t ms, uint8_t salt) {
  uint16_t ticks = ms >> (8 - TWINKLE_SPEED);
  uint8_t fastcycle8 = ticks;
  uint16_t slowcycle16 = (ticks >> 8) + salt;
  slowcycle16 += sin8(slowcycle16);
  slowcycle16 = (slowcycle16 * 2053) + 1384;
  uint8_t slowcycle8 = (slowcycle16 & 0xFF) + (slowcycle16 >> 8);

  uint8_t bright = 0;
  if (((slowcycle8 & 0x0E) / 2) < TWINKLE_DENSITY) {
    bright = AttackDecayWave8(fastcycle8);
  }

  uint8_t hue = slowcycle8 - salt;
  CRGB c;
  if (bright > 0) {
    c = ColorFromPalette(gCurrentPalette, hue, bright, NOBLEND);
    if (COOL_LIKE_INCANDESCENT == 1) {
      CoolLikeIncandescent(c, fastcycle8);
    }
  } else {
    c = CRGB::Black;
  }
  return c;
}

uint8_t AttackDecayWave8(uint8_t i) {
  if (i < 86) {
    return i * 3;
  } else {
    i -= 86;
    return 255 - (i + (i / 2));
  }
}

void CoolLikeIncandescent(CRGB &c, uint8_t phase) {
  if (phase < 128) return;
  uint8_t cooling = (phase - 128) >> 4;
  c.g = qsub8(c.g, cooling);
  c.b = qsub8(c.b, cooling * 2);
}

// Advance to the next color palette in the list (above).
void ChooseNextColorPalette(CRGBPalette16 &pal) {
  if (cyclePatterns) {
    const uint8_t numberOfPalettes =
        sizeof(ActivePaletteList) / sizeof(ActivePaletteList[0]);
    static uint8_t whichPalette = -1;
    whichPalette = addmod8(whichPalette, 1, numberOfPalettes);
    pal = *(ActivePaletteList[whichPalette]);
  }
}

void PublishParticleAttributes() {
  Particle.function("lightsOn", ParticleTurnLightsOn);
  Particle.function("lightsOff", ParticleTurnLightsOff);
  Particle.function("toggleLights", ParticleToggleLights);
  Particle.function("brightness", ParticleSetBrightness);
  Particle.function("notify", ParticleAlert);
  Particle.function("nextPattern", ParticleNextPattern);
  Particle.function("MerryXMAS", ParticleMerryXMAS);
}

int ParticleTurnLightsOn(String input) {
  ParticleToggleLights("0=1");
  return 0;
}

int ParticleTurnLightsOff(String input) {
  ParticleToggleLights("0=0");
  return 0;
}

int ParticleSetBrightness(String brightness) {
  lightBrightness = MAX_BRIGHTNESS * brightness.toInt() / 100;
  FastLED.setBrightness(lightBrightness);
  return lightBrightness;
}

int ParticleToggleLights(String args) {
  int index = args.toInt();
  int value;
  char szArgs[MAX_ARGS];
  args.toCharArray(szArgs, MAX_ARGS);
  sscanf(szArgs, "%d=%d", &index, &value);
  if (value == 1) {
    currentState = true;
    FastLED.show();
  } else if (value == 0) {
    currentState = false;
    FastLED.clear();
    FastLED.show();
  }
  return 1;
}

int ParticleAlert(String args) {
  shouldNotify = true;
  return 0;
}

int ParticleNextPattern(String args) {
  ChooseNextColorPalette(gTargetPalette);
  return 0;
}

int ParticleMerryXMAS(String args) {
  bool lightsAlreadyOn = lightState;
  if (!lightsAlreadyOn) {
    TurnLightsOn();
  }
  cyclePatterns = false;
  gCurrentPalette = RedGreenWhite_p;
  for (int i = 0; i < 2000; i++) {
    DrawTwinkles();
    FastLED.show();
    delay(10);
  }

  cyclePatterns = true;
  if (!lightsAlreadyOn) {
    TurnLightsOff();
  }
  return 0;
}
