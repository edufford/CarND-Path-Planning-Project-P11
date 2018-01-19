//
//  vehicle.hpp
//  Path_Planning
//
//  Created by Student on 1/11/18.
//

#ifndef vehicle_hpp
#define vehicle_hpp

#include <stdio.h>

#include <iostream>
#include <vector>
#include <deque>
#include <map>
#include <string>
#include <math.h>

#include "path_common.hpp"

enum VehIntents {
  kUnknown = -1,
  kKeepLane = 0,
  kPlanLaneChangeLeft = 1,
  kPlanLaneChangeRight = 2,
  kLaneChangeLeft = 3,
  kLaneChangeRight = 4
};

struct VehBehavior {
  VehIntents intent;
  int car_ahead_id;
  int tgt_lane;
  double tgt_time;
};

struct VehState {
  double x;
  double y;
  double s;
  double s_dot;
  double s_dotdot;
  double d;
  double d_dot;
  double d_dotdot;
};

struct VehTrajectory {
  std::deque<VehState> states;
  double probability;
};

class Vehicle {
public:
  
  int veh_id_;
  int lane_;
  VehState state_;
  VehTrajectory traj_;
  VehIntents intent_;
  
  /**
   * Constructor
   */
  Vehicle();
  Vehicle(int veh_id);
  
  /**
   * Destructor
   */
  virtual ~Vehicle();
  
  void UpdateState(double x, double y, double s, double d,
                   double s_dot, double d_dot,
                   double s_dotdot, double d_dotdot);
  
};


class EgoVehicle : public Vehicle {
public:
  
  //int idx_prev_path_;
  VehBehavior tgt_behavior_;
  
  std::vector<double> coeffs_JMT_s_; // [a0, a1, a2, a3, a4, a5]
  std::vector<double> coeffs_JMT_s_dot_; // [a1, 2*a2, 3*a3, 4*a4, 5*a5]
  std::vector<double> coeffs_JMT_s_dotdot_; // [2*a2, 6*a3, 12*a4, 20*a5]
  
  std::vector<double> coeffs_JMT_d_; // [a0, a1, a2, a3, a4, a5]
  std::vector<double> coeffs_JMT_d_dot_; // [a1, 2*a2, 3*a3, 4*a4, 5*a5]
  std::vector<double> coeffs_JMT_d_dotdot_; // [2*a2, 6*a3, 12*a4, 20*a5]
  
  /**
   * Constructor
   */
  EgoVehicle();
  EgoVehicle(int id);
  
  /**
   * Destructor
   */
  virtual ~EgoVehicle();
};

class DetectedVehicle : public Vehicle {
public:
  
  double s_rel_;
  double d_rel_;
  std::map<VehIntents, VehTrajectory> pred_trajs_;
  
  /**
   * Constructor
   */
  DetectedVehicle();
  DetectedVehicle(int id);
  
  /**
   * Destructor
   */
  virtual ~DetectedVehicle();
  
  void UpdateRelDist(double s_ego, double d_ego);
};

#endif /* vehicle_hpp */
