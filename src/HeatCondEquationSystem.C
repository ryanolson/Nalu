/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


#include <HeatCondEquationSystem.h>

#include <AssembleHeatCondWallSolverAlgorithm.h>
#include <AssembleHeatCondIrradWallSolverAlgorithm.h>
#include <AssembleScalarEdgeDiffSolverAlgorithm.h>
#include <AssembleScalarElemDiffSolverAlgorithm.h>
#include <AssembleScalarElemDiffNonConformalSolverAlgorithm.h>
#include <AssembleScalarEdgeDiffContactSolverAlgorithm.h>
#include <AssembleScalarDiffBCSolverAlgorithm.h>
#include <AssembleNodalGradAlgorithmDriver.h>
#include <AssembleNodalGradEdgeAlgorithm.h>
#include <AssembleNodalGradElemAlgorithm.h>
#include <AssembleNodalGradEdgeBoundaryAlgorithm.h>
#include <AssembleNodalGradElemBoundaryAlgorithm.h>
#include <AssembleNodalGradEdgeContactAlgorithm.h>
#include <AssembleNodalGradElemContactAlgorithm.h>
#include <AssembleNodeSolverAlgorithm.h>
#include <AssembleNonConformalElemDiffPenaltyAlgorithm.h>
#include <AuxFunctionAlgorithm.h>
#include <ConstantAuxFunction.h>
#include <CopyFieldAlgorithm.h>
#include <ContactManager.h>
#include <DirichletBC.h>
#include <EquationSystem.h>
#include <EquationSystems.h>
#include <Enums.h>
#include <ErrorIndicatorAlgorithmDriver.h>
#include <FieldFunctions.h>
#include <LinearSolvers.h>
#include <LinearSolver.h>
#include <LinearSystem.h>
#include <NaluEnv.h>
#include <Realm.h>
#include <Realms.h>
#include <HeatCondMassBackwardEulerNodeSuppAlg.h>
#include <HeatCondMassBDF2NodeSuppAlg.h>
#include <PstabErrorIndicatorEdgeAlgorithm.h>
#include <PstabErrorIndicatorElemAlgorithm.h>
#include <SimpleErrorIndicatorScalarElemAlgorithm.h>
#include <Simulation.h>
#include <SolutionOptions.h>
#include <TimeIntegrator.h>
#include <SolverAlgorithmDriver.h>

// user functions
#include <user_functions/SteadyThermalContactAuxFunction.h>
#include <user_functions/SteadyThermalContactSrcNodeSuppAlg.h>

// stk_util
#include <stk_util/parallel/Parallel.hpp>

// stk_mesh/base/fem
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/Field.hpp>
#include <stk_mesh/base/FieldParallel.hpp>
#include <stk_mesh/base/GetBuckets.hpp>
#include <stk_mesh/base/GetEntities.hpp>
#include <stk_mesh/base/CoordinateSystems.hpp>
#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/SkinMesh.hpp>
#include <stk_mesh/base/Comm.hpp>

// stk_io
#include <stk_io/StkMeshIoBroker.hpp>
#include <stk_io/IossBridge.hpp>

#include <stk_topology/topology.hpp>

// stk_util
#include <stk_util/parallel/ParallelReduce.hpp>
#include <stk_util/environment/CPUTime.hpp>

// basic c++
#include <iostream>
#include <math.h>

namespace sierra{
namespace nalu{

//==========================================================================
// Class Definition
//==========================================================================
// HeatCondEquationSystem - do some stuff
//==========================================================================
//--------------------------------------------------------------------------
//-------- constructor -----------------------------------------------------
//--------------------------------------------------------------------------
HeatCondEquationSystem::HeatCondEquationSystem(
  EquationSystems& eqSystems)
  : EquationSystem(eqSystems, "HeatCondEQS"),
    temperature_(NULL),
    dtdx_(NULL),
    tTmp_(NULL),
    dualNodalVolume_(NULL),
    coordinates_(NULL),
    exact_temperature_(NULL),
    exact_dtdx_(NULL),
    exact_laplacian_(NULL),
    density_(NULL),
    specHeat_(NULL),
    thermalCond_(NULL),
    scVolume_(NULL),
    edgeAreaVec_(NULL),
    assembleNodalGradAlgDriver_(new AssembleNodalGradAlgorithmDriver(realm_, "temperature", "dtdx")),
    isInit_(true),
    collocationForViscousTerms_(false),
    assembleNonConformalAlgDriver_(NULL)
{
  // extract solver name and solver object
  std::string solverName = realm_.equationSystems_.get_solver_block_name("temperature");
  LinearSolver *solver = realm_.root()->linearSolvers_->create_solver(solverName, EQ_TEMPERATURE);
  linsys_ = LinearSystem::create(realm_, 1, "HeatCondEQS", solver);

  // determine nodal gradient form
  set_nodal_gradient("temperature");
  NaluEnv::self().naluOutputP0() << "Edge projected nodal gradient for temperature: " << edgeNodalGradient_ <<std::endl;

  // push back EQ to manager
  realm_.equationSystems_.push_back(this);
}

//--------------------------------------------------------------------------
//-------- destructor ------------------------------------------------------
//--------------------------------------------------------------------------
HeatCondEquationSystem::~HeatCondEquationSystem()
{
  delete assembleNodalGradAlgDriver_;
  
  if ( NULL != assembleNonConformalAlgDriver_ )
    delete assembleNonConformalAlgDriver_;

}

//--------------------------------------------------------------------------
//-------- register_nodal_fields -------------------------------------------
//--------------------------------------------------------------------------
void
HeatCondEquationSystem::register_nodal_fields(
  stk::mesh::Part *part)
{
  stk::mesh::MetaData &meta_data = realm_.fixture_->meta_data();

  const int nDim = meta_data.spatial_dimension();

  const int numStates = realm_.number_of_states();

  // register dof; set it as a restart variable
  temperature_ = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "temperature", numStates));
  stk::mesh::put_field(*temperature_, *part);
  realm_.augment_restart_variable_list("temperature");

