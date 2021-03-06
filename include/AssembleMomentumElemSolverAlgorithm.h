/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


#ifndef AssembleMomentumElemSolverAlgorithm_h
#define AssembleMomentumElemSolverAlgorithm_h

#include<SolverAlgorithm.h>
#include<FieldTypeDef.h>

namespace stk {
namespace mesh {
class Part;
}
}

namespace sierra{
namespace nalu{

class Realm;

class AssembleMomentumElemSolverAlgorithm : public SolverAlgorithm
{
public:

  AssembleMomentumElemSolverAlgorithm(
    Realm &realm,
    stk::mesh::Part *part,
    EquationSystem *eqSystem);
  virtual ~AssembleMomentumElemSolverAlgorithm() {}
  virtual void initialize_connectivity();
  virtual void execute();

  double van_leer(
    const double &dqm,
    const double &dqp,
    const double &small);

  const double includeDivU_;
  const double meshMotion_;

  VectorFieldType *meshVelocity_;
  VectorFieldType *velocity_;
  VectorFieldType *coordinates_;
  GenericFieldType *dudx_;
  ScalarFieldType *density_;
  ScalarFieldType *viscosity_;
  GenericFieldType *massFlowRate_;
};

} // namespace nalu
} // namespace Sierra

#endif
