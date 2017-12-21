// ---------------------------------------------------------------------
//
// Copyright (c) 2017 The Regents of the University of Michigan and DFT-FE authors.
//
// This file is part of the DFT-FE code.
//
// The DFT-FE code is free software; you can use it, redistribute
// it, and/or modify it under the terms of the GNU Lesser General
// Public License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// The full text of the license can be found in the file LICENSE at
// the top level of the DFT-FE distribution.
//
// ---------------------------------------------------------------------
//
// @author Sambit Das (2017)
//

template<unsigned int FEOrder>
void forceClass<FEOrder>::configForceLinFEInit()
{

  dftPtr->matrix_free_data.initialize_dof_vector(d_configForceVectorLinFE,d_forceDofHandlerIndex);
  d_configForceVectorLinFE=0;//also zeros out the ghost vectors
}

template<unsigned int FEOrder>
void forceClass<FEOrder>::configForceLinFEFinalize()
{
  d_configForceVectorLinFE.compress(VectorOperation::add);//copies the ghost element cache to the owning element 
  d_constraintsNoneForce.distribute(d_configForceVectorLinFE);//distribute to constrained degrees of freedom (for example periodic)
  d_configForceVectorLinFE.update_ghost_values();


}

//compute configurational force on the mesh nodes using linear shape function generators
template<unsigned int FEOrder>
void forceClass<FEOrder>::computeConfigurationalForceTotalLinFE()
{

 
  configForceLinFEInit();

#ifdef ENABLE_PERIODIC_BC
  computeConfigurationalForceEEshelbyTensorPeriodicLinFE(); 
#else  
  computeConfigurationalForceEEshelbyTensorNonPeriodicLinFE(); 
#endif

  //computeConfigurationalForcePhiExtLinFE();
  //computeConfigurationalForceEselfNoSurfaceLinFE();
  computeConfigurationalForceEselfLinFE();
  configForceLinFEFinalize();

  
  std::map<std::pair<unsigned int,unsigned int>, unsigned int> ::const_iterator it; 
  for (it=d_atomsForceDofs.begin(); it!=d_atomsForceDofs.end(); ++it){
	 const std::pair<unsigned int,unsigned int> & atomIdPair= it->first;
	 const unsigned int atomForceDof=it->second;
	 std::cout<<" atomId: "<< atomIdPair.first << ", force component: "<<atomIdPair.second << ", force: "<<d_configForceVectorLinFE[atomForceDof] << std::endl;
  }
   

}

