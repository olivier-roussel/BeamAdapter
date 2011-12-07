/******************************************************************************
 *       SOFA, Simulation Open-Framework Architecture, version 1.0 beta 4      *
 *                (c) 2006-2009 MGH, INRIA, USTL, UJF, CNRS                    *
 *                                                                             *
 * This library is free software; you can redistribute it and/or modify it     *
 * under the terms of the GNU Lesser General Public License as published by    *
 * the Free Software Foundation; either version 2.1 of the License, or (at     *
 * your option) any later version.                                             *
 *                                                                             *
 * This library is distributed in the hope that it will be useful, but WITHOUT *
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License *
 * for more details.                                                           *
 *                                                                             *
 * You should have received a copy of the GNU Lesser General Public License    *
 * along with this library; if not, write to the Free Software Foundation,     *
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.          *
 *******************************************************************************
 *                               SOFA :: Modules                               *
 *                                                                             *
 * Authors: The SOFA Team and external contributors (see Authors.txt)          *
 *                                                                             *
 * Contact information: contact@sofa-framework.org                             *
 ******************************************************************************/
//
// C++ Implementation : BeamInterpolation / AdaptiveBeamForceFieldAndMass
//
// Description:
//
//
// Author: Christian Duriez, INRIA
//
// Copyright: See COPYING file that comes with this distribution
//
//
#ifndef SOFA_COMPONENT_FEM_BEAMINTERPOLATION_INL
#define SOFA_COMPONENT_FEM_BEAMINTERPOLATION_INL

#include "BeamInterpolation.h"

#include <sofa/core/behavior/ForceField.inl>
#include <sofa/core/topology/BaseMeshTopology.h>
//#include <sofa/component/topology/GridTopology.h>
//#include <sofa/simulation/common/Simulation.h>
#include <sofa/helper/PolarDecompose.h>
//#include <sofa/helper/gl/template.h>
//#include <sofa/helper/gl/Axis.h>
//#include <sofa/helper/rmath.h>
//#include <assert.h>
//#include <iostream>
//#include <set>
//#include <sofa/helper/system/gl.h>

#include <sofa/core/behavior/MechanicalState.h>
#include <sofa/defaulttype/VecTypes.h>
#include <sofa/defaulttype/RigidTypes.h>

//#include <sofa/defaulttype/SolidTypes.inl>
#include <sofa/helper/OptionsGroup.h>

#include <sofa/helper/gl/Cylinder.h>
#include <sofa/simulation/common/Simulation.h>
#include <sofa/helper/gl/Axis.h>
//#include <sofa/simulation/common/Node.h>




