// C/C++ header
#include <ctime>
#include <cmath>
#include <string>

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
#include "../scalars/scalars.hpp"
#include "../mesh_generator.hpp"

#include "../radiation/radiation.hpp"
#include "../reaction/reaction_network.hpp"
#include "../radiation/absorber/hydrogen_ionization.hpp"
#include "../reaction/reactions/photoionization.hpp"
#include "../reaction/reactions/hydrogen_reactions.hpp"



namespace {
  // user set variables
  Real G, Mp, Ms, Rp, period, a;
  Real dfloor, pfloor, sfloor;
  Real gas_gamma;
  Real wave_to_meters_conversion;

  // radiation variables
  Real rad_scaling;

  // tripathi conditions variables
  Real rho_p, cs, space_density_factor;
  Real r_0, r_e, rho_0, rho_e, P_0, P_e;
  Real r_replenish;

  enum species {ELEC = 0, HYD = 1, HPLUS = 2};

  AthenaArray<Real> initial_abundances;
} //namespace

  // Function declarations
  Real getRad(Coordinates *pcoord, int i, int j, int k);
  Real rho_func(Real r);
  Real press_func(Real rho);
  void getMBBounds(MeshBlock *pmb, int &il, int &iu, int &jl, int &ju, int &kl, int &ku);
  void gravity_func(MeshBlock *pmb, AthenaArray<Real> &g1, AthenaArray<Real> &g2, AthenaArray<Real> &g3);

  void SourceTerms(MeshBlock *pmb, const Real time, const Real dt,
    const AthenaArray<Real> &w, const AthenaArray<Real> &r,
    const AthenaArray<Real> &bcc,
    AthenaArray<Real> &du, AthenaArray<Real> &ds);
  Real RadiationTime(AthenaArray<Real> const &prim, Real time, int k, int j);

  // mesh generators
  MeshGenerator meshgen_x1, meshgen_x2, meshgen_x3;
  Real MeshSpacingX1(Real x, RegionSize rs);
  Real MeshSpacingX2(Real x, RegionSize rs);
  Real MeshSpacingX3(Real x, RegionSize rs);
  void SetInitialAbundances(MeshBlock *pmb, PassiveScalars *ps);

//----------------------------------------------------------------------------------------
// User Setup
//----------------------------------------------------------------------------------------
void Mesh::InitUserMeshData(ParameterInput *pin)
{
  EnrollUserExplicitGravityFunction(gravity_func);
  // EnrollUserExplicitSourceFunction(SourceTerms);

  // Radiation parameters
  rad_scaling = pin->GetOrAddReal("radiation", "radiation_scaling", 1.);

  wave_to_meters_conversion = pin->GetOrAddReal("radiation","wave_to_meters",1.e-7);
  EnrollUserRadiationScalingFunction(RadiationTime);

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

  rho_p = pin->GetOrAddReal("problem","rho_p",1.0e-15);
  cs = pin->GetOrAddReal("problem","cs",3.0e5);
  space_density_factor = pin->GetOrAddReal("problem", "space_density_factor", 1.e-4);
  r_replenish = pin->GetOrAddReal("problem", "r_replenish_Rp", 0.75)*Rp;

  // Tripathi initial conditions variables
  r_0 = 0.5*Rp;
  r_e = 1.02*Rp;

  rho_0 = rho_func(r_0);
  rho_e = rho_func(r_e);
  P_0   = press_func(rho_0);
  P_e   = press_func(rho_e);

  // Domain logic
  if (mesh_size.x1rat == -1.0) {
    meshgen_x1 = MeshGenerator(mesh_size.x1min, mesh_size.x1max, mesh_size.nx1, pin);
    EnrollUserMeshGenerator(X1DIR, MeshSpacingX1);
  }
  if (f2) {
    if (mesh_size.x2rat == -1.0) {
      meshgen_x2 = MeshGenerator(mesh_size.x2min, mesh_size.x2max, mesh_size.nx2, pin);  
      EnrollUserMeshGenerator(X2DIR, MeshSpacingX2);
    }
  }
  if (f3) {
    if (mesh_size.x3rat == -1.0) {
      meshgen_x3 = MeshGenerator(mesh_size.x3min, mesh_size.x3max, mesh_size.nx3, pin);
      EnrollUserMeshGenerator(X3DIR, MeshSpacingX3);
    }
  }
}

