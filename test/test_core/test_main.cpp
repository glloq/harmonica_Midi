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
#include "air/PumpPair.h"
#include "air/SinglePumpReversible.h"
#include "valve/Valve2in1.h"
#include "valve/ValveSolenoid.h"
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
  AirCaps   caps() const override { AirCaps c; c.simultaneous = true; return c; }
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
  AirCaps   caps() const override { AirCaps c; c.simultaneous = true; return c; }
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
  float nominal = air.lastBlow;                                      // vibScale = 1
  eng.handleMidi({MidiEvent::ControlChange, 0, CC_MODULATION, 127}); // vibrato à fond
  eng.update(150);                                                   // 3/4 période @5Hz -> creux
  TEST_ASSERT_TRUE(air.lastBlow < nominal - 0.05f);                  // amplitude modulée vers le bas
  eng.update(50);                                                    // 1/4 période -> sommet ~= nominal
  TEST_ASSERT_FLOAT_WITHIN(0.02f, nominal, air.lastBlow);            // jamais au-dessus (pas d'écrêtage)
}

// ---- Vérin double : intensité -> consigne de pression (fix actionnement) ----
void test_pi_intensity_setpoint() {
  Config c = defaultCfg();
  MockServoBus bus; MockStepper st; MockPressure p1, p2; MockEndstops es;
  es.bindPiston(&st, c.air.dual.travelMm);
  DualReservoirPiston air(st, p1, p2, es, bus, c.air.dual);
  air.begin(); air.startHoming();
  for (int i = 0; i < 300 && !air.isHomed(); ++i) air.update(i);
  air.request(Direction::Blow, 1.0f); air.update(1000);
  st.posMm = 150.0f; st.targetMm = 150.0f; p1.setKpa(0.0f); p2.setKpa(0.0f);
  air.request(Direction::Blow, 1.0f); air.update(1001);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, c.air.dual.pressureTargetKpa, air.currentSetpointKpa());
  air.request(Direction::Blow, 0.5f); air.update(1002);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, c.air.dual.pressureTargetKpa * 0.5f, air.currentSetpointKpa());
}

// ---- Vérin double : retour SIGNÉ -> recompression après dépression (fix fabs) --
void test_pi_signed_feedback() {
  Config c = defaultCfg();
  MockServoBus bus; MockStepper st; MockPressure p1, p2; MockEndstops es;
  es.bindPiston(&st, c.air.dual.travelMm);
  DualReservoirPiston air(st, p1, p2, es, bus, c.air.dual);
  air.begin(); air.startHoming();
  for (int i = 0; i < 300 && !air.isHomed(); ++i) air.update(i);
  air.request(Direction::Blow, 1.0f); air.update(1000);
  st.posMm = 150.0f; st.targetMm = 150.0f;
  p1.setKpa(-0.4f); p2.setKpa(-0.4f);        // rail souffle en dépression
  air.update(1001);
  // Signé : erreur = 0.3-(-0.4) > 0 -> piston commandé pour recomprimer (≠ figé).
  TEST_ASSERT_TRUE(st.targetMm != 150.0f);
}

