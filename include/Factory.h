// ============================================================================
//  Factory.h — construit le système complet à partir d'une Config.
//
//  C'est le SEUL endroit qui connaît les classes concrètes : il choisit
//  l'implémentation air/valve/transport et injecte la HAL réelle ou mock.
//  Rien n'est libéré (objets construits une fois au boot).
// ============================================================================
#pragma once
#include "Config.h"
#include "hal/Hal.h"
#include "air/IAirSource.h"
#include "valve/IValveDriver.h"
#include "valve/ISlideActuator.h"
#include "engine/HarmonicaMap.h"
#include "engine/NoteEngine.h"
#include "midi/MidiRouter.h"
#include "hal/Hal.h"

namespace harm {

struct System {
  IServoBus*       servos   = nullptr;
  IDigitalOutBus*  solenoids = nullptr;  // électro-vannes / électroaimants ; nullptr si aucun
  IStepper*        stepper  = nullptr;   // vérin / soufflet ; nullptr pour les pompes
  IPwmOut*         pumpBlow = nullptr;   // pompe souffle (ou pompe unique) ; nullptr sinon
  IPwmOut*         pumpDraw = nullptr;   // pompe aspiration ; nullptr sinon
  IPressureSensor* pA       = nullptr;   // R1 / chambre / plenum souffle
  IPressureSensor* pB       = nullptr;   // R2 / plenum aspiration ; nullptr si un seul capteur
  IEndstops*       endstops = nullptr;   // vérin ; nullptr sinon
  IAirSource*      air      = nullptr;
  IValveDriver*    valve    = nullptr;
  ISlideActuator*  slide    = nullptr;   // nullptr si non chromatique
  HarmonicaMap     map;
  NoteEngine       engine;
  MidiRouter       router;
  Config           cfg;
  bool             mock = false;
};

System* buildSystem(const Config& cfg);

// Échange l'harmonica active à chaud : coupe les notes, recharge le mapping et
// met à jour la config en mémoire (aucun redémarrage). Le câblage air/valve et
// le slide restent inchangés — passer au chromatique nécessite aussi slide.enabled.
inline void applyHarmonica(System& s, const HarmonicaCfg& h) {
  s.engine.panic();
  s.map.load(h);
  s.cfg.harmonica = h;
}

}  // namespace harm
