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

namespace harm {

struct System {
  IServoBus*       servos   = nullptr;
  IStepper*        stepper  = nullptr;
  IPressureSensor* pA       = nullptr;   // R1 (vérin) ou chambre unique (soufflet)
  IPressureSensor* pB       = nullptr;   // R2 (vérin) ; nullptr pour soufflet
  IEndstops*       endstops = nullptr;   // vérin ; nullptr pour soufflet
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

}  // namespace harm