//compute configurational force on the mesh nodes using linear shape function generators
template<unsigned int FEOrder>
void forceClass<FEOrder>::computeConfigurationalForceEEshelbyTensorNonPeriodicLinFE()
{
 
  const int numVectorizedArrayElements=VectorizedArray<double>::n_array_elements;
  const MatrixFree<3,double> & matrix_free_data=dftPtr->matrix_free_data;
  std::map<dealii::CellId, std::vector<double> >  **rhoOutValues=&(dftPtr->rhoOutValues);
  std::map<dealii::CellId, std::vector<double> > **gradRhoOutValues=&(dftPtr->gradRhoOutValues);
  //std::cout<< "n array elements" << numVectorizedArrayElements <<std::endl;

  FEEvaluation<C_DIM,1,C_num1DQuad<FEOrder>(),C_DIM>  forceEval(matrix_free_data,d_forceDofHandlerIndex, 0);
  FEEvaluation<C_DIM,FEOrder,C_num1DQuad<FEOrder>(),1> phiTotEval(matrix_free_data,dftPtr->phiTotDofHandlerIndex, 0);//no constraints
  FEEvaluation<C_DIM,FEOrder,C_num1DQuad<FEOrder>(),1> psiEval(matrix_free_data,dftPtr->eigenDofHandlerIndex , 0);//no constraints

  const int numQuadPoints=forceEval.n_q_points;
  const int numEigenVectors=dftPtr->eigenVectorsOrig[0].size();  
  DoFHandler<C_DIM>::active_cell_iterator subCellPtr;
  Tensor<1,C_DIM,VectorizedArray<double> > zeroTensor1;

  for (unsigned int idim=0; idim<C_DIM; idim++)
    zeroTensor1[idim]=make_vectorized_array(0.0);

  for (unsigned int cell=0; cell<matrix_free_data.n_macro_cells(); ++cell){
    forceEval.reinit(cell);
    phiTotEval.reinit(cell);
    phiTotEval.read_dof_values_plain(dftPtr->poissonPtr->phiTotRhoOut);//read without taking constraints into account
    phiTotEval.evaluate(true,true);
    psiEval.reinit(cell);

    std::vector<VectorizedArray<double> > rhoQuads(numQuadPoints,make_vectorized_array(0.0));
    std::vector<Tensor<1,C_DIM,VectorizedArray<double> > > gradRhoQuads(numQuadPoints,zeroTensor1);
    std::vector<VectorizedArray<double> > excQuads(numQuadPoints,make_vectorized_array(0.0));
    std::vector<Tensor<1,C_DIM,VectorizedArray<double> > > gradRhoExcQuads(numQuadPoints,zeroTensor1);

    const unsigned int numSubCells=matrix_free_data.n_components_filled(cell);
    
    for (unsigned int iSubCell=0; iSubCell<numSubCells; ++iSubCell){
       subCellPtr= matrix_free_data.get_cell_iterator(cell,iSubCell);
       dealii::CellId subCellId=subCellPtr->id();
       std::vector<double> exchValQuads(numQuadPoints);
       std::vector<double> corrValQuads(numQuadPoints); 
       if(dftPtr->d_xc_id == 4){
           pcout<< " GGA force computation not implemented yet"<<std::endl;
	   exit(-1);
       }
       else{
         xc_lda_exc(&(dftPtr->funcX),numQuadPoints,&((**rhoOutValues)[subCellId][0]),&exchValQuads[0]);
         xc_lda_exc(&(dftPtr->funcC),numQuadPoints,&((**rhoOutValues)[subCellId][0]),&corrValQuads[0]);     
         for (unsigned int q=0; q<numQuadPoints; ++q){
           rhoQuads[q][iSubCell]=(**rhoOutValues)[subCellId][q];
	   //gradRhoQuads[q][0][i]=(**gradRhoOutValues)[subCellId][3*q];
	   //gradRhoQuads[q][1][i]=(**gradRhoOutValues)[subCellId][3*q+1];
           //gradRhoQuads[q][2][i]=(**gradRhoOutValues)[subCellId][3*q+2];
	   excQuads[q][iSubCell]=exchValQuads[q]+corrValQuads[q];
         }
       }
    }   
    
    std::vector< VectorizedArray<double> > psiQuads(numQuadPoints*numEigenVectors,make_vectorized_array(0.0));
    std::vector<Tensor<1,C_DIM,VectorizedArray<double> > > gradPsiQuads(numQuadPoints*numEigenVectors,zeroTensor1);
   
    
    for (unsigned int iEigenVec=0; iEigenVec<numEigenVectors; ++iEigenVec){
      //psiEval.reinit(cell);	    
      psiEval.read_dof_values_plain(*((dftPtr->eigenVectorsOrig)[0][iEigenVec]));//read without taking constraints into account
      psiEval.evaluate(true,true);
      for (unsigned int q=0; q<numQuadPoints; ++q){
        psiQuads[q*numEigenVectors+iEigenVec]=psiEval.get_value(q);   
        gradPsiQuads[q*numEigenVectors+iEigenVec]=psiEval.get_gradient(q);	      
      }     
    }    
    

    for (unsigned int q=0; q<numQuadPoints; ++q){
       VectorizedArray<double> phiTot_q =phiTotEval.get_value(q);   
       Tensor<1,C_DIM,VectorizedArray<double> > gradPhiTot_q =phiTotEval.get_gradient(q);
       forceEval.submit_gradient(eshelbyTensor::getELocEshelbyTensorNonPeriodic(phiTot_q,
			                                         gradPhiTot_q,
						                 rhoQuads[q],
						                 gradRhoQuads[q],
						                 excQuads[q],
						                 gradRhoExcQuads[q],
						                 psiQuads.begin()+q*numEigenVectors,
						                 gradPsiQuads.begin()+q*numEigenVectors,
								 (dftPtr->eigenValues)[0],
								 dftPtr->fermiEnergy,
								 dftPtr->d_TVal),
		                                                 q);
    }
    forceEval.integrate (false,true);
    forceEval.distribute_local_to_global(d_configForceVectorLinFE);//also takes care of constraints

  }
} 

