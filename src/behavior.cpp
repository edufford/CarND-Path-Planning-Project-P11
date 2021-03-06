//
//  behavior.cpp
//  Path_Planning
//
//  Created by Student on 1/12/18.
//

#include "behavior.hpp"

/**
 * Cost function to set target lane considering:
 *   #1) Cost by distance to car ahead
 *   #2) Cost by speed of car ahead
 *   #3) Cost of fast car coming up from close behind
 *   #4) Cost of changing lanes
 *   #5) Cost of frequent lane changes
 * Return best lane # with lowest cost
 */
int LaneCostFcn(const EgoVehicle &ego_car,
                const std::map<int, DetectedVehicle> &detected_cars,
                const std::map<int, std::vector<int>> &car_ids_by_lane) {
  
  std::vector<double> cost_by_lane;
  const int ego_id = ego_car.GetID();
  const int ego_lane = ego_car.GetLane();
  const double ego_spd = ego_car.GetState().s_dot;
  
  // Set cost for each lane
  for (int lane_idx = 0; lane_idx < kNumLanes; ++lane_idx) {
    const int lane_num = lane_idx + 1;
    double lane_cost = 0.;
    
    // #1) Cost by distance to car ahead
    auto car_ahead = FindCarInLane(kFront, lane_num, ego_id, ego_car,
                                   detected_cars, car_ids_by_lane);
    const int car_id_ahead = std::get<0>(car_ahead);
    double rel_s_ahead = kSensorRange;
    if (car_id_ahead != ego_id) {
      rel_s_ahead = detected_cars.at(car_id_ahead).GetRelS();
    }
    // Cost is 0 at dist = full sensor range, ~1 at dist = 0 m
    const double cost_ahead_dist = (1-LogCost(rel_s_ahead, kSensorRange));
    lane_cost += kCostDistAhead * cost_ahead_dist;
    
    // #2) Cost by speed of car ahead
    double s_dot_ahead = kTargetSpeed;
    if (car_id_ahead != ego_id) {
      s_dot_ahead = detected_cars.at(car_id_ahead).GetState().s_dot;
    }
    // Cost is 0 at spd = full target speed, ~1 at spd = 0 m/s
    const double cost_ahead_spd = (1-LogCost(s_dot_ahead, kTargetSpeed));
    lane_cost += kCostSpeedAhead * cost_ahead_spd;
    
    // #3) Cost of fast car coming up from close behind
    auto car_behind = FindCarInLane(kBack, lane_num, ego_id, ego_car,
                                    detected_cars, car_ids_by_lane);
    const int car_id_behind = std::get<0>(car_behind);
    double rel_s_behind = -kSensorRange;
    double rel_s_dot_behind = 0.;
    if (car_id_behind != ego_id) {
      rel_s_behind = detected_cars.at(car_id_behind).GetRelS();
      rel_s_dot_behind = (detected_cars.at(car_id_behind).GetState().s_dot
                          - ego_spd);
      rel_s_dot_behind = std::max(rel_s_dot_behind, 0.); // guard neg delta spd
    }
    if (abs(rel_s_behind) < kTgtFollowDist) {
      // Cost is 0 at delta spd = 0, ~1 at delta spd = kRelSpeedBehind m/s
      const double cost_behind_spd = LogCost(rel_s_dot_behind, kRelSpeedBehind);
      lane_cost += kCostSpeedBehind * cost_behind_spd;
    }
    
    // #4) Cost of changing lanes
    if (lane_num != ego_lane) {
      // Cost is 0 at lane = ego lane, +1 at each lane away from ego lane
      const double cost_lane_change = (abs(ego_lane - lane_num));
      lane_cost += kCostChangeLanes * cost_lane_change;
    }
    
    // #5) Cost of frequent lane changes
    if ((ego_car.GetLaneChangeCounter() > 0)
        && (lane_num != ego_car.GetTgtBehavior().tgt_lane)) {
      // Cost is 0 at counter = 0, multiple of counter value at counter > 0
      lane_cost += kCostFreqLaneChange * ego_car.GetLaneChangeCounter();
    }
    
    // Save total cost for this lane to vector
    cost_by_lane.push_back(lane_cost);
    
    // Debug logging
    if (kDBGBehavior != 0) {
      std::cout << "Cost function lane: " << lane_num << ", cost: "
      << lane_cost << std::endl;
    }
  }
  
  // Choose lowest cost lane
  auto it_best_cost = min_element(cost_by_lane.begin(), cost_by_lane.end());
  int best_lane = (it_best_cost - cost_by_lane.begin()) + 1;
  
  return best_lane;
}