  dtdx_ =  &(meta_data.declare_field<VectorFieldType>(stk::topology::NODE_RANK, "dtdx"));
  stk::mesh::put_field(*dtdx_, *part, nDim);

  // delta solution for linear solver
  tTmp_ =  &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "tTmp"));
  stk::mesh::put_field(*tTmp_, *part);

  dualNodalVolume_ = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "dual_nodal_volume"));
  stk::mesh::put_field(*dualNodalVolume_, *part);

  coordinates_ =  &(meta_data.declare_field<VectorFieldType>(stk::topology::NODE_RANK, "coordinates"));
  stk::mesh::put_field(*coordinates_, *part, nDim);

  // props
  density_ = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "density"));
  stk::mesh::put_field(*density_, *part);

  specHeat_ = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "specific_heat"));
  stk::mesh::put_field(*specHeat_, *part);

  thermalCond_ = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "thermal_conductivity"));
  stk::mesh::put_field(*thermalCond_, *part);

  // push to property list
  realm_.augment_property_map(DENSITY_ID, density_);
  realm_.augment_property_map(SPEC_HEAT_ID, specHeat_);
  realm_.augment_property_map(THERMAL_COND_ID, thermalCond_);

  // make sure all states are properly populated (restart can handle this)
  if ( numStates > 2 && !realm_.restarted_simulation() ) {
    ScalarFieldType &tempN = temperature_->field_of_state(stk::mesh::StateN);
    ScalarFieldType &tempNp1 = temperature_->field_of_state(stk::mesh::StateNP1);

    CopyFieldAlgorithm *theCopyAlgA
      = new CopyFieldAlgorithm(realm_, part,
                               &tempNp1, &tempN,
                               0, 1,
                               stk::topology::NODE_RANK);

    copyStateAlg_.push_back(theCopyAlgA);
  }
}

//--------------------------------------------------------------------------
//-------- register_edge_fields -------------------------------------------
//--------------------------------------------------------------------------
void
HeatCondEquationSystem::register_edge_fields(
  stk::mesh::Part *part)
{

  stk::mesh::MetaData &meta_data = realm_.fixture_->meta_data();

  //====================================================
  // Register edge data
  //====================================================
  if ( realm_.realmUsesEdges_ ) {
    const int nDim = meta_data.spatial_dimension();
    edgeAreaVec_ = &(meta_data.declare_field<VectorFieldType>(stk::topology::EDGE_RANK, "edge_area_vector"));
    stk::mesh::put_field(*edgeAreaVec_, *part, nDim);
  }

}

//--------------------------------------------------------------------------
//-------- register_element_fields -------------------------------------------
//--------------------------------------------------------------------------
void
HeatCondEquationSystem::register_element_fields(
  stk::mesh::Part *part,
  const stk::topology &theTopo)
{
  //====================================================
  // Register element data
  //====================================================

  stk::mesh::MetaData &meta_data = realm_.fixture_->meta_data();

  const int numScvIp = theTopo.num_nodes();
  scVolume_ = &(meta_data.declare_field<GenericFieldType>(stk::topology::ELEMENT_RANK, "sc_volume"));
  stk::mesh::put_field(*scVolume_, *part, numScvIp );

  // deal with heat conduction error indicator; elemental field of size unity
  if ( realm_.solutionOptions_->activateAdaptivity_) {
    const int numIp = 1;
    GenericFieldType *pstabEI= &(meta_data.declare_field<GenericFieldType>(stk::topology::ELEMENT_RANK, "error_indicator"));
    stk::mesh::put_field(*pstabEI, *part, numIp);
  }
  
}

