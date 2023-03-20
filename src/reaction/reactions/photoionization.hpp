// C/C++ headers
#include <string>

// Athena++ headers
#include "../../athena.hpp"
#include "../reaction_network.hpp"

class MeshBlock;
class ReactionNetwork;
class Reaction;
class Absorber;
class IonizingAbsorber;
class PassiveScalars;

class Photoionization: public Reaction {
public:
  Photoionization(ReactionNetwork *pnetwork, std::string name, IonizingAbsorber *pabs, 
    int ion_num, int elec_num);
  void react(int k, int j, int i);

protected:
  int scalar_num, ion_scalar_num, electron_scalar_num;
  Real ionization_energy;
  IonizingAbsorber *pmy_abs;
};