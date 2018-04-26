// Driver works, but you _must_ start it near the origin looking in +Z.

#include <poser.h>
#include <survive.h>
#include <survive_reproject.h>

#include "epnp/epnp.h"
#include "linmath.h"
#include "survive_cal.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <survive_imu.h>

#define MAX_PT_PER_SWEEP SENSORS_PER_OBJECT

typedef struct {
	int sweepaxis;
	int sweeplh;

	FLT normal_at_errors[MAX_PT_PER_SWEEP][3]; // Value is actually normalized, not just normal to sweep plane.
	FLT quantity_errors[MAX_PT_PER_SWEEP];
	FLT angles_at_pts[MAX_PT_PER_SWEEP];
	SurvivePose object_pose_at_hit[MAX_PT_PER_SWEEP];
	uint8_t sensor_ids[MAX_PT_PER_SWEEP];

	LinmathPoint3d MixingPositions[NUM_LIGHTHOUSES][2];
	SurvivePose InteralPoseUsedForCalc;	//Super high speed vibratey and terrible.
	FLT MixingConfidence[NUM_LIGHTHOUSES][2];
	FLT last_angle_lh_axis[NUM_LIGHTHOUSES][2];
	int ptsweep;

	SurviveIMUTracker tracker;
} CharlesPoserData;

