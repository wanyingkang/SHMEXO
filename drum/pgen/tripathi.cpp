// C/C++ header
#include <ctime>
#include <cmath>

// Athena++ headers
#include "../athena.hpp"
#include "../athena_arrays.hpp"
#include "../parameter_input.hpp"
#include "../coordinates/coordinates.hpp"
#include "../eos/eos.hpp"
#include "../field/field.hpp"
#include "../hydro/hydro.hpp"
#include "../mesh/mesh.hpp"
#include "../globals.hpp"
#include "../math/interpolation.h"
#include "../utils/utils.hpp"
#include "../thermodynamics/thermodynamics.hpp"
#include "../radiation/radiation.hpp"
#include "../radiation/freedman_mean.hpp"
#include "../radiation/hydrogen_absorber.hpp"
#include "../radiation/user_absorber.hpp"
#include "../radiation/null_absorber.hpp"
#include "../scalars/scalars.hpp"

// user set variables
Real G, Mp, Ms, Rp, period, a;
Real dfloor, pfloor, sfloor;
Real gas_gamma;
Real wave_to_meters_conversion;

Real dist_, ref_dist_;
Real r_0, r_e, rho_0, rho_e, P_0, P_e;

AthenaArray<Real> rad_flux_ion;
AthenaArray<Real> rad_eng;
AthenaArray<Real> ionization_rate, recombination_rate, recombinative_cooling, lya_cooling, output_du;

//Constants -- for problem file?
Real A0=6.30431812E-22; // (m^2)
Real nu_0=3.2898419603E15; // (1/s) Rydberg frequency
Real c=2.998E8; // m/s
Real mh=1.674E-27; // kg
Real nm_0=91.126705058; // Hydrogen ionization wavelength in nm
Real Ry=2.1798723611E-18;// J (joules)
Real rho_p = 1.0e-12; // kg/m3
Real cs    = 3.0e3;   // m/s

// Utility Functions
Real getRad(Coordinates *pcoord, int i, int j, int k);
Real rho_func(Real r);
Real press_func(Real rho);

//----------------------------------------------------------------------------------------
// Output Variables
//----------------------------------------------------------------------------------------
void MeshBlock::InitUserMeshBlockData(ParameterInput *pin)
{
  // User outputs
  AllocateUserOutputVariables(10);
  SetUserOutputVariableName(0, "temp");
  SetUserOutputVariableName(1, "g1");
  SetUserOutputVariableName(2, "g2");
  SetUserOutputVariableName(3, "g3");
  SetUserOutputVariableName(4, "rad_eng");
  SetUserOutputVariableName(5, "ionization_rate");
  SetUserOutputVariableName(6, "recombination_rate");
  SetUserOutputVariableName(7, "recombinative_cooling");
  SetUserOutputVariableName(8, "lya_cooling");
  SetUserOutputVariableName(9, "du");

  // User mesh data
  // 0 -- rad_flux_ion
  // 0 -- rad_flux_ion
  // 0 -- rad_flux_ion
  // 0 -- rad_flux_ion
  // 0 -- rad_flux_ion
  // 0 -- rad_flux_ion
  AllocateRealUserMeshBlockDataField(1);
  ruser_meshblock_data[0].NewAthenaArray();
}

