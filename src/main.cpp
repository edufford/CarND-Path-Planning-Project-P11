#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"

#include "path_common.hpp"
#include "vehicle.hpp"
#include "sensor_fusion.hpp"
#include "prediction.hpp"
#include "behavior.hpp"
#include "trajectory.hpp"

//using namespace std;

// for convenience
using json = nlohmann::json;

/**
 * Checks if the SocketIO event has JSON data.
 * If there is data the JSON object in string format will be returned,
 * else the empty string "" will be returned.
 */
std::string hasData(std::string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != std::string::npos) {
    return "";
  } else if (b1 != std::string::npos && b2 != std::string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

/**
 * Debug print output of the road lanes with detected vehicle positions
 */
void DebugPrintRoad(const std::map<int, DetectedVehicle> &detected_cars,
                    const EgoVehicle &ego_car) {
  
  std::cout << std::endl;
  std::string lane_mark;
  
  for (double i = kSensorRange; i > -kSensorRange; i = i - 10) {
    for (int j_lane = 1; j_lane <= kNumLanes; ++j_lane) {
      std::cout << "|";
      lane_mark = "  ";
      
      if ((i == 0) && (j_lane == ego_car.lane_)) {
        lane_mark = "@@"; // mark ego car
      }
      else {
        // Detected cars as map
        for (auto it = detected_cars.begin(); it != detected_cars.end(); ++it) {
          if ((it->second.s_rel_ <= i+4) && (it->second.s_rel_ > i-6) &&
              (it->second.lane_ == j_lane)) {
            if (it->second.veh_id_ < 10) {
              lane_mark = "0" + std::to_string(it->second.veh_id_);
            }
            else {
              lane_mark = std::to_string(it->second.veh_id_);
            }
          }
        }
      }
      std::cout << lane_mark;
    }
    std::cout << "|" << std::endl;
  }
  std::cout << std::endl;
}

/**
 * Main loop
 */
int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and dx,dy normal vectors
  std::vector<double> map_x_raw;
  std::vector<double> map_y_raw;
  std::vector<double> map_s_raw;
  std::vector<double> map_dx_raw;
  std::vector<double> map_dy_raw;
  
  std::string map_file_ = "../../data/highway_map.csv";

  std::ifstream in_map_(map_file_.c_str(), std::ifstream::in);
  std::string line;
  while (getline(in_map_, line)) {
  	std::istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_x_raw.push_back(x);
  	map_y_raw.push_back(y);
  	map_s_raw.push_back(s);
  	map_dx_raw.push_back(d_x);
  	map_dy_raw.push_back(d_y);
  }
  
  // Reinterpolate map waypoints
  auto waypts_interp = InterpolateMap(map_s_raw, map_x_raw, map_y_raw,
                                      map_dx_raw, map_dy_raw, kMapInterpInc);
  
  /*
  // DEBUG
  std::cout << "** Map interpolation for s, x, y, dx, dy **" << std::endl;
  for (int i = 0; i < waypts_interp.size(); ++i) {
    std::cout << "Map " << i << ":" << std::endl;
    for (int j = 0; j < waypts_interp[i].size(); ++j) {
      std::cout << waypts_interp[i][j] << std::endl;
    }
    std::cout << std::endl;
  }
  */
  
  // Instantiate ego car object and map of detected cars to hold their data
  EgoVehicle ego_car = EgoVehicle(-1);
  std::map<int, DetectedVehicle> detected_cars;
  long int loop = 0;
  auto t_last = std::chrono::time_point_cast<std::chrono::milliseconds>
                (std::chrono::high_resolution_clock::now())
                .time_since_epoch().count();
  
  h.onMessage([&loop, &t_last, &waypts_interp, &ego_car, &detected_cars]
              (uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
                
    // Log time at start of processing received data
    auto t_msg = std::chrono::time_point_cast<std::chrono::milliseconds>
                (std::chrono::high_resolution_clock::now())
                .time_since_epoch().count();
                
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {
      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        std::string event = j[0].get<std::string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
          // Main car's localization Data
          const double car_x = j[1]["x"];
          const double car_y = j[1]["y"];
          //const double car_s = j[1]["s"];
          //const double car_d = j[1]["d"];
          //const double car_yaw = j[1]["yaw"];
          //const double car_speed = j[1]["speed"];

          // Previous path data given to the Planner
          const auto previous_path_x = j[1]["previous_path_x"];
          const auto previous_path_y = j[1]["previous_path_y"];
          
          // Previous path's end s and d values
          //const double end_path_s = j[1]["end_path_s"];
          //const double end_path_d = j[1]["end_path_d"];

          // List of detected cars on same side of road
          const auto sensor_fusion = j[1]["sensor_fusion"];
          
          std::vector<double> map_interp_s = waypts_interp[0];
          std::vector<double> map_interp_x = waypts_interp[1];
          std::vector<double> map_interp_y = waypts_interp[2];
          std::vector<double> map_interp_dx = waypts_interp[3];
          std::vector<double> map_interp_dy = waypts_interp[4];
          
          // DEBUG Log raw car (x,y) values every cycle
          if (kDBGMain == 3) {
            std::cout << "t: " << t_msg << ", x: " << car_x
                      << ", y: " << car_y << std::endl;
          }
          
          // Run path planning algorithm at slower cycle time
          if ((t_msg - t_last) > kPathCycleTimeMS) {
            
            if (kDBGMain != 0) {
              std::cout << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << std::endl;
              std::cout << "Loop #" << loop << ", t=" << t_msg
                        << " ms" << std::endl << std::endl;
            }
            t_last = t_msg;
            loop++;
            
            /**
             * Sensor Fusion
             *   1. Update ego car's state
             *   2. Process detected cars within sensor range
             *   3. Sort detected cars to start from farthest ahead of ego car
             * Output:
             *   ego_car, detected_cars
             */
            
            // Store prev ego traj and find current idx from prev processed path
            VehTrajectory prev_ego_traj = ego_car.traj_;
            const int prev_path_size = previous_path_x.size();
            
            const int idx_current_pt = GetCurrentTrajIndex(prev_ego_traj,
                                                           prev_path_size);
            
            // Process ego car's state
            VehState new_ego_state = ProcessEgoState(car_x, car_y,
                                                     idx_current_pt,
                                                     prev_ego_traj,
                                                     map_interp_s,
                                                     map_interp_x,
                                                     map_interp_y);
            ego_car.UpdateState(new_ego_state);
            
            // Process detected cars' states
            ProcessDetectedCars(detected_cars, ego_car, sensor_fusion,
                                map_interp_s, map_interp_x, map_interp_y,
                                map_interp_dx, map_interp_dy);
            
            // Group detected car id's in a map by lane #
            auto car_ids_by_lane = SortDetectedCarsByLane(detected_cars);
            
            
            /**
             * Prediction
             *   1. Predict detected car behaviors and sort by lane
             *   2. Predict trajectories for each veh over fixed time horizon
             * Output:
             *   detected_cars, car_ids_by_lane
             */
            
            PredictBehavior(detected_cars, ego_car, car_ids_by_lane,
                            map_interp_s, map_interp_x, map_interp_y);
            
            /**
             * Behavior Planning
             *   1. Use FSM to decide intent ("KL", "PLCL", "PLCR", "LCL", "LCR")
             *   Example criteria to decide lane change:
             *     Check distance and speed of current preceding vehicle,
             *     Find lane with fastest preceding vehicles,
             *     Find lane with farthest preceding vehicles
             * Output:
             *   car_target_behavior [FSM state, car ahead, target lane, target time to achieve target]
             */
            
            // Set target lane by cost function
            int prev_tgt_lane = ego_car.tgt_behavior_.tgt_lane;
            ego_car.tgt_behavior_.tgt_lane = LaneCostFcn(ego_car, detected_cars,
                                                         car_ids_by_lane);
            
            // Set target intent based on target lane
            ego_car.tgt_behavior_.intent = BehaviorFSM(ego_car, detected_cars,
                                                       car_ids_by_lane);
            
            // Set target time
            ego_car.tgt_behavior_.tgt_time = kNewPathTime;

            // Set target speed
            ego_car.tgt_behavior_.tgt_speed = SetTargetSpeed(ego_car,
                                                             detected_cars,
                                                             car_ids_by_lane);
            
            // Update frequent lane change counter
            ego_car.counter_lane_change = UpdateCounterLC(ego_car,
                                                          prev_tgt_lane);
            
            
            /**
             * Trajectory Generation
             *   1. Generate potential end states for target behavior with perturbations
             *   2. Generate potential JMT trajectories for each end state
             *   3. Evaluate trajectories with cost function to select best trajectory
             *       (keep following distance, prevent collisions, keep lane center,
             *        minimize lateral jerk, etc)
             *   4. Rate limit best trajectory
             *   5. Convert best trajectory from (s,d) to (x,y)
             * Output:
             *   next_xy_vals [trajectory_x, trajectory_y]
             */
            
            // Keep prev path buffer states and append new trajectory after that
            ego_car.traj_.states.clear();
            ego_car.traj_ = GetBufferTrajectory(idx_current_pt, prev_ego_traj);
            
            VehTrajectory new_traj = GetEgoTrajectory(ego_car, detected_cars,
                                                      car_ids_by_lane,
                                                      map_interp_s,
                                                      map_interp_x,
                                                      map_interp_y);
            
            for (int i = 0; i < new_traj.states.size(); ++i) {
              ego_car.traj_.states.push_back(new_traj.states[i]);
            }
            
            
            /**
             * Control
             *   1. Pack and send JSON path trajectory coordinates
             */

            // Pack path vectors of x and y coordinates
            std::vector<double> next_x_vals;
            std::vector<double> next_y_vals;
            for (int i = 0; i < ego_car.traj_.states.size(); ++i) {
              next_x_vals.push_back(ego_car.traj_.states[i].x);
              next_y_vals.push_back(ego_car.traj_.states[i].y);
            }
            
            // Output path vectors to JSON message
            json msgJson;
            msgJson["next_x"] = next_x_vals;
            msgJson["next_y"] = next_y_vals;
            auto msg = "42[\"control\","+ msgJson.dump()+"]";
            
            // Send new path to simulator
            ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
            
            // Debug logging
            if (kDBGMain == 1) {
              // Log time at end of processing
              auto t_end = std::chrono::time_point_cast
              <std::chrono::milliseconds>
              (std::chrono::high_resolution_clock::now())
              .time_since_epoch().count();
              std::cout << "Processing time = " << (t_end-t_msg)
              << " ms" << std::endl;
              if ((t_end-t_msg) > kPathCycleTimeMS) {
                std::cout << "WARNING! Processing time exceeded path cycle time"
                << std::endl;
              }
              
              // Print out road diagram
              DebugPrintRoad(detected_cars, ego_car);
            }
            else if ((kDBGMain == 2) || (kDBGMain == 3)) {
              // Detailed telemetry output
              std::cout << loop << ", t: " << t_msg
              << ", num_prev_path: " << previous_path_x.size()
              << ", idx_current_pt: " << idx_current_pt
              << ", x: " << ego_car.state_.x
              << ", y: " << ego_car.state_.y
              << ", s: " << ego_car.state_.s
              << ", s_dot: " << ego_car.state_.s_dot
              << ", s_dotdot: " << ego_car.state_.s_dotdot
              << ", d: " << ego_car.state_.d
              << ", d_dot: " << ego_car.state_.d_dot
              << ", d_dotdot: " << ego_car.state_.d_dotdot;
              
              std::cout << ", traj_x: ";
              for (int i = 0; i < ego_car.traj_.states.size(); ++i) {
                std::cout << ego_car.traj_.states[i].x << ";";
              }
              
              std::cout << ", traj_y: ";
              for (int i = 0; i < ego_car.traj_.states.size(); ++i) {
                std::cout << ego_car.traj_.states[i].y << ";";
              }
              
              std::cout << ", prev_path_x: ";
              for (int i = 0; i < previous_path_x.size(); ++i) {
                std::cout << previous_path_x[i] << ";";
              }
              
              std::cout << ", prev_path_y: ";
              for (int i = 0; i < previous_path_y.size(); ++i) {
                std::cout << previous_path_y[i] << ";";
              }
              
              std::cout << ", traj_s: ";
              for (int i = 0; i < ego_car.traj_.states.size(); ++i) {
                std::cout << ego_car.traj_.states[i].s << ";";
              }
              
              std::cout << ", traj_d: ";
              for (int i = 0; i < ego_car.traj_.states.size(); ++i) {
                std::cout << ego_car.traj_.states[i].d << ";";
              }
              
              std::cout << std::endl;
            }
          }
          else {
            // Send previous path back to simulator to continue driving it
            json msgJson;
            msgJson["next_x"] = previous_path_x;
            msgJson["next_y"] = previous_path_y;
            auto msg = "42[\"control\","+ msgJson.dump()+"]";
            ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          }
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    //ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
