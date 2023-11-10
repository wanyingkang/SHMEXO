// C/C++ headers
#include <string>
#include <vector>

// Athena++ headers
#include "../../athena.hpp"
#include "../reaction_network.hpp"
#include "../reaction.hpp"

class TripletHydrogenCollision: public ReactionTemplate {
public:
  using ReactionTemplate::ReactionTemplate;
  Real alpha(Real T, int k, int j, int i);
  // Real beta(Real T, int k, int j, int i);
};

class ChargeExchangeHeliumToHyd: public ReactionTemplate {
public:
  using ReactionTemplate::ReactionTemplate;
  Real alpha(Real T, int k, int j, int i);
  // Real beta(Real T, int k, int j, int i);
};

class ChargeExchangeHydToHelium: public ReactionTemplate {
public:
  using ReactionTemplate::ReactionTemplate;
  Real alpha(Real T, int k, int j, int i);
  // Real beta(Real T, int k, int j, int i);
};

class HeElectronCollisions: public ReactionTemplate {
public:
  HeElectronCollisions(std::string name, std::vector<int> species, std::vector<Real> stoichiometry, std::string data_file_, int alpha_column_, int beta_column_);

  Real alpha(Real T, int k, int j, int i);
  Real beta(Real T, int k, int j, int i);
protected:
  Real TempScalingFactor(Real T);

  int n_file;
  std::vector<Real> file_T, alpha_data, beta_data;
};

class He_e_collisions: public Reaction {
public:
  He_e_collisions(std::string data_file, std::string name, int singlet_num, int triplet_num, int elec_num);
  ~He_e_collisions();
  void react(int k, int j, int i);
protected:
  int n_col_type = 4;

  int singlet_scalar_num, triplet_scalar_num, electron_scalar_num;
  AthenaArray<Real> file_data;

  int n_file;

  Real *file_T;
  Real **file_q;
  Real **file_L;
  enum {x11 = 0, x13 = 1, x31 = 2, x33 = 3};

  Real *temp_q;
  Real *temp_L;
};