//compute configurational force on the mesh nodes using linear shape function generators
template<unsigned int FEOrder>
void forceClass<FEOrder>::computeConfigurationalForceEEshelbyTensorPeriodicLinFE()
{
 
  const int numVectorizedArrayElements=VectorizedArray<double>::n_array_elements;
  const MatrixFree<3,double> & matrix_free_data=dftPtr->matrix_free_data;
  std::map<dealii::CellId, std::vector<double> >  **rhoOutValues=&(dftPtr->rhoOutValues);
  std::map<dealii::CellId, std::vector<double> > **gradRhoOutValues=&(dftPtr->gradRhoOutValues);
  //std::cout<< "n array elements" << numVectorizedArrayElements <<std::endl;

  FEEvaluation<C_DIM,1,C_num1DQuad<FEOrder>(),C_DIM>  forceEval(matrix_free_data,d_forceDofHandlerIndex, 0);
  FEEvaluation<C_DIM,FEOrder,C_num1DQuad<FEOrder>(),1> phiTotEval(matrix_free_data,dftPtr->phiTotDofHandlerIndex, 0);//no constraints
  FEEvaluation<C_DIM,FEOrder,C_num1DQuad<FEOrder>(),2> psiEval(matrix_free_data,dftPtr->eigenDofHandlerIndex , 0);//no constraints

  const int numQuadPoints=forceEval.n_q_points;
  const int numEigenVectors=dftPtr->eigenVectorsOrig[0].size();  
  const int numKPoints=dftPtr->d_kPointWeights.size();
  DoFHandler<C_DIM>::active_cell_iterator subCellPtr;
  Tensor<1,2,VectorizedArray<double> > zeroTensor1;zeroTensor1[0]=make_vectorized_array(0.0);zeroTensor1[1]=make_vectorized_array(0.0);
  Tensor<1,2, Tensor<1,C_DIM,VectorizedArray<double> > > zeroTensor2;
  Tensor<1,C_DIM,VectorizedArray<double> > zeroTensor3;
  for (unsigned int idim=0; idim<C_DIM; idim++){
    zeroTensor2[0][idim]=make_vectorized_array(0.0);
    zeroTensor2[1][idim]=make_vectorized_array(0.0);
    zeroTensor3[idim]=make_vectorized_array(0.0);
  }

  for (unsigned int cell=0; cell<matrix_free_data.n_macro_cells(); ++cell){
    forceEval.reinit(cell);
    phiTotEval.reinit(cell);
    psiEval.reinit(cell);    
    phiTotEval.read_dof_values_plain(dftPtr->poissonPtr->phiTotRhoOut);//read without taking constraints into account
    phiTotEval.evaluate(true,true);


    std::vector<VectorizedArray<double> > rhoQuads(numQuadPoints,make_vectorized_array(0.0));
    std::vector<Tensor<1,C_DIM,VectorizedArray<double> > > gradRhoQuads(numQuadPoints,zeroTensor3);
    std::vector<VectorizedArray<double> > excQuads(numQuadPoints,make_vectorized_array(0.0));
    std::vector<Tensor<1,C_DIM,VectorizedArray<double> > > gradRhoExcQuads(numQuadPoints,zeroTensor3);

    const unsigned int numSubCells=matrix_free_data.n_components_filled(cell);
    
    for (unsigned int iSubCell=0; iSubCell<numSubCells; ++iSubCell){
       subCellPtr= matrix_free_data.get_cell_iterator(cell,iSubCell);
       dealii::CellId subCellId=subCellPtr->id();
       std::vector<double> exchValQuads(numQuadPoints);
       std::vector<double> corrValQuads(numQuadPoints); 
       if(dftPtr->d_xc_id == 4){
           pcout<< " GGA force computation not implemented yet"<<std::endl;
	   exit(-1);
       }
       else{
         xc_lda_exc(&(dftPtr->funcX),numQuadPoints,&((**rhoOutValues)[subCellId][0]),&exchValQuads[0]);
         xc_lda_exc(&(dftPtr->funcC),numQuadPoints,&((**rhoOutValues)[subCellId][0]),&corrValQuads[0]);     
         for (unsigned int q=0; q<numQuadPoints; ++q){
           rhoQuads[q][iSubCell]=(**rhoOutValues)[subCellId][q];
	   //gradRhoQuads[q][0][i]=(**gradRhoOutValues)[subCellId][3*q];
	   //gradRhoQuads[q][1][i]=(**gradRhoOutValues)[subCellId][3*q+1];
           //gradRhoQuads[q][2][i]=(**gradRhoOutValues)[subCellId][3*q+2];
	   excQuads[q][iSubCell]=exchValQuads[q]+corrValQuads[q];
         }
       }
    }   
    
    std::vector<Tensor<1,2,VectorizedArray<double> > > psiQuads(numQuadPoints*numEigenVectors*numKPoints,zeroTensor1);
    std::vector<Tensor<1,2,Tensor<1,C_DIM,VectorizedArray<double> > > > gradPsiQuads(numQuadPoints*numEigenVectors*numKPoints,zeroTensor2);
   
   for (unsigned int ikPoint=0; ikPoint<numKPoints; ++ikPoint){ 
    for (unsigned int iEigenVec=0; iEigenVec<numEigenVectors; ++iEigenVec){
      //psiEval.reinit(cell);	    
      psiEval.read_dof_values_plain(*((dftPtr->eigenVectorsOrig)[ikPoint][iEigenVec]));//read without taking constraints into account
      psiEval.evaluate(true,true);
      for (unsigned int q=0; q<numQuadPoints; ++q){
        psiQuads[q*numEigenVectors*numKPoints+numEigenVectors*ikPoint+iEigenVec]=psiEval.get_value(q);   
        gradPsiQuads[q*numEigenVectors*numKPoints+numEigenVectors*ikPoint+iEigenVec]=psiEval.get_gradient(q);	 
      }     
    } 
   }
    

    for (unsigned int q=0; q<numQuadPoints; ++q){
       VectorizedArray<double> phiTot_q =phiTotEval.get_value(q);   
       Tensor<1,C_DIM,VectorizedArray<double> > gradPhiTot_q =phiTotEval.get_gradient(q);

       forceEval.submit_gradient(eshelbyTensor::getELocEshelbyTensorPeriodic(phiTot_q,
			                                      gradPhiTot_q,
						              rhoQuads[q],
						              gradRhoQuads[q],
						              excQuads[q],
						              gradRhoExcQuads[q],
						              psiQuads.begin()+q*numEigenVectors*numKPoints,
						              gradPsiQuads.begin()+q*numEigenVectors*numKPoints,
							      dftPtr->d_kPointCoordinates,
							      dftPtr->d_kPointWeights,
							      dftPtr->eigenValues,
							      dftPtr->fermiEnergy,
							      dftPtr->d_TVal),
		                                              q);
    }
    forceEval.integrate (false,true);
    forceEval.distribute_local_to_global(d_configForceVectorLinFE);//also takes care of constraints

  }

}

