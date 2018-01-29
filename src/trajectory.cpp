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

VehTrajectory GetEgoTrajectory(const EgoVehicle &ego_car,
                               const std::map<int, DetectedVehicle> &detected_cars,
                               const std::map<int, std::vector<int>> &car_ids_by_lane,
                               const std::vector<double> &map_interp_s,
                               const std::vector<double> &map_interp_x,
                               const std::vector<double> &map_interp_y) {

  // Initialize random generators
  std::random_device rand_dev;
  std::default_random_engine random_gen(rand_dev());
  std::normal_distribution<double> dist_v(kRandSpdMean, kRandSpdDev);
  std::normal_distribution<double> dist_t(kRandTimeMean, kRandTimeDev);

  // Set start state
  VehState start_state;
  if (ego_car.traj_.states.size() > 0) {
    start_state = ego_car.traj_.states.back();
  }
  else {
    start_state = ego_car.state_;
  }
  
  // Set target time and speed from behavior target
  double t_tgt = ego_car.tgt_behavior_.tgt_time;
  double v_tgt = ego_car.tgt_behavior_.tgt_speed;

  // Set target D based on behavior target lane
  double d_tgt;
  if ((ego_car.tgt_behavior_.tgt_lane > ego_car.lane_)
      && (ego_car.tgt_behavior_.intent == kLaneChangeRight)) {
    // "Lane Change Right"
    d_tgt = tgt_lane2tgt_d(ego_car.lane_ + 1);
  }
  else if ((ego_car.tgt_behavior_.tgt_lane < ego_car.lane_)
           && (ego_car.tgt_behavior_.intent == kLaneChangeLeft)) {
    // "Lane Change Left"
    d_tgt = tgt_lane2tgt_d(ego_car.lane_ - 1);
  }
  else {
    // "Keep Lane" and "Plan Lane Change Left/Right"
    d_tgt = tgt_lane2tgt_d(ego_car.lane_);
  }
  
  // Generate multiple potential trajectories
  std::vector<VehTrajectory> possible_trajs;
  for (int i = 0; i < kTrajGenNum; ++i) {
    
    double v_delta = 0;
    double t_delta = 0;
    // After the 1st base traj, sample some variations in target speed and time
    if (i > 0) {
      v_delta = dist_v(random_gen);
      t_delta = dist_t(random_gen);
    }

    // Calculate initial trajectory with the random deviation
    double t_tgt_var = t_tgt + t_delta; // allow longer time
    double v_tgt_var = v_tgt - v_delta; // allow slower speed
    
    VehTrajectory traj_var = GetTrajectory(start_state, t_tgt_var, v_tgt_var,
                                           d_tgt, kMaxA, map_interp_s,
                                           map_interp_x, map_interp_y);

    // Limit traj for max speed and accel
    auto adj_ratios = CheckTrajFeasibility(traj_var);
    double spd_adj_ratio = adj_ratios[0];
    double a_adj_ratio = adj_ratios[1];
    if ((spd_adj_ratio != 1.0) || (a_adj_ratio != 1.0)) {
      traj_var = GetTrajectory(start_state, t_tgt_var,
                               (v_tgt_var * spd_adj_ratio - kSpdAdjOffset),
                               d_tgt, (kMaxA * a_adj_ratio - kAccAdjOffset),
                               map_interp_s, map_interp_x, map_interp_y);
    }
    
    // Evaluate traj cost using other vehicle predicted paths
    traj_var.cost = EvalTrajCost(traj_var, ego_car, detected_cars);
    
    /*
    if ((i > 0) && (traj_var.cost <= (1.-kCostRandDev))) {
      traj_var.cost += kCostRandDev; // add slight penalty to deviated target trajs
    }
     */
    
    possible_trajs.push_back(traj_var);

    std::cout << "Possible traj# " << i << " t=" << t_tgt_var << " v=" << v_tgt_var << " cost = " << traj_var.cost << std::endl;
  }
  
  // Add backup traj for keeping current lane?

  // Get traj with lowest cost
  VehTrajectory best_traj;
  int best_traj_idx = -1;
  double lowest_cost = std::numeric_limits<double>::max();
  for (int i = 0; i < possible_trajs.size(); ++i) {
    if (possible_trajs[i].cost < lowest_cost) {
      lowest_cost = possible_trajs[i].cost;
      best_traj = possible_trajs[i];
      best_traj_idx = i;
    }
  }
  
  std::cout << "\nBest traj #" << best_traj_idx << " cost = " << lowest_cost << "\n" << std::endl;
  
  /*
  // If lowest risk is too high, check if a backup traj has lower risk
  if (lowest_risk > kTrajRiskLimit) {
    std::cout << "\n *** Target traj TOO RISKY! ***" << std::endl;
    
    // Check a backup traj of keeping current lane
    double backupKL_d_tgt = tgt_lane2tgt_d(ego_car.lane_);
    VehTrajectory backupKL_traj = GetTrajectory(start_state, t_tgt, v_tgt, backupKL_d_tgt, kMaxA,
                                      map_interp_s, map_interp_x, map_interp_y);
    double backupKL_risk = EvalTrajRisk(backupKL_traj, ego_car, detected_cars);
    
    if (backupKL_risk < lowest_risk) {
      std::cout << " *** Keeping current lane has lower risk ***" << std::endl;
      lowest_risk = backupKL_risk;
      d_tgt = backupKL_d_tgt;
      best_traj = backupKL_traj;
    }
   
    // Check a backup traj of hard braking
    double backupHB_v_tgt = kTgtMinSpeed;
    VehTrajectory backupHB_traj = GetTrajectory(start_state, t_tgt, backupHB_v_tgt, d_tgt, kMaxA,
                                              map_interp_s, map_interp_x, map_interp_y);
    double backupHB_risk = EvalTrajRisk(backupHB_traj, ego_car, detected_cars);
    
    if (backupHB_risk < lowest_risk) {
      std::cout << " *** Hard braking has lower risk ***" << std::endl;
      lowest_risk = backupHB_risk;
      v_tgt = backupHB_v_tgt;
      best_traj = backupHB_traj;
    }
    
    // Check a backup traj of LCL
    if (ego_car.lane_ > 1) {
      double backupLCL_d_tgt = tgt_lane2tgt_d(ego_car.lane_ - 1);
      VehTrajectory backupLCL_traj = GetTrajectory(start_state, t_tgt, v_tgt, backupLCL_d_tgt, kMaxA, map_interp_s, map_interp_x, map_interp_y);
      double backupLCL_risk = EvalTrajRisk(backupLCL_traj, ego_car, detected_cars);
      
      if (backupLCL_risk < lowest_risk) {
        std::cout << " *** Lane Change Left has lower risk ***" << std::endl;
        lowest_risk = backupLCL_risk;
        d_tgt = backupLCL_d_tgt;
        best_traj = backupLCL_traj;
      }
    }
    
    // Check a backup traj of LCR
    if (ego_car.lane_ < kNumLanes) {
      double backupLCR_d_tgt = tgt_lane2tgt_d(ego_car.lane_ + 1);
      VehTrajectory backupLCR_traj = GetTrajectory(start_state, t_tgt, v_tgt, backupLCR_d_tgt, kMaxA, map_interp_s, map_interp_x, map_interp_y);
      double backupLCR_risk = EvalTrajRisk(backupLCR_traj, ego_car, detected_cars);
      
      if (backupLCR_risk < lowest_risk) {
        std::cout << " *** Lane Change Right has lower risk ***" << std::endl;
        lowest_risk = backupLCR_risk;
        d_tgt = backupLCR_d_tgt;
        best_traj = backupLCR_traj;
      }
    }
   
  }
  */
  
  /*
  // Check traj feasibility and get adj ratios
  auto adj_ratios = CheckTrajFeasibility(best_traj);
  double spd_adj_ratio = adj_ratios[0];
  double a_adj_ratio = adj_ratios[1];

  std::cout << "              Spd adj: " << spd_adj_ratio << ", A adj: " << a_adj_ratio << std::endl;

  // Recalculate trajectory if needed to prevent over-speed/accel
  if ((spd_adj_ratio != 1.0) || (a_adj_ratio != 1.0)) {
    best_traj = GetTrajectory(start_state, t_tgt,
                              (v_tgt * spd_adj_ratio - kSpdAdjOffset),
                              d_tgt, (kMaxA * a_adj_ratio - kAccAdjOffset),
                              map_interp_s, map_interp_x, map_interp_y);
  }
  */
  
  // DEBUG Recheck final traj for over-speed/accel
  auto debug_junk = CheckTrajFeasibility(best_traj);
  
  return best_traj;
}


