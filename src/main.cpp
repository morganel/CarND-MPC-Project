#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "MPC.h"
#include "json.hpp"

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.rfind("}]");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

// Evaluate a polynomial.
double polyeval(Eigen::VectorXd coeffs, double x) {
  double result = 0.0;
  for (int i = 0; i < coeffs.size(); i++) {
    result += coeffs[i] * pow(x, i);
  }
  return result;
}

// Converts vector of doubles to Eigen::VectorXd
// Eigen::VectorXd convert_vector(vector<double> x) {
//   Eigen::VectorXd x2 (x.size());
//   for (size_t i; i < x.size(); i++) {
//     x2(i) = x[i];
//   }
//   return x2;
// }

// Fit a polynomial.
// Adapted from
// https://github.com/JuliaMath/Polynomials.jl/blob/master/src/Polynomials.jl#L676-L716
Eigen::VectorXd polyfit(Eigen::VectorXd xvals, Eigen::VectorXd yvals,
                        int order) {
  assert(xvals.size() == yvals.size());
  assert(order >= 1 && order <= xvals.size() - 1);
  Eigen::MatrixXd A(xvals.size(), order + 1);

  for (int i = 0; i < xvals.size(); i++) {
    A(i, 0) = 1.0;
  }

  for (int j = 0; j < xvals.size(); j++) {
    for (int i = 0; i < order; i++) {
      A(j, i + 1) = A(j, i) * xvals(j);
    }
  }

  auto Q = A.householderQr();
  auto result = Q.solve(yvals);
  return result;
}

int main() {
  uWS::Hub h;

  // MPC is initialized here!
  MPC mpc;

  h.onMessage([&mpc](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    string sdata = string(data).substr(0, length);
    cout << sdata << endl;
    if (sdata.size() > 2 && sdata[0] == '4' && sdata[1] == '2') {
      string s = hasData(sdata);
      if (s != "") {
        auto j = json::parse(s);
        string event = j[0].get<string>();
        if (event == "telemetry") {
          // j[1] is the data JSON object
          vector<double> global_ptsx = j[1]["ptsx"];
          vector<double> global_ptsy = j[1]["ptsy"];
          double global_px = j[1]["x"];
          double global_py = j[1]["y"];
          double global_psi = j[1]["psi"];
          double v = j[1]["speed"];

          // convert to car centered coordinates
          vector<double> ptsx;
          vector<double> ptsy;

          ptsx.resize(global_ptsx.size());
          ptsy.resize(global_ptsy.size());

          for (int i = 0; i < global_ptsx.size(); i++)
          {
            auto x = global_ptsx[i] - global_px;
            auto y = global_ptsy[i] - global_py;
            ptsx[i] = x * cos(global_psi) + y * sin(global_psi);
            ptsy[i] = -x * sin(global_psi) + y * cos(global_psi);
          }

          // convert from vector<double> to Eigen::VectorXd to match input to polyfit
          Eigen::VectorXd x (ptsx.size());
          Eigen::VectorXd y (ptsy.size());
          for (size_t i = 0; i < ptsx.size(); i++) {
            x(i) = ptsx[i];
            y(i) = ptsy[i];
          }

          Eigen::VectorXd coeffs;
          coeffs = polyfit(x, y, 3);
          
          // The cross track error is calculated by evaluating at polynomial at x, f(x)
          // and subtracting y. x and y are at 0 to represent the car.
          double cte = polyeval(coeffs, 0);
          
          // Due to the sign starting at 0, the orientation error is -f'(x).
          // derivative of coeffs[0] + coeffs[1] * x + coeffs[2] * x^2 + coeffs[3] * x^3 -> coeffs[1] when x = 0
          double epsi = -atan(coeffs[1]);
          
          // in the car centered coordinate, we have a new state:
          Eigen::VectorXd new_state(6);
          new_state << 0.0, 0.0, 0.0, v, cte, epsi;
          
          // run solver to get the next state and the value of the actuators
          vector<double> next_and_actuators = mpc.Solve(new_state, coeffs);
          
          double steer_value;
          double throttle_value;
          steer_value = next_and_actuators[6];
          throttle_value = next_and_actuators[7];

          json msgJson;
          msgJson["steering_angle"] = -steer_value;
          msgJson["throttle"] = throttle_value;

          //Display the MPC predicted trajectory 
          //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
          // the points in the simulator are connected by a Green line
          msgJson["mpc_x"] = mpc.trajectory_x;;
          msgJson["mpc_y"] = mpc.trajectory_y;

          //Display the waypoints/reference line
          //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
          // the points in the simulator are connected by a Yellow line

          msgJson["next_x"] = ptsx;
          msgJson["next_y"] = ptsy;


          auto msg = "42[\"steer\"," + msgJson.dump() + "]";
          std::cout << msg << std::endl;
          // Latency
          // The purpose is to mimic real driving conditions where
          // the car does actuate the commands instantly.
          //
          // Feel free to play around with this value but should be to drive
          // around the track with 100ms latency.
          //
          // NOTE: REMEMBER TO SET THIS TO 100 MILLISECONDS BEFORE
          // SUBMITTING.
          this_thread::sleep_for(chrono::milliseconds(100));
          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
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
    ws.close();
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