//compute configurational force contribution from nuclear self energy on the mesh nodes using linear shape function generators
template<unsigned int FEOrder>
void forceClass<FEOrder>::computeConfigurationalForceEselfLinFE()
{
  const std::vector<std::vector<double> > & atomLocations=dftPtr->atomLocations;
  const std::vector<std::vector<double> > & imagePositions=dftPtr->d_imagePositions;
  const std::vector<double> & imageCharges=dftPtr->d_imageCharges;
  //configurational force contribution from the volume integral
  QGauss<C_DIM>  quadrature(C_num1DQuad<FEOrder>());
  FEValues<C_DIM> feForceValues (FEForce, quadrature, update_gradients | update_JxW_values);
  FEValues<C_DIM> feVselfValues (dftPtr->FE, quadrature, update_gradients);
  const unsigned int   forceDofsPerCell = FEForce.dofs_per_cell;
  const unsigned int   forceBaseIndicesPerCell = forceDofsPerCell/FEForce.components;
  Vector<double>       elementalForce (forceDofsPerCell);
  const unsigned int   numQuadPoints = quadrature.size();
  std::vector<types::global_dof_index> forceLocalDofIndices(forceDofsPerCell);
  const int numberBins=dftPtr->d_bins.size();
  std::vector<Tensor<1,C_DIM,double> > gradVselfQuad(numQuadPoints);
  std::vector<int> baseIndexDofsVec(forceBaseIndicesPerCell*C_DIM);
  Tensor<1,C_DIM,double> baseIndexForceVec;

  for (unsigned int ibase=0; ibase<forceBaseIndicesPerCell; ++ibase)
  {
    for (unsigned int idim=0; idim<C_DIM; idim++)
       baseIndexDofsVec[C_DIM*ibase+idim]=FEForce.component_to_system_index(idim,ibase);
  }

  for(int iBin = 0; iBin < numberBins; ++iBin)
  {
    const std::vector<DoFHandler<C_DIM>::active_cell_iterator> & cellsVselfBallDofHandler=d_cellsVselfBallsDofHandler[iBin];	   
    const std::vector<DoFHandler<C_DIM>::active_cell_iterator> & cellsVselfBallDofHandlerForce=d_cellsVselfBallsDofHandlerForce[iBin]; 
    const vectorType & iBinVselfField= dftPtr->d_vselfFieldBins[iBin];
    std::vector<DoFHandler<C_DIM>::active_cell_iterator>::const_iterator iter1;
    std::vector<DoFHandler<C_DIM>::active_cell_iterator>::const_iterator iter2;
    iter2 = cellsVselfBallDofHandlerForce.begin();
    for (iter1 = cellsVselfBallDofHandler.begin(); iter1 != cellsVselfBallDofHandler.end(); ++iter1)
    {
	DoFHandler<C_DIM>::active_cell_iterator cell=*iter1;
	DoFHandler<C_DIM>::active_cell_iterator cellForce=*iter2;
	feVselfValues.reinit(cell);
	feVselfValues.get_function_gradients(iBinVselfField,gradVselfQuad);

	feForceValues.reinit(cellForce);
	cellForce->get_dof_indices(forceLocalDofIndices);
	elementalForce=0.0;
	for (unsigned int ibase=0; ibase<forceBaseIndicesPerCell; ++ibase)
	{
           baseIndexForceVec=0;		
	   for (unsigned int qPoint=0; qPoint<numQuadPoints; ++qPoint)
	   { 
	     baseIndexForceVec+=eshelbyTensor::getVselfBallEshelbyTensor(gradVselfQuad[qPoint])*feForceValues.shape_grad(baseIndexDofsVec[C_DIM*ibase],qPoint)*feForceValues.JxW(qPoint);
	   }//q point loop
	   for (unsigned int idim=0; idim<C_DIM; idim++)
	      elementalForce[baseIndexDofsVec[C_DIM*ibase+idim]]=baseIndexForceVec[idim];
	}//base index loop

	d_constraintsNoneForce.distribute_local_to_global(elementalForce,forceLocalDofIndices,d_configForceVectorLinFE);
        ++iter2;
     }//cell loop 
  }//bin loop


  //configurational force contribution from the surface integral
  
  QGauss<C_DIM-1>  faceQuadrature(C_num1DQuad<FEOrder>());
  FEFaceValues<C_DIM> feForceFaceValues (FEForce, faceQuadrature, update_values | update_JxW_values | update_normal_vectors | update_quadrature_points);
  //FEFaceValues<C_DIM> feVselfFaceValues (FE, faceQuadrature, update_gradients);
  const unsigned int faces_per_cell=GeometryInfo<C_DIM>::faces_per_cell;
  const unsigned int   numFaceQuadPoints = faceQuadrature.size();
  const unsigned int   forceDofsPerFace = FEForce.dofs_per_face;
  const unsigned int   forceBaseIndicesPerFace = forceDofsPerFace/FEForce.components;
  Vector<double>       elementalFaceForce(forceDofsPerFace);
  std::vector<types::global_dof_index> forceFaceLocalDofIndices(forceDofsPerFace);
  std::vector<types::global_dof_index> vselfLocalDofIndices(dftPtr->FE.dofs_per_cell);
  std::vector<unsigned int> baseIndexFaceDofsForceVec(forceBaseIndicesPerFace*C_DIM);
  Tensor<1,C_DIM,double> baseIndexFaceForceVec;
  const int numberGlobalAtoms = atomLocations.size();
	   
  for (unsigned int iFaceDof=0; iFaceDof<forceDofsPerFace; ++iFaceDof)
  {
     std::pair<unsigned int, unsigned int> baseComponentIndexPair=FEForce.face_system_to_component_index(iFaceDof); 
     baseIndexFaceDofsForceVec[C_DIM*baseComponentIndexPair.second+baseComponentIndexPair.first]=iFaceDof;
  }
  for(int iBin = 0; iBin < numberBins; ++iBin)
  {
    std::map<dealii::types::global_dof_index, int> & closestAtomBinMap = dftPtr->d_closestAtomBin[iBin];
    const std::map<DoFHandler<C_DIM>::active_cell_iterator,std::vector<unsigned int > >  & cellsVselfBallSurfacesDofHandler=d_cellFacesVselfBallSurfacesDofHandler[iBin];	   
    const std::map<DoFHandler<C_DIM>::active_cell_iterator,std::vector<unsigned int > >  & cellsVselfBallSurfacesDofHandlerForce=d_cellFacesVselfBallSurfacesDofHandlerForce[iBin]; 
    const vectorType & iBinVselfField= dftPtr->d_vselfFieldBins[iBin];
    std::map<DoFHandler<C_DIM>::active_cell_iterator,std::vector<unsigned int > >::const_iterator iter1;
    std::map<DoFHandler<C_DIM>::active_cell_iterator,std::vector<unsigned int > >::const_iterator iter2;
    iter2 = cellsVselfBallSurfacesDofHandlerForce.begin();
    for (iter1 = cellsVselfBallSurfacesDofHandler.begin(); iter1 != cellsVselfBallSurfacesDofHandler.end(); ++iter1)
    {
	DoFHandler<C_DIM>::active_cell_iterator cell=iter1->first;
	cell->get_dof_indices(vselfLocalDofIndices);
        const int closestAtomId=closestAtomBinMap[vselfLocalDofIndices[0]];//is same for all faces in the cell
        double closestAtomCharge;
	Point<C_DIM> closestAtomLocation;
	if(closestAtomId < numberGlobalAtoms)
	{
           closestAtomLocation[0]=atomLocations[closestAtomId][2];
	   closestAtomLocation[1]=atomLocations[closestAtomId][3];
	   closestAtomLocation[2]=atomLocations[closestAtomId][4];
	   if(dftPtr->d_isPseudopotential)
	      closestAtomCharge = atomLocations[closestAtomId][1];
           else
	      closestAtomCharge = atomLocations[closestAtomId][0];
        }
	else{
           const int imageId=closestAtomId-numberGlobalAtoms;
	   closestAtomCharge = imageCharges[imageId];
           closestAtomLocation[0]=imagePositions[imageId][0];
	   closestAtomLocation[1]=imagePositions[imageId][1];
	   closestAtomLocation[2]=imagePositions[imageId][2];
        }

	DoFHandler<C_DIM>::active_cell_iterator cellForce=iter2->first;

	const std::vector<unsigned int > & dirichletFaceIds= iter1->second;
	for (unsigned int index=0; index< dirichletFaceIds.size(); index++){
           const int faceId=dirichletFaceIds[index];
	   //feVselfFaceValues.reinit(cell,faceId);
	   //std::vector<Tensor<1,C_DIM,double> > gradVselfFaceQuad(numFaceQuadPoints);
	   //feVselfFaceValues.get_function_gradients(iBinVselfField,gradVselfFaceQuad);
            
	   feForceFaceValues.reinit(cellForce,faceId);
	   cellForce->face(faceId)->get_dof_indices(forceFaceLocalDofIndices);
	   elementalFaceForce=0;

	   for (unsigned int ibase=0; ibase<forceBaseIndicesPerFace; ++ibase){
             baseIndexFaceForceVec=0;
	     //const int a=forceFaceLocalDofIndices[baseIndexFaceDofsForceVec[C_DIM*ibase]];
	     //Point<C_DIM> faceBaseDofPos=d_supportPointsForce[forceFaceLocalDofIndices[baseIndexFaceDofsForceVec[C_DIM*ibase]]];
	     for (unsigned int qPoint=0; qPoint<numFaceQuadPoints; ++qPoint)
	     {  
	       Point<C_DIM> quadPoint=feForceFaceValues.quadrature_point(qPoint);
	       Tensor<1,C_DIM,double> dispClosestAtom=quadPoint-closestAtomLocation;
	       const double dist=dispClosestAtom.norm();
	       Tensor<1,C_DIM,double> gradVselfFaceQuadExact=-closestAtomCharge*dispClosestAtom/dist/dist/dist;

	       /*
	       Point<C_DIM> debugPoint1,debugPoint2; debugPoint1[0]=-4;debugPoint1[1]=-4;debugPoint1[2]=4;
	       debugPoint2=debugPoint1; debugPoint2[0]=-debugPoint2[0];
	       if (faceBaseDofPos.distance(debugPoint1)<1e-5 || faceBaseDofPos.distance(debugPoint2)<1e-5){
		 const int cellDofIndex=FEForce.face_to_cell_index(baseIndexFaceDofsForceVec[C_DIM*ibase],faceId,cellForce->face_orientation(faceId),cellForce->face_flip(faceId),cellForce->face_rotation(faceId));
		 const int b=forceLocalDofIndices[cellDofIndex];
	         std::cout<< "faceId "<< faceId <<" , " <<gradVselfFaceQuadExact<< " shapeval: "<< feForceFaceValues.shape_value(cellDofIndex,qPoint) << "a: "<<a<<" b: "<< b<< " cellDofIndex: "<< cellDofIndex << " center: "<< cellForce->center() << std::endl;
	       }
               */
             
	       baseIndexFaceForceVec-=eshelbyTensor::getVselfBallEshelbyTensor(gradVselfFaceQuadExact)*feForceFaceValues.normal_vector(qPoint)*feForceFaceValues.JxW(qPoint)*feForceFaceValues.shape_value(FEForce.face_to_cell_index(baseIndexFaceDofsForceVec[C_DIM*ibase],faceId,cellForce->face_orientation(faceId),cellForce->face_flip(faceId),cellForce->face_rotation(faceId)),qPoint);
	       
	     }//q point loop
	     for (unsigned int idim=0; idim<C_DIM; idim++){
	       elementalFaceForce[baseIndexFaceDofsForceVec[C_DIM*ibase+idim]]=baseIndexFaceForceVec[idim];
	     }
	   }//base index loop
	   d_constraintsNoneForce.distribute_local_to_global(elementalFaceForce,forceFaceLocalDofIndices,d_configForceVectorLinFE);
	}//face loop
        ++iter2;
     }//cell loop 
  }//bin loop 
}



