//
//  trajectory.cpp
//  Path_Planning
//
//  Created by Student on 1/12/18.
//

#include "trajectory.hpp"

/**
 * Use a part of the previous ego trajectory as the start of the next
 * trajectory as a buffer to keep smooth continuity for communication between
 * the path planner and the simulator driving the points.
 * Returns the buffer portion set of states of the previous trajectory, or an
 * empty trajectory if the current index is at 0.
 */
VehTrajectory GetBufferTrajectory(int idx_current_pt,
                                  VehTrajectory prev_ego_traj) {
  
  VehTrajectory traj_prev_buffer;
  int buffer_pts = kPathBufferTime / kSimCycleTime;
  if (idx_current_pt > 0) {
    int end_pt = std::min(idx_current_pt+1 + buffer_pts,
                          int(prev_ego_traj.states.size()));
    for (int i = idx_current_pt+1; i < end_pt; ++i) {
      traj_prev_buffer.states.push_back(prev_ego_traj.states[i]);
    }
  }
  
  return traj_prev_buffer;
}

/**
 * Get a new trajectory for the ego car with target end state based on the
 * target behavior.  Multiple trajectories are generated including the base
 * target and additional variations for slower speed and longer time.  Each
 * trajectory is checked for over-speed/accel in (x,y) coordinates and adjusted
 * to stay within limits.  Each trajectory is evaluated for cost based on
 * accumulated risk of collision with nearby car predicted paths and amount of
 * deviation from the base target.  Trajectories with cost above a risk
 * threshold are discarded. A backup trajectory of keeping lane at a slower
 * speed is included if all other trajectories exceeded the risk threshold.
 */
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
  const double t_tgt = ego_car.tgt_behavior_.tgt_time;
  const double v_tgt = ego_car.tgt_behavior_.tgt_speed;
  const double a_tgt = kMaxA;

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
    const double t_tgt_var = t_tgt + t_delta; // allow longer time
    const double v_tgt_var = v_tgt - v_delta; // allow slower speed
    
    VehTrajectory traj_var = GetTrajectory(start_state, t_tgt_var, v_tgt_var,
                                           d_tgt, a_tgt, map_interp_s,
                                           map_interp_x, map_interp_y);

    // Limit traj for max speed and accel
    auto adj_ratios = CheckTrajFeasibility(traj_var);
    const double spd_adj_ratio = adj_ratios[0];
    const double a_adj_ratio = adj_ratios[1];
    if ((spd_adj_ratio != 1.0) || (a_adj_ratio != 1.0)) {
      traj_var = GetTrajectory(start_state, t_tgt_var,
                               (v_tgt_var * spd_adj_ratio - kSpdAdjOffset),
                               d_tgt, (a_tgt * a_adj_ratio - kAccAdjOffset),
                               map_interp_s, map_interp_x, map_interp_y);
    }

    // Debug logging
    if (kDBGTrajectory != 0) {
      std::cout << "Possible traj# " << i << " t=" << t_tgt_var
                << " v=" << mps2mph(v_tgt_var) << std::endl;
    }

    // Evaluate traj cost using other vehicle predicted paths
    traj_var.cost = EvalTrajCost(traj_var, ego_car, detected_cars);

    // Only keep traj's with cost below thresh
    if (traj_var.cost < kTrajCostThresh) {
      possible_trajs.push_back(traj_var);
    }
  }
  
  // Add backup traj to keep current D if all possible traj's were too risky
  if (possible_trajs.size() == 0) {
    const double d_backup = tgt_lane2tgt_d(ego_car.lane_);
    const double v_backup = v_tgt - kMinFollowTgtSpeedDec;
    VehTrajectory traj_backup = GetTrajectory(start_state, t_tgt, v_backup,
                                              d_backup, a_tgt, map_interp_s,
                                              map_interp_x, map_interp_y);
    
    // Limit traj for max speed and accel
    auto adj_ratios = CheckTrajFeasibility(traj_backup);
    const double spd_adj_ratio = adj_ratios[0];
    const double a_adj_ratio = adj_ratios[1];
    if ((spd_adj_ratio != 1.0) || (a_adj_ratio != 1.0)) {
      traj_backup = GetTrajectory(start_state, t_tgt,
                                  (v_backup * spd_adj_ratio - kSpdAdjOffset),
                                  d_backup,
                                  (a_tgt * a_adj_ratio - kAccAdjOffset),
                                  map_interp_s, map_interp_x, map_interp_y);
    }
    
    // Debug logging
    if (kDBGTrajectory != 0) {
      std::cout << "All traj's are too risky!  Use backup traj to keep D = "
                << d_backup << std::endl;
    }
    
    traj_backup.cost = EvalTrajCost(traj_backup, ego_car, detected_cars);
    possible_trajs.push_back(traj_backup);
  }
  
  // Get traj with lowest cost
  VehTrajectory best_traj;
  int best_traj_idx = -1;
  double lowest_cost = std::numeric_limits<double>::max();
  for (int i = 0; i < possible_trajs.size(); ++i) {
    if (possible_trajs[i].cost < lowest_cost) {
      best_traj_idx = i;
      lowest_cost = possible_trajs[i].cost;
      best_traj = possible_trajs[i];
    }
  }
  
  // Debug logging
  if (kDBGTrajectory != 0) {
    std::cout << "\nBest traj #" << best_traj_idx << " cost = " << lowest_cost
              << "\n" << std::endl;
  }
  
  return best_traj;
}