// ---- Note vers un trou hors holeCount : ignorée ----------------------------
void test_hole_out_of_range_ignored() {
  HarmonicaCfg h; h.holeCount = 4; h.noteCount = 2;
  h.notes[0] = {60, 2, Direction::Blow, false, 1.0f, 0.0f};   // trou 2 < 4 : valide
  h.notes[1] = {64, 9, Direction::Blow, false, 1.0f, 0.0f};   // trou 9 >= 4 : ignoré
  HarmonicaMap m; m.load(h);
  TEST_ASSERT_TRUE(m.lookup(60).valid);
  TEST_ASSERT_FALSE(m.lookup(64).valid);
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

// ============================================================================
//  Pompes continues (PumpPair) — régulation, veille, purge
// ============================================================================
void test_pumppair_regulates_and_idles() {
  PumpPairCfg cfg;                       // minDuty 0.15 par défaut sur les 2 pompes
  cfg.bleedChannel = 5;
  MockPwmOut blow("blow"), draw("draw");
  MockPressure pB, pD;
  MockServoBus servos;
  PumpPair air(blow, draw, pB, &pD, &servos, cfg);
  air.begin();
  TEST_ASSERT_TRUE(air.caps().simultaneous);
  TEST_ASSERT_FALSE(air.caps().hasPiston);
  TEST_ASSERT_TRUE(air.caps().hasPumps);
  TEST_ASSERT_EQUAL_INT((int)Rail::A, (int)air.railForDirection(Direction::Blow));
  TEST_ASSERT_EQUAL_INT((int)Rail::B, (int)air.railForDirection(Direction::Draw));
  TEST_ASSERT_EQUAL_INT(cfg.bleedOpenAngle, servos.lastAngle[5]);   // repos : purge ouverte

  air.request(Direction::Blow, 1.0f);    // pression mesurée nulle => le PI pousse
  for (uint32_t t = 0; t < 10; ++t) air.update(t * 20);
  TEST_ASSERT_TRUE(blow.value > cfg.blowPump.minDuty);
  TEST_ASSERT_EQUAL_INT(cfg.bleedClosedAngle, servos.lastAngle[5]);   // purge refermée
  TEST_ASSERT_EQUAL_FLOAT(0.0f, draw.value);                          // pompe aspiration au repos

  pB.setKpa(cfg.pressureTargetKpa);      // consigne atteinte => "prêt"
  for (uint32_t t = 10; t < 40; ++t) air.update(t * 20);
  TEST_ASSERT_TRUE(air.status().ready);

  air.release(Direction::Blow);
  air.update(900);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, blow.value);                          // retour à la veille
  TEST_ASSERT_EQUAL_INT(cfg.bleedOpenAngle, servos.lastAngle[5]);
}

void test_pumppair_manual_duty() {
  PumpPairCfg cfg;
  MockPwmOut blow("blow"), draw("draw");
  MockPressure pB, pD;
  MockServoBus servos;
  PumpPair air(blow, draw, pB, &pD, &servos, cfg);
  air.begin();
  TEST_ASSERT_TRUE(air.setManualDuty(Direction::Blow, 0.5f));
  air.update(20);
  // 0.5 remis à l'échelle dans [minDuty, maxDuty] = 0.15 + 0.5 x 0.85
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.575f, blow.value);
  air.setManualDuty(Direction::Blow, -1.0f);        // rendu à la régulation
  air.update(40);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, blow.value);        // aucune demande => veille
}

// Capteur unique : la dépression est déduite par symétrie.
void test_pumppair_shared_sensor() {
  PumpPairCfg cfg; cfg.sharedSensor = true;
  MockPwmOut blow("blow"), draw("draw");
  MockPressure pB;
  MockServoBus servos;
  PumpPair air(blow, draw, pB, nullptr, &servos, cfg);
  air.begin();
  pB.setKpa(0.2f);
  TEST_ASSERT_EQUAL_UINT8(1, air.caps().pressureSensors);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, air.currentPressure(Direction::Draw));
}

// ============================================================================
//  Pompe unique + aiguillage
// ============================================================================
void test_single_pump_diverter() {
  SinglePumpCfg cfg;                     // servo, souffle 30°, aspiration 150°, neutre 90°
  MockPwmOut pump("pump");
  MockPressure sensor;
  MockServoBus servos;
  SinglePumpReversible air(pump, sensor, &servos, nullptr, cfg);
  air.begin();
  TEST_ASSERT_FALSE(air.supportsSimultaneousDirections());
  TEST_ASSERT_TRUE(air.caps().hasDiverter);
  TEST_ASSERT_EQUAL_INT(cfg.neutralAngle, servos.lastAngle[cfg.diverterChannel]);

  air.update(0);
  air.request(Direction::Blow, 1.0f);
  TEST_ASSERT_EQUAL_INT(cfg.blowAngle, servos.lastAngle[cfg.diverterChannel]);
  TEST_ASSERT_TRUE(air.switching());                 // bascule en cours : pas encore prêt
  air.update(cfg.switchMs + 10);
  TEST_ASSERT_FALSE(air.switching());
  TEST_ASSERT_TRUE(pump.value > 0.0f);               // la pompe monte en régime

  air.request(Direction::Draw, 1.0f);                // l'aiguillage bascule
  TEST_ASSERT_EQUAL_INT(cfg.drawAngle, servos.lastAngle[cfg.diverterChannel]);
  air.release(Direction::Draw);
  TEST_ASSERT_EQUAL_INT(cfg.neutralAngle, servos.lastAngle[cfg.diverterChannel]);
}

