/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


#ifndef SolutionOptions_h
#define SolutionOptions_h

#include <NaluParsing.h>
#include <Enums.h>

// standard c++
#include <string>
#include <map>
#include <utility>

namespace sierra{
namespace nalu{

enum ErrorIndicatorType {
  EIT_NONE                = 0,
  EIT_PSTAB               = 1 << 1,
  EIT_LIMITER             = 1 << 2 ,
  EIT_SIMPLE_BASE         = 1 << 3,
  EIT_SIMPLE_VORTICITY    = EIT_SIMPLE_BASE + (1 << 4),
  EIT_SIMPLE_VORTICITY_DX = EIT_SIMPLE_BASE + (1 << 5),
  EIT_SIMPLE_DUDX2        = EIT_SIMPLE_BASE + (1 << 6)
};


class SolutionOptions
{
public:

  SolutionOptions();
  ~SolutionOptions();

  void load(const YAML::Node & node);
  void initialize_turbulence_constants();
  double hybridDefault_;
  double alphaDefault_;
  double alphaUpwDefault_;
  double upwDefault_;
  double lamScDefault_;
  double lamPrDefault_;
  double turbScDefault_;
  double turbPrDefault_;
  bool nocDefault_;
  double referenceDensity_;
  double referenceTemperature_;
  double thermalExpansionCoeff_;
  double stefanBoltzmann_;
  double nearestFaceEntrain_;
  double includeDivU_;
  bool mdotInterpRhoUTogether_;
  bool isTurbulent_;
  TurbulenceModel turbulenceModel_;
  bool meshMotion_;
  bool meshDeformation_;
  bool externalMeshDeformation_;
  bool activateUniformRefinement_;
  bool uniformRefineSaveAfter_;
  std::vector<int> refineAt_;
  bool activateAdaptivity_;
  ErrorIndicatorType errorIndicatorType_;
  int adaptivityFrequency_;
  bool useMarker_;
  double refineFraction_;
  double unrefineFraction_;
  double physicalErrIndCriterion_;
  double physicalErrIndUnrefCriterionMultipler_;
  double maxRefinementNumberOfElementsFraction_;
  bool adapterExtraOutput_;
  bool useAdapter_;
  int maxRefinementLevel_;
  double extrusionCorrectionFac_;
  bool ncAlgGaussLabatto_;
  NonConformalAlgType ncAlgType_;

  // turbulence model coeffs
  std::map<TurbulenceModelConstant, double> turbModelConstantMap_;
  
  // numerics related
  std::map<std::string, double> hybridMap_;
  std::map<std::string, double> alphaMap_;
  std::map<std::string, double> alphaUpwMap_;
  std::map<std::string, double> upwMap_;
  std::map<std::string, bool> limiterMap_;

  // property related
  std::map<std::string, double> lamScMap_;
  std::map<std::string, double> lamPrMap_;
  std::map<std::string, double> turbScMap_;
  std::map<std::string, double> turbPrMap_;

  // source
  std::map<std::string, std::vector<std::string> > srcTermsMap_;
  std::map<std::string, std::vector<double> > srcTermParamMap_;

  // nodal gradient
  std::map<std::string, std::string> nodalGradMap_;

  // non-orthogonal correction
  std::map<std::string, bool> nocMap_;

  // read any fields from input files
  std::map<std::string, std::string> inputVarFromFileMap_;

  // mesh motion
  std::map<std::string, std::pair<std::vector<std::string>, double > > meshMotionMap_;

  std::vector<double> gravity_;

  std::string name_;

};

} // namespace nalu
} // namespace Sierra

#endif