/**
 * Get a trajectory for a specified start state and a target time, speed,
 * Frenet d value, and accel using JMT and basic kinematic estimations.  The
 * JMT is applied to Frenet s and d coordinates separately, and then the (s,d)
 * points are converted to (x,y) points.  The (x,y) points are also checked for
 * a minimum separation distance and filtered to prevent low speed jitter.
 */
VehTrajectory GetTrajectory(VehState start_state, double t_tgt,
                            double v_tgt, double d_tgt, double a_tgt,
                            const std::vector<double> &map_interp_s,
                            const std::vector<double> &map_interp_x,
                            const std::vector<double> &map_interp_y) {
  
  VehTrajectory new_traj;
  
  //// Generate S trajectory ////
  
  double s_est;
  double s_dot_est;
  double s_dotdot_est;
  
  // Estimate s, s_dot, s_dot_dot with basic kinematics approximating with
  // constant accel to keep a reasonable JMT
  const double t_maxa = abs(v_tgt - start_state.s_dot) / a_tgt;
  const double a_signed = (v_tgt > start_state.s_dot) ? a_tgt : -a_tgt;
  if (t_maxa > t_tgt) {
    // Cut off target v and a to limit t
    s_dot_est = start_state.s_dot + a_signed * t_tgt;
    s_dotdot_est = a_signed;
  }
  else {
    // Can achieve target speed in time
    s_dot_est = v_tgt;
    s_dotdot_est = (s_dot_est - start_state.s_dot) / t_tgt;
  }
  s_est = start_state.s + start_state.s_dot*t_tgt + 0.5*s_dotdot_est*sq(t_tgt);
  
  std::vector<double> start_state_s = {start_state.s, start_state.s_dot,
                                       start_state.s_dotdot};
  std::vector<double> end_state_s = {s_est, s_dot_est, s_dotdot_est};
  
  auto coeffs_JMT_s = JMT(start_state_s, end_state_s, t_tgt);
  auto coeffs_JMT_s_dot = DiffPoly(coeffs_JMT_s);
  auto coeffs_JMT_s_dotdot = DiffPoly(coeffs_JMT_s_dot);
  
  //// Generate D trajectory ////
  
  const double d_est = d_tgt;
  const double d_dot_est = 0; // finish lane change by end of traj
  const double d_dotdot_est = 0; // finish lane change by end of traj
  
  std::vector<double> start_state_d = {start_state.d, start_state.d_dot,
                                       start_state.d_dotdot};
  std::vector<double> end_state_d = {d_est, d_dot_est, d_dotdot_est};
  
  auto coeffs_JMT_d = JMT(start_state_d, end_state_d, t_tgt);
  auto coeffs_JMT_d_dot = DiffPoly(coeffs_JMT_d);
  auto coeffs_JMT_d_dotdot = DiffPoly(coeffs_JMT_d_dot);
  
  // Look up (s,d) vals for each sim cycle time step and convert to (x,y)
  const int num_pts = t_tgt / kSimCycleTime;
  double t;
  for (int i = 1; i < num_pts; ++i) {
    t = i * kSimCycleTime; // idx 0 is 1st point ahead of car, start from i = 1
    
    VehState state;
    state.s = std::fmod(EvalPoly(t, coeffs_JMT_s), kMaxS);
    state.s_dot = EvalPoly(t, coeffs_JMT_s_dot);
    state.s_dotdot = EvalPoly(t, coeffs_JMT_s_dotdot);
    state.d = EvalPoly(t, coeffs_JMT_d);
    state.d_dot = EvalPoly(t, coeffs_JMT_d_dot);
    state.d_dotdot = EvalPoly(t, coeffs_JMT_d_dotdot);
    
    std::vector<double> state_xy = GetHiResXY(state.s, state.d, map_interp_s,
                                              map_interp_x, map_interp_y);
    state.x = state_xy[0];
    state.y = state_xy[1];
    
    // Check for min (x,y) dist from prev point, re-push prev point if too small
    if (i > 1) {
      const int prev_idx = new_traj.states.size() - 1; // last pushed point
      const double dist_pnt = Distance(state.x, state.y,
                                       new_traj.states[prev_idx].x,
                                       new_traj.states[prev_idx].y);
      if (dist_pnt < kMinTrajPntDist) {
        state = new_traj.states[prev_idx]; // set state back to copy of previous
      }
    }
    
    new_traj.states.push_back(state);
  }
  
  return new_traj;
}