// Variante électro-vanne : l'aiguillage passe par le bus de sorties.
void test_single_pump_solenoid_diverter() {
  SinglePumpCfg cfg;
  cfg.diverter = DiverterImpl::Solenoid;
  cfg.diverterChannel = 3;
  MockPwmOut pump("pump");
  MockPressure sensor;
  MockDigitalBus bus;
  SinglePumpReversible air(pump, sensor, nullptr, &bus, cfg);
  air.begin();
  air.request(Direction::Blow, 1.0f);
  TEST_ASSERT_TRUE(bus.on[3]);
  air.request(Direction::Draw, 1.0f);
  TEST_ASSERT_FALSE(bus.on[3]);
}

// ============================================================================
//  Valves à électro-vannes
// ============================================================================
void test_solenoid2in1_follows_rail() {
  Config c = defaultCfg();
  c.valve.impl = ValveImpl::Solenoid2in1;
  c.valve.holeCount = 2;
  c.valve.holesS2[0] = {0, 0, 1};
  c.valve.holesS2[1] = {1, 2, 3};
  MockDigitalBus bus;
  FakeAir air;
  ValveSolenoid2in1 valve(bus, c.valve);
  valve.bindAirSource(&air);
  valve.begin();
  TEST_ASSERT_TRUE(valve.supportsPerHoleDirection());

  valve.setHoleState(0, Direction::Blow);          // souffle = rail A -> canal 0
  TEST_ASSERT_TRUE(bus.on[0]);
  TEST_ASSERT_FALSE(bus.on[1]);
  valve.setHoleState(0, Direction::Draw);          // aspiration = rail B -> canal 1
  TEST_ASSERT_FALSE(bus.on[0]);
  TEST_ASSERT_TRUE(bus.on[1]);

  air.blow = Rail::B;                              // inversion du piston
  valve.setHoleState(0, Direction::Blow);          // le souffle bascule sur le canal 1
  TEST_ASSERT_FALSE(bus.on[0]);
  TEST_ASSERT_TRUE(bus.on[1]);

  valve.setHoleState(0, Direction::Closed);
  TEST_ASSERT_FALSE(bus.on[0]);
  TEST_ASSERT_FALSE(bus.on[1]);
}

// « Peak & hold » : pic plein courant puis maintien réduit.
void test_solenoid_peak_and_hold() {
  Config c = defaultCfg();
  c.valve.impl = ValveImpl::Solenoid1in1;
  c.valve.holeCount = 1;
  c.valve.holesS1[0] = {0, 4};
  c.valve.solenoids.holdDuty = 0.4f;
  c.valve.solenoids.peakMs = 50;
  MockDigitalBus bus;
  ValveSolenoid1in1 valve(bus, c.valve);
  valve.begin();
  TEST_ASSERT_FALSE(valve.supportsPerHoleDirection());

  valve.update(1000);
  valve.setHoleState(0, Direction::Blow);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, bus.duty[4]);      // pic
  valve.update(1030);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, bus.duty[4]);      // toujours dans le pic
  valve.update(1060);
  TEST_ASSERT_EQUAL_FLOAT(0.4f, bus.duty[4]);      // maintien réduit
  valve.setHoleState(0, Direction::Closed);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, bus.duty[4]);
}