//compute configurational force on the mesh nodes using linear shape function generators
template<unsigned int FEOrder>
void forceClass<FEOrder>::computeConfigurationalForcePhiExtLinFE()
{
  
  FEEvaluation<C_DIM,1,C_num1DQuad<FEOrder>(),C_DIM>  forceEval(dftPtr->matrix_free_data,d_forceDofHandlerIndex, 0);

  FEEvaluation<C_DIM,FEOrder,C_num1DQuad<FEOrder>(),1> eshelbyEval(dftPtr->matrix_free_data,dftPtr->phiExtDofHandlerIndex, 0);//no constraints
   
  
  for (unsigned int cell=0; cell<dftPtr->matrix_free_data.n_macro_cells(); ++cell){
    forceEval.reinit(cell);
    eshelbyEval.reinit(cell);
    eshelbyEval.read_dof_values_plain(dftPtr->poissonPtr->phiExt);
    eshelbyEval.evaluate(true,true);
    for (unsigned int q=0; q<forceEval.n_q_points; ++q){
	 VectorizedArray<double> phiExt_q =eshelbyEval.get_value(q);   
	 Tensor<1,C_DIM,VectorizedArray<double> > gradPhiExt_q =eshelbyEval.get_gradient(q);
	 forceEval.submit_gradient(eshelbyTensor::getPhiExtEshelbyTensor(phiExt_q,gradPhiExt_q),q);
    }
    forceEval.integrate (false,true);
    forceEval.distribute_local_to_global(d_configForceVectorLinFE);//also takes care of constraints

  }
} 