//--------------------------------------------------------------------------
//-------- register_interior_algorithm -------------------------------------
//--------------------------------------------------------------------------
void
HeatCondEquationSystem::register_interior_algorithm(
  stk::mesh::Part *part)
{

  // types of algorithms
  const AlgorithmType algType = INTERIOR;

  ScalarFieldType &tempNp1 = temperature_->field_of_state(stk::mesh::StateNP1);
  VectorFieldType &dtdxNone = dtdx_->field_of_state(stk::mesh::StateNone);

  // non-solver; contribution to projected nodal gradient; allow for element-based shifted
  std::map<AlgorithmType, Algorithm *>::iterator it
    = assembleNodalGradAlgDriver_->algMap_.find(algType);
  if ( it == assembleNodalGradAlgDriver_->algMap_.end() ) {
    Algorithm *theAlg = NULL;
    if ( edgeNodalGradient_ && realm_.realmUsesEdges_ ) {
      theAlg = new AssembleNodalGradEdgeAlgorithm(realm_, part, &tempNp1, &dtdxNone);
    }
    else {
      theAlg = new AssembleNodalGradElemAlgorithm(realm_, part, &tempNp1, &dtdxNone, edgeNodalGradient_);
    }
    assembleNodalGradAlgDriver_->algMap_[algType] = theAlg;
  }
  else {
    it->second->partVec_.push_back(part);
  }

  // solver; interior edge contribution (diffusion)
  std::map<AlgorithmType, SolverAlgorithm *>::iterator itsi
    = solverAlgDriver_->solverAlgMap_.find(algType);
  if ( itsi == solverAlgDriver_->solverAlgMap_.end() ) {
    SolverAlgorithm *theAlg = NULL;
    if ( realm_.realmUsesEdges_ ) {
    theAlg = new AssembleScalarEdgeDiffSolverAlgorithm(realm_, part, this,
        &tempNp1, &dtdxNone, thermalCond_);
    }
    else {
      theAlg = new AssembleScalarElemDiffSolverAlgorithm(realm_, part, this,
          &tempNp1, &dtdxNone, thermalCond_,
          collocationForViscousTerms_);
    }
    solverAlgDriver_->solverAlgMap_[algType] = theAlg;
  }
  else {
    itsi->second->partVec_.push_back(part);
  }

  // time term; nodally lumped
  const AlgorithmType algMass = MASS;
  std::map<AlgorithmType, SolverAlgorithm *>::iterator itsm
    = solverAlgDriver_->solverAlgMap_.find(algMass);
  if ( itsm == solverAlgDriver_->solverAlgMap_.end() ) {
    // create the solver alg
    AssembleNodeSolverAlgorithm *theAlg
      = new AssembleNodeSolverAlgorithm(realm_, part, this);
    solverAlgDriver_->solverAlgMap_[algMass] = theAlg;

    // now create the supplemental alg for mass term
    if ( realm_.number_of_states() == 2 ) {
      HeatCondMassBackwardEulerNodeSuppAlg *theMass
        = new HeatCondMassBackwardEulerNodeSuppAlg(realm_);
      theAlg->supplementalAlg_.push_back(theMass);
    }
    else {
      HeatCondMassBDF2NodeSuppAlg *theMass
        = new HeatCondMassBDF2NodeSuppAlg(realm_);
      theAlg->supplementalAlg_.push_back(theMass);
    }

    // Add src term supp alg...; limited number supported
    std::map<std::string, std::vector<std::string> >::iterator isrc 
      = realm_.solutionOptions_->srcTermsMap_.find("temperature");
    if ( isrc != realm_.solutionOptions_->srcTermsMap_.end() ) {
      std::vector<std::string> mapNameVec = isrc->second;
      for (size_t k = 0; k < mapNameVec.size(); ++k ) {
        std::string sourceName = mapNameVec[k];
        if (sourceName == "steady_2d_thermal" ) {
          SteadyThermalContactSrcNodeSuppAlg *theSrc
	    = new SteadyThermalContactSrcNodeSuppAlg(realm_);
          theAlg->supplementalAlg_.push_back(theSrc);
        }
        else {
          throw std::runtime_error("HeatCondEquationSystem::only steady_2d_thermal src term is supported");
        }
      }
    }
  }
  else {
    itsm->second->partVec_.push_back(part);
  }

  // deal with adaptivity
  if ( realm_.solutionOptions_->activateAdaptivity_) {

    // non-solver alg
    it = realm_.errorIndicatorAlgDriver_->algMap_.find(algType);
    if ( it == realm_.errorIndicatorAlgDriver_->algMap_.end() ) {
      Algorithm *theAlg = NULL;
      if ( realm_.solutionOptions_->errorIndicatorType_ & EIT_PSTAB ) {
        if ( realm_.realmUsesEdges_)
          theAlg = new PstabErrorIndicatorEdgeAlgorithm(realm_, part, temperature_, dtdx_, true);
        else
          theAlg = new PstabErrorIndicatorElemAlgorithm(realm_, part, temperature_, dtdx_, true);
      }
      else if ( realm_.solutionOptions_->errorIndicatorType_ & EIT_SIMPLE_DUDX2 ) {
        theAlg = new SimpleErrorIndicatorScalarElemAlgorithm(realm_, part, temperature_, dtdx_);
      }
      else {
        throw std::runtime_error("Sorry, only zz-like EI for heat conduction is supported");
      }
      realm_.errorIndicatorAlgDriver_->algMap_[algType] = theAlg;
    }
    else {
      it->second->partVec_.push_back(part);
    }
  }
  
}

