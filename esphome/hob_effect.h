#pragma once
// Het kookplaat-effect: de knop bepaalt hoeveel ringen er branden, en de ringen
// lichten één voor één langzaam op, van binnen naar buiten.
// De getallen hieronder kun je gerust aanpassen.
#include <cmath>
#include "esphome.h"

// De ringen van één kookplaat, van binnen naar buiten. Het totaal moet gelijk
// zijn aan leds_per_hob in keukentje.yaml.
static const int RING_SIZES[] = {1, 8, 12, 16, 24, 32};
static const int RING_COUNT = sizeof(RING_SIZES) / sizeof(RING_SIZES[0]);
static const int HOB_COUNT = 4;

// Begint de plaat aan de buitenkant als je de knop een klein stukje opendraait?
// Dan komt de data binnen aan de andere kant: zet dit op true.
static const bool DATA_ENTERS_AT_CENTER = false;

static const float RING_FADE_IN_SEC = 1.5f;   // zo lang doet één ring over aangaan
static const float RING_FADE_OUT_SEC = 0.8f;  // zo lang doet één ring over uitgaan
static const uint32_t AUTO_OFF_MS = 30UL * 60UL * 1000UL;  // 30 minuten

// Stroom voor alle LEDs samen. De 18650-module levert 2,2 A: daar gaat ca. 0,25 A
// af voor de ESP32 en 0,25 A marge. Andere voeding? Pas dit getal aan.
static const float LED_BUDGET_MA = 1700.0f;
// Bovengrens voor de helderheid (0.0 - 1.0), om warmte in de epoxy te beperken.
static const float MAX_LEVEL = 0.30f;

static const float LED_IDLE_MA = 1.0f;   // een LED verbruikt dit ook als hij uit is
static const float RED_MA = 20.0f;       // rood op volle sterkte
static const float GREEN_RATIO = 0.28f;  // meer groen is meer oranje

static const float KNOB_OFF_VOLTS = 0.20f;    // hieronder staat de plaat uit
static const float KNOB_MAX_VOLTS = 3.00f;    // hierboven branden alle ringen
static const float KNOB_MOVED_VOLTS = 0.10f;  // kleiner verschil is ruis

struct Hob {
  float rings_lit = 0.0f;  // bijvoorbeeld 2.5: twee ringen aan, de derde half
  float last_knob = 0.0f;
  uint32_t last_touch_ms = 0;
  uint32_t last_draw_ms = 0;
  bool asleep = false;
};

static Hob hobs[HOB_COUNT];

// De helderheid waarbij alle platen samen, volledig aan, binnen het budget blijven.
inline float allowed_level() {
  int leds = 0;
  for (int size : RING_SIZES) leds += size;
  leds *= HOB_COUNT;
  const float level = (LED_BUDGET_MA - leds * LED_IDLE_MA) / (leds * RED_MA * (1.0f + GREEN_RATIO));
  return std::fmax(0.0f, std::fmin(MAX_LEVEL, level));
}

inline float knob_to_rings(float volts) {
  if (volts < KNOB_OFF_VOLTS) return 0.0f;
  float position = (volts - KNOB_OFF_VOLTS) / (KNOB_MAX_VOLTS - KNOB_OFF_VOLTS);
  if (position > 1.0f) position = 1.0f;
  // Net boven "uit" brandt de binnenste ring al.
  return 1.0f + (RING_COUNT - 1) * position;
}

inline uint8_t to_channel(float value) { return static_cast<uint8_t>(value + 0.5f); }

inline void draw_hob(esphome::light::AddressableLight &it, Hob &hob, float volts) {
  if (std::isnan(volts)) volts = 0.0f;
  const uint32_t now = esphome::millis();

  float dt = (now - hob.last_draw_ms) / 1000.0f;
  hob.last_draw_ms = now;
  if (dt > 0.2f) dt = 0.2f;

  // Knop gedraaid? Dan begint de timer voor automatisch uitschakelen opnieuw.
  if (std::fabs(volts - hob.last_knob) > KNOB_MOVED_VOLTS) {
    hob.last_knob = volts;
    hob.last_touch_ms = now;
    hob.asleep = false;
  }
  if (now - hob.last_touch_ms > AUTO_OFF_MS) hob.asleep = true;

  const float target = hob.asleep ? 0.0f : knob_to_rings(volts);
  if (hob.rings_lit < target) {
    hob.rings_lit = std::fmin(target, hob.rings_lit + dt / RING_FADE_IN_SEC);
  } else {
    hob.rings_lit = std::fmax(target, hob.rings_lit - dt / RING_FADE_OUT_SEC);
  }

  const float red = 255.0f * allowed_level();
  const float green = red * GREEN_RATIO;

  // Loop langs de LEDs in de volgorde waarin de data ze bereikt.
  int led = 0;
  for (int step = 0; step < RING_COUNT; step++) {
    const int ring = DATA_ENTERS_AT_CENTER ? step : RING_COUNT - 1 - step;
    const float amount = std::fmax(0.0f, std::fmin(1.0f, hob.rings_lit - ring));
    const esphome::Color color(to_channel(red * amount), to_channel(green * amount), 0);
    for (int i = 0; i < RING_SIZES[ring] && led < it.size(); i++) {
      it[led++] = color;
    }
  }
  while (led < it.size()) it[led++] = esphome::Color(0, 0, 0);
}
