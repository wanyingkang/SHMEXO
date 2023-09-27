// C/C++ headers
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <sstream>    // stringstream
#include <cmath>
#include <cassert>  // assert

// Athena++ headers
#include "../../athena_arrays.hpp"
#include "../../parameter_input.hpp"
#include "../../globals.hpp"
#include "../../scalars/scalars.hpp"
#include "../radiation.hpp"
#include "absorber.hpp"
#include "ionizing_absorber.hpp"

IonizingAbsorber::IonizingAbsorber(RadiationBand *pband, std::string name, int my_scalar_number, int my_ion_number, ParameterInput *pin):
  Absorber(pband, name, my_scalar_number, pin)
{
  ion_num = my_ion_number;
  ionization_energy = ps->energy(my_ion_number) - ps->energy(my_scalar_number);
  nu_0 = ionization_energy/planck_constant;
  lambda_0 = speed_of_light/nu_0;

  Spectrum *spec = pband->spec;
  int nspec = pband->nspec;

  CalculateEnergyFunctions(spec, nspec);
}

void IonizingAbsorber::CalculateEnergyFunctions(Spectrum const *spec, int nspec) {
  Real wave;

  for (int n = 0; n < nspec; ++n) {
    wave = spec[n].wave * pmy_band->wavelength_coefficient;
    if (wave > lambda_0) {
      // what do we do!!
      // region where cross section is zero so should never be relevant
      // default values
      h(n) = 0.0;
      q(n) = 0.0; 
    }
    else {
      h(n) = wave/lambda_0;
      q(n) = 1.0 - h(n);
    }
  }
}