/**
 * Check trajectory feasibility for over-speed and over-accel limits
 */
std::vector<double> CheckTrajFeasibility(const VehTrajectory traj) {

  // Check for (x,y) over-speed/accel and return adj ratios to compensate
  double spd_adj_ratio = 1.0;
  double a_adj_ratio = 1.0;
  
  double v_peak = 0;
  double xy_speed = 0;

  double a_peak = 0;
  double xy_accel = 0;
  double ave_speed = 0;
  double ave_speed_prev = 0;
  constexpr int kAccelAveSamples = 10;
  
  for (int i = 1; i < traj.states.size(); ++i) {
    // Check for over-speed
    xy_speed = (Distance(traj.states[i].x, traj.states[i].y,
                         traj.states[i-1].x, traj.states[i-1].y)
                / kSimCycleTime);
    if (xy_speed > v_peak) { v_peak = xy_speed; }
    
    // Check for over-accel
    ave_speed += xy_speed; // accumulate speeds
    if ((i % kAccelAveSamples) == 0) {
      ave_speed = ave_speed / kAccelAveSamples; // calc ave speed
      if (i > kAccelAveSamples) {
        // After prev ave speed was initialized, calculate ave accel
        xy_accel = abs(ave_speed - ave_speed_prev) / (10*kSimCycleTime);
        if (xy_accel > a_peak) { a_peak = xy_accel; }
      }
      ave_speed_prev = ave_speed; // store prev ave speed
      ave_speed = 0; // reset for accumulation
    }
  }
  
  // Calculate adjustment ratios
  if (v_peak > kTargetSpeed) { spd_adj_ratio = kTargetSpeed / v_peak; }
  if (a_peak > kMaxA) { a_adj_ratio = kMaxA / a_peak; }
  
  std::cout << "Traj check: v_peak = " << mps2mph(v_peak) << " mph, a_peak = " << a_peak << std::endl;

  return {spd_adj_ratio, a_adj_ratio};
}