//--------------------------------------------------------------------------
//-------- register_wall_bc ------------------------------------------------
//--------------------------------------------------------------------------
void
HeatCondEquationSystem::register_wall_bc(
  stk::mesh::Part *part,
  const stk::topology &/*theTopo*/,
  const WallBoundaryConditionData &wallBCData)
{

  const AlgorithmType algType = WALL;

  // np1
  ScalarFieldType &tempNp1 = temperature_->field_of_state(stk::mesh::StateNP1);
  VectorFieldType &dtdxNone = dtdx_->field_of_state(stk::mesh::StateNone);

  stk::mesh::MetaData &meta_data = realm_.fixture_->meta_data();

  // non-solver; dtdx; allow for element-based shifted; all bcs are of generic type "WALL"
  std::map<AlgorithmType, Algorithm *>::iterator it
    = assembleNodalGradAlgDriver_->algMap_.find(algType);
  if ( it == assembleNodalGradAlgDriver_->algMap_.end() ) {
    Algorithm *theAlg = NULL;
    if ( edgeNodalGradient_ && realm_.realmUsesEdges_ ) {
      theAlg = new AssembleNodalGradEdgeBoundaryAlgorithm(realm_, part, &tempNp1, &dtdxNone);
    }
    else {
      theAlg = new AssembleNodalGradElemBoundaryAlgorithm(realm_, part, &tempNp1, &dtdxNone, edgeNodalGradient_);
    }
    assembleNodalGradAlgDriver_->algMap_[algType] = theAlg;
  }
  else {
    it->second->partVec_.push_back(part);
  }

  // extract the value for user specified temperaure and save off the AuxFunction
  WallUserData userData = wallBCData.userData_;
  std::string temperatureName = "temperature";
  UserDataType theDataType = get_bc_data_type(userData, temperatureName);

  if ( userData.tempSpec_ ||  FUNCTION_UD == theDataType ) {
 
    // register boundary data; temperature_bc
    ScalarFieldType *theBcField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "temperature_bc"));
    stk::mesh::put_field(*theBcField, *part);

    AuxFunction *theAuxFunc = NULL;
    if ( CONSTANT_UD == theDataType ) {
      Temperature theTemp = userData.temperature_;
      std::vector<double> userSpec(1);
      userSpec[0] = theTemp.temperature_;
      // new it
      theAuxFunc = new ConstantAuxFunction(0, 1, userSpec);
    }
    else {
      // extract the name
      std::string fcnName = get_bc_function_name(userData, temperatureName);
      // switch on the name found...
      if ( fcnName == "steady_2d_thermal" ) {
        theAuxFunc = new SteadyThermalContactAuxFunction();
      }
      else {
        throw std::runtime_error("Only steady_2d_thermal user functions supported");
      }
    }
    
    // bc data alg
    AuxFunctionAlgorithm *auxAlg
      = new AuxFunctionAlgorithm(realm_, part,
                                 theBcField, theAuxFunc,
                                 stk::topology::NODE_RANK);
    bcDataAlg_.push_back(auxAlg);
    
    // copy temperature_bc to temperature np1...
    CopyFieldAlgorithm *theCopyAlg
      = new CopyFieldAlgorithm(realm_, part,
                               theBcField, &tempNp1,
                               0, 1,
                               stk::topology::NODE_RANK);
    bcDataMapAlg_.push_back(theCopyAlg);

    // Dirichlet bc
    std::map<AlgorithmType, SolverAlgorithm *>::iterator itd =
      solverAlgDriver_->solverDirichAlgMap_.find(algType);
    if ( itd == solverAlgDriver_->solverDirichAlgMap_.end() ) {
      DirichletBC *theAlg
        = new DirichletBC(realm_, this, part, &tempNp1, theBcField, 0, 1);
      solverAlgDriver_->solverDirichAlgMap_[algType] = theAlg;
    }
    else {
      itd->second->partVec_.push_back(part);
    }
  }
  else if( userData.heatFluxSpec_ && !userData.robinParameterSpec_ ) {

    const AlgorithmType algTypeHF = WALL_HF;

    ScalarFieldType *theBcField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "heat_flux_bc"));
    stk::mesh::put_field(*theBcField, *part);

    NormalHeatFlux heatFlux = userData.q_;
    std::vector<double> userSpec(1);
    userSpec[0] = heatFlux.qn_;

    // new it
    ConstantAuxFunction *theAuxFunc = new ConstantAuxFunction(0, 1, userSpec);

    // bc data alg
    AuxFunctionAlgorithm *auxAlg
      = new AuxFunctionAlgorithm(realm_, part,
                                 theBcField, theAuxFunc,
                                 stk::topology::NODE_RANK);
    bcDataAlg_.push_back(auxAlg);

    // solver; lhs; same for edge and element-based scheme
    std::map<AlgorithmType, SolverAlgorithm *>::iterator itsi =
        solverAlgDriver_->solverAlgMap_.find(algTypeHF);
    if ( itsi == solverAlgDriver_->solverAlgMap_.end() ) {
      AssembleScalarDiffBCSolverAlgorithm *theAlg
        = new AssembleScalarDiffBCSolverAlgorithm(realm_, part, this,
            theBcField, realm_.realmUsesEdges_);
      solverAlgDriver_->solverAlgMap_[algTypeHF] = theAlg;
    }
    else {
      itsi->second->partVec_.push_back(part);
    }

  }
  else if ( userData.irradSpec_ ) {
    
    const AlgorithmType algTypeRAD = WALL_RAD;

    // check for emissivity
    if ( !userData.emissSpec_)
      throw std::runtime_error("Sorry, irradiation was specified while emissivity was not");

    // register boundary data;
    ScalarFieldType *irradField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "irradiation"));
    stk::mesh::put_field(*irradField, *part);
    ScalarFieldType *emissField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "emissivity"));
    stk::mesh::put_field(*emissField, *part);

    // aux algs; irradiation
    Irradiation irrad = userData.irradiation_;
    std::vector<double> irradUserSpec(1);
    irradUserSpec[0] = irrad.irradiation_;
    AuxFunction *irradAuxFunc = new ConstantAuxFunction(0, 1, irradUserSpec);
    
    AuxFunctionAlgorithm *irradAuxAlg
      = new AuxFunctionAlgorithm(realm_, part,
          irradField, irradAuxFunc,
          stk::topology::NODE_RANK);
    
    // aux algs; emissivity
    Emissivity emiss = userData.emissivity_;
    std::vector<double> emissUserSpec(1);
    emissUserSpec[0] = emiss.emissivity_;
    AuxFunction *emissAuxFunc = new ConstantAuxFunction(0, 1, emissUserSpec);
    
    AuxFunctionAlgorithm *emissAuxAlg
      = new AuxFunctionAlgorithm(realm_, part,
          emissField, emissAuxFunc,
          stk::topology::NODE_RANK);
    
    // if this is a multi-physics coupling, only populate IC for irradiation (xfer will handle it)
    if ( userData.isInterface_ ) {
      // xfer will handle population; only need to populate the initial value
      realm_.initCondAlg_.push_back(irradAuxAlg);
    }
    else {
      // put it on bcData
      bcDataAlg_.push_back(irradAuxAlg);
    }
    
    // emissivity is placed on bc data (never via XFER)
    bcDataAlg_.push_back(emissAuxAlg);

    // solver; lhs
    std::map<AlgorithmType, SolverAlgorithm *>::iterator itsi =
      solverAlgDriver_->solverAlgMap_.find(algTypeRAD);
    if ( itsi == solverAlgDriver_->solverAlgMap_.end() ) {
      AssembleHeatCondIrradWallSolverAlgorithm *theAlg
        = new AssembleHeatCondIrradWallSolverAlgorithm(realm_, part, this,
            realm_.realmUsesEdges_);
      solverAlgDriver_->solverAlgMap_[algTypeRAD] = theAlg;
    }
    else {
      itsi->second->partVec_.push_back(part);
    }
  }
  else if ( userData.htcSpec_ || userData.refTempSpec_ || userData.robinParameterSpec_ ) {
    
    const AlgorithmType algTypeCHT = WALL_CHT;

    // If the user specified a Robin parameter, this is a Robin-type CHT; otherwise, it's convection
    bool isRobinCHT = userData.robinParameterSpec_;
    bool isConvectionCHT = !isRobinCHT;

    // first make sure all appropriate variables were specified
    if (isConvectionCHT)
    {
      if ( !userData.refTempSpec_)
        throw std::runtime_error("Sorry, h was specified while Tref was not");
      if ( !userData.htcSpec_)
        throw std::runtime_error("Sorry, Tref was specified while h was not");
    }
    else
    {
      if ( !userData.refTempSpec_)
        throw std::runtime_error("Sorry, Robin parameter was specified while Tref was not");
      if ( !userData.heatFluxSpec_)
        throw std::runtime_error("Sorry, Robin parameter was specified while heat flux was not");
    }

    // register boundary data
    ScalarFieldType *normalHeatFluxField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "normal_heat_flux"));
    stk::mesh::put_field(*normalHeatFluxField, *part);
    ScalarFieldType *tRefField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "reference_temperature"));
    stk::mesh::put_field(*tRefField, *part);

    ScalarFieldType *alphaField = NULL;
    if (isConvectionCHT)
    {
      alphaField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "heat_transfer_coefficient"));
    }
    if (isRobinCHT)
    {
      alphaField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "robin_coupling_parameter"));
    }
    stk::mesh::put_field(*alphaField, *part);
  
    // aux algs
    AuxFunctionAlgorithm *alphaAuxAlg;
    if (isRobinCHT)
    {
      RobinCouplingParameter alpha = userData.robinCouplingParameter_;
      std::vector<double> alphaUserSpec(1);
      alphaUserSpec[0] = alpha.robinCouplingParameter_;
      AuxFunction *alphaAuxFunc = new ConstantAuxFunction(0, 1, alphaUserSpec);
      alphaAuxAlg = new AuxFunctionAlgorithm(realm_,
                                             part,
                                             alphaField,
                                             alphaAuxFunc,
                                             stk::topology::NODE_RANK);
    }

    AuxFunctionAlgorithm *htcAuxAlg;
    if (isConvectionCHT)
    {
      HeatTransferCoefficient htc = userData.heatTransferCoefficient_;
      std::vector<double> htcUserSpec(1);
      htcUserSpec[0] = htc.heatTransferCoefficient_;
      AuxFunction *htcAuxFunc = new ConstantAuxFunction(0, 1, htcUserSpec);
      htcAuxAlg = new AuxFunctionAlgorithm(realm_, 
                                           part,
                                           alphaField, 
                                           htcAuxFunc,
                                           stk::topology::NODE_RANK);
    }

    NormalHeatFlux heatFlux = userData.q_;
    std::vector<double> qnUserSpec(1);
    // For convection, pass a zero heat flux field; for Robin, use specified value
    qnUserSpec[0] = (isRobinCHT ? heatFlux.qn_ : 0.0);
    AuxFunction *qnAuxFunc = new ConstantAuxFunction(0, 1, qnUserSpec);
    AuxFunctionAlgorithm *qnAuxAlg = new AuxFunctionAlgorithm(realm_,
                                                              part,
                                                              normalHeatFluxField,
                                                              qnAuxFunc,
                                                              stk::topology::NODE_RANK);
  
    ReferenceTemperature tRef = userData.referenceTemperature_;
    std::vector<double> tRefUserSpec(1);
    tRefUserSpec[0] = tRef.referenceTemperature_;
    AuxFunction *tRefAuxFunc = new ConstantAuxFunction(0, 1, tRefUserSpec);
    AuxFunctionAlgorithm *tRefAuxAlg = new AuxFunctionAlgorithm(realm_, 
                                                                part,
                                                                tRefField, 
                                                                tRefAuxFunc,
                                                                stk::topology::NODE_RANK);


    // decide where to put population of data
    // Normal heat flux, reference temperature, and coupling parameter
    // come from a transfer if this is an interface, so in that case
    // only need to populate the initial values
    if ( userData.isInterface_ ) {
      // xfer will handle population; only need to populate the initial value
      realm_.initCondAlg_.push_back(tRefAuxAlg);
      if (isRobinCHT) 
      {
        realm_.initCondAlg_.push_back(alphaAuxAlg);
        realm_.initCondAlg_.push_back(qnAuxAlg);
      }
      if (isConvectionCHT) realm_.initCondAlg_.push_back(htcAuxAlg);
    }
    else {
      // put it on bcData
      bcDataAlg_.push_back(tRefAuxAlg);
      if (isRobinCHT)
      {
        bcDataAlg_.push_back(alphaAuxAlg);
        bcDataAlg_.push_back(qnAuxAlg);
      }
      if (isConvectionCHT) bcDataAlg_.push_back(htcAuxAlg);
    }
    // For convection-type, normal heat flux remains zero -- just set at IC
    if (isConvectionCHT) realm_.initCondAlg_.push_back(qnAuxAlg);

    // solver contribution
    std::map<AlgorithmType, SolverAlgorithm *>::iterator itsi =
      solverAlgDriver_->solverAlgMap_.find(algTypeCHT);
    if ( itsi == solverAlgDriver_->solverAlgMap_.end() ) {
      AssembleHeatCondWallSolverAlgorithm *theAlg
        = new AssembleHeatCondWallSolverAlgorithm(realm_, 
                                                  part, 
                                                  this,
                                                  tRefField,
                                                  alphaField,
                                                  normalHeatFluxField,
                                                  realm_.realmUsesEdges_);
      solverAlgDriver_->solverAlgMap_[algTypeCHT] = theAlg;
    }
    else {
      itsi->second->partVec_.push_back(part);
    }
    
  }

}