int PoserCharlesRefine(SurviveObject *so, PoserData *pd) {
	CharlesPoserData *dd = so->PoserData;
	if (!dd)
	{
		so->PoserData = dd = calloc(sizeof(CharlesPoserData), 1);
		SurvivePose object_pose_out;
		memcpy(&object_pose_out, &LinmathPose_Identity, sizeof(LinmathPose_Identity));
		memcpy(&dd->InteralPoseUsedForCalc, &LinmathPose_Identity, sizeof(LinmathPose_Identity));
		so->PoseConfidence = 1.0;
		PoserData_poser_pose_func(pd, so, &object_pose_out);
	}

	SurviveSensorActivations *scene = &so->activations;
	switch (pd->pt) {
	case POSERDATA_IMU: {
		// Really should use this...
		PoserDataIMU *imuData = (PoserDataIMU *)pd;

		//TODO: Actually do Madgwick's algorithm
		LinmathQuat	applymotion;
		const SurvivePose * object_pose = &so->OutPose;
		imuData->gyro[0] *= 1./240.;
		imuData->gyro[1] *= 1./240.;
		imuData->gyro[2] *= 1./240.;
		quatfromeuler( applymotion, imuData->gyro );
		//printf( "%f %f %f\n", imuData->gyro [0], imuData->gyro [1], imuData->gyro [2] );

		LinmathQuat InvertQuat;
		quatgetreciprocal(InvertQuat, object_pose->Rot);

		//Apply a tiny tug to re-right headset based on the gravity vector.
		LinmathVec3d reright = { 0, 0, 1 };
		LinmathVec3d normup;
		normalize3d( normup, imuData->accel );
		LinmathVec3d correct_diff;
		quatrotatevector(reright, InvertQuat, reright);
		sub3d( correct_diff, normup, reright  );
		scale3d( correct_diff, correct_diff, -0.001 );	//This is the coefficient applying the drag.
		add3d( correct_diff, correct_diff, reright );
		LinmathQuat  reright_quat;
		normalize3d( correct_diff, correct_diff );
		quatfrom2vectors( reright_quat, reright, correct_diff );



		SurvivePose object_pose_out;
		quatrotateabout(object_pose_out.Rot, object_pose->Rot, applymotion );		//Contribution from Gyro
		quatrotateabout(object_pose_out.Rot, object_pose_out.Rot, reright_quat);	//Contribution from Accelerometer
		quatnormalize(object_pose_out.Rot, object_pose_out.Rot);


		copy3d( object_pose_out.Pos, object_pose->Pos );
		PoserData_poser_pose_func(pd, so, &object_pose_out);
		quatcopy( dd->InteralPoseUsedForCalc.Rot, object_pose_out.Rot);

		//PoserDataIMU *imu = (PoserDataIMU *)pd;
		//survive_imu_tracker_integrate(so, &dd->tracker, imu);
		//PoserData_poser_pose_func(pd, so, &dd->tracker.pose);

		return 0;
	}
	case POSERDATA_LIGHT: {
		int i;
		PoserDataLight *ld = (PoserDataLight *)pd;
		int lhid = ld->lh;
		int senid = ld->sensor_id;
		BaseStationData *bsd = &so->ctx->bsd[ld->lh];
		if (!bsd->PositionSet)
			break;
		SurvivePose *lhp = &bsd->Pose;
		FLT inangle = ld->angle;
		int sensor_id = ld->sensor_id;
		int axis = dd->sweepaxis;
		//const SurvivePose *object_pose = &so->OutPose;

		dd->sweeplh = lhid;

		// FOR NOW, drop LH1.
		// if( lhid == 1 ) break;

		//		const FLT * sensor_normal = &so->sensor_normals[senid*3];
		//		FLT sensor_normal_worldspace[3];
		//		ApplyPoseToPoint(sensor_normal_worldspace,   object_pose, sensor_inpos);

		const FLT *sensor_inpos = &so->sensor_locations[senid * 3];
		FLT sensor_position_worldspace[3];
		// XXX Once I saw this get pretty wild (When in playback)
		// I had to invert the values of sensor_inpos.  Not sure why.
		ApplyPoseToPoint(sensor_position_worldspace, &dd->InteralPoseUsedForCalc, sensor_inpos);

		// printf( "%f %f %f  == > %f %f %f\n", sensor_inpos[0], sensor_inpos[1], sensor_inpos[2],
		// sensor_position_worldspace[0], sensor_position_worldspace[1], sensor_position_worldspace[2] );
		// = sensor position, relative to lighthouse center.
		FLT sensorpos_rel_lh[3];
		sub3d(sensorpos_rel_lh, sensor_position_worldspace, lhp->Pos);

		// Next, define a normal in global space of the plane created by the sweep hit.
		// Careful that this must be normalized.
		FLT sweep_normal[3];


		FLT inangles[2];
		FLT outangles[2];
		inangles[axis] = inangle;
		inangles[!axis] = dd->last_angle_lh_axis[lhid][!axis];
		survive_apply_bsd_calibration(so->ctx, lhid, inangles, outangles );
		FLT angle = outangles[axis];
		

		// If 1, the "y" axis. //XXX Check me.
		if (axis) // XXX Just FYI this should include account for skew
		{
			sweep_normal[0] = 0;
			sweep_normal[1] = cos(angle);
			sweep_normal[2] = sin(angle);
			// printf( "+" );
		} else {
			sweep_normal[0] = cos(angle);
			sweep_normal[1] = 0;
			sweep_normal[2] = -sin(angle);
			// printf( "-" );
		}

		// Need to apply the lighthouse's transformation to the sweep's normal.
		quatrotatevector(sweep_normal, lhp->Rot, sweep_normal);

		// Compute point-line distance between sensorpos_rel_lh and the plane defined by sweep_normal.
		// Do this by projecting sensorpos_rel_lh (w) onto sweep_normal (v).
		// You can do this by |v dot w| / |v| ... But we know |v| is 1. So...
		FLT dist = dot3d(sensorpos_rel_lh, sweep_normal);

		if ((i = dd->ptsweep) < MAX_PT_PER_SWEEP) {
			int repeat = 0;
			int k;

			//Detect repeated hits.  a rare problem but can happen with lossy sources of pose.
			for( k = 0; k < dd->ptsweep; k++ )
			{
				if( dd->sensor_ids[k] == sensor_id )
				{
					repeat = 1;
					i = k;
				}
			}
			memcpy(dd->normal_at_errors[i], sweep_normal, sizeof(FLT) * 3);
			dd->quantity_errors[i] = dist;
			dd->angles_at_pts[i] = angle;
			dd->sensor_ids[i] = sensor_id;
			memcpy(&dd->object_pose_at_hit[i], &dd->InteralPoseUsedForCalc, sizeof(SurvivePose));
			if( !repeat )
				dd->ptsweep++;
		}

		dd->last_angle_lh_axis[lhid][axis] = inangle;

		return 0;
	}

	case POSERDATA_SYNC: {
		PoserDataLight *l = (PoserDataLight *)pd;
		int lhid = l->lh;

		// you can get sweepaxis and sweeplh.
		if (dd->ptsweep) {
			int i;
			int lhid = dd->sweeplh;
			int axis = dd->sweepaxis;
			int pts = dd->ptsweep;
			//const SurvivePose *object_pose =
			//	&so->OutPose; // XXX TODO Should pull pose from approximate time when LHs were scanning it.

			BaseStationData *bsd = &so->ctx->bsd[lhid];
			SurvivePose *lh_pose = &bsd->Pose;

			int validpoints = 0;
			int ptvalid[MAX_PT_PER_SWEEP];
			FLT avgerr = 0.0;
			FLT vec_correct[3] = {0., 0., 0.};
			FLT avgang = 0.0;

// Tunable parameters:
#define MIN_HIT_QUALITY 0.5 // Determines which hits to cull.
#define HIT_QUALITY_BASELINE                                                                                           \
	0.0001 // Determines which hits to cull.  Actually SQRT(baseline) if 0.0001, it is really 1cm

#define CORRECT_LATERAL_POSITION_COEFFICIENT 0.7 // Explodes if you exceed 1.0
#define CORRECT_TELESCOPTION_COEFFICIENT 7.00	 // Converges even as high as 10.0 and doesn't explode.
#define CORRECT_ROTATION_COEFFICIENT                                                                                   \
	0.2 // This starts to fall apart above 5.0, but for good reason.  It is amplified by the number of points seen.
#define ROTATIONAL_CORRECTION_MAXFORCE 0.01

			// Step 1: Determine standard of deviation, and average in order to
			// drop points that are likely in error.
			{
				// Calculate average
				FLT avgerr_orig = 0.0;
				FLT stddevsq = 0.0;
				for (i = 0; i < pts; i++)
					avgerr_orig += dd->quantity_errors[i];
				avgerr_orig /= pts;

				// Calculate standard of deviation.
				for (i = 0; i < pts; i++) {
					FLT diff = dd->quantity_errors[i] - avgerr_orig;
					stddevsq += diff * diff;
				}
				stddevsq /= pts;

				for (i = 0; i < pts; i++) {
					FLT err = dd->quantity_errors[i];
					FLT diff = err - avgerr_orig;
					diff *= diff;
					int isptvalid = (diff * MIN_HIT_QUALITY <= stddevsq + HIT_QUALITY_BASELINE) ? 1 : 0;
					ptvalid[i] = isptvalid;
					if (isptvalid) {
						avgang += dd->angles_at_pts[i];
						avgerr += err;
						validpoints++;
					}
				}
				avgang /= validpoints;
				avgerr /= validpoints;
			}

			// Step 2: Determine average lateral error.
			// We can actually always perform this operation.  Even with only one point.
			{
				FLT avg_err[3] = {0, 0, 0}; // Positional error.
				for (i = 0; i < pts; i++) {
					if (!ptvalid[i])
						continue;
					FLT *nrm = dd->normal_at_errors[i];
					FLT err = dd->quantity_errors[i];
					avg_err[0] = avg_err[0] + nrm[0] * err;
					avg_err[1] = avg_err[1] + nrm[1] * err;
					avg_err[2] = avg_err[2] + nrm[2] * err;
				}

				// NOTE: The "avg_err" is not geometrically centered.  This is actually
				// probably okay, since if you have sevearl data points to one side, you
				// can probably trust that more.
				scale3d(avg_err, avg_err, 1. / validpoints);

				// We have "Average error" now.  A vector in worldspace.
				// This can correct for lateral error, but not distance from camera.

				// XXX TODO: Should we check to see if we only have one or
				// two points to make sure the error on this isn't unusually high?
				// If calculated error is unexpectedly high, then we should probably
				// Not apply the transform.
				scale3d(avg_err, avg_err, -CORRECT_LATERAL_POSITION_COEFFICIENT);
				add3d(vec_correct, vec_correct, avg_err);
			}



			// Step 3: Control telecoption from lighthouse.
			// we need to find out what the weighting is to determine "zoom"
			if (validpoints > 1) // Can't correct "zoom" with only one point.
			{
				FLT zoom = 0.0;
				FLT rmsang = 0.0;
				for (i = 0; i < pts; i++) {
					if (!ptvalid[i])
						continue;
					FLT delang = dd->angles_at_pts[i] - avgang;
					FLT delerr = dd->quantity_errors[i] - avgerr;
					if (axis)
						delang *= -1; // Flip sign on alternate axis because it's measured backwards.
					zoom += delerr * delang;
					rmsang += delang * delang;
				}

				// Control into or outof lighthouse.
				// XXX Check to see if we need to sqrt( the rmsang), need to check convergance behavior close to
				// lighthouse.
				// This is a questionable step.
				zoom /= sqrt(rmsang);

				zoom *= CORRECT_TELESCOPTION_COEFFICIENT;

				FLT veccamalong[3];
				sub3d(veccamalong, lh_pose->Pos, dd->InteralPoseUsedForCalc.Pos);
				normalize3d(veccamalong, veccamalong);
				scale3d(veccamalong, veccamalong, zoom);
				add3d(vec_correct, veccamalong, vec_correct);
			}


#if 0
			LinmathPoint3d LastDelta;
			sub3d( LastDelta, dd->MixingPositions[lhid][axis], dd->InteralPoseUsedForCalc.Pos );
			//Compare with "vec_correct"
			
			LinmathPoint3d DeltaDelta;
			sub3d( DeltaDelta, vec_correct, LastDelta );


			//SurvivePose object_pose_out;

			memcpy( dd->MixingPositions[lhid][axis], vec_correct, sizeof( vec_correct ) );

			LinmathPoint3d system_average_adjust = { 0, 0, 0 };
			
			printf( "%f %f %f + %f %f %f\n", vec_correct[0], vec_correct[1], vec_correct[2], dd->InteralPoseUsedForCalc.Pos[0], dd->InteralPoseUsedForCalc.Pos[1], dd->InteralPoseUsedForCalc.Pos[2] );

#endif
			add3d(dd->InteralPoseUsedForCalc.Pos, vec_correct, dd->InteralPoseUsedForCalc.Pos);



			//quatcopy(object_pose_out.Rot, dd->InteralPoseUsedForCalc.Rot);

			// Stage 4: "Tug" on the rotation of the object, from all of the sensor's pov.
			// If we were able to determine likliehood of a hit in the sweep instead of afterward
			// we would actually be able to perform this on a per-hit basis.
			if (validpoints > 1) {
				LinmathQuat correction;
				quatcopy(correction, LinmathQuat_Identity);
				for (i = 0; i < pts; i++) {
					if (!ptvalid[i])
						continue;
					FLT dist = dd->quantity_errors[i] - avgerr;
					FLT angle = dd->angles_at_pts[i];
					int sensor_id = dd->sensor_ids[i];
					FLT *normal = dd->normal_at_errors[i];
					const SurvivePose *object_pose_at_hit = &dd->object_pose_at_hit[i];
					const FLT *sensor_inpos = &so->sensor_locations[sensor_id * 3];

					LinmathQuat world_to_object_space;
					quatgetreciprocal(world_to_object_space, object_pose_at_hit->Rot);
					FLT correction_in_object_space[3]; // The amount across the surface of the object the rotation
													   // should happen.

					quatrotatevector(correction_in_object_space, world_to_object_space, normal);
					dist *= CORRECT_ROTATION_COEFFICIENT;
					if (dist > ROTATIONAL_CORRECTION_MAXFORCE)
						dist = ROTATIONAL_CORRECTION_MAXFORCE;
					if (dist < -ROTATIONAL_CORRECTION_MAXFORCE)
						dist = -ROTATIONAL_CORRECTION_MAXFORCE;

					// Now, we have a "tug" vector in object-local space.  Need to apply the torque.
					FLT vector_from_center_of_object[3];
					normalize3d(vector_from_center_of_object, sensor_inpos);
					// scale3d(vector_from_center_of_object, sensor_inpos, 10.0 );
					//			vector_from_center_of_object[2]*=-1;
					//				vector_from_center_of_object[1]*=-1;
					//					vector_from_center_of_object[0]*=-1;
					// vector_from_center_of_object
					scale3d(vector_from_center_of_object, vector_from_center_of_object, 1);

					FLT new_vector_in_object_space[3];
					// printf( "%f %f %f %f\n", object_pose_at_hit->Rot[0], object_pose_at_hit->Rot[1],
					// object_pose_at_hit->Rot[2], object_pose_at_hit->Rot[3] );
					// printf( "%f %f %f  // %f %f %f // %f\n", vector_from_center_of_object[0],
					// vector_from_center_of_object[1], vector_from_center_of_object[2], correction_in_object_space[0],
					// correction_in_object_space[1], correction_in_object_space[2], dist );
					scale3d(correction_in_object_space, correction_in_object_space, -dist);
					add3d(new_vector_in_object_space, vector_from_center_of_object, correction_in_object_space);

					normalize3d(new_vector_in_object_space, new_vector_in_object_space);

					LinmathQuat corrective_quaternion;
					quatfrom2vectors(corrective_quaternion, vector_from_center_of_object, new_vector_in_object_space);
					quatrotateabout(correction, correction, corrective_quaternion);
					// printf( "%f -> %f %f %f => %f %f %f [%f %f %f %f]\n", dist, vector_from_center_of_object[0],
					// vector_from_center_of_object[1], vector_from_center_of_object[2],
					// correction_in_object_space[0], correction_in_object_space[1], correction_in_object_space[2],
					// corrective_quaternion[0],corrective_quaternion[1],corrective_quaternion[1],corrective_quaternion[3]);
				}
				// printf( "Applying: %f %f %f %f\n", correction[0], correction[1], correction[2], correction[3] );
				// Apply our corrective quaternion to the output.
				quatrotateabout(dd->InteralPoseUsedForCalc.Rot, dd->InteralPoseUsedForCalc.Rot, correction);
				quatnormalize(dd->InteralPoseUsedForCalc.Rot, dd->InteralPoseUsedForCalc.Rot);
			}

			memcpy( dd->MixingPositions[lhid][axis], dd->InteralPoseUsedForCalc.Pos, sizeof( dd->InteralPoseUsedForCalc.Pos ) );
			dd->MixingConfidence[lhid][axis] = (validpoints)?((validpoints>1)?1.0:0.5):0;


			//Box filter all of the guesstimations, and emit the new guess.
			{
				FLT MixedAmount = 0;
				LinmathPoint3d MixedPosition = { 0, 0, 0 };
				int l = 0, a = 0;
				if( lhid == 0 && axis == 0 ) for( l = 0; l < NUM_LIGHTHOUSES; l++ ) for( a = 0; a < 2; a++ ) dd->MixingConfidence[l][a] -= 0.1;

				for( l = 0; l < NUM_LIGHTHOUSES; l++ ) for( a = 0; a < 2; a++ )
				{
					LinmathPoint3d MixThis = { 0, 0, 0 };
					FLT Confidence = dd->MixingConfidence[l][a];
					if(Confidence < 0 ) Confidence = 0;
					scale3d( MixThis, dd->MixingPositions[l][a], Confidence );
					add3d( MixedPosition, MixedPosition, MixThis );
					MixedAmount += Confidence;
					//printf( "%f ", Confidence );
				}
				scale3d( MixedPosition, MixedPosition, 1./MixedAmount );

				printf( "Reprojection disagreements:" );
				for( l = 0; l < NUM_LIGHTHOUSES; l++ ) for( a = 0; a < 2; a++ )
				{
					printf( "%f ", dist3d( dd->MixingPositions[l][a], MixedPosition ) );
				}
				printf( "\n" );

				//printf( "%f\n", MixedAmount );
				SurvivePose object_pose_out;
				quatcopy(object_pose_out.Rot, dd->InteralPoseUsedForCalc.Rot );
				copy3d( object_pose_out.Pos, MixedPosition );
				PoserData_poser_pose_func(pd, so, &object_pose_out);
			}
		//	FLT var_meters = 0.5;
		//	FLT error = 0.00001;
		//	FLT var_quat = error + .05;
		//	FLT var[7] = {error * var_meters, error * var_meters, error * var_meters, error * var_quat,
		//				  error * var_quat,   error * var_quat,   error * var_quat};
		//
		//	survive_imu_tracker_integrate_observation(so, l->timecode, &dd->tracker, &object_pose_out, var);
		//	PoserData_poser_pose_func(pd, so, &dd->tracker.pose);

			dd->ptsweep = 0;
		}

		dd->sweepaxis = l->acode & 1;
		// printf( "SYNC %d %p\n", l->acode, dd );
		break;
	}
	case POSERDATA_FULL_SCENE: {
		// return opencv_solver_fullscene(so, (PoserDataFullScene *)(pd));
	}
	}
	return -1;
}

REGISTER_LINKTIME(PoserCharlesRefine);
