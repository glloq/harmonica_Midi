// ============================================================================
//  test_core — tests unitaires de la logique cœur (env native).
//  Lancer avec :  pio test -e native
// ============================================================================
#include <unity.h>
#include "Factory.h"
#include "web/ConfigStore.h"
#include "web/DefaultConfig.h"
#include "hal/Mocks.h"
#include "air/DualReservoirPiston.h"
#include "valve/Valve2in1.h"
#include "midi/MidiParser.h"
#include "util/PIController.h"

using namespace harm;

void setUp() { MockLog::enabled = false; }
void tearDown() {}

static Config defaultCfg() { Config c; ConfigStore::deserialize(kDefaultConfigJson(), c); return c; }

// ---- HarmonicaMap -----------------------------------------------------------
void test_map_lookup() {
  Config c = defaultCfg();
  HarmonicaMap m; m.load(c.harmonica);
  auto a = m.lookup(60); TEST_ASSERT_TRUE(a.valid); TEST_ASSERT_EQUAL_INT(0, a.hole);
  TEST_ASSERT_EQUAL_INT((int)Direction::Blow, (int)a.direction);
  auto b = m.lookup(62); TEST_ASSERT_TRUE(b.valid); TEST_ASSERT_EQUAL_INT(0, b.hole);
  TEST_ASSERT_EQUAL_INT((int)Direction::Draw, (int)b.direction);
  // note 67 en double (trou1 aspir. AVANT trou2 souffle) : la 1re entrée gagne
  auto g = m.lookup(67); TEST_ASSERT_TRUE(g.valid); TEST_ASSERT_EQUAL_INT(1, g.hole);
  TEST_ASSERT_EQUAL_INT((int)Direction::Draw, (int)g.direction);
  TEST_ASSERT_FALSE(m.lookup(30).valid);   // non mappée
}

void test_slide_mapping() {
  HarmonicaCfg h; h.hasSlide = true; h.noteCount = 2;
  h.notes[0] = {60, 0, Direction::Blow, false, 1.0f};
  h.notes[1] = {61, 0, Direction::Blow, true, 1.0f};
  HarmonicaMap m; m.load(h);
  TEST_ASSERT_TRUE(m.needsSlide());
  TEST_ASSERT_FALSE(m.lookup(60).slide);
  TEST_ASSERT_TRUE(m.lookup(61).slide);
}

// ---- Matrice de capacité ----------------------------------------------------
void test_mixed_capability() {
  System* s = buildSystem(defaultCfg());          // vérin double + valve 2-en-1
  TEST_ASSERT_TRUE(s->engine.mixedCapable());
  Config c = defaultCfg();
  c.air.impl = AirImpl::SingleBellows; c.valve.impl = ValveImpl::Valve1in1; c.valve.holeCount = 2;
  System* b = buildSystem(c);
  TEST_ASSERT_FALSE(b->engine.mixedCapable());     // soufflet + 1-en-1
}

// ---- Arbitrage mono-direction ----------------------------------------------
void test_arbitration_reject() {
  Config c = defaultCfg();
  c.air.impl = AirImpl::SingleBellows; c.valve.impl = ValveImpl::Valve1in1; c.valve.holeCount = 2;
  c.engine.arbitration = Arbitration::Reject;
  System* s = buildSystem(c);
  s->engine.handleMidi({MidiEvent::NoteOn, 0, 60, 100});     // souffle trou0
  TEST_ASSERT_EQUAL_INT((int)Direction::Blow, (int)s->engine.activeDirection());
  s->engine.handleMidi({MidiEvent::NoteOn, 0, 67, 100});     // aspir. trou1 -> conflit -> rejeté
  TEST_ASSERT_EQUAL_INT(1, s->engine.activeVoiceCount());
  TEST_ASSERT_EQUAL_INT((int)Direction::Blow, (int)s->engine.activeDirection());
}