//--------------------------------------------------------------------------
//-------- register_contact_bc ---------------------------------------------
//--------------------------------------------------------------------------
void
HeatCondEquationSystem::register_contact_bc(
  stk::mesh::Part *part,
  const stk::topology &theTopo,
  const ContactBoundaryConditionData &contactBCData)
{
  const AlgorithmType algType = CONTACT;

  ScalarFieldType &tempNp1 = temperature_->field_of_state(stk::mesh::StateNP1);
  VectorFieldType &dtdxNone = dtdx_->field_of_state(stk::mesh::StateNone); 

  ContactUserData userData = contactBCData.userData_;
  const bool useHermiteInterpolation = userData.useHermiteInterpolation_;

  if ( realm_.realmUsesEdges_ ) {

    // register halo_t if using the element-based projected nodal gradient
    ScalarFieldType *haloT = NULL;
    if ( !edgeNodalGradient_ ) {
      stk::mesh::MetaData &meta_data = realm_.fixture_->meta_data();
      haloT = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "halo_t"));
      stk::mesh::put_field(*haloT, *part);
    }
    
    // non-solver; contribution to Gjt
    std::map<AlgorithmType, Algorithm *>::iterator it =
      assembleNodalGradAlgDriver_->algMap_.find(algType);
    if ( it == assembleNodalGradAlgDriver_->algMap_.end() ) {
      Algorithm *theAlg = NULL;
      if ( edgeNodalGradient_ ) {
        theAlg = new AssembleNodalGradEdgeContactAlgorithm(realm_, part, &tempNp1, &dtdxNone);
      }
      else {
        theAlg = new AssembleNodalGradElemContactAlgorithm(realm_, part, &tempNp1, &dtdxNone, haloT);
      }
      assembleNodalGradAlgDriver_->algMap_[algType] = theAlg;
    }
    else {
      it->second->partVec_.push_back(part);
    }
  
    // solver; lhs
    std::map<AlgorithmType, SolverAlgorithm *>::iterator itsi =
      solverAlgDriver_->solverAlgMap_.find(algType);
    if ( itsi == solverAlgDriver_->solverAlgMap_.end() ) {
      AssembleScalarEdgeDiffContactSolverAlgorithm *theAlg
        = new AssembleScalarEdgeDiffContactSolverAlgorithm(realm_, part, this,
                                                           temperature_, dtdx_, thermalCond_,
                                                           useHermiteInterpolation);
      solverAlgDriver_->solverAlgMap_[algType] = theAlg;
    }
    else {
      itsi->second->partVec_.push_back(part);
    }
  }
  else {
    throw std::runtime_error("Element-based scheme not supported with contact bc");
  }
}