// ============================================================================
//  Moteur de jeu : bends actionnés, délai valve->air, note bloquée, CC7
// ============================================================================
namespace {
// Moteur de jeu monté sur une source d'air enregistreuse (aucun matériel).
struct EngineRig {
  Config c;
  MockServoBus bus;
  RecordingAir air;
  Valve2in1 valve;
  HarmonicaMap map;
  NoteEngine engine;
  explicit EngineRig(Config cfg) : c(cfg), valve(bus, c.valve) {
    map.load(c.harmonica);
    engine.begin(&air, &valve, &map, nullptr, c.engine);
  }
};
}  // namespace

void test_bend_actuation() {
  Config c = defaultCfg();
  c.engine.bendEnabled = true;
  c.engine.bendPressureGain = 0.5f;
  c.engine.velocityToIntensity = false;      // base = 1.0 : on isole l'effet du bend
  c.harmonica.noteCount = 2;
  c.harmonica.notes[0] = {60, 0, Direction::Draw, false, 0.5f, 0.0f};
  c.harmonica.notes[1] = {61, 1, Direction::Draw, false, 0.5f, -1.0f};   // bend d'un demi-ton
  EngineRig r(c);
  r.engine.handleMidi({MidiEvent::NoteOn, 0, 60, 100});
  const float natural = r.air.lastDraw;
  r.engine.handleMidi({MidiEvent::NoteOff, 0, 60, 0});
  r.engine.handleMidi({MidiEvent::NoteOn, 0, 61, 100});
  const float bent = r.air.lastDraw;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, natural);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.75f, bent);       // 0.5 x (1 + 0.5 x 1 demi-ton)
  TEST_ASSERT_TRUE(bent > natural);
}

// Bend désactivé : la note bendée demande la même pression que la naturelle.
void test_bend_actuation_disabled() {
  Config c = defaultCfg();
  c.engine.bendEnabled = false;
  c.engine.velocityToIntensity = false;
  c.harmonica.noteCount = 1;
  c.harmonica.notes[0] = {61, 1, Direction::Draw, false, 0.5f, -1.0f};
  EngineRig r(c);
  r.engine.handleMidi({MidiEvent::NoteOn, 0, 61, 100});
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, r.air.lastDraw);
}

void test_hole_settle_delays_air() {
  Config c = defaultCfg();
  c.engine.holeSettleMs = 20;              // la valve part avant l'air
  EngineRig r(c);
  r.engine.update(1000);
  r.engine.handleMidi({MidiEvent::NoteOn, 0, 60, 100});
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.air.lastBlow);        // air pas encore accordé
  TEST_ASSERT_EQUAL_INT(30, r.bus.lastAngle[0]);        // ... mais la valve a bougé (rail A = 30°)
  r.engine.update(1010);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.air.lastBlow);
  r.engine.update(1025);
  TEST_ASSERT_TRUE(r.air.lastBlow > 0.0f);              // course finie : l'air suit
}

void test_note_max_hold_cuts_stuck_note() {
  Config c = defaultCfg();
  c.engine.noteMaxHoldMs = 100;
  EngineRig r(c);
  r.engine.update(0);
  r.engine.handleMidi({MidiEvent::NoteOn, 0, 60, 100});   // NoteOff jamais reçu
  TEST_ASSERT_EQUAL_INT(1, r.engine.activeVoiceCount());
  r.engine.update(50);
  TEST_ASSERT_EQUAL_INT(1, r.engine.activeVoiceCount());
  r.engine.update(200);
  TEST_ASSERT_EQUAL_INT(0, r.engine.activeVoiceCount());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.air.lastBlow);
}

void test_cc7_volume() {
  Config c = defaultCfg();
  c.engine.velocityToIntensity = false;
  EngineRig r(c);
  r.engine.handleMidi({MidiEvent::NoteOn, 0, 60, 100});
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, r.air.lastBlow);
  r.engine.handleMidi({MidiEvent::ControlChange, 0, CC_VOLUME, 64});
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.5f, r.air.lastBlow);
  r.engine.handleMidi({MidiEvent::ControlChange, 0, CC_VOLUME, 127});
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, r.air.lastBlow);
}