void test_arbitration_steal() {
  Config c = defaultCfg();
  c.air.impl = AirImpl::SingleBellows; c.valve.impl = ValveImpl::Valve1in1; c.valve.holeCount = 2;
  c.engine.arbitration = Arbitration::Steal;
  System* s = buildSystem(c);
  s->engine.handleMidi({MidiEvent::NoteOn, 0, 60, 100});     // souffle
  s->engine.handleMidi({MidiEvent::NoteOn, 0, 67, 100});     // aspir. -> vole la direction
  TEST_ASSERT_EQUAL_INT(1, s->engine.activeVoiceCount());
  TEST_ASSERT_EQUAL_INT((int)Direction::Draw, (int)s->engine.activeDirection());
}

// ---- Vérin double : inversion => échange de rail ---------------------------
void test_reversal_swaps_rail() {
  Config c = defaultCfg();
  MockServoBus bus; MockStepper st; MockPressure p1, p2; MockEndstops es;
  es.bindPiston(&st, c.air.dual.travelMm);
  DualReservoirPiston air(st, p1, p2, es, bus, c.air.dual);
  air.begin(); air.startHoming();
  for (int i = 0; i < 300 && !air.isHomed(); ++i) air.update(i);
  TEST_ASSERT_TRUE(air.isHomed());

  Rail before = air.railForDirection(Direction::Blow);
  uint16_t gen0 = air.status().assignmentGen;
  air.request(Direction::Blow, 1.0f);               // pression mock = 0 => piston file vers la butée
  int i = 0;
  while (air.status().assignmentGen == gen0 && i < 800) { air.update(1000 + i); ++i; }
  TEST_ASSERT_TRUE(air.status().assignmentGen > gen0);          // une inversion a eu lieu
  TEST_ASSERT_TRUE(air.railForDirection(Direction::Blow) != before);  // le rail souffle a basculé
}

// ---- Parseur MIDI (running status, temps réel, vel0) -----------------------
void test_midi_parser() {
  MidiParser p;
  int on = 0, off = 0, cc = 0; uint8_t lastNote = 0, lastVel = 0;
  MidiSink sink = [&](const MidiEvent& e) {
    if (e.type == MidiEvent::NoteOn) { ++on; lastNote = e.data1; lastVel = e.data2; }
    else if (e.type == MidiEvent::NoteOff) ++off;
    else ++cc;
  };
  // 0x92 NoteOn ch2 | running 64,0 | 0x82 NoteOff | CC11 | 0xF8 realtime (ignoré) | 0x90 NoteOn
  uint8_t stream[] = {0x92, 60, 100, 64, 0, 0x82, 60, 0, 0xB0, 11, 64, 0xF8, 0x90, 72, 80};
  for (uint8_t b : stream) p.feed(b, sink);
  TEST_ASSERT_EQUAL_INT(3, on);
  TEST_ASSERT_EQUAL_INT(1, off);
  TEST_ASSERT_EQUAL_INT(1, cc);
  TEST_ASSERT_EQUAL_INT(72, lastNote);
  TEST_ASSERT_EQUAL_INT(80, lastVel);
}

// ---- Valve 2-en-1 : sélection du rail selon la direction -------------------
namespace {
struct FakeAir : IAirSource {
  Rail blow = Rail::A;
  bool begin() override { return true; }
  void update(uint32_t) override {}
  void request(Direction, float) override {}
  void release(Direction) override {}
  float currentPressure(Direction) const override { return 0.0f; }
  Rail railForDirection(Direction d) const override {
    if (d == Direction::Blow) return blow;
    if (d == Direction::Draw) return (blow == Rail::A) ? Rail::B : Rail::A;
    return Rail::None;
  }
  bool supportsSimultaneousDirections() const override { return true; }
  void startHoming() override {}
  bool isHomed() const override { return true; }
  void startCentering() override {}
  AirStatus status() const override { return {}; }
};

// Source d'air qui enregistre la dernière intensité demandée (pour vibrato).
struct RecordingAir : IAirSource {
  float lastBlow = 0.0f, lastDraw = 0.0f;
  bool begin() override { return true; }
  void update(uint32_t) override {}
  void request(Direction d, float i) override { if (d == Direction::Blow) lastBlow = i; else if (d == Direction::Draw) lastDraw = i; }
  void release(Direction d) override { if (d == Direction::Blow) lastBlow = 0.0f; else if (d == Direction::Draw) lastDraw = 0.0f; }
  float currentPressure(Direction) const override { return 0.0f; }
  Rail railForDirection(Direction d) const override {
    return d == Direction::Blow ? Rail::A : (d == Direction::Draw ? Rail::B : Rail::None);
  }
  bool supportsSimultaneousDirections() const override { return true; }
  void startHoming() override {}
  bool isHomed() const override { return true; }
  void startCentering() override {}
  AirStatus status() const override { return {}; }
};
}  // namespace

