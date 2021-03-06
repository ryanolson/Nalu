/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


#ifndef DgInfo_h
#define DgInfo_h

//==============================================================================
// Includes and forwards
//==============================================================================

#include <stk_mesh/base/Entity.hpp>

#include <vector>

namespace sierra {
namespace nalu {

class MasterElement;

//=============================================================================
// Class Definition
//=============================================================================
// DgInfo
//=============================================================================
/**
 * * @par Description:
 * - class for halo stuff.
 *
 * @par Design Considerations:
 * -
 */
//=============================================================================
class DgInfo {

 public:

  // constructor and destructor
  DgInfo(
    int parallelRank,
    uint64_t globalFaceId,
    uint64_t localGaussPointId,
    int currentGaussPointId,
    stk::mesh::Entity currentFace,
    stk::mesh::Entity currentElement,
    const int currentFaceOrdinal,
    MasterElement *meFCCurrent,
    MasterElement *meSCSurrent,
    const int nDim);
  
  ~DgInfo();
  
  int parallelRank_;
  uint64_t globalFaceId_;
  uint64_t localGaussPointId_;
  int currentGaussPointId_;

  stk::mesh::Entity currentFace_;
  stk::mesh::Entity currentElement_;
  const int currentFaceOrdinal_;
  MasterElement *meFCCurrent_;
  MasterElement *meSCSCurrent_;

  int nDim_;
  double bestX_;
  int opposingFaceIsGhosted_;

  // search provides opposing face
  stk::mesh::Entity opposingFace_;

  // face:element relations provide connected element to opposing face
  stk::mesh::Entity opposingElement_;

  // for the opposing face, what is its ordinal?
  int opposingFaceOrdinal_;

  // master element for opposing face
  MasterElement *meFCOpposing_;

  // master element for opposing face connected element
  MasterElement *meSCSOpposing_;

  // coordinates of gauss points on current face
  std::vector<double> currentGaussPointCoords_;

  // iso-parametric coordinates for gauss point on current face (-1:1)
  std::vector<double> currentIsoParCoords_;

  // iso-parametric coordinates for gauss point on opposing face (-1:1)
  std::vector<double> opposingIsoParCoords_;
  
};
  
} // end sierra namespace
} // end nalu namespace

#endif