Real MeshSpacingX1(Real x, RegionSize rs) {
  return meshgen_x1.MeshSpacing(x);
}

Real MeshSpacingX2(Real x, RegionSize rs) {
  return meshgen_x2.MeshSpacing(x);
}

Real MeshSpacingX3(Real x, RegionSize rs) {
  return meshgen_x3.MeshSpacing(x);
}

// outputs and stored values
void MeshBlock::InitUserMeshBlockData(ParameterInput *pin)
{
  // User outputs
  AllocateUserOutputVariables(3);
  SetUserOutputVariableName(0, "temp");
  SetUserOutputVariableName(1, "t2");
  SetUserOutputVariableName(2, "flux");
  // SetUserOutputVariableName(2, "ionization_rate");
  // SetUserOutputVariableName(3, "recombination_rate");
  // SetUserOutputVariableName(4, "recombinative_cooling");
  // SetUserOutputVariableName(5, "lya_cooling");
  // SetUserOutputVariableName(6, "du");

  // User mesh data
  // 0 -- energy/time absorbed into absorber -- turns into ions
  // 1 -- radiation energy absorbed prad->du
  // 2 -- ionization rate
  // 3 -- recombination rate
  // 4 -- recombinative cooling rate
  // 5 -- lyman alpha cooling rate
  // 6 -- output du?
  AllocateRealUserMeshBlockDataField(1);
  ruser_meshblock_data[0].NewAthenaArray(ncells3, ncells2, ncells1);
  // ruser_meshblock_data[1].NewAthenaArray(ncells3, ncells2, ncells1);
  // ruser_meshblock_data[2].NewAthenaArray(ncells3, ncells2, ncells1);
  // ruser_meshblock_data[3].NewAthenaArray(ncells3, ncells2, ncells1);
  // ruser_meshblock_data[4].NewAthenaArray(ncells3, ncells2, ncells1);
  // ruser_meshblock_data[5].NewAthenaArray(ncells3, ncells2, ncells1);
  // ruser_meshblock_data[6].NewAthenaArray(ncells3, ncells2, ncells1);
}

//----------------------------------------------------------------------------------------
// Output Variables
//----------------------------------------------------------------------------------------

void MeshBlock::UserWorkBeforeOutput(ParameterInput *pin)
{
  int il, iu, jl, ju, kl, ku;
  getMBBounds(this, il, iu, jl, ju, kl, ku);

  for (int k = kl; k <= ku; ++k) {
    for (int j = jl; j <= ju; ++j) {
      for (int i = il; i <= iu; ++i) {
        Real R = pthermo->GetRd();
        Real T = phydro->w(IPR,k,j,i)/(R * phydro->w(IDN,k,j,i));
        Real ion_f = pscalars->r(HPLUS,k,j,i);
        T = T * (1.-ion_f/2.);

        Real t2;
        peos->Temperature(phydro->w, pscalars->s, pscalars->m, t2, k, j, i);

        user_out_var(0,k,j,i) = T;
        user_out_var(1,k,j,i) = t2;
        // user_out_var(1,k,j,i) = phydro->hsrc.g1(k,j,i);
        // user_out_var(2,k,j,i) = phydro->hsrc.g2(k,j,i);
        // user_out_var(3,k,j,i) = phydro->hsrc.g3(k,j,i);
        user_out_var(2,k,j,i) = ruser_meshblock_data[0](k,j,i);

        // for (int o=0; o<6; ++o) {
        //   user_out_var(o+4,k,j,i) = ruser_meshblock_data[o+1](k,j,i);
        // }
      }
    }
  }
}

void ReactionNetwork::InitUserReactions(ParameterInput *pin) {
  Reaction *rxn = new H_recombination(this, "H Recombination", HYD, HPLUS, ELEC);
  return;
}

Absorber* RadiationBand::GetAbsorberByName(std::string name, ParameterInput *pin)
{
  std::stringstream msg;
  if (name == "HYDROGEN_IONIZATION") {
    HydrogenIonization *a = new HydrogenIonization(this, HYD, name, pin);

    ReactionNetwork *pnetwork = pmy_rad->pmy_block->pnetwork;
    Reaction *rxn = new Photoionization(pnetwork, name, a, HPLUS, ELEC);

    return a;
  }
  else {
    msg << "### FATAL ERROR in RadiationBand::AddAbsorber"
        << std::endl << "unknown absorber: '" << name <<"' ";
    ATHENA_ERROR(msg);
  }
}