void MeshBlock::UserWorkBeforeOutput(ParameterInput *pin)
{
  Real dq[1+NVAPOR], rh;

  AthenaArray<Real> vol, farea;
  vol.NewAthenaArray(ncells1);
  farea.NewAthenaArray(ncells1+1);

  for (int k = ks; k <= ke; ++k) {
    for (int j = js; j <= je; ++j) {
      pcoord->Face1Area(k, j, is, ie+1, farea);
      pcoord->CellVolume(k,j, is, ie,   vol);

      for (int i = is-NGHOST; i <= ie+NGHOST; ++i) {
        Real R = pthermo->GetRd();
        Real T = phydro->w(IPR,k,j,i)/(R * phydro->w(IDN,k,j,i));
        Real ion_f = pscalars->r(1,k,j,i);
        T = T * (1.-ion_f/2.);

        user_out_var(0,k,j,i) = T;
        user_out_var(1,k,j,i) = phydro->hsrc.g1(k,j,i);
        user_out_var(2,k,j,i) = phydro->hsrc.g2(k,j,i);
        user_out_var(3,k,j,i) = phydro->hsrc.g3(k,j,i);

        user_out_var(4,k,j,i) = rad_eng(k,j,i);
        user_out_var(5,k,j,i) = ionization_rate(k,j,i);
        user_out_var(6,k,j,i) = recombination_rate(k,j,i);
        user_out_var(7,k,j,i) = recombinative_cooling(k,j,i);
        user_out_var(8,k,j,i) = lya_cooling(k,j,i);
        user_out_var(9,k,j,i) = output_du(k,j,i);
      }
    }
  }
}
//----------------------------------------------------------------------------------------
// Absorber Info
//----------------------------------------------------------------------------------------
// oddly expensive calculation!
Real AbsorptionCoefficient(Absorber const *pabs, Real wave, Real const prim[], int k, int j, int i)
{
  // convert microns to meters
  Real freq = c / (wave * wave_to_meters_conversion);

  PassiveScalars *ps = pabs->pmy_band->pmy_rad->pmy_block->pscalars;
  Real n_neutral = ps->s(0,k,j,i)/mh;

  // cover low energy and edge case
  if (freq < nu_0)
    return 0.;
  else if (freq == nu_0)
    return A0*n_neutral;

  // Real eps   = sqrt(freq/nu_0 - 1.);
  // Real term1 = A0 * pow(nu_0/freq,4.);
  
  // Real numerator   = exp(4. - 4.*(atan2(eps,1)/eps));
  // Real denominator = 1. - exp(-(2*M_PI)/eps);

  // Real sigma = term1 * numerator/denominator; // cross-section m^2

  // return sigma * n_neutral; // 1/m

  return A0 * n_neutral; // 1/m
}

// makes shit slow!
Real EnergyAbsorption(Absorber *pabs, Real wave, Real flux, int k, int j, int i)
{
  Real wave_nm = (wave * wave_to_meters_conversion) * 1e9;
  if (wave_nm > nm_0) {
    // energy not absorbed
    // should also be reflected in tau_lambda = 0
    return flux;
  }
  else {
    // fraction of energy turned into heat
    // removes ionizatoin energy cost

    // Real energy_fraction = 1 - (wave_nm/nm_0);
    Real energy_fraction = 0.15;

    rad_flux_ion(k,j,i) += (1-energy_fraction) * flux;

    return flux*energy_fraction;
  }
}

void RadiationBand::AddAbsorber(std::string name, std::string file, ParameterInput *pin)
{ 
  std::stringstream msg;
  if (name == "HYDROGEN_IONIZATION") {
    UserDefinedAbsorber hydrogen_ionization(this);

    hydrogen_ionization.EnrollUserAbsorptionCoefficientFunc(AbsorptionCoefficient);
    hydrogen_ionization.EnrollUserEnergyAbsorptionFunc(EnergyAbsorption);
    pabs->AddAbsorber(hydrogen_ionization);
  } else {
    msg << "### FATAL ERROR in RadiationBand::AddAbsorber"
        << std::endl << "unknown absorber: '" << name <<"' ";
    ATHENA_ERROR(msg);
  }
}

//----------------------------------------------------------------------------------------
// Additional Physics
//----------------------------------------------------------------------------------------