void test_valve2in1_rail() {
  Config c = defaultCfg();
  MockServoBus bus; FakeAir air;
  Valve2in1 v(bus, c.valve); v.bindAirSource(&air); v.begin();
  v.setHoleState(0, Direction::Blow);
  TEST_ASSERT_EQUAL_INT(c.valve.holes2[0].railAangle, bus.lastAngle[0]);   // souffle -> rail A
  v.setHoleState(0, Direction::Draw);
  TEST_ASSERT_EQUAL_INT(c.valve.holes2[0].railBangle, bus.lastAngle[0]);   // aspir. -> rail B
  air.blow = Rail::B;                                                       // inversion des rôles
  v.setHoleState(0, Direction::Blow);
  TEST_ASSERT_EQUAL_INT(c.valve.holes2[0].railBangle, bus.lastAngle[0]);   // souffle -> rail B désormais
  v.setHoleState(0, Direction::Closed);
  TEST_ASSERT_EQUAL_INT(c.valve.holes2[0].closedAngle, bus.lastAngle[0]);  // fermé
}

// ---- Preset d'harmonica autonome -> HarmonicaCfg ---------------------------
void test_deserialize_harmonica_preset() {
  const char* preset = R"({"name":"Chromo","holeCount":12,"hasSlide":true,
    "notes":[{"note":60,"hole":0,"dir":"blow","slide":false},
             {"note":61,"hole":0,"dir":"blow","slide":true}]})";
  HarmonicaCfg h;
  TEST_ASSERT_TRUE(ConfigStore::deserializeHarmonica(preset, h));
  TEST_ASSERT_TRUE(h.hasSlide);
  TEST_ASSERT_EQUAL_INT(12, h.holeCount);
  TEST_ASSERT_EQUAL_INT(2, h.noteCount);
  HarmonicaMap m; m.load(h);
  TEST_ASSERT_TRUE(m.needsSlide());
  TEST_ASSERT_TRUE(m.lookup(61).slide);
  TEST_ASSERT_FALSE(m.lookup(60).slide);
}

// ---- Échange d'harmonica à chaud -------------------------------------------
void test_hot_swap_harmonica() {
  System* s = buildSystem(defaultCfg());                   // diatonique
  TEST_ASSERT_TRUE(s->map.lookup(60).valid);
  s->engine.handleMidi({MidiEvent::NoteOn, 0, 60, 100});
  TEST_ASSERT_EQUAL_INT(1, s->engine.activeVoiceCount());
  const char* preset = R"({"name":"X","holeCount":1,"hasSlide":false,"notes":[{"note":72,"hole":0,"dir":"blow"}]})";
  HarmonicaCfg h; TEST_ASSERT_TRUE(ConfigStore::deserializeHarmonica(preset, h));
  applyHarmonica(*s, h);
  TEST_ASSERT_EQUAL_INT(0, s->engine.activeVoiceCount());   // panic a coupé les notes
  TEST_ASSERT_FALSE(s->map.lookup(60).valid);               // ancien mapping parti
  TEST_ASSERT_TRUE(s->map.lookup(72).valid);                // nouveau mapping actif
}

// ---- saveHarmonica : remplace la section harmonica, préserve le reste ------
void test_save_harmonica_splice() {
  ConfigStore store; store.begin();
  TEST_ASSERT_EQUAL_INT(20, store.config().harmonica.noteCount);
  const char* preset = R"({"name":"Two","holeCount":1,"hasSlide":false,
    "notes":[{"note":60,"hole":0,"dir":"blow"},{"note":62,"hole":0,"dir":"draw"}]})";
  TEST_ASSERT_TRUE(store.saveHarmonica(preset));
  TEST_ASSERT_EQUAL_INT(2, store.config().harmonica.noteCount);
  TEST_ASSERT_EQUAL_INT((int)ValveImpl::Valve2in1, (int)store.config().valve.impl);  // reste préservé
}