/**
 * Finite State Machine for behavior intents.  Possible intents are:
 *   1. Keep Lane (KL)
 *   2. Plan Lane Change Left (PLCL)
 *   3. Plan Lane Change Right (PLCR)
 *   4. Lane Change Left (LCL)
 *   5. Lane Change Right (LCR)
 * Returns the target intent.
 */
VehIntents BehaviorFSM(const EgoVehicle &ego_car,
                       const std::map<int, DetectedVehicle> &detected_cars,
                       const std::map<int, std::vector<int>> &car_ids_by_lane) {
  
  VehIntents tgt_intent;
  const double gap_on_left = EgoCheckSideGap(kLeft, ego_car, detected_cars,
                                             car_ids_by_lane);
  const double gap_on_right = EgoCheckSideGap(kRight, ego_car, detected_cars,
                                              car_ids_by_lane);
  const VehBehavior ego_beh = ego_car.GetTgtBehavior();
  const VehIntents cur_intent = ego_beh.intent;
  const int cur_tgt_lane = ego_beh.tgt_lane;
  const int ego_lane = ego_car.GetLane();
  
  // Behavior Intent FSM
  switch (cur_intent) {
    case kKeepLane: {
      if (cur_tgt_lane < ego_lane) {
        tgt_intent = kPlanLaneChangeLeft; // tgt lane on left, plan LC
      }
      else if (cur_tgt_lane > ego_lane) {
        tgt_intent = kPlanLaneChangeRight; // tgt lane on right, plan LC
      }
      else {
        tgt_intent = kKeepLane; // stay in KL
      }
      break;
    }
    case kPlanLaneChangeLeft: {
      if (gap_on_left > kLaneChangeMinGap) {
        tgt_intent = kLaneChangeLeft; // lane on left is clear, change lanes
      }
      else if (cur_tgt_lane < ego_lane) {
        tgt_intent = kPlanLaneChangeLeft; // stay in PLCL
      }
      else {
        tgt_intent = kKeepLane; // go back to keep lane
      }
      break;
    }
    case kPlanLaneChangeRight: {
      if (gap_on_right > kLaneChangeMinGap) {
        tgt_intent = kLaneChangeRight; // lane on right is clear, change lanes
      }
      else if (cur_tgt_lane > ego_lane) {
        tgt_intent = kPlanLaneChangeRight; // stay in PLCR
      }
      else {
        tgt_intent = kKeepLane; // go back to keep lane
      }
      break;
    }
    case kLaneChangeLeft: {
      if ((cur_tgt_lane < ego_lane) && (gap_on_left > kLaneChangeMinGap)) {
        tgt_intent = kLaneChangeLeft; // stay in LCL
      }
      else {
        tgt_intent = kKeepLane; // go back to keep lane
      }
      break;
    }
    case kLaneChangeRight: {
      if ((cur_tgt_lane > ego_lane) && (gap_on_right > kLaneChangeMinGap)) {
        tgt_intent = kLaneChangeRight; // stay in LCR
      }
      else {
        tgt_intent = kKeepLane; // go back to keep lane
      }
      break;
    }
    default: {
      //assert(false);
      tgt_intent = cur_intent; // pass-through, should not occur
    }
  }
  
  return tgt_intent;
}

/**
 * Set behavior target speed based on the target intent
 */