/**
 * Evaluate trajectory's cost based on collision risk and deviation from target
 */
double EvalTrajCost(const VehTrajectory traj, const EgoVehicle &ego_car,
                    const std::map<int, DetectedVehicle> &detected_cars) {
  
  double traj_cost_risk = 0.0;
  double traj_cost_tgtdev = 0.0;
  
  const int idx_start_traj = ego_car.traj_.states.size(); // start after buffer
  double collision_risk_sum = 0.0;

  // Check each time step of the traj to find overlap with other car pred paths
  for (int i = 0; i < traj.states.size(); i += kEvalRiskStep) {
    const double ego_s = traj.states[i].s;
    const double ego_d = traj.states[i].d;
    for (auto it = detected_cars.begin(); it != detected_cars.end(); ++it) {
      
      // Check each predicted path of this detected vehicle at this time step
      DetectedVehicle car = it->second;
      
      for (auto it2 = car.pred_trajs_.begin(); it2 != car.pred_trajs_.end(); ++it2) {
        VehTrajectory car_traj = it2->second;
        
        // Stop if predicted traj is too short
        if ((idx_start_traj+i) > car_traj.states.size()) { break; }
        
        double car_s = car_traj.states[idx_start_traj+i].s;
        double car_d = car_traj.states[idx_start_traj+i].d;
        
        // Check if ego car and other car would be too close at this time step
        if ((abs(ego_s - car_s) < kCollisionSThresh)
            && (abs(ego_d - car_d) < kCollisionDThresh)) {
          // Risk probability with exponential decay over predicted time
          collision_risk_sum += car_traj.probability * exp(-i*kSimCycleTime);
        }
      } // loop to detected car's next predicted path
    } // loop to next detected car
  } // loop to traj's next time step
  traj_cost_risk += kTrajCostRisk * collision_risk_sum;
  
  // Add traj cost based on deviation from base target
  const double t_traj = traj.states.size() * kSimCycleTime;
  const double t_tgtdev = abs(ego_car.tgt_behavior_.tgt_time - t_traj);
  const double v_traj = traj.states.back().s_dot;
  const double v_tgtdev = abs(ego_car.tgt_behavior_.tgt_speed - v_traj);
  traj_cost_tgtdev += kTrajCostDeviation * (t_tgtdev + v_tgtdev);

  std::cout << "  Eval traj cost: risk = " << traj_cost_risk << " tgt_dev = " << traj_cost_tgtdev << std::endl;
  
  return (traj_cost_risk + traj_cost_tgtdev);
}