//----------------------------------------------------------------------------------------
// Additional Physics
//----------------------------------------------------------------------------------------


// void SourceTerms(MeshBlock *pmb, const Real time, const Real dt,
//   const AthenaArray<Real> &w, const AthenaArray<Real> &r,
//   const AthenaArray<Real> &bcc,
//   AthenaArray<Real> &du, AthenaArray<Real> &ds)
// {
//   int is = pmb->is, js = pmb->js, ks = pmb->ks;
//   int ie = pmb->ie, je = pmb->je, ke = pmb->ke;

//   AthenaArray<Real> vol;
//   vol.NewAthenaArray(pmb->ncells1);

//   PassiveScalars *ps = pmb->pscalars;

//   for (int k = ks; k <= ke; ++k) {
//     for (int j = js; j <= je; ++j) {
//       pmb->pcoord->CellVolume(k,j,is,ie,vol);

//       for (int i = is; i <= ie; ++i) {
//       }
//     }
//   }
// }

// needs validation
void gravity_func(MeshBlock *pmb, AthenaArray<Real> &g1, AthenaArray<Real> &g2, AthenaArray<Real> &g3) {
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

  int il, iu, jl, ju, kl, ku;
  getMBBounds(pmb, il, iu, jl, ju, kl, ku);

  for (int k = kl; k <= ku; ++k) {
    for (int j = jl; j <= ju; ++j) {
      for (int i = il; i <= iu; ++i) {
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
        // so long as less than reset radius, this shouldn't matter
        if (r < 0.5 * Rp) {
          r = 0.5*Rp;
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

Real RadiationTime(AthenaArray<Real> const &prim, Real time, int k, int j) {
  // tripathi
  Real time_factor = 5 * erf(time/8.e4 - 1.5)+5.1;
  // Real time_factor = (std::tanh(time/2000. - 5.)+1.)/2.;

  return rad_scaling*time_factor;
}

//----------------------------------------------------------------------------------------
// Initial Conditions
//----------------------------------------------------------------------------------------
void SetInitialConditions(Real rad, Real &dens, Real &press, Real &v1, Real &v2, Real &v3) {
  v1 = v2 = v3 = 0;
  
  if (rad <= r_0) {
    dens  = rho_0;
    press = P_0;
  }
  else if (rad <= r_e) {
    dens  = rho_func(rad);
    press = press_func(dens);
  }
  else {
    dens  = rho_e * space_density_factor;
    press = P_e;
  }
}

void SetInitialAbundances(MeshBlock *pmb, PassiveScalars *ps) {
  int il, iu, jl, ju, kl, ku;
  Real rad;
  Real minval = sfloor / ps->m(ELEC);

  getMBBounds(pmb, il, iu, jl, ju, kl, ku);

  // set number densities, fill in mass later
  for (int k = kl; k <= ku; ++k) {
    for (int j = jl; j <= ju; ++j) {
      for (int i = il; i <= iu; ++i) {
        rad = getRad(pmb->pcoord, i, j, k);
        
        if (rad <= r_e) {
          initial_abundances(HYD,k,j,i)   = 1/ps->m(HYD) - minval;
          initial_abundances(ELEC,k,j,i)  = minval;
          initial_abundances(HPLUS,k,j,i) = minval;
        }
        else {
          initial_abundances(HYD,k,j,i)   = minval;
          initial_abundances(ELEC,k,j,i)  = 1/ps->m(HYD) - minval;
          initial_abundances(HPLUS,k,j,i) = 1/ps->m(HYD) - minval;
        }

        for (int n = 0; n < NSCALARS; ++n)
        {
          initial_abundances(n,k,j,i) *= ps->m(n);
        }
      }
    }
  }
}

void MeshBlock::ProblemGenerator(ParameterInput *pin)
{
  if (NSCALARS != 3) {
    std::stringstream msg;
    msg << "### FATAL ERROR in Problem Generator" << std::endl
        << "    NSCALARS ("<< NSCALARS <<") must be exactly 3." << std::endl;
    ATHENA_ERROR(msg);
  }


  initial_abundances.NewAthenaArray(NSCALARS, ncells3, ncells2, ncells1);
  SetInitialAbundances(this, pscalars);

  // setup initial condition
  int il, iu, jl, ju, kl, ku;
  getMBBounds(this, il, iu, jl, ju, kl, ku);

  Real rad;
  Real dens, press, v1, v2, v3;

  v1 = v2 = v3 = 0;

  for (int i = il; i <= iu; ++i) {
    for (int j = jl; j <= ju; ++j) {
      for (int k = kl; k <= ku; ++k) {
        rad = getRad(pcoord, i, j, k);

        SetInitialConditions(rad, dens, press, v1, v2, v3);

        phydro->w(IPR,k,j,i) = press;
        phydro->w(IDN,k,j,i) = dens;
        phydro->w(IV1,k,j,i) = v1;
        phydro->w(IV2,k,j,i) = v2;
        phydro->w(IV3,k,j,i) = v3;

        // s0 -- neutral hydrogen
        for (int n = 0; n < NSCALARS; ++n)
        {
          pscalars->s(n,k,j,i) = initial_abundances(n,k,j,i) * dens;
        }
      }
    }
  }
  //-- end file loading

  // set spectral properties
  for (int k = kl; k <= ku; ++k)
    for (int j = jl; j <= ju; ++j)
      prad->CalculateRadiativeTransfer(phydro->w, pscalars->s, pmy_mesh->time, k, j);

  peos->PrimitiveToConserved(phydro->w, pfield->bcc, phydro->u, pcoord, is, ie, js, je, ks, ke);

  pnetwork->Initialize();
}

// resets things inside r_replenish
void MeshBlock::UserWorkInLoop() {
  // happens at last stage, at end

  Real gm1 = gas_gamma - 1.0;
  Real rad, dens, press, ion_f, v1, v2, v3;

  bool nancheck = false;
  std::stringstream nan_msg;
  nan_msg << "### FATAL ERROR" << std::endl;
  nan_msg << "    nan value detected in (";

  // calculate energy that goes into ionizing
  int il, iu, jl, ju, kl, ku;
  getMBBounds(this, il, iu, jl, ju, kl, ku);

  for (int k = kl; k <= ku; ++k) {
    for (int j = jl; j <= ju; ++j) {
      for (int i = il; i <= iu; ++i) {
        rad = getRad(pcoord, i, j, k);

        if (std::isnan(phydro->w(IDN,k,j,i))) {
          nancheck = true;
          nan_msg << "dens";
        } else if (std::isnan(phydro->w(IPR,k,j,i))) {
          nancheck = true;
          nan_msg << "press";
        } else if (std::isnan(phydro->w(IV1,k,j,i))) {
          nancheck = true;
          nan_msg << "vel1";
        } else if (std::isnan(phydro->w(IV2,k,j,i))) {
          nancheck = true;
          nan_msg << "vel2";
        } else if (std::isnan(phydro->w(IV3,k,j,i))) {
          nancheck = true;
          nan_msg << "vel3";
        }


        if (nancheck) {
          nan_msg << ") at (k=" << k << ", j=" << j << ", i=" << i << ")." << std::endl;
          nan_msg << " at time: " << pmy_mesh->time << " cycle: " << pmy_mesh->ncycle << std::endl;
          std::cout << nan_msg.str() << std::endl;
          ATHENA_ERROR(nan_msg);
        }

        // reset conditions interior to r_replenish
        // probably best to do both cons and prim
        if (rad <= r_replenish) {
          SetInitialConditions(rad, dens, press, v1, v2, v3);

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
          for (int n = 0; n < NSCALARS; ++n)
          {
            pscalars->r(n,k,j,i) = initial_abundances(n,k,j,i);
            pscalars->s(n,k,j,i) = initial_abundances(n,k,j,i) * dens;
          }
        }
      } // i
    } // j
  } // k
  return;
}


//----------------------------------------------------------------------------------------
// Utility
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

void getMBBounds(MeshBlock *pmb, int &il, int &iu, int &jl, int &ju, int &kl, int &ku) {
  il = pmb->is-NGHOST;
  iu = pmb->ie+NGHOST;

  jl = pmb->block_size.nx2 == 1 ? pmb->js : pmb->js-NGHOST;
  ju = pmb->block_size.nx2 == 1 ? pmb->je : pmb->je+NGHOST;
  
  kl = pmb->block_size.nx3 == 1 ? pmb->ks : pmb->ks-NGHOST;
  ku = pmb->block_size.nx3 == 1 ? pmb->ke : pmb->ke+NGHOST;
}