// Le masque de trous alimente l'affichage temps réel de l'UI.
void test_hole_mask() {
  Config c = defaultCfg();
  EngineRig r(c);
  r.engine.handleMidi({MidiEvent::NoteOn, 0, 60, 100});   // trou 0, souffle
  r.engine.handleMidi({MidiEvent::NoteOn, 0, 71, 100});   // trou 2, aspiration
  TEST_ASSERT_EQUAL_UINT32(1u, r.engine.holeMask(Direction::Blow));
  TEST_ASSERT_EQUAL_UINT32(4u, r.engine.holeMask(Direction::Draw));
  r.engine.panic();
  TEST_ASSERT_EQUAL_UINT32(0u, r.engine.holeMask(Direction::Blow));
}

// ============================================================================
//  Configuration : nouvelles sections et cohérence du nombre de trous
// ============================================================================
void test_parse_new_air_sections() {
  Config c = defaultCfg();
  TEST_ASSERT_EQUAL_INT(2, c.version);
  TEST_ASSERT_EQUAL_INT((int)PumpDrive::Ledc, (int)c.air.pumps.blowPump.drive);
  TEST_ASSERT_EQUAL_INT(18, c.air.pumps.blowPump.pin);
  TEST_ASSERT_EQUAL_INT(19, c.air.pumps.drawPump.pin);
  TEST_ASSERT_EQUAL_UINT8(0x77, c.air.pumps.drawSensor.addr);
  TEST_ASSERT_EQUAL_UINT8(255, c.air.pumps.bleedChannel);
  TEST_ASSERT_EQUAL_INT((int)DiverterImpl::Servo, (int)c.air.singlePump.diverter);
  TEST_ASSERT_EQUAL_INT(150, c.air.singlePump.switchMs);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.4f, c.valve.solenoids.holdDuty);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.15f, c.engine.bendPressureGain);
  TEST_ASSERT_TRUE(c.system.autoHomeOnBoot);
}

// Le nombre de trous doit suivre la LISTE de l'implémentation sélectionnée.
void test_holecount_follows_valve_impl() {
  const char* json = R"({
    "valve": { "impl": "solenoid2in1",
      "valve2in1": { "holes": [ {"hole":0,"channel":0}, {"hole":1,"channel":1}, {"hole":2,"channel":2} ] },
      "solenoid2in1": { "holes": [ {"hole":0,"railAchannel":4,"railBchannel":5} ] } } })";
  Config c;
  TEST_ASSERT_TRUE(ConfigStore::deserialize(json, c));
  TEST_ASSERT_EQUAL_INT((int)ValveImpl::Solenoid2in1, (int)c.valve.impl);
  TEST_ASSERT_EQUAL_UINT8(1, c.valve.holeCount);
  TEST_ASSERT_EQUAL_UINT8(4, c.valve.holesS2[0].railAchannel);
  TEST_ASSERT_EQUAL_UINT8(5, c.valve.holesS2[0].railBchannel);
}

void test_parse_gpio_solenoid_bus() {
  const char* json = R"({ "valve": { "impl": "solenoid1in1",
    "solenoids": { "impl": "gpio", "activeLow": true, "gpioPins": [13, 14, 27] },
    "solenoid1in1": { "holes": [ {"hole":0,"channel":0} ] } } })";
  Config c;
  TEST_ASSERT_TRUE(ConfigStore::deserialize(json, c));
  TEST_ASSERT_EQUAL_INT((int)DigitalBusImpl::Gpio, (int)c.valve.solenoids.impl);
  TEST_ASSERT_TRUE(c.valve.solenoids.activeLow);
  TEST_ASSERT_EQUAL_UINT8(3, c.valve.solenoids.gpioCount);
  TEST_ASSERT_EQUAL_INT(27, c.valve.solenoids.gpioPins[2]);
}

