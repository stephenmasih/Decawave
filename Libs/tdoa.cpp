/*************************************************
 *	
 *  Class for computing 3D position from Time-Difference of Arrival information.
 *  Position is computed using an Extended Kalman Filter.
 *
 *  
 *
 *  Created on: 06/19/2017
 *  Author: Joao Paulo Jansch Porto <janschp2(at)illinois.edu>
 *
 *  Changelog:
 *      v0.2 - initial release (06/19/2017)
 *
 *************************************************/


#include "tdoa.h"

TDOA::TDOA(void)
{
	tdoaCount = 0;
	nr_states = STATE_DIM;
	S.setZero(STATE_DIM);
	S(STATE_X) = 2;
	S(STATE_Y) = 2.6;
	P.setZero(STATE_DIM, STATE_DIM);
	
	P(STATE_X, STATE_X) = powf(100,2);
	P(STATE_Y, STATE_Y) = powf(100,2);
	P(STATE_Z, STATE_Z) = powf(1,2);
	
	P(STATE_VX, STATE_VX) = powf(0.01,2);
	P(STATE_VY, STATE_VY) = powf(0.01,2);
	P(STATE_VZ, STATE_VZ) = powf(0.01,2);
	
	A = Eigen::MatrixXf::Identity(STATE_DIM,STATE_DIM);
	A(STATE_X,STATE_VX) = 0.016;
	A(STATE_Y,STATE_VY) = 0.016;
	A(STATE_Z,STATE_VZ) = 0.016;;
	
	initAnchorPos();
	stdDev = 0.15f;
}

void TDOA::setTransitionMat(Eigen::MatrixXf transition_mat)
{
	if( (transition_mat.rows() != nr_states) || (transition_mat.cols() != nr_states) )
	{
		// If provided transition_mat is of wrong size, ignore input
		return;
	}

	A = transition_mat;

}

void TDOA::setPredictionMat(Eigen::MatrixXf prediction_mat)
{
	if( (prediction_mat.rows() != nr_states) || (prediction_mat.cols() != nr_states) )
	{
		// If provided transition_mat is of wrong size, ignore input
		return;
	}

	P = prediction_mat;
}

void TDOA::setAncPosition(int anc_num, vec3d_t anc_pos)
{
	if( (anc_num < 0) || (anc_num > MAX_NR_ANCHORS) )
	{
		//invalid anchor number
		return;
	}

	anchorPosition[anc_num] = anc_pos;
}

void TDOA::setAncPosition(int anc_num, float x, float y, float z)
{
	vec3d_t temp;
	temp.x = x;
	temp.y = y;
	temp.z = z;
	setAncPosition(anc_num, temp);
}

vec3d_t TDOA::getAncPosition(int anc_num)
{
	return anchorPosition[anc_num];
}

void TDOA::scalarTDOADistUpdate(uint8_t Ar, uint8_t An, float distanceDiff)
{
  /**
   * Measurement equation:
   * dR = dT + d1 - d0
   */

	float measurement = distanceDiff;

	// predict based on current state
	float x = S(STATE_X);
	float y = S(STATE_Y);
	float z = S(STATE_Z);

	float x1 = anchorPosition[An].x, y1 = anchorPosition[An].y, z1 = anchorPosition[An].z;
	float x0 = anchorPosition[Ar].x, y0 = anchorPosition[Ar].y, z0 = anchorPosition[Ar].z;

	float d1 = sqrtf(powf(x - x1, 2) + powf(y - y1, 2) + powf(z - z1, 2));
	float d0 = sqrtf(powf(x - x0, 2) + powf(y - y0, 2) + powf(z - z0, 2));

	float predicted = d1 - d0;
	float error = measurement - predicted;

	Eigen::RowVectorXf h = Eigen::RowVectorXf::Constant(STATE_DIM, 0);

	h(STATE_X) = ((x - x1) / d1 - (x - x0) / d0);
	h(STATE_Y) = ((y - y1) / d1 - (y - y0) / d0);
	h(STATE_Z) = ((z - z1) / d1 - (z - z0) / d0);

	stateEstimatorScalarUpdate(h, error, stdDev);

}

void TDOA::stateEstimatorScalarUpdate(Eigen::RowVectorXf H, float error, float stdMeasNoise)
{
	// The Kalman gain as a column vector
	static Eigen::VectorXf K(STATE_DIM);

	// Temporary matrices for the covariance updates
	static Eigen::MatrixXf tmpNN1m(STATE_DIM, STATE_DIM);
	static Eigen::MatrixXf tmpNN2m(STATE_DIM, STATE_DIM);
	static Eigen::MatrixXf tmpNN3m(STATE_DIM, STATE_DIM);
	static Eigen::VectorXf PHTm(STATE_DIM);
	static Eigen::MatrixXf I = Eigen::MatrixXf::Identity(STATE_DIM, STATE_DIM);

	// ====== INNOVATION COVARIANCE ======
	PHTm = P*H.transpose(); // PH'
	float R = stdMeasNoise*stdMeasNoise;
	float HPHR = H*PHTm + R; // HPH' + R

	// ====== MEASUREMENT UPDATE ======
	// Calculate the Kalman gain and perform the state update
	K = PHTm/HPHR;
	S = S + K*error;
	

	// ====== COVARIANCE UPDATE ======
	P = (I - K*H)*P;

}

void TDOA::stateEstimatorPredict()
{
	// Covariance update
	P = A*P*A.transpose();
}

vec3d_t TDOA::getLocation(void)
{
	vec3d_t pos;
	pos.x = S(STATE_X);
	pos.y = S(STATE_Y);
	pos.z = S(STATE_Z);
	
	//For debugging purposes:
	std::cout << S.transpose() << std::endl;
	
	return pos;
}

void TDOA::initAnchorPos(void)
{
	anchorPosition[0].x = 4.628;
	anchorPosition[0].y = 0.600;
	anchorPosition[0].z = 1.312;
	
	anchorPosition[1].x = 4.628;
	anchorPosition[1].y = 3.810;
	anchorPosition[1].z = 1.297;
	
	anchorPosition[2].x = 0.043;
	anchorPosition[2].y = 4.210;
	anchorPosition[2].z = 1.302;
	
	anchorPosition[3].x = 0.123;
	anchorPosition[3].y = 1.673;
	anchorPosition[3].z = 1.903;
}
