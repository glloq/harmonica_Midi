// ============================================================================
//  IValveDriver.h — interface "stratégie" de la distribution d'air par trou.
// ============================================================================
#pragma once
#include "../Types.h"
#include "../air/IAirSource.h"

namespace harm {

class IValveDriver {
public:
  virtual ~IValveDriver() = default;
  virtual bool    begin() = 0;
  virtual void    update(uint32_t nowMs) = 0;                    // lissage optionnel
  virtual void    setHoleState(uint8_t hole, Direction dir) = 0; // Closed / Blow / Draw
  virtual void    allOff() = 0;
  virtual uint8_t holeCount() const = 0;
  virtual bool    supportsPerHoleDirection() const = 0;  // 2in1=true, 1in1=false
  virtual void    bindAirSource(IAirSource* src) = 0;    // 2in1 : Direction -> Rail -> angle
};

}  // namespace harm