double SetTargetSpeed(const EgoVehicle &ego_car,
                      const std::map<int, DetectedVehicle> &detected_cars,
                      const std::map<int, std::vector<int>> &car_ids_by_lane) {
  
  // Set initial target speed to base target speed parameter
  double target_speed = kTargetSpeed;
  const VehBehavior ego_beh = ego_car.GetTgtBehavior();
  
  // Limit target speed by car ahead in current lane
  target_speed = TargetSpeedKL(target_speed, ego_car, detected_cars,
                               car_ids_by_lane);
  
  // Set target speed if planning or changing lanes with cars in the way
  if (ego_beh.intent == kPlanLaneChangeLeft) {
    target_speed = TargetSpeedPLC(kLeft, target_speed, ego_car, detected_cars,
                                  car_ids_by_lane);
  }
  else if (ego_beh.intent == kPlanLaneChangeRight) {
    target_speed = TargetSpeedPLC(kRight, target_speed, ego_car, detected_cars,
                                  car_ids_by_lane);
  }
  else if (ego_beh.intent == kLaneChangeLeft) {
    target_speed = TargetSpeedLC(kLeft, target_speed, ego_car, detected_cars,
                                 car_ids_by_lane);
  }
  else if (ego_beh.intent == kLaneChangeRight) {
    target_speed = TargetSpeedLC(kRight, target_speed, ego_car, detected_cars,
                                 car_ids_by_lane);
  }
  
  // Final min/max guard
  target_speed = std::max(target_speed, kTgtMinSpeed);
  target_speed = std::min(target_speed, kTargetSpeed);
  
  return target_speed;
}

/**
 * Set target speed for Keep Lane intent (follow car ahead)
 */
double TargetSpeedKL(double base_tgt_spd, const EgoVehicle &ego_car,
                     const std::map<int, DetectedVehicle> &detected_cars,
                     const std::map<int, std::vector<int>> &car_ids_by_lane) {

  double tgt_speed = base_tgt_spd; // default is to pass-through base speed

  // Check for closest car ahead in current lane
  const auto car_ahead = FindCarInLane(kFront, ego_car.GetLane(),
                                       ego_car.GetID(), ego_car, detected_cars,
                                       car_ids_by_lane);
  const int car_id_ahead = std::get<0>(car_ahead);
  const double rel_s_ahead = std::get<1>(car_ahead);
  
  // Set base target speed limited by car ahead
  if (car_id_ahead >= 0) {
    const double dist_ahead = detected_cars.at(car_id_ahead).GetRelS();
    
    if (dist_ahead < kTgtStartFollowDist) {
      
      // Decrease target speed proportionally to distance of car ahead
      const double spd_ahead = detected_cars.at(car_id_ahead).GetState().s_dot;
      const double spd_slope = ((spd_ahead - kTargetSpeed)
                                / (kTgtFollowDist - kTgtStartFollowDist));
      
      tgt_speed = spd_slope * (dist_ahead - kTgtStartFollowDist) + kTargetSpeed;
      
      // Decrease target speed by a fixed decrement offset if too close
      if (dist_ahead < kTgtMinFollowDist) {
        tgt_speed = spd_ahead - kTgtMinFollowSpeedDec;
      }
      
      // Debug logging
      if (kDBGBehavior != 0) {
        std::cout << "\nBase target speed = " << mps2mph(tgt_speed)
        << " mph, car ahead id# " << car_id_ahead << " rel_s = " << rel_s_ahead
        << "m" << std::endl;
      }
    }
  }
  
  return tgt_speed;
}

/**
 * Set target speed for Plan Lane Change Left/Right intents (look for gap)
 */