//--------------------------------------------------------------------------
//-------- register_non_conformal_bc ---------------------------------------
//--------------------------------------------------------------------------
void
HeatCondEquationSystem::register_non_conformal_bc(
    stk::mesh::Part *currentPart)
{

  if ( realm_.realmUsesEdges_ ) {
    throw std::runtime_error("Error: no support for non-conformal edge-based scheme");
  }
  
  // create the driver for post-porcessed quantities
  if ( NULL == assembleNonConformalAlgDriver_ ) {
    assembleNonConformalAlgDriver_ = new AlgorithmDriver(realm_);
  }
  
  const AlgorithmType algType = NON_CONFORMAL;

  // np1
  ScalarFieldType &tempNp1 = temperature_->field_of_state(stk::mesh::StateNP1);
  VectorFieldType &dtdxNone = dtdx_->field_of_state(stk::mesh::StateNone);

  stk::mesh::MetaData &meta_data = realm_.fixture_->meta_data();

  // non-solver; dtdx; allow for element-based shifted; all bcs are of generic type "WALL"
  std::map<AlgorithmType, Algorithm *>::iterator it
    = assembleNodalGradAlgDriver_->algMap_.find(algType);
  if ( it == assembleNodalGradAlgDriver_->algMap_.end() ) {
    Algorithm *theAlg = NULL;
    if ( edgeNodalGradient_ && realm_.realmUsesEdges_ ) {
      theAlg = new AssembleNodalGradEdgeBoundaryAlgorithm(realm_, currentPart, &tempNp1, &dtdxNone);
    }
    else {
      theAlg = new AssembleNodalGradElemBoundaryAlgorithm(realm_, currentPart, &tempNp1, &dtdxNone, edgeNodalGradient_);
    }
    assembleNodalGradAlgDriver_->algMap_[algType] = theAlg;
  }
  else {
    it->second->partVec_.push_back(currentPart);
  }

  // assemble and normalized lambda/L
  ScalarFieldType *ncNormalFlux = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "nc_t_normal_flux"));
  ScalarFieldType *ncPenalty = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "nc_t_penalty"));
  ScalarFieldType *ncArea = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "nc_t_assembled_area"));
  stk::mesh::put_field(*ncNormalFlux, *currentPart);
  stk::mesh::put_field(*ncPenalty, *currentPart);
  stk::mesh::put_field(*ncArea, *currentPart);
 
  std::map<AlgorithmType, Algorithm *>::iterator itnc
    = assembleNonConformalAlgDriver_->algMap_.find(algType);
  if ( itnc == assembleNonConformalAlgDriver_->algMap_.end() ) {
    Algorithm *theAlg 
      = new AssembleNonConformalElemDiffPenaltyAlgorithm(realm_, currentPart, temperature_, ncNormalFlux, ncPenalty, ncArea, thermalCond_, realm_.realmUsesEdges_);
    assembleNonConformalAlgDriver_->algMap_[algType] = theAlg;
  }
  else {
    itnc->second->partVec_.push_back(currentPart);
  }
  
  // solver; lhs; same for edge and element-based scheme
  std::map<AlgorithmType, SolverAlgorithm *>::iterator itsi =
    solverAlgDriver_->solverAlgMap_.find(algType);
  if ( itsi == solverAlgDriver_->solverAlgMap_.end() ) {
    AssembleScalarElemDiffNonConformalSolverAlgorithm *theAlg
      = new AssembleScalarElemDiffNonConformalSolverAlgorithm(realm_, currentPart, this, 
                                                              temperature_, ncNormalFlux, thermalCond_, ncPenalty);
    solverAlgDriver_->solverAlgMap_[algType] = theAlg;
  }
  else {
    itsi->second->partVec_.push_back(currentPart);
  }
  
}

