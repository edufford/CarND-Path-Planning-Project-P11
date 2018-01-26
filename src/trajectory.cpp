//
//  trajectory.cpp
//  Path_Planning
//
//  Created by Student on 1/12/18.
//

#include "trajectory.hpp"

VehTrajectory GetTrajectory(VehState start_state, double t_tgt,
                            double v_tgt, double d_tgt, double a_tgt,
                            const std::vector<double> &map_interp_s,
                            const std::vector<double> &map_interp_x,
                            const std::vector<double> &map_interp_y) {
 
  /*
  if (t_tgt < kNewPathTime) {
    std::cout << "decel" << std::endl;
  }
  */
  
  VehTrajectory new_traj;
  
  // Generate S trajectory
  
  double s_est;
  double s_dot_est;
  double s_dot_dot_est;
  
  // Set estimated s, s_dot, s_dot_dot to keep a reasonable JMT with basic kinematics
  const double t_maxa = abs(v_tgt - start_state.s_dot) / a_tgt;
  double a_signed = (v_tgt > start_state.s_dot) ? a_tgt: -a_tgt;
  if (t_maxa > t_tgt) {
    // Cut off target v and a to limit t
    s_dot_est = start_state.s_dot + a_signed * t_tgt;
    s_dot_dot_est = a_signed;
  }
  else {
    // Can achieve target speed in time
    s_dot_est = v_tgt;
    s_dot_dot_est = (s_dot_est - start_state.s_dot) / t_tgt;
  }
  
  s_est = start_state.s + start_state.s_dot*t_tgt + 0.5*s_dot_dot_est*sq(t_tgt);
  
  std::vector<double> start_state_s = {start_state.s, start_state.s_dot, start_state.s_dotdot};
  std::vector<double> end_state_s = {s_est, s_dot_est, s_dot_dot_est};

  new_traj.coeffs_JMT_s = JMT(start_state_s, end_state_s, t_tgt);
  new_traj.coeffs_JMT_s_dot = DiffPoly(new_traj.coeffs_JMT_s);
  new_traj.coeffs_JMT_s_dotdot = DiffPoly(new_traj.coeffs_JMT_s_dot);
  
  // Generate D trajectory
  
  double d_est = d_tgt;
  double d_dot_est = 0;
  double d_dot_dot_est = 0;
  
  std::vector<double> start_state_d = {start_state.d, start_state.d_dot,
                                       start_state.d_dotdot};
  std::vector<double> end_state_d = {d_est, d_dot_est, d_dot_dot_est};
  
  new_traj.coeffs_JMT_d = JMT(start_state_d, end_state_d, t_tgt);
  new_traj.coeffs_JMT_d_dot = DiffPoly(new_traj.coeffs_JMT_d);
  new_traj.coeffs_JMT_d_dotdot = DiffPoly(new_traj.coeffs_JMT_d_dot);
  
  // Look up (s,d) vals for each sim cycle time step and convert to raw (x,y)
  const int num_pts = t_tgt / kSimCycleTime;

  for (int i = 1; i < num_pts; ++i) {
    double t = i * kSimCycleTime; // idx 0 is 1st point ahead of car
    
    VehState state;
    state.s = std::fmod(EvalPoly(t, new_traj.coeffs_JMT_s), kMaxS);
    state.s_dot = EvalPoly(t, new_traj.coeffs_JMT_s_dot);
    state.s_dotdot = EvalPoly(t, new_traj.coeffs_JMT_s_dotdot);
    state.d = EvalPoly(t, new_traj.coeffs_JMT_d);
    state.d_dot = EvalPoly(t, new_traj.coeffs_JMT_d_dot);
    state.d_dotdot = EvalPoly(t, new_traj.coeffs_JMT_d_dotdot);

    std::vector<double> state_xy = GetHiResXY(state.s, state.d, map_interp_s,
                                              map_interp_x, map_interp_y);
    
    state.x = state_xy[0];
    state.y = state_xy[1];
    
    // Check for min (x,y) dist from prev point, re-push prev point if too small
    if (i > 1) {
      int prev_idx = new_traj.states.size()-1;
      const double dist_pnt = Distance(state.x, state.y,
                                      new_traj.states[prev_idx].x,
                                      new_traj.states[prev_idx].y);
      
      if (dist_pnt < kMinTrajPntDist) {
        state = new_traj.states[prev_idx];
      }
    }
    
    new_traj.states.push_back(state);
  }
  
  /*
  // DEBUG Check (x,y)-(s,d) conversion accuracy
  std::vector<double> car_sd = GetHiresFrenet(ego_car.state_.x,
                                              ego_car.state_.y,
                                              map_interp_s,
                                              map_interp_x,
                                              map_interp_y);
  
  std::vector<double> car_xy = GetHiresXY(car_sd[0], car_sd[1], map_interp_s,
                                          map_interp_x, map_interp_y);
  
  std::cout << "x: " << ego_car.state_.x << ", y: " << ego_car.state_.y
            << ", xy->s: " << car_sd[0] << ", xy->d: " << car_sd[1]
            << ", sd->x: " << car_xy[0] << ", sd->y: " << car_xy[1];
  std::cout << "" << std::endl;
  */
  
  return new_traj;
}

