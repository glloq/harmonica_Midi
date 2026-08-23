// ============================================================================
//  IAirSource.h — interface "stratégie" de la source d'air.
//
//  Le moteur exprime une INTENTION acoustique (souffle/aspiration + intensité).
//  L'implémentation traduit ça en mouvement moteur + ouverture de valves, et
//  expose quelle correspondance Rail<->Direction est active (elle peut changer).
// ============================================================================
#pragma once
#include "../Types.h"

namespace harm {

class IAirSource {
public:
  virtual ~IAirSource() = default;
  virtual bool  begin() = 0;
  virtual void  update(uint32_t nowMs) = 0;          // tick de contrôle non bloquant

  virtual void  request(Direction dir, float intensity01) = 0;  // demande d'une direction
  virtual void  release(Direction dir) = 0;

  virtual float currentPressure(Direction dir) const = 0;   // kPa (magnitude)
  virtual Rail  railForDirection(Direction dir) const = 0;  // quel réservoir MAINTENANT
  virtual bool  supportsSimultaneousDirections() const = 0; // vérin=true, soufflet=false

  virtual void  startHoming() = 0;
  virtual bool  isHomed() const = 0;
  virtual void  startCentering() = 0;                // ouvre les 2 valves, pas de son
  virtual AirStatus status() const = 0;
  virtual AirCaps   caps() const = 0;                       // ce que le montage sait faire
  virtual float currentSetpointKpa() const { return 0.0f; }   // consigne PI (télémétrie)

  // Banc d'essai : force le régime d'une pompe (duty < 0 => retour à la régulation).
  // Renvoie false si le montage n'a pas de pompe pilotable.
  virtual bool  setManualDuty(Direction /*dir*/, float /*duty01*/) { return false; }
};

}  // namespace harm