void SourceTerms(MeshBlock *pmb, const Real time, const Real dt,
  const AthenaArray<Real> &w, const AthenaArray<Real> &r,
  const AthenaArray<Real> &bcc,
  AthenaArray<Real> &du, AthenaArray<Real> &ds)
{
  int is = pmb->is, js = pmb->js, ks = pmb->ks;
  int ie = pmb->ie, je = pmb->je, ke = pmb->ke;

  AthenaArray<Real> vol;
  vol.NewAthenaArray(pmb->ncells1);

  PassiveScalars *ps = pmb->pscalars;

  for (int k = ks; k <= ke; ++k) {
    for (int j = js; j <= je; ++j) {
      pmb->pcoord->CellVolume(k,j,is,ie,vol);

      for (int i = is; i <= ie; ++i) {
        // just for output land
        rad_eng(k,j,i) = pmb->prad->du(k,j,i); // J m-3 s-1

        // number densities
        Real n_ion = ps->s(1,k,j,i)/mh;
        Real n_neu = ps->s(0,k,j,i)/mh;

        //ionization
        // negative sign bc downward energy transfer
        Real energy_ion = -dt * rad_flux_ion(k,j,i);
        Real n_ion_gain = energy_ion/Ry/vol(i);

        // Real n = ps->s(0,k,j,i)/mh;
        // Real Fx = pmb->prad->pband->bflxdn(k,j,i)/2.563e-18;// Divide by 16eV per photon to get photon number flux
        // Real I = Fx * (n_neu * A0);
        // Real n_ion_gain = dt * I;

        if (n_ion_gain > n_neu) {
          std::stringstream msg;
          msg << "### FATAL ERROR in SourceTerms" << std::endl
              << "    n_ion_gain ("<< n_ion_gain <<") is greater than n_neutral ("<< n_neu <<")." << std::endl
              << "    Re-run with lower timestep." << std::endl;
        }

        // recombination
        Real R = pmb->pthermo->GetRd();
        Real T = w(IPR,k,j,i)/(R * w(IDN,k,j,i));
        Real ion_f = ps->r(1,k,j,i);
        T = T * (1.- ion_f/2.);

        // Real T = pmb->pthermo->Temp(pmb->phydro->w.at(k,j,i));
        Real alpha_B  = 2.59E-19 * pow(T/1.E4,-0.7); // m3 s-1
        Real n_recomb = dt * alpha_B * (n_ion * n_ion); //m-3

        // cooling processes
        // -- recomb cooling
        Real kb = 1.3806504E-23; // J/K
        Real recomb_cooling_const = 6.11E-16 * pow(T,-0.89); // m3 s-1
        Real recomb_cooling_rate = recomb_cooling_const * (kb * T) * (n_ion * n_ion); //J s-1 m-3

        // -- lya cooling
        Real lya_cooling_const = 7.5E-32 * exp(-118348./T); // J m3 s-1
        Real lya_cooling_rate = lya_cooling_const * n_ion * n_neu; // J m-3 s-1

        du(IEN,k,j,i) -= (recomb_cooling_rate + lya_cooling_rate) * dt; // J m-3
        ds(0,k,j,i) += ( n_recomb - n_ion_gain) * mh;
        ds(1,k,j,i) += (-n_recomb + n_ion_gain) * mh;

        ionization_rate(k,j,i) = n_ion_gain/dt;
        recombination_rate(k,j,i) = n_recomb/dt;

        lya_cooling(k,j,i) = lya_cooling_rate;
        recombinative_cooling(k,j,i) = recomb_cooling_rate;
        output_du(k,j,i) = du(IEN,k,j,i);
      }
    }
  }

  // clear eng deposited for next iteration
  rad_flux_ion.ZeroClear();
}

// needs validation
void Gravity(MeshBlock *pmb, AthenaArray<Real> &g1, AthenaArray<Real> &g2, AthenaArray<Real> &g3) {
  int is = pmb->is, js = pmb->js, ks = pmb->ks;
  int ie = pmb->ie, je = pmb->je, ke = pmb->ke;

  Real x,y,z;
  Real r, rsq, rs, rs_sq;
  Real gp, gs, gc;

  bool spherical_coords;
  if (std::strcmp(COORDINATE_SYSTEM, "cartesian") == 0) {
    spherical_coords = false;
  }
  else if (std::strcmp(COORDINATE_SYSTEM, "spherical_polar") == 0) {
    spherical_coords = true;
  }


  for (int k=ks; k<=ke; ++k) {
    for (int j=js; j<=je; ++j) {
      for (int i=is-NGHOST; i<=ie+NGHOST; ++i) {
        // simplify in 1D spherical
        if (spherical_coords) {
          x = pmb->pcoord->x1v(i);
          y = z = 0;

          r = x;
          rs = a-x;
        }
        else {
          x = pmb->pcoord->x1v(i);
          y = pmb->pcoord->x2v(j);
          z = pmb->pcoord->x3v(k);

          // planet placed at (x,y,z) = (0,0,0)
          rsq = x*x + y*y + z*z;
          r = sqrt(rsq);

          // star placed at (x,y,z) = (a,0,0)
          rs_sq = (a-x)*(a-x) + y*y + z*z;
          rs = sqrt(rs_sq);
          // rs = (a-x, -y, -z)
        }

        // enforce rmin for center of planet
        if (r < 0.2 * Rp) {
          r = 0.2*Rp;
        }

        // magnitudes (div by r)
        // planet -- in (-r) direction
        gp = (G * Mp) / pow(r,3);

        // star -- in (a-r) direction
        gs = (G * Ms) / pow(rs,3);
        // centrifugal -- in ( -(a-r) ) direction (note negative sign)
        gc = (G * Ms) / pow(a,3);

        // missing negative signs?
        g1(k, j, i) = gp * (-x) + (gs - gc) * (a-x);
        g2(k, j, i) = gp * (-y) + (gs - gc) * (-y);
        g3(k, j, i) = gp * (-z) + (gs - gc) * (-z);
      }
    }
  }
}

