#pragma once
// Het kookplaat-effect: de draaiknop bepaalt hoeveel ringen er branden, en de
// ringen lichten één voor één langzaam op, van binnen naar buiten. Staan alle
// vier de platen op vol, dan loopt er een lichtgolf van de binnenste naar de
// buitenste ring. De getallen hieronder kun je gerust aanpassen.
#include <cmath>
#include "esphome.h"

// De ringen van één kookplaat, van binnen naar buiten. Het totaal moet gelijk
// zijn aan leds_per_hob in keukentje.yaml.
static const int RING_SIZES[] = {1, 8, 12, 16, 24, 32};
static const int RING_COUNT = sizeof(RING_SIZES) / sizeof(RING_SIZES[0]);
static const int HOB_COUNT = 4;

// Begint de plaat aan de buitenkant als je de knop één klik draait?
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

static const float CHASE_RING_SEC = 0.25f;   // zo snel gaat de golf per ring
static const float CHASE_TAIL_RINGS = 1.5f;  // lengte van de staart, in ringen

struct Hob {
  float rings_lit = 0.0f;  // bijvoorbeeld 2.5: twee ringen aan, de derde half
  float position = 0.0f;   // stand van de knop: 0.0 is uit, 1.0 is vol
  float last_clicks = 0.0f;
  uint32_t last_touch_ms = 0;
  uint32_t last_draw_ms = 0;
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

inline uint8_t to_channel(float value) { return static_cast<uint8_t>(value + 0.5f); }

inline bool all_knobs_open() {
  for (const Hob &hob : hobs) {
    if (hob.position < 1.0f) return false;
  }
  return true;
}

// Hoe fel een ring is in het looplicht (0.0 - 1.0). Alle platen lopen gelijk,
// omdat de golf alleen van de klok afhangt.
inline float chase_amount(int ring, uint32_t now) {
  const uint32_t ring_ms = static_cast<uint32_t>(CHASE_RING_SEC * 1000.0f);
  const uint32_t period_ms = static_cast<uint32_t>((RING_COUNT + CHASE_TAIL_RINGS + 1.0f) * ring_ms);
  const float head = static_cast<float>(now % period_ms) / ring_ms;
  const float distance = head - ring;
  if (distance < 0.0f) return std::fmax(0.0f, 1.0f + distance);  // de golf komt eraan
  return std::fmax(0.0f, 1.0f - distance / CHASE_TAIL_RINGS);    // de staart dooft uit
}

// Tekent één kookplaat. Geeft true terug als de knop op nul gezet moet worden,
// omdat de plaat te lang niet is aangeraakt.
inline bool draw_hob(esphome::light::AddressableLight &it, Hob &hob, float clicks, int clicks_full) {
  if (std::isnan(clicks)) clicks = 0.0f;
  const uint32_t now = esphome::millis();

  float dt = (now - hob.last_draw_ms) / 1000.0f;
  hob.last_draw_ms = now;
  if (dt > 0.2f) dt = 0.2f;

  // Knop gedraaid of ingedrukt? Dan begint de timer voor automatisch uit opnieuw.
  bool auto_off = false;
  if (clicks != hob.last_clicks) {
    hob.last_clicks = clicks;
    hob.last_touch_ms = now;
  } else if (clicks > 0.0f && now - hob.last_touch_ms > AUTO_OFF_MS) {
    hob.last_touch_ms = now;
    auto_off = true;
  }

  hob.position = std::fmax(0.0f, std::fmin(1.0f, clicks / clicks_full));
  const float target = hob.position * RING_COUNT;
  if (hob.rings_lit < target) {
    hob.rings_lit = std::fmin(target, hob.rings_lit + dt / RING_FADE_IN_SEC);
  } else {
    hob.rings_lit = std::fmax(target, hob.rings_lit - dt / RING_FADE_OUT_SEC);
  }

  const float red = 255.0f * allowed_level();
  const float green = red * GREEN_RATIO;
  const bool chase = all_knobs_open();

  // Loop langs de LEDs in de volgorde waarin de data ze bereikt.
  int led = 0;
  for (int step = 0; step < RING_COUNT; step++) {
    const int ring = DATA_ENTERS_AT_CENTER ? step : RING_COUNT - 1 - step;
    const float amount = chase ? chase_amount(ring, now) : std::fmax(0.0f, std::fmin(1.0f, hob.rings_lit - ring));
    const esphome::Color color(to_channel(red * amount), to_channel(green * amount), 0);
    for (int i = 0; i < RING_SIZES[ring] && led < it.size(); i++) {
      it[led++] = color;
    }
  }
  while (led < it.size()) it[led++] = esphome::Color(0, 0, 0);
  return auto_off;
}