namespace sofa
{

namespace component
{

namespace fem
{


//////////// useful tool

template <class DataTypes>
void BeamInterpolation<DataTypes>::RotateFrameForAlignX(const Quat &input, Vec3 &x, Quat &output)
{
    x.normalize();
    Vec3 x0=input.inverseRotate(x);

    Real cTheta=x0[0];
     Real theta;
    if (cTheta>0.9999999999)
    {
        output = input;
    }
    else
    {
        theta=acos(cTheta);
        // axis of rotation
        Vec3 dw(0,-x0[2],x0[1]);
        dw.normalize();

        // computation of the rotation
        Quat inputRoutput;
        inputRoutput.axisToQuat(dw, theta);

        output=input*inputRoutput;
    }


}




/* ************* ADAPTIVE INTERPOLATION ************** */

template <class DataTypes>
 void BeamInterpolation<DataTypes>::init()
{
    this->f_printLog.setValue(true);

    sofa::core::objectmodel::BaseContext* context = this->getContext();

    this->_constantRadius._r = this->radius.getValue();
    this->_constantRadius._rInner = this->innerRadius.getValue();
    double r = this->radius.getValue();
    double rInner = this->innerRadius.getValue();


    this->_constantRadius._Iz = M_PI*(r*r*r*r - rInner*rInner*rInner*rInner)/4.0;

    //_Iz = M_PI*(r*r*r*r)/4.0;
    this->_constantRadius._Iy = this->_constantRadius._Iz ;
    this->_constantRadius._J = this->_constantRadius._Iz + this->_constantRadius._Iy;
    this->_constantRadius._A = M_PI*(r*r - rInner*rInner);

    this->_constantRadius._Asy = 0.0;
    this->_constantRadius._Asz = 0.0;



    // Init Adaptive Topology:
    this->_topology = context->getMeshTopology();


}


template <class DataTypes>
void BeamInterpolation<DataTypes>::bwdInit()
{


    if(!this->verifyTopology())
        serr<<"no topology found"<<sendl;


    sofa::core::objectmodel::BaseContext* context = this->getContext();


    this->Edge_List.clear();

    for (int i=0; i<this->_topology->getNbEdges(); i++)
    {
            this->Edge_List.push_back(i);
    }


    if(!dofsAndBeamsAligned.getValue())
    {
            this->DOF0_Transform_node0.resize( this->Edge_List.size());
            this->DOF1_Transform_node1.resize( this->Edge_List.size());
    }


    this->Length_List.clear();
    for (unsigned int i=0; i<this->Edge_List.size(); i++)
    {

            unsigned int nd0Id, nd1Id;
            this->getNodeIndices(i,nd0Id, nd1Id);
            sofa::core::behavior::MechanicalState<DataTypes>* mstate = dynamic_cast<sofa::core::behavior::MechanicalState<DataTypes>*>(context->getMechanicalState());
            if(mstate==NULL)
            {
                    serr<<" mstate :can not do the cast"<<sendl;
                    return;
            }
            Vec3 beam_segment =(*mstate->getX())[nd1Id].getCenter() - (*mstate->getX())[nd0Id].getCenter();
            this->Length_List.push_back(beam_segment.norm());
    }



    if(!this->verifyTopology())
            serr<<"WARNING bwdInit failed"<<sendl;

    this->_mstate = dynamic_cast< sofa::core::behavior::MechanicalState<DataTypes> *> (this->getContext()->getMechanicalState());
    if(this->_mstate==NULL)
            serr<<"WARNING no MechanicalState found bwdInit failed"<<sendl;


}



// verify that we got a non-null pointer on the topology
// verify that the this->Edge_List do not contain non existing edges

template<class DataTypes>
bool BeamInterpolation<DataTypes>::verifyTopology(){


    if (this->_topology==NULL)
        {
            serr << "ERROR(BeamInterpolation): object must have a BaseMeshTopology (i.e. EdgeSetTopology or MeshTopology)."<<sendl;
        return false;
    }
    else
    {
        if(this->_topology->getNbEdges()==0)
        {
            serr << "ERROR(BeamInterpolation): topology is empty."<<sendl;
            return false;
        }

        this->_topologyEdges = &this->_topology->getEdges();

        for (unsigned int j=0; j<this->Edge_List.size(); j++)
        {
            if(this->Edge_List[j] > this->_topologyEdges->size()){
                 serr<<"WARNING defined this->Edge_List is not compatible with topology"<<sendl;
                 return false;
             }
        }

    }
    return true;

}


template<class DataTypes>
void BeamInterpolation<DataTypes>::clear()
{
    if(this->brokenInTwo)
    {
        this->Edge_List.resize(this->_numBeamsNotUnderControl);
        this->Length_List.resize(this->_numBeamsNotUnderControl);
        this->DOF0_Transform_node0.resize(this->_numBeamsNotUnderControl);
        this->DOF1_Transform_node1.resize(this->_numBeamsNotUnderControl);
    }
    else
    {
        this->Edge_List.clear();
        this->Length_List.clear();
        this->DOF0_Transform_node0.clear();
        this->DOF1_Transform_node1.clear();
    }
}

template<class DataTypes>
void BeamInterpolation<DataTypes>::addBeam(const BaseMeshTopology::EdgeID &eID  , const Real &length, const Real &x0, const Real &x1, const Real &angle)
{
    this->Edge_List.push_back(eID);
    this->Length_List.push_back(length);
    std::pair<Real,Real> x_pair(x0,x1);

    Quat QuatX ;
    QuatX.axisToQuat(Vec3(1,0,0), angle);
    QuatX.normalize();

    // as a angle is set between DOFs and Beam, they are no more aligned
    this->dofsAndBeamsAligned.setValue(false);
    this->DOF0_Transform_node0.push_back(Transform(Vec3(0,0,0), QuatX ));
    this->DOF1_Transform_node1.push_back(Transform(Vec3(0,0,0), QuatX ));
}


template<class DataTypes>
 void BeamInterpolation<DataTypes>::getBeamAtCurvAbs(const Real& x_input, unsigned int &edgeInList_output, Real& baryCoord_output)
 {
     //lTotalRest = total length of the
     Real lTotalRest = this->getRestTotalLength();
     //LTotal =  length sum of the beams that are "out"
     Real LTotal=0.0;
     unsigned int start=0;


     // we find the length of the beam that is "out"
     for (unsigned int e=start; e<this->Edge_List.size(); e++)
     {
         LTotal += this->getLength(e);
     }


     // x_i = abs_curv from the begining of the instrument
     Real  x_i = x_input + LTotal - lTotalRest;

     if( x_i < 0.0)
     {
         edgeInList_output = start;
         baryCoord_output = 0;
         return;
     }

     // we compute the x value of each node :the topology (stored in Edge_list) is supposed to be a regular seq of segment
     Real x = 0;

     for (unsigned int e=start; e<this->Edge_List.size(); e++)
     {
         x += this->getLength(e);
         if(x > x_i)
         {
             edgeInList_output = e;
             Real x0 = x - this->getLength(e);
             baryCoord_output =(x_i-x0) / this->getLength(e);
             return;
         }
     }

     edgeInList_output = this->Edge_List.size()-1;
     baryCoord_output = 1.0;

}


template<class DataTypes>
void BeamInterpolation<DataTypes>::getDOFtoLocalTransform(unsigned int edgeInList,Transform &DOF0_H_local0, Transform &DOF1_H_local1)
{

    if(dofsAndBeamsAligned.getValue())
    {
        DOF0_H_local0.clear();
        DOF1_H_local1.clear();
        return;
    }
    DOF0_H_local0 = this->DOF0_Transform_node0[edgeInList];
    DOF1_H_local1 = this->DOF1_Transform_node1[edgeInList];

}


template<class DataTypes>
void BeamInterpolation<DataTypes>::getDOFtoLocalTransformInGlobalFrame(unsigned int edgeInList, Transform &DOF0Global_H_local0, Transform &DOF1Global_H_local1, const VecCoord &x)
{

    Transform DOF0_H_local0, DOF1_H_local1;
    getDOFtoLocalTransform(edgeInList, DOF0_H_local0, DOF1_H_local1);

    unsigned int node0Idx, node1Idx;
    this->getNodeIndices(edgeInList, node0Idx, node1Idx);

    // Computes the Rotation to global from DOF0 and DOF1
    Transform global_R_DOF0(Vec3(0,0,0), x[node0Idx].getOrientation());
    Transform global_R_DOF1(Vec3(0,0,0), x[node1Idx].getOrientation());

    // - rotation due to the optional transformation
    DOF0Global_H_local0 = global_R_DOF0*DOF0_H_local0;
    DOF1Global_H_local1 = global_R_DOF1*DOF1_H_local1;

}
template<class DataTypes>
void BeamInterpolation<DataTypes>::computeTransform(unsigned int edgeInList,  Transform &global_H0_local,  Transform &global_H1_local,
                                                            Transform& local0_H_local1, Quat& local_R_local0, const VecCoord &x)
{

    //<<"computeTransform for edge"<< edgeInList<<std::endl;

    // 1. Get the indices of element and nodes
    unsigned int node0Idx, node1Idx;
    getNodeIndices( edgeInList,  node0Idx, node1Idx );



    //2. Computes the optional rigid transformation of DOF0_Transform_node0 and DOF1_Transform_node1
    Transform DOF0_H_local0, DOF1_H_local1;
    getDOFtoLocalTransform(edgeInList, DOF0_H_local0,  DOF1_H_local1);


    // 3. Computes the transformation global To local for both nodes

    Transform global_H_DOF0(x[node0Idx].getCenter(), x[node0Idx].getOrientation());
    Transform global_H_DOF1(x[node1Idx].getCenter(), x[node1Idx].getOrientation());
        // - add a optional transformation
    Transform global_H_local0 = global_H_DOF0*DOF0_H_local0;
    Transform global_H_local1 = global_H_DOF1*DOF1_H_local1;





 // 4. Compute the local frame

    // SIMPLIFICATION: local = local0:
    local_R_local0.clear();

    global_H_DOF0.set(Vec3(0,0,0), x[node0Idx].getOrientation());
    global_H_DOF1.set(Vec3(0,0,0), x[node1Idx].getOrientation());
    // - rotation due to the optional transformation
    global_H_local0 = global_H_DOF0*DOF0_H_local0;
    global_H_local1 = global_H_DOF1*DOF1_H_local1;


    global_H0_local = global_H_local0;
    Quat local0_R_local1 = local0_H_local1.getOrientation();
    Transform local0_HR_local1(Vec3(0,0,0), local0_R_local1);

    global_H1_local = global_H_local1 * local0_HR_local1.inversed();

}


template<class DataTypes>
void BeamInterpolation<DataTypes>::computeTransform2(unsigned int edgeInList,  Transform &global_H_local0,  Transform &global_H_local1, const VecCoord &x)
{
    // 1. Get the indices of element and nodes
    unsigned int node0Idx, node1Idx;
    getNodeIndices( edgeInList,  node0Idx, node1Idx );



    //2. Computes the optional rigid transformation of DOF0_Transform_node0 and DOF1_Transform_node1
    Transform DOF0_H_local0, DOF1_H_local1;
    getDOFtoLocalTransform(edgeInList, DOF0_H_local0,  DOF1_H_local1);


    // 3. Computes the transformation global To local for both nodes
    Transform global_H_DOF0(x[node0Idx].getCenter(),x[node0Idx].getOrientation());
    Transform global_H_DOF1(x[node1Idx].getCenter(),x[node1Idx].getOrientation());
        // - add a optional transformation
     global_H_local0 = global_H_DOF0*DOF0_H_local0;
     global_H_local1 = global_H_DOF1*DOF1_H_local1;
}

template<class DataTypes>
void BeamInterpolation<DataTypes>::getRestTransform(unsigned int edgeInList, Transform &local0_H_local1_rest)
{
    // the beam is straight: the transformation between local0 and local1 is provided by the length of the beam
    local0_H_local1_rest.set(Vec3(this->Length_List[edgeInList],0,0), Quat());
}

template<class DataTypes>
void BeamInterpolation<DataTypes>::getSplineRestTransform(unsigned int edgeInList, Transform &local_H_local0_rest, Transform &local_H_local1_rest)
{
	// the beam is straight: local is in the middle of local0 and local1
	// the transformation between local0 and local1 is provided by the length of the beam
	local_H_local0_rest.set(-Vec3(this->Length_List[edgeInList]/2,0,0), Quat());
	local_H_local1_rest.set(Vec3(this->Length_List[edgeInList]/2,0,0), Quat());
}


template<class DataTypes>
void BeamInterpolation<DataTypes>::getNodeIndices(unsigned int edgeInList, unsigned int &node0Idx, unsigned int &node1Idx )
{

    if ( this->_topologyEdges==NULL)
    {
        serr<<" in  getNodeIndices no _topologyEdges defined"<<sendl;
    }

    // 1. Get the indices of element and nodes
    ElementID e = this->Edge_List[edgeInList] ;
    core::topology::BaseMeshTopology::Edge edge=  (*this->_topologyEdges)[e];
    node0Idx = edge[0];
    node1Idx = edge[1];
}

template<class DataTypes>
void BeamInterpolation<DataTypes>::getInterpolationParam(unsigned int edgeInList, Real &_L, Real &_A, Real &_Iy ,
                                                                 Real &_Iz, Real &_Asy, Real &_Asz, Real &_J)
{
    // get the length of the beam:
    _L = this->Length_List[edgeInList];

    BeamSection bS = this->getBeamSection(edgeInList);
    _A=bS._A;
    _Iy=bS._Iy;
    _Iz=bS._Iz;
    _Asy=bS._Asy;
    _Asz=bS._Asz;
    _J=bS._J;
}


template<class DataTypes>
void BeamInterpolation<DataTypes>::getSplinePoints(unsigned int edgeInList, const VecCoord &x, Vec3& P0, Vec3& P1, Vec3& P2, Vec3 &P3)
{
    Transform global_H_local0, global_H_local1;
    computeTransform2(edgeInList,  global_H_local0,  global_H_local1, x);

   // << " getSplinePoints  : global_H_local0 ="<<global_H_local0<<"    global_H_local1 ="<<global_H_local1<<std::endl;
    Real _L = this->Length_List[edgeInList];

    P0=global_H_local0.getOrigin();
    P3=global_H_local1.getOrigin();

    P1= P0 + global_H_local0.getOrientation().rotate(Vec3(1.0,0,0))*(_L/3.0);
    P2= P3 + global_H_local1.getOrientation().rotate(Vec3(-1,0,0))*(_L/3.0);

}


template<class DataTypes>
void BeamInterpolation<DataTypes>::computeStrechAndTwist(unsigned int edgeInList, const VecCoord &x, Vec3 &ResultNodeO, Vec3 &ResultNode1)
{

    // ResultNode = [half length of the beam (m), geometrical Twist angle (rad), additional mechanical Twist angle (rad)]

    //spline:
    Vec3 P0, P1, P2, P3;
    this->getSplinePoints(edgeInList, x, P0, P1, P2, P3);
    ///////// TODO :
    unsigned int node0Idx, node1Idx;
    this->getNodeIndices(edgeInList,node0Idx,node1Idx);

    Real length0, length1;
    length0=0.0;
    length1=0.0;

    Vec3 seg;
    Vec3 pos = x[node0Idx].getCenter();
    Vec3 n_x, n_y, n_z, x_b, y_b, z_b;
    Quat R0 =  x[node0Idx].getOrientation();
    R0.normalize();
    n_x = R0.rotate(Vec3(1.0,0.0,0.0));
    n_y = R0.rotate(Vec3(0.0,1.0,0.0));
    n_z = R0.rotate(Vec3(0.0,0.0,1.0));


    for (Real bx=0.02; bx<1.00001; bx+=0.02)
    {
        // compute length
        seg  = -pos;
        pos = P0*(1-bx)*(1-bx)*(1-bx) + P1*3*bx*(1-bx)*(1-bx) + P2*3*bx*bx*(1-bx) + P3*bx*bx*bx;
        seg += pos;
        if(bx<0.50001)
            length0 += seg.norm();
        else
            length1 += seg.norm();

        // compute frame => Kenneth method
        n_x =  P0*(-3*(1-bx)*(1-bx)) + P1*(3-12*bx+9*bx*bx) + P2*(6*bx-9*bx*bx) + P3*(3*bx*bx);
        n_x.normalize();
        n_z = n_x.cross(n_y);
        n_z.normalize();
        n_y = n_z.cross(n_x);
        n_y.normalize();

        if(bx>0.49999 && bx < 0.50001)
        {
            //bx == 0.5 => frame of the beam (without mechanical twist)
            x_b = n_x;
            y_b = n_y;
            z_b = n_z;
        }
    }

    // computation of twist angle:
    Quat globalRgeom1;
    globalRgeom1 = globalRgeom1.createQuaterFromFrame(n_x,n_y,n_z);
    Vec3 y_geom1 = globalRgeom1.rotate(Vec3(0.0,1.0,0.0));
    Vec3 z_geom1 = globalRgeom1.rotate(Vec3(0.0,0.0,1.0));

    Vec3 x_1, y_1;
    Quat R1 =  x[node1Idx].getOrientation();
    R1.normalize();
    x_1 = R1.rotate(Vec3(1.0,0.0,0.0));
    y_1 = R1.rotate(Vec3(0.0,1.0,0.0));


    //<<" Test : x_1 = "<<x_1<< "  should be equal to ="<< globalRgeom1.rotate(Vec3(1.0,0.0,0.0))<<std::endl;
    Real cosTheta= y_1 * y_geom1;
    Real theta;
    if(cosTheta > 1.0 )
        theta=0.0;
    else
    {
        if (y_1*z_geom1 < 0.0)
            theta = -acos(cosTheta);
        else
            theta= acos(cosTheta);
    }

    ResultNodeO[0]=length0;    ResultNodeO[1]=0.0;  ResultNodeO[2]= theta/2.0;
    ResultNode1[0]=length1;    ResultNode1[1]=0.0;  ResultNode1[2]= theta/2.0;

    //<<" length 0 ="<<length0<<"  length1 ="<<length1 <<"   total length ="<<this->getLength(edgeInList)<<std::endl;

}


template<class DataTypes>
void BeamInterpolation<DataTypes>::interpolatePointUsingSpline(unsigned int edgeInList, const Real& baryCoord, const Vec3& localPos, const VecCoord &x, Vec3& posResult)
{

   // <<" interpolatePointUsingSpline : "<< edgeInList<<"  xbary="<<baryCoord<<"  localPos"<<localPos<<std::endl;
    Real _L = this->Length_List[edgeInList];

    if(localPos.norm() >_L*1e-10)
    {
        Vec3 x_local(0,0,0);
        Transform global_H_localInterpol;

        this->InterpolateTransformUsingSpline(edgeInList, baryCoord, x_local, x, global_H_localInterpol );

        posResult = global_H_localInterpol.getOrigin() + global_H_localInterpol.getOrientation().rotate(localPos);

    }
    else
    {
        // TODO => remove call to computeTransform2 => make something faster !
        Transform global_H_local0, global_H_local1;
        computeTransform2(edgeInList,  global_H_local0,  global_H_local1, x);
        Vec3 DP=global_H_local0.getOrigin() - global_H_local1.getOrigin();

        if( DP.norm()< _L*0.01 )
        {
        	posResult = global_H_local0.getOrigin();
        	return;
        }


        Vec3 P0,P1,P2,P3;

        P0=global_H_local0.getOrigin();
        P3=global_H_local1.getOrigin();

        P1= P0 + global_H_local0.getOrientation().rotate(Vec3(1.0,0,0))*(_L/3.0);
        P2= P3 + global_H_local1.getOrientation().rotate(Vec3(-1,0,0))*(_L/3.0);

        Real bx=baryCoord;

        posResult = P0*(1-bx)*(1-bx)*(1-bx) + P1*3*bx*(1-bx)*(1-bx) + P2*3*bx*bx*(1-bx) + P3*bx*bx*bx;
    }

}



template<class DataTypes>
void BeamInterpolation<DataTypes>::InterpolateTransformUsingSpline(Transform& global_H_localResult,
                                                                           const Real &baryCoord,
                                                                           const Transform &global_H_local0,
                                                                           const Transform &global_H_local1,
                                                                           const Real &L)
{


    Vec3 P0,P1,P2,P3,dP01, dP12;

    P0=global_H_local0.getOrigin();
    P3=global_H_local1.getOrigin();
    P1= P0 + global_H_local0.getOrientation().rotate(Vec3(1.0,0,0))*(L/3.0);
    P2= P3 + global_H_local1.getOrientation().rotate(Vec3(-1,0,0))*(L/3.0);



    Real bx=baryCoord;
    Vec3 posResult;
    Quat quatResult;

    dP01 = P1-P0;
    dP12 = P2-P1;

    if(dP01*dP12<0.0)
    {
        quatResult.slerp(global_H_local0.getOrientation(),global_H_local1.getOrientation(),bx,true);
        posResult = P0 *(1-bx) + P3*bx;
    }
    else
    {

         posResult = P0*(1-bx)*(1-bx)*(1-bx) + P1*3*bx*(1-bx)*(1-bx) + P2*3*bx*bx*(1-bx) + P3*bx*bx*bx;

        Vec3 n_x =  P0*(-3*(1-bx)*(1-bx)) + P1*(3-12*bx+9*bx*bx) + P2*(6*bx-9*bx*bx) + P3*(3*bx*bx);

        Quat R0, R1, Rslerp;

        this->RotateFrameForAlignX(global_H_local0.getOrientation(), n_x, R0);
        this->RotateFrameForAlignX(global_H_local1.getOrientation(), n_x, R1);

        Rslerp.slerp(R0,R1,bx,true);
        Rslerp.normalize();
        this->RotateFrameForAlignX(Rslerp, n_x,quatResult);

    }


    global_H_localResult.set(posResult, quatResult);


}


template<class DataTypes>
void BeamInterpolation<DataTypes>::getTangent(Vec3& t, const Real& baryCoord, const Transform &global_H_local0, const Transform &global_H_local1,const Real &L)
{

        Vec3 P0,P1,P2,P3, t0, t1;

        P0=global_H_local0.getOrigin();
        P3=global_H_local1.getOrigin();
        P1= P0 + global_H_local0.getOrientation().rotate(Vec3(1.0,0,0))*(L/3.0);
        P2= P3 + global_H_local1.getOrientation().rotate(Vec3(-1,0,0))*(L/3.0);

        t =  P0*(-3*(1-baryCoord)*(1-baryCoord)) + P1*(3-12*baryCoord+9*baryCoord*baryCoord) + P2*(6*baryCoord-9*baryCoord*baryCoord) + P3*(3*baryCoord*baryCoord);

        t.normalize();

}


template<class DataTypes>
void BeamInterpolation<DataTypes>::ComputeTotalBendingRotationAngle(Real& BendingAngle, const Real& dx_computation, const Transform &global_H_local0, const Transform &global_H_local1,const Real &L,
                                                                    const Real& baryCoordMin, const Real& baryCoordMax)
{

        Vec3 P0,P1,P2,P3, t0, t1;

        P0=global_H_local0.getOrigin();
        P3=global_H_local1.getOrigin();
        P1= P0 + global_H_local0.getOrientation().rotate(Vec3(1.0,0,0))*(L/3.0);
        P2= P3 + global_H_local1.getOrientation().rotate(Vec3(-1,0,0))*(L/3.0);


        t0= P0*(-3*(1-baryCoordMin)*(1-baryCoordMin)) + P1*(3-12*baryCoordMin+9*baryCoordMin*baryCoordMin) + P2*(6*baryCoordMin-9*baryCoordMin*baryCoordMin) + P3*(3*baryCoordMin*baryCoordMin);
        t0.normalize();

        BendingAngle=0.0;

        unsigned int numComputationPoints = (unsigned int) ceil((baryCoordMax-baryCoordMin)*(L/dx_computation));

        for (unsigned int i=0; i<numComputationPoints; i++)
        {
            Real bx= ((Real)((i+1)/numComputationPoints))*(baryCoordMax-baryCoordMin) + baryCoordMin;
            t1 =  P0*(-3*(1-bx)*(1-bx)) + P1*(3-12*bx+9*bx*bx) + P2*(6*bx-9*bx*bx) + P3*(3*bx*bx);
            t1.normalize();

            if(dot(t0,t1)<1.0)
                BendingAngle += acos(dot(t0,t1));

            t0=t1;
        }

}

/*

template<class DataTypes>
void BeamInterpolation<DataTypes>::BaryCoordForRotationAngle(Real& baryCoordMax, const Real& BendingAngle, const Real& dx_computation, const Transform &global_H_local0, const Transform &global_H_local1,const Real &L,
                                                                    const Real& baryCoordMin)
{

        Vec3 P0,P1,P2,P3, t0, t1;

        P0=global_H_local0.getOrigin();
        P3=global_H_local1.getOrigin();
        P1= P0 + global_H_local0.getOrientation().rotate(Vec3(1.0,0,0))*(L/3.0);
        P2= P3 + global_H_local1.getOrientation().rotate(Vec3(-1,0,0))*(L/3.0);



        t0= P0*(-3*(1-baryCoordMin)*(1-baryCoordMin)) + P1*(3-12*baryCoordMin+9*baryCoordMin*baryCoordMin) + P2*(6*baryCoordMin-9*baryCoordMin*baryCoordMin) + P3*(3*baryCoordMin*baryCoordMin);

        Real CummulativeBendingAngle=0.0;

        unsigned int numComputationPoints = (unsigned int) ceil((1.0-baryCoordMin)*(L/dx_computation));


        for (unsigned int i=0; i<numComputationPoints; i++)
        {
            Real bx= ((Real)((i+1)/numComputationPoints))*(1.0-baryCoordMin) + baryCoordMin;
            t1 =  P0*(-3*(1-bx)*(1-bx)) + P1*(3-12*bx+9*bx*bx) + P2*(6*bx-9*bx*bx) + P3*(3*bx*bx);
            t1.normalize();

            if(dot(t0,t1)<1.0)
                CummulativeBendingAngle += acos(dot(t0,t1));

            if(CummulativeBendingAngle>BendingAngle)
            {

                // for more precision, we can perform a linear interpolation...
                // ya= angle_at_i-1 = CummulativeBendingAngle-acos(dot(t0,t1)); xa= = bx-(1/ numComputationPoints)*(1.0-baryCoordMin);
                // yb= angle_at_i = CummulativeBendingAngle; xb=bx;

                // linear interpolation: y=ax+b
                // a=(yb-ya)/(xb-xa);  = acos(dot(t0,t1))/  ((1/ numComputationPoints)*(1.0-baryCoordMin))
                // b = yb - axb = CummulativeBendingAngle - a*bx;

                Real a = acos(dot(t0,t1))/  ((1.0/ numComputationPoints)*(1.0-baryCoordMin));
                Real b=CummulativeBendingAngle - a*bx;

                baryCoordMax= (BendingAngle - b)/a;

                return;

            }

            t0=t1;
        }

        baryCoordMax=1.0;


}
*/


template<class DataTypes>
void BeamInterpolation<DataTypes>::InterpolateTransformUsingSpline(unsigned int edgeInList, const Real& baryCoord, const Vec3& localPos, const VecCoord &x, Transform &global_H_localInterpol)
{
    Transform global_H_local0, global_H_local1;
    computeTransform2(edgeInList,  global_H_local0,  global_H_local1, x);

    Real _L = this->Length_List[edgeInList];

    if(localPos.norm() >1e-10*_L)
        serr<<"WARNING in InterpolateTransformUsingSpline interpolate frame only on the central curve of the beam"<<sendl;

    InterpolateTransformUsingSpline(global_H_localInterpol, baryCoord, global_H_local0, global_H_local1, _L);
}



template<class DataTypes>
void BeamInterpolation<DataTypes>::InterpolateTransformAndVelUsingSpline(unsigned int edgeInList, const Real& baryCoord, const Vec3& localPos, const VecCoord &x, const VecDeriv &v,
                                                   Transform &global_H_localInterpol, Deriv &v_interpol)
{

    // 1. Get the indices of element and nodes
    unsigned int node0Idx, node1Idx;
    getNodeIndices( edgeInList,  node0Idx, node1Idx );

    //2. Computes the optional rigid transformation of DOF0_Transform_node0 and DOF1_Transform_node1
    Transform DOF0_H_local0, DOF1_H_local1;
    getDOFtoLocalTransform(edgeInList, DOF0_H_local0,  DOF1_H_local1);


    // 3. Computes the transformation global To local for both nodes
    Transform global_H_DOF0(x[node0Idx].getCenter(),x[node0Idx].getOrientation());
    Transform global_H_DOF1(x[node1Idx].getCenter(),x[node1Idx].getOrientation());

    // - add a optional transformation
    Transform global_H_local0, global_H_local1;
    global_H_local0 = global_H_DOF0*DOF0_H_local0;
    global_H_local1 = global_H_DOF1*DOF1_H_local1;



    // 4. Computes the transformation
    Real _L = this->Length_List[edgeInList];

    if(localPos.norm() >1e-10*_L)
        serr<<"WARNING in InterpolateTransformUsingSpline interpolate frame only on the central curve of the beam"<<sendl;

    InterpolateTransformUsingSpline(global_H_localInterpol, baryCoord, global_H_local0, global_H_local1, _L);


    //--------------- compute velocity interpolation --------------------//


    //5. Computes the velocities of the dof (in their own reference frame...)
    SpatialVector VelDOF0inDOF0, VelDOF1inDOF1;
    VelDOF0inDOF0.setLinearVelocity(   global_H_DOF0.backProjectVector( v[node0Idx].getVCenter() )  );
    VelDOF0inDOF0.setAngularVelocity(  global_H_DOF0.backProjectVector( v[node0Idx].getVOrientation() )  );
    VelDOF1inDOF1.setLinearVelocity(   global_H_DOF1.backProjectVector( v[node1Idx].getVCenter() )  );
    VelDOF1inDOF1.setAngularVelocity(  global_H_DOF1.backProjectVector( v[node1Idx].getVOrientation() )  );


    //6. Computes the velocities of the nodes (in their own reference frame...)
    SpatialVector VelNode0inNode0, VelNode1inNode1;
    VelNode0inNode0= DOF0_H_local0.inversed()*VelDOF0inDOF0;
    VelNode1inNode1= DOF1_H_local1.inversed()*VelDOF1inDOF1;

    //7. Interpolate the result and put in "Deriv" vector...

    v_interpol.getVCenter()= global_H_local0.projectVector(VelNode0inNode0.getLinearVelocity() ) * (1-baryCoord) +
            global_H_local1.projectVector(VelNode1inNode1.getLinearVelocity() ) * baryCoord;

    v_interpol.getVOrientation()= global_H_local0.projectVector(VelNode0inNode0.getAngularVelocity() ) * (1-baryCoord) +
            global_H_local1.projectVector(VelNode1inNode1.getAngularVelocity() ) * baryCoord;


}

template<class DataTypes>
void BeamInterpolation<DataTypes>::MapForceOnNodeUsingSpline(unsigned int edgeInList, const Real& baryCoord, const Vec3& localPos, const VecCoord& x, const Vec3& finput,
                                                             SpatialVector& FNode0output, SpatialVector& FNode1output )
{

    //1. get the curvilinear abs and spline parameters
    Real bx = baryCoord;
    Real a0=(1-bx)*(1-bx)*(1-bx);
    Real a1=3*bx*(1-bx)*(1-bx);
    Real a2=3*bx*bx*(1-bx);
    Real a3=bx*bx*bx;

    //2. computes a force on the 4 points of the spline:
    Vec3 F0, F1, F2, F3;
    F0 = finput*a0;
    F1 = finput*a1;
    F2 = finput*a2;
    F3 = finput*a3;

    //3. influence of these forces on the nodes of the beam    => TODO : simplify the computations !!!
    Transform DOF0Global_H_local0, DOF1Global_H_local1;
    this->getDOFtoLocalTransformInGlobalFrame(edgeInList, DOF0Global_H_local0, DOF1Global_H_local1, x);

    //rotate back to local frame
    SpatialVector f0, f1,f2,f3;
    f0.setForce( DOF0Global_H_local0.getOrientation().inverseRotate(F0) );
    f1.setForce( DOF0Global_H_local0.getOrientation().inverseRotate(F1) );
    f2.setForce( DOF1Global_H_local1.getOrientation().inverseRotate(F2) );
    f3.setForce( DOF1Global_H_local1.getOrientation().inverseRotate(F3) );

    // computes the torque created on DOF0 and DOF1 by f1 and f2
    Real L = this->getLength(edgeInList);

    if(localPos.norm() > L*1e-10)
    {
            f0.setTorque(localPos.cross(f0.getForce()+f1.getForce()));
            f3.setTorque(localPos.cross(f2.getForce()+f3.getForce()));
    }
    else
    {
            f0.setTorque(Vec3(0,0,0));
            f3.setTorque(Vec3(0,0,0));

    }

    Vec3 lever(L/3,0,0);
    f1.setTorque(lever.cross(f1.getForce()));
    f2.setTorque(-lever.cross(f2.getForce()));


    // back to the DOF0 and DOF1 frame:
    FNode0output = DOF0Global_H_local0 * (f0+f1);
    FNode1output = DOF1Global_H_local1 * (f2+f3);


}


template<class DataTypes>
bool BeamInterpolation<DataTypes>::breaksInTwo(const Real& /*x_min_out*/,  Real& /*x_break*/, int& /*numBeamsNotUnderControlled*/ )
{
    return false;
}




}// namespace fem

} // namespace component

} // namespace sofa

#endif  /* SOFA_COMPONENT_FEM_BEAMINTERPOLATION_INL */
