//
//  prediction.cpp
//  Path_Planning
//
//  Created by Student on 1/9/18.
//

#include "prediction.hpp"

void PredictBehavior(std::map<int, DetectedVehicle> &detected_cars,
                     const std::map<int, std::vector<int>> &car_ids_by_lane) {
  
  std::map<int, std::vector<int>>::const_iterator it = car_ids_by_lane.begin();
  while (it != car_ids_by_lane.end()) {
    // Loop through each lane's vector of car id's
    for (int i=0; i < it->second.size(); ++i) {
      int cur_car_id = it->second[i];
      DetectedVehicle* cur_car = &detected_cars[cur_car_id];
      
      // Predict behavior for this detected car
      constexpr double kLatVelLaneChange = 5.0; // mph
      if ((cur_car->intent_ == kKeepLane) && (cur_car->d_dot_ > mph2mps(kLatVelLaneChange))) {
        cur_car->intent_ = kLaneChangeRight;
        
        // DEBUG Print detected lane change
        std::cout << "** Lane change right detected by car #" << cur_car->veh_id_ << std::endl;
      }
      else if ((cur_car->intent_ == kKeepLane) && (cur_car->d_dot_ < mph2mps(-kLatVelLaneChange))) {
        cur_car->intent_ = kLaneChangeLeft;
        
        // DEBUG Print detected lane change
        std::cout << "** Lane change left detected by car #" << cur_car->veh_id_ << std::endl;
      }
      else if (((cur_car->intent_ == kLaneChangeRight)
                 && (cur_car->d_dot_ < mph2mps(kLatVelLaneChange)))
               || ((cur_car->intent_ == kLaneChangeLeft)
                   && (cur_car->d_dot_ > mph2mps(-kLatVelLaneChange)))) {
        cur_car->intent_ = kKeepLane;
        
        // DEBUG Print detected lane change ended
        std::cout << "** End of lane change by car #" << cur_car->veh_id_ << std::endl;
      }
      else if (cur_car->intent_ == kUnknown) {
        cur_car->intent_ = kKeepLane;
      }
    }
    it++;
  }
}

void PredictTrajectory(std::map<int, DetectedVehicle> &detected_cars,
                       const std::map<int, std::vector<int>> &car_ids_by_lane,
                       double predict_time) {
  
}