Real RadiationTime(AthenaArray<Real> const &prim, Real time, int k, int j, int il, int iu) {
  Real rad_scaling = (ref_dist_*ref_dist_)/(dist_*dist_);

  // tripathi
  Real time_factor = 5 * erf(time/8.e4 - 1.5)+5.1;
  // Real time_factor = (std::tanh(time/2000. - 5.)+1.)/2.;

  return rad_scaling*time_factor;
}

//----------------------------------------------------------------------------------------
// User Setup
//----------------------------------------------------------------------------------------
void Mesh::InitUserMeshData(ParameterInput *pin)
{
  EnrollUserExplicitGravityFunction(Gravity);
  EnrollUserExplicitSourceFunction(SourceTerms);

  // Radiation parameters
  dist_ = pin->GetOrAddReal("radiation", "distance", 1.);
  ref_dist_ = pin->GetOrAddReal("radiation", "reference_distance", 1.);

  wave_to_meters_conversion = pin->GetOrAddReal("radiation","wave_to_meters",1.e-9);
  EnrollUserRadiationTimeFunc(RadiationTime);

  // Gravity/System Parameters
  G = pin->GetReal("problem","G");
  Mp = pin->GetReal("problem","Mp");
  Ms = pin->GetReal("problem","Ms");
  Rp = pin->GetReal("problem","Rp");
  period = pin->GetReal("problem","period");

  Real x = 4. * pow(M_PI,2.) / (G * Ms);
  a = pow( pow(period*86400.,2.)/x ,(1./3));

  gas_gamma = pin->GetReal("hydro","gamma");

  dfloor = pin->GetOrAddReal("hydro", "dfloor", 0);
  pfloor = pin->GetOrAddReal("hydro", "pfloor", 0);
  sfloor = pin->GetOrAddReal("hydro", "sfloor", 0);

  r_0 = 0.5*Rp;
  r_e = 1.02*Rp;

  rho_0 = rho_func(r_0);
  rho_e = rho_func(r_e);
  P_0   = press_func(rho_0);
  P_e   = press_func(rho_e);
}

//----------------------------------------------------------------------------------------
// Initial Conditions
//----------------------------------------------------------------------------------------
Real rho_func(Real r) {  
  Real gm1   = gas_gamma - 1.0;

  Real K = pow(rho_p,1.0-gas_gamma) * pow(cs,2);
  Real rho_term_1 = gm1/gas_gamma * G * Mp/K;
  Real rho_term_2 = (1.0/r - 1.0/Rp);
  Real rho_term_3 = pow(rho_p,gm1);

  return pow(rho_term_1*rho_term_2 + rho_term_3, 1.0/gm1);
}

Real press_func(Real rho) {
  Real K = pow(rho_p,1.0-gas_gamma) * pow(cs,2);
  return K * pow(rho, gas_gamma);
}

Real getRad(Coordinates *pcoord, int i, int j, int k) {
  if (std::strcmp(COORDINATE_SYSTEM, "cartesian") == 0) {
    Real x = pcoord->x1v(i);
    Real y = pcoord->x2v(j);
    Real z = pcoord->x3v(k);

    return  sqrt(x*x + y*y + z*z);
  } else if (std::strcmp(COORDINATE_SYSTEM, "spherical_polar") == 0) {
    return pcoord->x1v(i);
  }
  else {
    std::stringstream msg;
    msg << "### FATAL ERROR" << std::endl
        << "    Coordinate System (" << COORDINATE_SYSTEM << ") must be either cartesian or spherical_polar." << std::endl;
    ATHENA_ERROR(msg);
  }
}

void SetInitialConditions(Real rad, Real &dens, Real &press, Real &ion_f, Real &v1, Real &v2, Real &v3) {
  v1 = v2 = v3 = 0;
  
  if (rad <= r_0) {
    dens  = rho_0;
    press = P_0;
    ion_f = sfloor;
  }
  else if (rad <= r_e) {
    dens  = rho_func(rad);
    press = press_func(dens);
    ion_f = sfloor;
  }
  else {
    dens  = rho_e * 1.0e-4;
    press = P_e;
    ion_f = 1-sfloor;
  }
}