//--------------------------------------------------------------------------
//-------- initialize ------------------------------------------------------
//--------------------------------------------------------------------------
void
HeatCondEquationSystem::initialize()
{
  solverAlgDriver_->initialize_connectivity();
  linsys_->finalizeLinearSystem();
}

//--------------------------------------------------------------------------
//-------- reinitialize_linear_system --------------------------------------
//--------------------------------------------------------------------------
void
HeatCondEquationSystem::reinitialize_linear_system()
{

  // delete linsys
  delete linsys_;

  // delete old solver
  const EquationType theEqID = EQ_TEMPERATURE;
  LinearSolver *theSolver = NULL;
  std::map<EquationType, LinearSolver *>::const_iterator iter
    = realm_.root()->linearSolvers_->solvers_.find(theEqID);
  if (iter != realm_.root()->linearSolvers_->solvers_.end()) {
    theSolver = (*iter).second;
    delete theSolver;
  }

  // create new solver
  std::string solverName = realm_.equationSystems_.get_solver_block_name("temperature");
  LinearSolver *solver = realm_.root()->linearSolvers_->create_solver(solverName, EQ_TEMPERATURE);
  linsys_ = LinearSystem::create(realm_, 1, "HeatCondEQS", solver);

  // initialize
  solverAlgDriver_->initialize_connectivity();
  linsys_->finalizeLinearSystem();
}

