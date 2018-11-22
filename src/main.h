int NextPattern(String args);
void ChooseNextColorPalette(CRGBPalette16 &pal);

uint8_t AttackDecayWave8(uint8_t i);

CRGB ComputeOneTwinkle(uint32_t ms, uint8_t salt);
void CoolLikeIncandescent(CRGB &c, uint8_t phase);
void DrawTwinkles();
void BlinkRainbow();
void Rainbow();

void TurnLightsOn();
void TurnLightsOff();

int ParticleTurnLightsOn(String input);
int ParticleTurnLightsOff(String input);
int ParticleToggleLights(String input);
int ParticleAlert(String input);
int ParticleNextPattern(String input);
int ParticleMerryXMAS(String input);
int ParticleSetBrightness(String input);
void PublishParticleAttributes();
