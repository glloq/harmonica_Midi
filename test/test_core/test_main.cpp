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
  return UNITY_END();
}