template<unsigned int FEOrder>
void forceClass<FEOrder>::computeConfigurationalForceEselfNoSurfaceLinFE()
{
  FEEvaluation<C_DIM,1,C_num1DQuad<FEOrder>(),C_DIM>  forceEval(dftPtr->matrix_free_data,d_forceDofHandlerIndex, 0);

  FEEvaluation<C_DIM,FEOrder,C_num1DQuad<FEOrder>(),1> eshelbyEval(dftPtr->matrix_free_data,dftPtr->phiExtDofHandlerIndex, 0);//no constraints
   
  for (unsigned int iBin=0; iBin< dftPtr->d_vselfFieldBins.size() ; iBin++){
    for (unsigned int cell=0; cell<dftPtr->matrix_free_data.n_macro_cells(); ++cell){
      forceEval.reinit(cell);
      eshelbyEval.reinit(cell);
      eshelbyEval.read_dof_values_plain(dftPtr->d_vselfFieldBins[iBin]);
      eshelbyEval.evaluate(false,true);
      for (unsigned int q=0; q<forceEval.n_q_points; ++q){
	  
	  Tensor<1,C_DIM,VectorizedArray<double> > gradVself_q =eshelbyEval.get_gradient(q);

	  forceEval.submit_gradient(eshelbyTensor::getVselfBallEshelbyTensor(gradVself_q),q);
 
      }
      forceEval.integrate (false,true);
      forceEval.distribute_local_to_global (d_configForceVectorLinFE);
    }
  }
  
   
}
