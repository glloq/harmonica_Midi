// ============================================================================
//  PIController.h — régulateur Proportionnel-Intégral minimal.
//
//  Sortie bornée [outMin, outMax] et anti-emballement (intégrale bornée iMax).
//  Sans dépendance : testable sur PC. Utilisé par les sources d'air pour tenir
//  la pression cible en avançant le piston/soufflet proportionnellement à
//  l'erreur de pression.
// ============================================================================
#pragma once

namespace harm {

struct PIController {
  float kp = 1.0f, ki = 0.0f;
  float integral = 0.0f;
  float outMin = 0.0f, outMax = 1.0f, iMax = 1.0f;

  void configure(float p, float i, float omin, float omax, float imax) {
    kp = p; ki = i; outMin = omin; outMax = omax; iMax = imax; integral = 0.0f;
  }
  void reset() { integral = 0.0f; }

  float update(float error, float dt) {
    if (dt < 0.0f) dt = 0.0f;                        // garde : pas d'intégration sur dt invalide
    // Anti-windup conditionnel : on n'intègre PAS si la sortie brute est déjà
    // saturée et que l'erreur pousse encore dans le sens de la saturation.
    const float rawOut = kp * error + ki * (integral + error * dt);
    const bool saturating = (rawOut > outMax && error > 0.0f) || (rawOut < outMin && error < 0.0f);
    if (!saturating) {
      integral += error * dt;
      if (integral > iMax) integral = iMax;
      else if (integral < -iMax) integral = -iMax;
    }
    float out = kp * error + ki * integral;
    if (out > outMax) out = outMax;
    else if (out < outMin) out = outMin;
    return out;
  }
};

}  // namespace harm
