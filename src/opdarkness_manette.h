// opdarkness - manette virtuelle pilotée par un script (essais automatiques, voir opdarkness_manette.cpp).

#pragma once

#include <rex/runtime.h>

namespace opdarkness::manette {

// Enveloppe config.input_factory pour ajouter la manette du script si od_manette n'est pas vide.
// À appeler dans OnPreSetup.
void WrapInput(rex::RuntimeConfig& config);

}  // namespace opdarkness::manette
