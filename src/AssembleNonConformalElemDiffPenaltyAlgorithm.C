/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


// nalu
#include <AssembleNonConformalElemDiffPenaltyAlgorithm.h>

#include <FieldTypeDef.h>
#include <Realm.h>
#include <TimeIntegrator.h>
#include <master_element/MasterElement.h>

// stk_mesh/base/fem
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/Field.hpp>
#include <stk_mesh/base/FieldParallel.hpp>
#include <stk_mesh/base/GetBuckets.hpp>
#include <stk_mesh/base/GetEntities.hpp>
#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/Part.hpp>

// stk_io
#include <stk_io/StkMeshIoBroker.hpp>

// basic c++
#include <math.h>

namespace sierra{
namespace nalu{

//==========================================================================
// Class Definition
//==========================================================================
// AssembleNonConformalElemDiffPenaltyAlgorithm - nodal lambda, flux
//==========================================================================
//--------------------------------------------------------------------------
//-------- constructor -----------------------------------------------------
//--------------------------------------------------------------------------
AssembleNonConformalElemDiffPenaltyAlgorithm::AssembleNonConformalElemDiffPenaltyAlgorithm(
  Realm &realm,
  stk::mesh::Part *part,
  ScalarFieldType *scalarQ,
  ScalarFieldType *ncNormalFlux,
  ScalarFieldType *ncPenalty,
  ScalarFieldType *ncArea,
  ScalarFieldType *diffFluxCoeff,
  const bool useShifted)
  : Algorithm(realm, part),
    scalarQ_(scalarQ),
    ncNormalFlux_(ncNormalFlux),
    ncPenalty_(ncPenalty),
    ncArea_(ncArea),
    diffFluxCoeff_(diffFluxCoeff),
    useShifted_(useShifted),
    coordinates_(NULL),
    exposedAreaVec_(NULL)
{
  // save off fields
  stk::mesh::MetaData & meta_data = realm_.fixture_->meta_data();
  coordinates_ = meta_data.get_field<VectorFieldType>(stk::topology::NODE_RANK, realm_.get_coordinates_name());
  exposedAreaVec_ = meta_data.get_field<GenericFieldType>(meta_data.side_rank(), "exposed_area_vector");
}

//--------------------------------------------------------------------------
//-------- execute ---------------------------------------------------------
//--------------------------------------------------------------------------
void
AssembleNonConformalElemDiffPenaltyAlgorithm::execute()
{

  // zero out fields...
  zero_fields();
  
  stk::mesh::BulkData & bulk_data = realm_.fixture_->bulk_data();
  stk::mesh::MetaData & meta_data = realm_.fixture_->meta_data();

  const int nDim = meta_data.spatial_dimension();

  // nodal fields to gather
  std::vector<double> ws_coordinates;
  std::vector<double> ws_scalarQ;
  std::vector<double> ws_diffFluxCoeff;
 
  // master element
  std::vector<double> ws_face_shape_function;
  std::vector<double> ws_dndx;
  std::vector<double> ws_det_j;

  // define vector of parent topos; should always be UNITY in size
  std::vector<stk::topology> parentTopo;

  // define some common selectors
  stk::mesh::Selector s_locally_owned_union = meta_data.locally_owned_part()
    &stk::mesh::selectUnion(partVec_);

  stk::mesh::BucketVector const& face_buckets =
    realm_.get_buckets( meta_data.side_rank(), s_locally_owned_union );
  for ( stk::mesh::BucketVector::const_iterator ib = face_buckets.begin();
        ib != face_buckets.end() ; ++ib ) {
    stk::mesh::Bucket & b = **ib ;

    // extract connected element topology
    b.parent_topology(stk::topology::ELEMENT_RANK, parentTopo);
    ThrowAssert ( parentTopo.size() == 1 );
    stk::topology theElemTopo = parentTopo[0];
    MasterElement *meSCS = realm_.get_surface_master_element(theElemTopo);
    const int nodesPerElement = meSCS->nodesPerElement_;

    // face master element
    MasterElement *meFC = realm_.get_surface_master_element(b.topology());
    const int nodesPerFace = meFC->nodesPerElement_;

    // size some things that are useful
    int num_face_nodes = b.topology().num_nodes();
    std::vector<int> face_node_ordinals(num_face_nodes);

    // algorithm related; element nodes
    ws_coordinates.resize(nodesPerElement*nDim);
    ws_scalarQ.resize(nodesPerElement);
    // face nodes
    ws_diffFluxCoeff.resize(nodesPerFace);
    ws_face_shape_function.resize(nodesPerFace*nodesPerFace);
    ws_dndx.resize(nDim*nodesPerFace*nodesPerElement);
    ws_det_j.resize(nodesPerFace);

    // pointers
    double *p_coordinates = &ws_coordinates[0];
    double *p_scalarQ = &ws_scalarQ[0];
    double *p_diffFluxCoeff = &ws_diffFluxCoeff[0];
    double *p_face_shape_function = &ws_face_shape_function[0];
    double *p_dndx = &ws_dndx[0];

    // shape function
    if ( useShifted_ )
      meFC->shifted_shape_fcn(&p_face_shape_function[0]);
    else
      meFC->shape_fcn(&p_face_shape_function[0]);
    
    const stk::mesh::Bucket::size_type length   = b.size();

    for ( stk::mesh::Bucket::size_type k = 0 ; k < length ; ++k ) {

      // get face
      stk::mesh::Entity face = b[k];

      // pointer to face data
      const double * areaVec = stk::mesh::field_data(*exposedAreaVec_, b, k);

      //======================================
      // gather nodal data off of face
      //======================================
      stk::mesh::Entity const * face_node_rels = bulk_data.begin_nodes(face);
      num_face_nodes = bulk_data.num_nodes(face);
      // sanity check on num nodes
      ThrowAssert( num_face_nodes == nodesPerFace );
      for ( int ni = 0; ni < num_face_nodes; ++ni ) {
        stk::mesh::Entity node = face_node_rels[ni];
        // gather scalars
        p_diffFluxCoeff[ni] = *stk::mesh::field_data(*diffFluxCoeff_, node);
      }

      // extract the connected element to this exposed face; should be single in size!
      stk::mesh::Entity const * face_elem_rels = b.begin_elements(k);
      ThrowAssert( b.num_elements(k) == 1 );

      // get element; its face ordinal number and populate face_node_ordinals
      stk::mesh::Entity element = face_elem_rels[0];
      const int face_ordinal = b.begin_element_ordinals(k)[0];
      theElemTopo.side_node_ordinals(face_ordinal, face_node_ordinals.begin());

      //==========================================
      // gather nodal data off of element
      //==========================================
      stk::mesh::Entity const * elem_node_rels = bulk_data.begin_nodes(element);
      int num_nodes = bulk_data.num_nodes(element);
      // sanity check on num nodes
      ThrowAssert( num_nodes == nodesPerElement );
      for ( int ni = 0; ni < num_nodes; ++ni ) {
        stk::mesh::Entity node = elem_node_rels[ni];
        // gather scalars
        p_scalarQ[ni] = *stk::mesh::field_data(*scalarQ_, node);
        // gather vectors
        double * coords = stk::mesh::field_data(*coordinates_, node);
        const int offSet = ni*nDim;
        for ( int j=0; j < nDim; ++j ) {
          p_coordinates[offSet+j] = coords[j];
        }
      }

      // compute dndx
      double scs_error = 0.0;
      meSCS->face_grad_op(1, face_ordinal, &p_coordinates[0], &p_dndx[0], &ws_det_j[0], &scs_error);
      
      for ( int ip = 0; ip < num_face_nodes; ++ip ) {

        const int opposingNode = meSCS->opposingNodes(face_ordinal,ip);
        const int nearestNode = face_node_ordinals[ip];

        // node on the face
        stk::mesh::Entity nodeR = elem_node_rels[nearestNode];

        // pointers to nearest node data
        double *ncNormalFlux = stk::mesh::field_data(*ncNormalFlux_, nodeR);
        double *ncPenalty = stk::mesh::field_data(*ncPenalty_, nodeR);
        double *ncArea = stk::mesh::field_data(*ncArea_, nodeR);
       
        // offset for bip area vector and types of shape function
        const int faceOffSet = ip*nDim;
        const int offSetSF_face = ip*nodesPerFace;

        // interpolate to bip
        double diffFluxCoeffBip = 0.0;
        for ( int ic = 0; ic < nodesPerFace; ++ic ) {
          const double r = p_face_shape_function[offSetSF_face+ic];
          diffFluxCoeffBip += r*p_diffFluxCoeff[ic];
        }

        // characteristic length
        double charLength = 0.0;
        for ( int j = 0; j < nDim; ++j ) {
          double dxj = p_coordinates[opposingNode*nDim+j] - p_coordinates[nearestNode*nDim+j];
          charLength += dxj*dxj;
        }
        charLength = std::sqrt(charLength);

        // handle flux due to on and off face in a single loop (on/off provided above)
        double dndx = 0.0;
        for ( int ic = 0; ic < nodesPerElement; ++ic ) {
          const int offSetDnDx = nDim*nodesPerElement*ip + ic*nDim;
          const double scalarQIC = p_scalarQ[ic];
          for ( int j = 0; j < nDim; ++j ) {
            const double axj = areaVec[faceOffSet+j];
            const double dndxj = p_dndx[offSetDnDx+j];
            const double dTdA = dndxj*axj*scalarQIC;
            dndx    += dTdA;
          }
        }

        // compute assembled area
        double aMag = 0.0;
        for ( int j = 0; j < nDim; ++j ) {
          const double axj = areaVec[faceOffSet+j];
          aMag += axj*axj;
        }
        aMag = std::sqrt(aMag);
        
        // assemble the nodal quantities
        *ncNormalFlux -= diffFluxCoeffBip*dndx;
        *ncPenalty += diffFluxCoeffBip/charLength*aMag;
        *ncArea += aMag;
      }
    }
  }
  
  // assemble fields
  assemble_and_normalize();

}

//--------------------------------------------------------------------------
//-------- destructor ------------------------------------------------------
//--------------------------------------------------------------------------
AssembleNonConformalElemDiffPenaltyAlgorithm::~AssembleNonConformalElemDiffPenaltyAlgorithm()
{
  // does nothing
}

//--------------------------------------------------------------------------
//-------- zero_fields -----------------------------------------------------
//--------------------------------------------------------------------------
void
AssembleNonConformalElemDiffPenaltyAlgorithm::zero_fields()
{  
  stk::mesh::BulkData & bulk_data = realm_.fixture_->bulk_data();
  stk::mesh::MetaData & meta_data = realm_.fixture_->meta_data();

  // define some common selectors; select all nodes (locally and shared)
  stk::mesh::Selector s_all_nodes
    = (meta_data.locally_owned_part() | meta_data.globally_shared_part())
    &stk::mesh::selectField(*ncPenalty_);

  stk::mesh::BucketVector const& node_buckets =
    realm_.get_buckets( stk::topology::NODE_RANK, s_all_nodes );
  for ( stk::mesh::BucketVector::const_iterator ib = node_buckets.begin() ;
        ib != node_buckets.end() ; ++ib ) {
    stk::mesh::Bucket & b = **ib ;
    const stk::mesh::Bucket::size_type length   = b.size();
    double * ncPenalty = stk::mesh::field_data(*ncPenalty_, b);
    double * ncNormalFlux = stk::mesh::field_data(*ncNormalFlux_, b);
    double * ncArea = stk::mesh::field_data(*ncArea_, b);
    for ( stk::mesh::Bucket::size_type k = 0 ; k < length ; ++k ) {
      ncNormalFlux[k] = 0.0;
      ncPenalty[k] = 0.0; 
      ncArea[k] = 0.0; 
    }
  }
}

//--------------------------------------------------------------------------
//-------- assemble_and_normalize ------------------------------------------
//--------------------------------------------------------------------------
void
AssembleNonConformalElemDiffPenaltyAlgorithm::assemble_and_normalize()
{
  stk::mesh::BulkData & bulk_data = realm_.fixture_->bulk_data();
  stk::mesh::MetaData & meta_data = realm_.fixture_->meta_data();

  std::vector<stk::mesh::FieldBase*> fields;
  fields.push_back(ncPenalty_);
  fields.push_back(ncArea_);
  stk::mesh::parallel_sum(bulk_data, fields);

  if ( realm_.hasPeriodic_) {
    const unsigned sizeOfField = 1;
    realm_.periodic_field_update(ncPenalty_, sizeOfField);
    realm_.periodic_field_update(ncNormalFlux_, sizeOfField);
    realm_.periodic_field_update(ncArea_, sizeOfField);
  }

  // normalize
  stk::mesh::Selector s_all_nodes
    = (meta_data.locally_owned_part() | meta_data.globally_shared_part())
    &stk::mesh::selectField(*ncPenalty_);
  
  stk::mesh::BucketVector const& node_buckets =
    realm_.get_buckets( stk::topology::NODE_RANK, s_all_nodes );
  for ( stk::mesh::BucketVector::const_iterator ib = node_buckets.begin() ;
        ib != node_buckets.end() ; ++ib ) {
    stk::mesh::Bucket & b = **ib ;
    const stk::mesh::Bucket::size_type length   = b.size();
    double * ncNormalFlux = stk::mesh::field_data(*ncNormalFlux_, b);
    double * ncPenalty = stk::mesh::field_data(*ncPenalty_, b);
    const double * ncArea = stk::mesh::field_data(*ncArea_, b);
    for ( stk::mesh::Bucket::size_type k = 0 ; k < length ; ++k ) {
      ncNormalFlux[k] /= ncArea[k];
      ncPenalty[k] /= ncArea[k];
    }
  }
  
}

} // namespace nalu
} // namespace Sierra