VehTrajectory GetEgoTrajectory(EgoVehicle &ego_car,
                               const std::map<int, DetectedVehicle> &detected_cars,
                               const std::map<int, std::vector<int>> &car_ids_by_lane,
                               const std::vector<double> &map_interp_s,
                               const std::vector<double> &map_interp_x,
                               const std::vector<double> &map_interp_y) {
  
  // Set start state
  VehState start_state;
  if (ego_car.traj_.states.size() > 0) {
    start_state = ego_car.traj_.states.back();
  }
  else {
    start_state = ego_car.state_;
  }
  
  // Set target time and speed
  double t_tgt = ego_car.tgt_behavior_.tgt_time;
  
  double v_tgt;
  if (ego_car.tgt_behavior_.intent == kPlanLaneChangeLeft) {
    // Set target speed a little slower than car behind on left
    auto car_behind = GetCarBehindInLane(kLeft, ego_car.veh_id_, ego_car,
                                         detected_cars, car_ids_by_lane);
    int car_id_behind = std::get<0>(car_behind);
    //double rel_s_behind = std::get<1>(car_behind);
    if (detected_cars.count(car_id_behind) > 0) {
      v_tgt = detected_cars.at(car_id_behind).state_.s_dot - kTgtSpeedDec;
    }
    else {
      v_tgt = ego_car.tgt_behavior_.tgt_speed;
    }
  }
  else if (ego_car.tgt_behavior_.intent == kPlanLaneChangeRight) {
    // Set target speed a little slower than car behind on right
    auto car_behind = GetCarBehindInLane(kRight, ego_car.veh_id_, ego_car,
                                         detected_cars, car_ids_by_lane);
    int car_id_behind = std::get<0>(car_behind);
    //double rel_s_behind = std::get<1>(car_behind);
    if (detected_cars.count(car_id_behind) > 0) {
      v_tgt = detected_cars.at(car_id_behind).state_.s_dot - kTgtSpeedDec;
    }
    else {
      v_tgt = ego_car.tgt_behavior_.tgt_speed;
    }
  }
  else {
    v_tgt = ego_car.tgt_behavior_.tgt_speed;
  }
  
  // Set target D based on target lane
  double d_tgt;
  if ((ego_car.tgt_behavior_.tgt_lane > ego_car.lane_)
      && (ego_car.tgt_behavior_.intent == kLaneChangeRight)) {
    // Lane Change Right
    d_tgt = tgt_lane2tgt_d(ego_car.lane_ + 1);
  }
  else if ((ego_car.tgt_behavior_.tgt_lane < ego_car.lane_)
           && (ego_car.tgt_behavior_.intent == kLaneChangeLeft)) {
    // Lane Change Left
    d_tgt = tgt_lane2tgt_d(ego_car.lane_ - 1);
  }
  else {
    // Keep Lane and Plan Lane Change Left/Right
    d_tgt = tgt_lane2tgt_d(ego_car.lane_);
  }
  
  // Calculate initial trajectory
  VehTrajectory traj = GetTrajectory(start_state, t_tgt, v_tgt, d_tgt, kMaxA,
                                     map_interp_s, map_interp_x, map_interp_y);

  /*
  // DEBUG Check (x,y)-(s,d) conversion accuracy
  std::vector<double> car_sd = GetHiResFrenet(start_state.x,
                                              start_state.y,
                                              map_interp_s,
                                              map_interp_x,
                                              map_interp_y);
  
  std::vector<double> car_xy = GetHiResXY(car_sd[0], car_sd[1], map_interp_s,
                                          map_interp_x, map_interp_y);
  
  std::cout << "car x: " << ego_car.state_.x << ", car y: " << ego_car.state_.y
  << ", start x: " << start_state.x << ", start y: " << start_state.y
  << ", xy->s: " << car_sd[0] << ", xy->d: " << car_sd[1]
  << ", sd->x: " << car_xy[0] << ", sd->y: " << car_xy[1];
  std::cout << "" << std::endl;
  */
  
  // Check for (x,y) over-speed/accel and recalculate trajectory to compensate
  double v_peak = 0;
  double xy_speed = 0;
  double ave_speed = 0;
  double ave_speed_prev = 0;
  double a_peak = 0;
  double xy_accel = 0;
  for (int i = 1; i < traj.states.size(); ++i) {
    // Check for over-speed
    const double xy_dist = Distance(traj.states[i].x, traj.states[i].y,
                                    traj.states[i-1].x, traj.states[i-1].y);
    xy_speed = xy_dist / kSimCycleTime;

    // Check for over-accel
    constexpr int kAveSamples = 10;
    if ((i > 1) && ((i % kAveSamples) == 0)) {
      ave_speed = 0.;
      for (int j = 0; j < kAveSamples; ++j) {
        ave_speed += (Distance(traj.states[i - j].x, traj.states[i - j].y,
                               traj.states[i-1 - j].x, traj.states[i-1 - j].y)
                      / kSimCycleTime);
      }
      ave_speed = ave_speed / kAveSamples;
      if (i == kAveSamples) { ave_speed_prev = ave_speed; }
      xy_accel = (ave_speed - ave_speed_prev) / (10*kSimCycleTime);
    }
    
    // Update peak values
    if (xy_speed > v_peak) { v_peak = xy_speed; }
    if (abs(xy_accel) > a_peak) { a_peak = abs(xy_accel); }
    ave_speed_prev = ave_speed;
  }
  
  //std::cout << "Before: v_peak = " << mps2mph(v_peak) << " mph, a_peak = " << a_peak << std::endl;
  
  // Recalculate trajectory if needed to prevent over-speed/accel
  if ((v_peak > kTargetSpeed) || (a_peak > kMaxA)) {
    double spd_adj_ratio = 1.0;
    double a_adj_ratio = 1.0;
    if (v_peak > kTargetSpeed) { spd_adj_ratio = (kTargetSpeed / v_peak); }
    if (a_peak > kMaxA) { a_adj_ratio = (kMaxA / a_peak); }
    
    traj = GetTrajectory(start_state, t_tgt,
                         (v_tgt * spd_adj_ratio - kSpdAdjOffset),
                         d_tgt, (kMaxA * a_adj_ratio), map_interp_s,
                         map_interp_x, map_interp_y);

    //std::cout << "Spd adj: " << spd_adj_ratio << ", A adj: " << a_adj_ratio << std::endl;
  }
  
  /*
  // DEBUG Recheck for over-speed/accel
  v_peak = 0;
  xy_speed = 0;
  ave_speed = 0;
  ave_speed_prev = 0;
  a_peak = 0;
  xy_accel = 0;
  for (int i = 1; i < traj.states.size(); ++i) {
    // Check for over-speed
    const double xy_dist = Distance(traj.states[i].x, traj.states[i].y,
                                    traj.states[i-1].x, traj.states[i-1].y);
    xy_speed = xy_dist / kSimCycleTime;
    
    // Check for over-accel
    constexpr int kAveSamples = 10;
    if ((i > 1) && ((i % kAveSamples) == 0)) {
      ave_speed = 0.;
      for (int j = 0; j < kAveSamples; ++j) {
        ave_speed += (Distance(traj.states[i - j].x, traj.states[i - j].y,
                               traj.states[i-1 - j].x, traj.states[i-1 - j].y)
                      / kSimCycleTime);
      }
      ave_speed = ave_speed / kAveSamples;
      if (i == kAveSamples) { ave_speed_prev = ave_speed; }
      xy_accel = (ave_speed - ave_speed_prev) / (10*kSimCycleTime);
    }
    
    // Update peak values
    if (xy_speed > v_peak) { v_peak = xy_speed; }
    if (abs(xy_accel) > a_peak) { a_peak = abs(xy_accel); }
    ave_speed_prev = ave_speed;
  }
  //std::cout << "After: v_peak = " << mps2mph(v_peak) << " mph, a_peak = " << a_peak << std::endl;
  */
  
  return traj;
}
