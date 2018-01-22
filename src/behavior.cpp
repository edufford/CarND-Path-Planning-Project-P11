//
//  behavior.cpp
//  Path_Planning
//
//  Created by Student on 1/12/18.
//

#include "behavior.hpp"

void VehBehaviorFSM(EgoVehicle &ego_car,
                    const std::map<int, DetectedVehicle> &detected_cars,
                    const std::map<int, std::vector<int>> &car_ids_by_lane_ahead,
                    const std::map<int, std::vector<int>> &car_ids_by_lane_behind) {
  
  // KL: Behavior to keep current lane
  ego_car.tgt_behavior_.intent = kKeepLane;
  ego_car.tgt_behavior_.tgt_lane = ego_car.lane_;
  ego_car.tgt_behavior_.tgt_time = kNewPathTime;
  
  constexpr double kTgtFollowDist = 30.; // m
  constexpr double kTgtMinFollowDist = 10.; // m
  constexpr double kTgtSpeedDec = 3.; // m/s
  
  double target_speed = kTargetSpeed;
  double min_rel_s = std::numeric_limits<double>::max();
  int car_id_ahead = -1;
  
  // Check for closest car ahead in current lane
  if (car_ids_by_lane_ahead.count(ego_car.lane_) > 0) {
    car_id_ahead = car_ids_by_lane_ahead.at(ego_car.lane_).front();
    min_rel_s = detected_cars.at(car_id_ahead).s_rel_;
    
    // Set target speed based on car ahead
    if (car_id_ahead >= 0) {
      const DetectedVehicle* car_ahead = &detected_cars.at(car_id_ahead);

      if (car_ahead->s_rel_ < kTgtMinFollowDist) {
        target_speed = car_ahead->state_.s_dot - kTgtSpeedDec;
      }
      else if (car_ahead->s_rel_ < kTgtFollowDist) {
        target_speed = car_ahead->state_.s_dot;
      }
      std::cout << "rel_s ahead = " << min_rel_s << std::endl;
    }
  }
  
  // Min/max guard
  if (target_speed > kTargetSpeed) {
    target_speed = kTargetSpeed;
  }
  else if (target_speed < 0.) {
    target_speed = 0.;
  }
  
  ego_car.tgt_behavior_.tgt_speed = target_speed;
  
  // DEBUG
  std::cout << "tgt_speed (mph) = " << mps2mph(ego_car.tgt_behavior_.tgt_speed) << std::endl;
  
  // DUMMY lane change behavior
  if ((ego_car.state_.s_dot < kTargetSpeed - mph2mps(10))
      && (min_rel_s < kTgtFollowDist)) {
    if (ego_car.lane_ > 1) {
      ego_car.tgt_behavior_.tgt_lane--; // DUMMY
    }
    else if (ego_car.lane_ < 3) {
      ego_car.tgt_behavior_.tgt_lane++; // DUMMY
    }
    
  }
  
}