void
HeatCondEquationSystem::predict_state()
{

  // FIXME... move this to a generalized base class method
  stk::mesh::MetaData & meta_data = realm_.fixture_->meta_data();

  ScalarFieldType &dofN = temperature_->field_of_state(stk::mesh::StateN);
  ScalarFieldType &dofNp1 = temperature_->field_of_state(stk::mesh::StateNP1);

  // define some common selectors; select all nodes (locally and shared)
  // where mixFrac is defined
  stk::mesh::Selector s_all_nodes
    = (meta_data.locally_owned_part() | meta_data.globally_shared_part())
    &stk::mesh::selectField(*temperature_);

  //===========================================================
  // copy state N into N+1
  //===========================================================

  stk::mesh::BucketVector const& node_buckets =
    realm_.get_buckets( stk::topology::NODE_RANK, s_all_nodes );
  for ( stk::mesh::BucketVector::const_iterator ib = node_buckets.begin() ;
        ib != node_buckets.end() ; ++ib ) {
    stk::mesh::Bucket & b = **ib ;
    const stk::mesh::Bucket::size_type length   = b.size();
    double * dN = stk::mesh::field_data(dofN, b);
    double * dNp1 = stk::mesh::field_data(dofNp1, b);
    for ( stk::mesh::Bucket::size_type k = 0 ; k < length ; ++k ) {
      dNp1[k] = dN[k];
    }
  }
}

//--------------------------------------------------------------------------
//-------- solve_and_update ------------------------------------------------
//--------------------------------------------------------------------------
void
HeatCondEquationSystem::solve_and_update()
{
  // initialize fields

  if ( isInit_ ) {
    assembleNodalGradAlgDriver_->execute();
    isInit_ = false;
  }

  for ( int k = 0; k < maxIterations_; ++k ) {

    NaluEnv::self().naluOutputP0() << " " << k+1 << "/" << maxIterations_
                    << std::setw(15) << std::right << name_ << std::endl;

    // post process non-conformal algorithm
    if ( NULL != assembleNonConformalAlgDriver_ )
      assembleNonConformalAlgDriver_->execute();
    
    // heat conduction assemble, load_complete and solve
    assemble_and_solve(tTmp_);

    // update
    double timeA = stk::cpu_time();
    field_axpby(
      realm_.fixture_->meta_data(),
      realm_.fixture_->bulk_data(),
      1.0, *tTmp_,
      1.0, *temperature_);
    double timeB = stk::cpu_time();
    timerAssemble_ += (timeB-timeA);
   
    // projected nodal gradient
    timeA = stk::cpu_time();
    assembleNodalGradAlgDriver_->execute();
    timeB = stk::cpu_time();
    timerMisc_ += (timeB-timeA);
  }
}

//--------------------------------------------------------------------------
//-------- register_initial_condition_fcn ------------------------------------------------
//--------------------------------------------------------------------------
void
HeatCondEquationSystem::register_initial_condition_fcn(
  stk::mesh::Part *part,
  const std::map<std::string, std::string> &theNames,
  const std::map<std::string, std::vector<double> > &/*theParams*/)
{
  // iterate map and check for name
  const std::string dofName = "temperature";
  std::map<std::string, std::string>::const_iterator iterName
    = theNames.find(dofName);
  if (iterName != theNames.end()) {
    std::string fcnName = (*iterName).second;
    AuxFunction *theAuxFunc = NULL;
    if ( fcnName == "steady_2d_thermal" ) {
      // create the function
      theAuxFunc = new SteadyThermalContactAuxFunction();      
    }
    else {
      throw std::runtime_error("HeatCondEquationSystem::register_initial_condition_fcn: steady_2d_thermal only supported");
    }
    
    // create the algorithm
    AuxFunctionAlgorithm *auxAlg
      = new AuxFunctionAlgorithm(realm_, part,
				 temperature_, theAuxFunc,
				 stk::topology::NODE_RANK);
    
    // push to ic
    realm_.initCondAlg_.push_back(auxAlg);
    
  }
}

} // namespace nalu
} // namespace Sierra