// ---- Régulateur PI ---------------------------------------------------------
void test_pi_controller() {
  PIController pi; pi.configure(1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, pi.update(0.5f, 1.0f));     // proportionnel pur
  pi.configure(10.0f, 0.0f, 0.0f, 1.0f, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, pi.update(0.5f, 0.1f));     // borne haute
  pi.configure(1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pi.update(-0.5f, 1.0f));    // borne basse
  PIController w; w.configure(0.0f, 1.0f, 0.0f, 10.0f, 0.5f);        // anti-windup iMax=0.5
  w.update(1.0f, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, w.update(1.0f, 1.0f));      // intégrale plafonnée
}

// ---- Vibrato de pression (CC1) ---------------------------------------------
void test_vibrato_engine() {
  Config c = defaultCfg();                       // vibratoRateHz=5, depth=0.25
  MockServoBus bus; RecordingAir air; Valve2in1 valve(bus, c.valve);
  HarmonicaMap map; map.load(c.harmonica);
  NoteEngine eng; eng.begin(&air, &valve, &map, nullptr, c.engine);
  eng.handleMidi({MidiEvent::NoteOn, 0, 60, 64});                    // base ~0.5 (souffle)
  float i0 = air.lastBlow;
  eng.handleMidi({MidiEvent::ControlChange, 0, CC_MODULATION, 127}); // vibrato à fond
  eng.update(0);                                                     // sin(0)=0 -> inchangé
  eng.update(50);                                                    // quart de période (5 Hz)
  TEST_ASSERT_TRUE(air.lastBlow > i0 + 0.05f);                       // l'intensité a monté
}

// ---- Pitch-bend (parseur + moteur) -----------------------------------------
void test_pitchbend() {
  MidiParser p; float pb = -9.0f;
  MidiSink sink = [&](const MidiEvent& e) {
    if (e.type == MidiEvent::PitchBend) { int v = ((int)e.data2 << 7) | e.data1; pb = (v - 8192) / 8192.0f; }
  };
  uint8_t center[] = {0xE0, 0x00, 0x40}; for (uint8_t b : center) p.feed(b, sink);   // 8192
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pb);
  uint8_t up[] = {0x7F, 0x7F}; for (uint8_t b : up) p.feed(b, sink);                 // running status, max
  TEST_ASSERT_TRUE(pb > 0.9f);

  System* s = buildSystem(defaultCfg());
  s->engine.handleMidi({MidiEvent::PitchBend, 0, 0x00, 0x40});
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s->engine.pitchBend());
  s->engine.handleMidi({MidiEvent::PitchBend, 0, 0x7F, 0x7F});
  TEST_ASSERT_TRUE(s->engine.pitchBend() > 0.9f);
  TEST_ASSERT_TRUE(s->engine.pitchBendSemitones() > 1.8f);          // ~+2 demi-tons
}

// ---- Mapping d'une note "bendée" -------------------------------------------
void test_bend_mapping() {
  const char* preset = R"({"name":"B","holeCount":1,"hasSlide":false,
    "notes":[{"note":62,"hole":0,"dir":"draw","bend":-1.0}]})";
  HarmonicaCfg h; TEST_ASSERT_TRUE(ConfigStore::deserializeHarmonica(preset, h));
  HarmonicaMap m; m.load(h);
  auto e = m.lookup(62);
  TEST_ASSERT_TRUE(e.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.0f, e.bendSemitones);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_map_lookup);
  RUN_TEST(test_slide_mapping);
  RUN_TEST(test_mixed_capability);
  RUN_TEST(test_arbitration_reject);
  RUN_TEST(test_arbitration_steal);
  RUN_TEST(test_reversal_swaps_rail);
  RUN_TEST(test_midi_parser);
  RUN_TEST(test_valve2in1_rail);
  RUN_TEST(test_deserialize_harmonica_preset);
  RUN_TEST(test_hot_swap_harmonica);
  RUN_TEST(test_save_harmonica_splice);
  RUN_TEST(test_pi_controller);
  RUN_TEST(test_vibrato_engine);
  RUN_TEST(test_pitchbend);
  RUN_TEST(test_bend_mapping);
  return UNITY_END();
}
