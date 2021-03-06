/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


// nalu
#include <AssembleNodalGradUEdgeBoundaryAlgorithm.h>
#include <Realm.h>
#include <master_element/MasterElement.h>

// stk_mesh/base/fem
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/Field.hpp>
#include <stk_mesh/base/GetBuckets.hpp>
#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/Part.hpp>

// stk_io
#include <stk_io/StkMeshIoBroker.hpp>

namespace sierra{
namespace nalu{

//==========================================================================
// Class Definition
//==========================================================================
// AssembleNodalGradUEdgeBoundaryAlgorithm - adds in boundary contribution
//==========================================================================
//--------------------------------------------------------------------------
//-------- constructor -----------------------------------------------------
//--------------------------------------------------------------------------
AssembleNodalGradUEdgeBoundaryAlgorithm::AssembleNodalGradUEdgeBoundaryAlgorithm(
  Realm &realm,
  stk::mesh::Part *part,
  VectorFieldType *velocity,
  GenericFieldType *dudx)
  : Algorithm(realm, part),
    velocity_(velocity),
    dudx_(dudx)
{
  // does nothing
}

//--------------------------------------------------------------------------
//-------- execute ---------------------------------------------------------
//--------------------------------------------------------------------------
void
AssembleNodalGradUEdgeBoundaryAlgorithm::execute()
{

  stk::mesh::MetaData & meta_data = realm_.fixture_->meta_data();

  const int nDim = meta_data.spatial_dimension();

  // extract fields
  GenericFieldType *exposedAreaVec = meta_data.get_field<GenericFieldType>(meta_data.side_rank(), "exposed_area_vector");
  ScalarFieldType *dualNodalVolume = meta_data.get_field<ScalarFieldType>(stk::topology::NODE_RANK, "dual_nodal_volume");

  // select all faces since toplogy is abstracted out
  stk::mesh::Selector s_locally_owned_union = meta_data.locally_owned_part()
    &stk::mesh::selectUnion(partVec_);

  stk::mesh::BucketVector const& face_buckets =
    realm_.get_buckets( meta_data.side_rank(), s_locally_owned_union );
  for ( stk::mesh::BucketVector::const_iterator ib = face_buckets.begin();
        ib != face_buckets.end() ; ++ib ) {
    stk::mesh::Bucket & b = **ib ;
    const stk::mesh::Bucket::size_type length   = b.size();
    for ( stk::mesh::Bucket::size_type k = 0 ; k < length ; ++k ) {

      // face data
      double * areaVec = stk::mesh::field_data(*exposedAreaVec, b, k);

      // face node relations for nodal gather
      stk::mesh::Entity const * face_node_rels = b.begin_nodes(k);

      // assemble into nodal location;
      // one-to-one mapping between nodes and face ip
      int num_nodes = b.num_nodes(k);
      for ( int ni = 0; ni < num_nodes; ++ni ) {

        stk::mesh::Entity node = face_node_rels[ni];

        // off set of area vector
        const int offSet = ni*nDim;

        double * dudx = stk::mesh::field_data(*dudx_, node);
        const double invVol = 1.0 / *stk::mesh::field_data(*dualNodalVolume, node);

        int counter = 0;
        const double *uNN = stk::mesh::field_data(*velocity_, node);
        for ( int i = 0; i < nDim; ++i ) {
          const double fac = uNN[i]*invVol;
          for ( int j=0; j < nDim; ++j ) {
            dudx[counter++] += fac*areaVec[offSet+j];
          }
        }
      }
    }
  }
}

} // namespace nalu
} // namespace Sierra