double TargetSpeedPLC(VehSides sidePLC, double base_tgt_spd,
                      const EgoVehicle &ego_car,
                      const std::map<int, DetectedVehicle> &detected_cars,
                      const std::map<int, std::vector<int>> &car_ids_by_lane) {
  
  double target_speed = base_tgt_spd; // default is to pass-through base speed
  const int check_lane = ego_car.GetLane() + sidePLC;
  const int ego_id = ego_car.GetID();
  
  // Check for closest car ahead in current lane
  const auto car_ahead = FindCarInLane(kFront, ego_car.GetLane(), ego_id,
                                       ego_car, detected_cars, car_ids_by_lane);
  const int car_id_ahead = std::get<0>(car_ahead);
  const double rel_s_ahead = std::get<1>(car_ahead);
  
  // Look for car ahead in lane on PLC side
  const auto car_side_ahead = FindCarInLane(kFront, check_lane, ego_id, ego_car,
                                            detected_cars, car_ids_by_lane);
  const int car_id_side_ahead = std::get<0>(car_side_ahead);
  const double rel_s_side_ahead = std::get<1>(car_side_ahead);
  
  // Look for car behind in lane on PLC side
  const auto car_side_behind = FindCarInLane(kBack, check_lane, ego_id, ego_car,
                                             detected_cars, car_ids_by_lane);
  const int car_id_side_behind = std::get<0>(car_side_behind);
  const double rel_s_side_behind = std::get<1>(car_side_behind);
  
  // Check which cars are close
  bool close_ahead = false;
  bool close_side_ahead = false;
  bool close_side_behind = false;
  if ((detected_cars.count(car_id_ahead) > 0)
      && (rel_s_ahead < kPLCCloseDist)) {
    close_ahead = true;
  }
  if ((detected_cars.count(car_id_side_ahead) > 0)
      && (rel_s_side_ahead < kLaneChangeMinGap)) {
    close_side_ahead = true;
  }
  if ((detected_cars.count(car_id_side_behind) > 0)
      && (abs(rel_s_side_behind) < kLaneChangeMinGap)) {
    close_side_behind = true;
  }
  
  // Slow down to find a gap if car ahead and a car on the side is close
  if ((close_ahead == true)
      && ((close_side_ahead == true) || (close_side_behind == true))) {
    
    target_speed -=  kPLCTgtSpeedDec;
      
    // Debug logging
    if (kDBGBehavior != 0) {
      std::cout << " Over-ride tgt speed for PLC to slow down to find a gap = "
                << mps2mph(target_speed) << " mph" << std::endl;
    }
  }
  // Otherwise, car ahead is not close so keep going ahead at original speed
  
  return target_speed;
}

/**
 * Set target speed for Lane Change Left/Right intents (check speed of car
 * ahead in lane changing into)
 */
double TargetSpeedLC(VehSides sideLC, double base_tgt_spd,
                      const EgoVehicle &ego_car,
                      const std::map<int, DetectedVehicle> &detected_cars,
                      const std::map<int, std::vector<int>> &car_ids_by_lane) {

  double target_speed = base_tgt_spd; // default is to pass-through base speed
  
  const int check_lane = ego_car.GetLane() + sideLC;
  const int ego_id = ego_car.GetID();
  
  // Look for car ahead in lane on LC side
  const auto car_side_ahead = FindCarInLane(kFront, check_lane, ego_id, ego_car,
                                            detected_cars, car_ids_by_lane);
  const int car_id_side_ahead = std::get<0>(car_side_ahead);
  const double rel_s_side_ahead = std::get<1>(car_side_ahead);
  
  // Set target speed to match car ahead in lane that ego is changing to
  if ((detected_cars.count(car_id_side_ahead) > 0)
      && (rel_s_side_ahead < kTgtStartFollowDist)) {
    
    const double spd_side_ahead = detected_cars.at(car_id_side_ahead)
                                  .GetState().s_dot;
    
    target_speed = spd_side_ahead;
    
    // Debug logging
    if (kDBGBehavior != 0) {
      std::cout << " Over-ride tgt speed to car #" << car_id_side_ahead
                << " ahead in lane changing to = " << mps2mph(target_speed)
                << " mph" << std::endl;
    }
  }
  
  return target_speed;
}