void MeshBlock::ProblemGenerator(ParameterInput *pin)
{
  // initialize data arrays
  rad_flux_ion.NewAthenaArray(ncells3, ncells2, ncells1);
  rad_flux_ion.ZeroClear();

  // output vars
  rad_eng.NewAthenaArray(ncells3,ncells2,ncells1);
  ionization_rate.NewAthenaArray(ncells3,ncells2,ncells1);
  recombination_rate.NewAthenaArray(ncells3,ncells2,ncells1);
  recombinative_cooling.NewAthenaArray(ncells3,ncells2,ncells1);
  lya_cooling.NewAthenaArray(ncells3,ncells2,ncells1);
  output_du.NewAthenaArray(ncells3,ncells2,ncells1);

  if (NSCALARS != 2) {
    std::stringstream msg;
    msg << "### FATAL ERROR in Problem Generator" << std::endl
        << "    NSCALARS ("<< NSCALARS <<") must be exactly 2." << std::endl;
    ATHENA_ERROR(msg);
  }

  // setup initial condition
  int il = is-NGHOST;
  int iu = ie+NGHOST;
  int kl = block_size.nx3 == 1 ? ks : ks-NGHOST;
  int ku = block_size.nx3 == 1 ? ke : ke+NGHOST;
  int jl = block_size.nx2 == 1 ? js : js-NGHOST;
  int ju = block_size.nx2 == 1 ? je : je+NGHOST;

  Real rad;
  Real dens, press, ion_f, v1, v2, v3;

  v1 = v2 = v3 = 0;

  for (int i = il; i <= iu; ++i) {
    for (int j = jl; j <= ju; ++j) {
      for (int k = kl; k <= ku; ++k) {
        rad = getRad(pcoord, i, j, k);

        SetInitialConditions(rad, dens, press, ion_f, v1, v2, v3);

        phydro->w(IPR,k,j,i) = press;
        phydro->w(IDN,k,j,i) = dens;
        phydro->w(IV1,k,j,i) = v1;
        phydro->w(IV2,k,j,i) = v2;
        phydro->w(IV3,k,j,i) = v3;

        // s0 -- neutral hydrogen
        pscalars->s(0,k,j,i) = (1-ion_f) * dens;
        // s1 -- ionized hydrogen
        pscalars->s(1,k,j,i) = ion_f * dens;
      }
    }
  }
  //-- end file loading

  // set spectral properties
  for (int k = kl; k <= ku; ++k)
    for (int j = jl; j <= ju; ++j)
      prad->CalculateRadiativeTransfer(phydro->w, pmy_mesh->time, k, j, is, ie+1);

  peos->PrimitiveToConserved(phydro->w, pfield->bcc, phydro->u, pcoord, is, ie, js, je, ks, ke);
}

// resets things inside 0.75Rp
void MeshBlock::UserWorkInLoop() {
  // happens at last stage, at end

  Real gm1 = gas_gamma - 1.0;
  Real rad, dens, press, ion_f, v1, v2, v3;

  // calculate energy that goes into ionizing
  for (int k = ks; k <= ke; ++k) {
    for (int j = js; j <= je; ++j) {
      for (int i = is; i <= ie; ++i) {
        rad = getRad(pcoord, i, j, k);

        // reset conditions interior to 0.75 Rp
        // probably best to do both cons and prim
        if (rad <= 0.75 * Rp) {
          SetInitialConditions(rad, dens, press, ion_f, v1, v2, v3);

          // prim
          phydro->w(IPR,k,j,i) = press;
          phydro->w(IDN,k,j,i) = dens;
          phydro->w(IV1,k,j,i) = v1;
          phydro->w(IV2,k,j,i) = v2;
          phydro->w(IV3,k,j,i) = v3;

          //cons
          phydro->u(IEN,k,j,i) = press/gm1;
          phydro->u(IDN,k,j,i) = dens;
          phydro->u(IM1,k,j,i) = v1*dens;
          phydro->u(IM2,k,j,i) = v2*dens;
          phydro->u(IM3,k,j,i) = v3*dens;

          // scalars
          // s0 -- neutral hydrogen
          pscalars->s(0,k,j,i) = (1.0-ion_f) * dens;
          pscalars->r(0,k,j,i) = (1.0-ion_f);
          // s1 -- ionized hydrogen
          pscalars->s(1,k,j,i) = ion_f * dens;
          pscalars->r(1,k,j,i) = ion_f;
        }
      } // i
    } // j
  } // k
  return;
}