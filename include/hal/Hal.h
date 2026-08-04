// ============================================================================
//  Hal.h — interfaces de la couche d'abstraction matérielle (HAL).
//
//  Air et valve ne parlent QU'À ces interfaces => on peut les piloter avec des
//  mocks (tests PC / mode simulation) ou du vrai matériel sans changer la
//  logique. Aucune dépendance Arduino ici.
// ============================================================================
#pragma once
#include "../Types.h"

namespace harm {

// Bus de servos (ex. PCA9685). Convertit un angle en impulsion selon la
// plage µs configurée dans l'implémentation concrète.
class IServoBus {
public:
  virtual ~IServoBus() = default;
  virtual bool begin() = 0;
  virtual void writeAngle(uint8_t channel, int deg) = 0;   // 0..180
  virtual void writeMicros(uint8_t channel, int us) = 0;   // impulsion directe (calibration)
};

// Moteur linéaire (piston sur tige filetée, ou actionneur de soufflet).
class IStepper {
public:
  virtual ~IStepper() = default;
  virtual void  begin() = 0;
  virtual void  enable(bool on) = 0;
  virtual void  setKinematics(float maxSpeedMmS, float accelMmS2, float stepsPerMm) = 0;
  virtual void  moveToMm(float mm) = 0;
  virtual float positionMm() const = 0;
  virtual bool  isRunning() const = 0;
  virtual void  run() = 0;    // à appeler très fréquemment (boucle de contrôle)
  virtual void  zero() = 0;   // fixe la position courante comme origine (0 mm)
};

// Capteur de pression (gauge/différentiel), magnitude en kPa après tare.
class IPressureSensor {
public:
  virtual ~IPressureSensor() = default;
  virtual bool  begin() = 0;
  virtual float readKpa() = 0;
  virtual void  tare() = 0;
};

// Fins de course mécaniques du vérin double.
class IEndstops {
public:
  virtual ~IEndstops() = default;
  virtual void begin() = 0;
  virtual bool triggeredR1() = 0;
  virtual bool triggeredR2() = 0;
  // Optionnel : relie une simulation de fin de course à la position du piston
  // (utilisé par les mocks ; sans effet sur le matériel réel). IStepper est
  // déclaré plus haut dans ce fichier.
  virtual void bindPiston(IStepper* /*piston*/, float /*travelMm*/) {}
};

}  // namespace harm