// ============================================================================
//  Factory : les 16 combinaisons air x valve se montent et jouent
// ============================================================================
void test_factory_matrix() {
  const AirImpl airs[] = {AirImpl::DualReservoirPiston, AirImpl::SingleBellows,
                          AirImpl::PumpPair, AirImpl::SinglePumpReversible};
  const ValveImpl valves[] = {ValveImpl::Valve2in1, ValveImpl::Valve1in1,
                              ValveImpl::Solenoid2in1, ValveImpl::Solenoid1in1};
  for (AirImpl a : airs) {
    for (ValveImpl v : valves) {
      Config c = defaultCfg();
      c.air.impl = a;
      c.valve.impl = v;
      c.valve.holeCount = 2;                       // les listes de secours en ont 2
      c.slide.enabled = true;
      c.slide.impl = (v == ValveImpl::Solenoid1in1) ? SlideImpl::Solenoid : SlideImpl::Servo;
      System* s = buildSystem(c);
      TEST_ASSERT_NOT_NULL(s->air);
      TEST_ASSERT_NOT_NULL(s->valve);
      TEST_ASSERT_NOT_NULL(s->slide);
      // Souffle+aspiration simultanés = capacité de la source ET de la valve.
      const bool expected = s->air->caps().simultaneous && s->valve->supportsPerHoleDirection();
      TEST_ASSERT_EQUAL_INT(expected, s->engine.mixedCapable());
      s->air->startHoming();
      for (uint32_t t = 0; t < 200; ++t) { s->air->update(t); s->valve->update(t); s->engine.update(t); }
      s->engine.handleMidi({MidiEvent::NoteOn, 0, 60, 100});
      for (uint32_t t = 200; t < 240; ++t) { s->air->update(t); s->valve->update(t); s->engine.update(t); }
      TEST_ASSERT_EQUAL_INT(1, s->engine.activeVoiceCount());
      s->engine.handleMidi({MidiEvent::NoteOff, 0, 60, 0});
      TEST_ASSERT_EQUAL_INT(0, s->engine.activeVoiceCount());
    }
  }
}

// Un slide à électroaimant s'engage sur le bus de sorties, pas sur les servos.
void test_solenoid_slide() {
  MockDigitalBus bus;
  SlideCfg cfg; cfg.enabled = true; cfg.impl = SlideImpl::Solenoid; cfg.channel = 7;
  SolenoidSlide slide(bus, cfg);
  slide.begin();
  TEST_ASSERT_FALSE(bus.on[7]);
  slide.setEngaged(true);
  TEST_ASSERT_TRUE(bus.on[7]);
  TEST_ASSERT_TRUE(slide.engaged());
  slide.setEngaged(false);
  TEST_ASSERT_FALSE(bus.on[7]);
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
  RUN_TEST(test_pi_intensity_setpoint);
  RUN_TEST(test_pi_signed_feedback);
  RUN_TEST(test_hole_out_of_range_ignored);
  RUN_TEST(test_pitchbend);
  RUN_TEST(test_bend_mapping);
  RUN_TEST(test_pumppair_regulates_and_idles);
  RUN_TEST(test_pumppair_manual_duty);
  RUN_TEST(test_pumppair_shared_sensor);
  RUN_TEST(test_single_pump_diverter);
  RUN_TEST(test_single_pump_solenoid_diverter);
  RUN_TEST(test_solenoid2in1_follows_rail);
  RUN_TEST(test_solenoid_peak_and_hold);
  RUN_TEST(test_bend_actuation);
  RUN_TEST(test_bend_actuation_disabled);
  RUN_TEST(test_hole_settle_delays_air);
  RUN_TEST(test_note_max_hold_cuts_stuck_note);
  RUN_TEST(test_cc7_volume);
  RUN_TEST(test_hole_mask);
  RUN_TEST(test_parse_new_air_sections);
  RUN_TEST(test_holecount_follows_valve_impl);
  RUN_TEST(test_parse_gpio_solenoid_bus);
  RUN_TEST(test_factory_matrix);
  RUN_TEST(test_solenoid_slide);
  return UNITY_END();
}