/**
 * Check trajectory feasibility for over-speed and over-accel limits.  Return
 * adjustment ratios based on the amount of over-limit.
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
  
  // Loop through each point in traj starting from 2nd point
  for (int i = 1; i < traj.states.size(); ++i) {
    
    // Check for over-speed from previous point
    xy_speed = (Distance(traj.states[i].x, traj.states[i].y,
                         traj.states[i-1].x, traj.states[i-1].y)
                / kSimCycleTime);
    if (xy_speed > v_peak) { v_peak = xy_speed; }
    
    // Check for over-accel with ave accel sampled over kAccelAveSamples points
    ave_speed += xy_speed; // accumulate speeds
    if ((i % kAccelAveSamples) == 0) {
      ave_speed = ave_speed / kAccelAveSamples; // calc ave speed
      if (i > kAccelAveSamples) {
        // After prev ave speed was initialized, calculate ave accel
        xy_accel = (abs(ave_speed - ave_speed_prev)
                    / (kAccelAveSamples * kSimCycleTime));
        if (xy_accel > a_peak) { a_peak = xy_accel; }
      }
      ave_speed_prev = ave_speed; // store prev ave speed
      ave_speed = 0; // reset for accumulation
    }
  }
  
  // Calculate adjustment ratios
  if (v_peak > kTargetSpeed) { spd_adj_ratio = kTargetSpeed / v_peak; }
  if (a_peak > kMaxA) { a_adj_ratio = kMaxA / a_peak; }
  
  // Debug logging
  if (kDBGTrajectory != 0) {
    std::cout << "Traj check: v_peak = " << mps2mph(v_peak)
              << " mph, a_peak = " << a_peak << std::endl;
  }
  
  return {spd_adj_ratio, a_adj_ratio};
}

/**
 * Evaluate trajectory's cost based on collision risk and deviation from target
 */
double EvalTrajCost(const VehTrajectory traj, const EgoVehicle &ego_car,
                    const std::map<int, DetectedVehicle> &detected_cars) {
  
  double traj_cost_risk = 0.0;
  double traj_cost_tgtdev = 0.0;
  double collision_risk_sum = 0.0;

  // Set start index to start checking other car's predicted paths after ego's
  // prev path buffer where the new trajectory will start
  const int idx_start_traj = ego_car.traj_.states.size();

  // Check each time step of the traj to find overlap with other car pred paths
  for (int i = 0; i < traj.states.size(); i += kEvalRiskStep) {
    const double ego_s = traj.states[i].s;
    const double ego_d = traj.states[i].d;
    for (auto it = detected_cars.begin(); it != detected_cars.end(); ++it) {
      
      // Check each predicted path of this detected vehicle at this time step
      DetectedVehicle car = it->second;
      
      for (auto it2 = car.pred_trajs_.begin();
                it2 != car.pred_trajs_.end(); ++it2) {
        VehTrajectory car_traj = it2->second;
        
        // Stop if predicted traj is too short
        if ((idx_start_traj+i) > car_traj.states.size()) { break; }
        
        const double car_s = car_traj.states[idx_start_traj+i].s;
        const double car_d = car_traj.states[idx_start_traj+i].d;
        
        // Check if ego car and other car would be too close at this time step
        if ((abs(ego_s - car_s) < kCollisionSThresh)
            && (abs(ego_d - car_d) < kCollisionDThresh)) {
          
          // Risk probability with exponential decay over predicted time
          collision_risk_sum += car_traj.probability * exp(-i * kSimCycleTime);
        }
      } // loop to detected car's next predicted path
    } // loop to next detected car
  } // loop to traj's next time step
  
  // Apply cost function factor to risk sum
  traj_cost_risk += kTrajCostRisk * collision_risk_sum;
  
  // Add traj cost based on deviation from base target
  const double t_traj = traj.states.size() * kSimCycleTime;
  const double t_tgtdev = abs(ego_car.tgt_behavior_.tgt_time - t_traj);
  const double v_traj = traj.states.back().s_dot;
  const double v_tgtdev = abs(ego_car.tgt_behavior_.tgt_speed - v_traj);
  traj_cost_tgtdev += kTrajCostDeviation * (t_tgtdev + v_tgtdev);

  // Debug logging
  if (kDBGTrajectory != 0) {
    std::cout << "  Eval traj cost: risk = " << traj_cost_risk
              << " tgt_dev = " << traj_cost_tgtdev << std::endl;
  }
  
  return (traj_cost_risk + traj_cost_tgtdev);
}
