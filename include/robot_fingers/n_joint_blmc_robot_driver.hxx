/**
 * \file
 * \brief Base driver for a generic n-joint BLMC robot.
 * \copyright Copyright (c) 2019, New York University and Max Planck
 *            Gesellschaft.
 */

#define TPL_NJBRD \
    template <typename Observation, size_t N_JOINTS, size_t N_MOTOR_BOARDS>
#define NJBRD NJointBlmcRobotDriver<Observation, N_JOINTS, N_MOTOR_BOARDS>

namespace robot_fingers
{
TPL_NJBRD
bool NJBRD::Config::is_within_hard_position_limits(const Vector &position) const
{
    return (hard_position_limits_lower.array() <= position.array()).all() &&
           (position.array() <= hard_position_limits_upper.array()).all();
}

TPL_NJBRD
void NJBRD::Config::print() const
{
    std::cout << "Configuration:\n"
              << "\t can_ports:";
    for (const auto &port : can_ports)
    {
        std::cout << " " << port;
    }
    std::cout << "\n"
              << "\t max_current_A: " << max_current_A << "\n"
              << "\t has_endstop: " << has_endstop << "\n"
              << "\t move_to_position_tolerance_rad: "
              << move_to_position_tolerance_rad << "\n"
              << "\t homing_method: " << get_homing_method_name(homing_method)
              << "\n"
              << "\t calibration:\n"
              << "\t\t endstop_search_torques_Nm: "
              << calibration.endstop_search_torques_Nm.transpose() << "\n"
              << "\t\t move_steps: " << calibration.move_steps << "\n"
              << "\t safety_kd: " << safety_kd.transpose() << "\n"
              << "\t position_control_gains:\n"
              << "\t\t kp: " << position_control_gains.kp.transpose() << "\n"
              << "\t\t kd: " << position_control_gains.kd.transpose() << "\n"
              << "\t hard_position_limits_\n"
              << "\t\t lower: " << hard_position_limits_lower.transpose()
              << "\n"
              << "\t\t upper: " << hard_position_limits_upper.transpose()
              << "\n"
              << "\t soft_position_limits_\n"
              << "\t\t lower: " << soft_position_limits_lower.transpose()
              << "\n"
              << "\t\t upper: " << soft_position_limits_upper.transpose()
              << "\n"
              << "\t home_offset_rad: " << home_offset_rad.transpose() << "\n"
              << "\t initial_position_rad: " << initial_position_rad.transpose()
              << "\n"
              << "\t shutdown_trajectory:\n";

    if (shutdown_trajectory.empty())
    {
        std::cout << "\t\t None\n";
    }
    else
    {
        for (const TrajectoryStep &step : shutdown_trajectory)
        {
            std::cout << "\t\t - target: "
                      << step.target_position_rad.transpose() << "\n"
                      << "\t\t   move_steps: " << step.move_steps << "\n";
        }
    }

    std::cout << "\t run_duration_logfiles:\n";
    if (run_duration_logfiles.empty())
    {
        std::cout << "\t\t None\n";
    }
    else
    {
        for (const std::string &filename : run_duration_logfiles)
        {
            std::cout << "\t\t - " << filename << "\n";
        }
    }

    std::cout << std::endl;
}

TPL_NJBRD
auto NJBRD::Config::load_config(const std::string &config_file_name) -> Config
{
    NJBRD::Config config;
    YAML::Node user_config;

    try
    {
        user_config = YAML::LoadFile(config_file_name);
    }
    catch (...)
    {
        std::cout << "FATAL: Failed to load configuration from '"
                  << config_file_name << "'." << std::endl;
        std::exit(1);
    }

    // replace values from the default config with the ones given in the
    // users config file

    // TODO: for some reason direct conversion is not working (despite
    // yaml-cpp implementing a generic conversion for std::array)
    // set_config_value<CanPortArray>(user_config, "can_ports",
    // &config.can_ports);
    try
    {
        for (size_t i = 0; i < config.can_ports.size(); i++)
        {
            config.can_ports[i] = user_config["can_ports"][i].as<std::string>();
        }
    }
    catch (...)
    {
        std::cerr << "FATAL: Failed to load parameter 'can_ports' from "
                     "configuration file"
                  << std::endl;
        std::exit(1);
    }

    set_config_value(user_config, "max_current_A", &config.max_current_A);
    set_config_value(user_config, "has_endstop", &config.has_endstop);

    if (user_config["homing_with_index"])
    {
        std::cout << "FATAL: The configuration option 'homing_with_index' is "
                     "obsolete.  Use 'homing_method' instead."
                  << std::endl;
        std::exit(1);
    }

    if (user_config["homing_method"])
    {
        std::string method_name;
        set_config_value(user_config, "homing_method", &method_name);
        try
        {
            config.homing_method = parse_homing_method_name(method_name);
        }
        catch (const std::invalid_argument &e)
        {
            std::cout << "FATAL: " << e.what() << std::endl;
            std::exit(1);
        }
    }
    else
    {
        std::cerr << "WARNING: 'homing_method' is not specified.  Using "
                     "backward-compatible default.  Explicitly specify a "
                     "homing method to silence this warning."
                  << std::endl;

        if (config.has_endstop)
        {
            config.homing_method = HomingMethod::ENDSTOP_INDEX;
        }
        else
        {
            config.homing_method = HomingMethod::NEXT_INDEX;
        }
    }

    set_config_value(user_config,
                     "move_to_position_tolerance_rad",
                     &config.move_to_position_tolerance_rad);

    if (user_config["calibration"])
    {
        YAML::Node calib = user_config["calibration"];

        set_config_value(calib,
                         "endstop_search_torques_Nm",
                         &config.calibration.endstop_search_torques_Nm);
        set_config_value(calib, "move_steps", &config.calibration.move_steps);
    }

    set_config_value(user_config, "safety_kd", &config.safety_kd);

    if (user_config["position_control_gains"])
    {
        YAML::Node pos_ctrl = user_config["position_control_gains"];

        set_config_value(pos_ctrl, "kp", &config.position_control_gains.kp);
        set_config_value(pos_ctrl, "kd", &config.position_control_gains.kd);
    }

    set_config_value(user_config,
                     "hard_position_limits_lower",
                     &config.hard_position_limits_lower);
    set_config_value(user_config,
                     "hard_position_limits_upper",
                     &config.hard_position_limits_upper);

    // soft limits are optional
    if (user_config["soft_position_limits_lower"])
    {
        set_config_value(user_config,
                         "soft_position_limits_lower",
                         &config.soft_position_limits_lower);
    }
    if (user_config["soft_position_limits_upper"])
    {
        set_config_value(user_config,
                         "soft_position_limits_upper",
                         &config.soft_position_limits_upper);
    }

    set_config_value(user_config, "home_offset_rad", &config.home_offset_rad);
    set_config_value(
        user_config, "initial_position_rad", &config.initial_position_rad);

    if (user_config["shutdown_trajectory"])
    {
        YAML::Node trajectory = user_config["shutdown_trajectory"];

        if (!trajectory.IsSequence())
        {
            std::cerr << "FATAL: Parameter 'shutdown_trajectory' from "
                         "configuration file is not a list."
                      << std::endl;
            std::exit(1);
        }

        for (size_t i = 0; i < trajectory.size(); i++)
        {
            TrajectoryStep step;
            set_config_value(trajectory[i],
                             "target_position_rad",
                             &step.target_position_rad);
            set_config_value(trajectory[i], "move_steps", &step.move_steps);
            config.shutdown_trajectory.push_back(step);
        }
    }

    if (user_config["run_duration_logfiles"])
    {
        YAML::Node logfiles = user_config["run_duration_logfiles"];

        if (!logfiles.IsSequence())
        {
            std::cerr << "FATAL: Parameter 'run_duration_logfiles' from "
                         "configuration file is not a list."
                      << std::endl;
            std::exit(1);
        }

        for (size_t i = 0; i < logfiles.size(); i++)
        {
            try
            {
                config.run_duration_logfiles.push_back(
                    logfiles[i].as<std::string>());
            }
            catch (const YAML::Exception &e)
            {
                std::cerr
                    << "FATAL: Failed to load run_duration_logfiles entry " << i
                    << " from configuration file" << std::endl;
                std::exit(1);
            };
        }
    }

    return config;
}

TPL_NJBRD
template <typename T>
void NJBRD::Config::set_config_value(const YAML::Node &user_config,
                                     const std::string &name,
                                     T *var)
{
    try
    {
        *var = user_config[name].as<T>();
    }
    catch (const YAML::Exception &e)
    {
        std::cerr << "FATAL: Failed to load parameter '" << name
                  << "' from configuration file" << std::endl;
        std::exit(1);
    };
}

TPL_NJBRD
auto NJBRD::create_motor_boards(
    const std::array<std::string, N_MOTOR_BOARDS> &can_ports) -> MotorBoards
{
    // setup can buses -----------------------------------------------------
    std::array<std::shared_ptr<blmc_drivers::CanBus>, N_MOTOR_BOARDS> can_buses;
    for (size_t i = 0; i < can_buses.size(); i++)
    {
        can_buses[i] = std::make_shared<blmc_drivers::CanBus>(can_ports[i]);
    }

    // set up motor boards -------------------------------------------------
    MotorBoards motor_boards;
    for (size_t i = 0; i < motor_boards.size(); i++)
    {
        motor_boards[i] = std::make_shared<blmc_drivers::CanBusMotorBoard>(
            can_buses[i], 1000, 10);
        /// \TODO: reduce the timeout further!!
    }

    for (size_t i = 0; i < motor_boards.size(); i++)
    {
        motor_boards[i]->wait_until_ready();
    }

    return motor_boards;
}

TPL_NJBRD
void NJBRD::pause_motors()
{
    for (size_t i = 0; i < motor_boards_.size(); i++)
    {
        motor_boards_[i]->pause_motors();
    }
}

TPL_NJBRD
auto NJBRD::get_measured_index_angles() const -> Vector
{
    return joint_modules_.get_measured_index_angles();
}

TPL_NJBRD
void NJBRD::initialize()
{
    // Initialization is moving the robot and thus needs to be executed in
    // a real-time thread.  This method only starts the thread and waits
    // for it to finish.  Actual implementation of initialization is in
    // `_initialize()`.

    real_time_tools::RealTimeThread realtime_thread;
    realtime_thread.create_realtime_thread(
        [](void *instance_pointer) {
            // instance_pointer = this, cast to correct type and call the
            // _initialize() method.
            ((NJBRD *)(instance_pointer))->_initialize();
            return (void *)nullptr;
        },
        this);
    realtime_thread.join();
}

TPL_NJBRD
auto NJBRD::get_idle_action() -> Action
{
    return Action::Position(config_.initial_position_rad);
}

TPL_NJBRD
auto NJBRD::apply_action(const NJBRD::Action &desired_action) -> Action
{
    if (!is_initialized_)
    {
        throw std::runtime_error(
            "Robot needs to be initialized before applying actions.  Run "
            "the `initialize()` method.");
    }

    return apply_action_uninitialized(desired_action);
}

TPL_NJBRD
std::string NJBRD::get_error()
{
    // Checks each board for errors and translates the error codes into
    // human-readable strings.  If multiple boards have errors, the messages
    // are concatenated.  Each message is prepended with the index of the
    // corresponding board.

    std::string error_msg = "";

    for (size_t i = 0; i < motor_boards_.size(); i++)
    {
        auto status_timeseries = motor_boards_[i]->get_status();
        if (status_timeseries->length() > 0)
        {
            std::string board_error_msg = "";
            using ErrorCodes = blmc_drivers::MotorBoardStatus::ErrorCodes;
            switch (status_timeseries->newest_element().error_code)
            {
                case ErrorCodes::NONE:
                    break;
                case ErrorCodes::ENCODER:
                    board_error_msg = "Encoder Error";
                    break;
                case ErrorCodes::CAN_RECV_TIMEOUT:
                    board_error_msg = "CAN Receive Timeout";
                    break;
                case ErrorCodes::CRIT_TEMP:
                    board_error_msg = "Critical Temperature";
                    break;
                case ErrorCodes::POSCONV:
                    board_error_msg =
                        "Error in SpinTAC Position Convert module";
                    break;
                case ErrorCodes::POS_ROLLOVER:
                    board_error_msg = "Position Rollover";
                    break;
                case ErrorCodes::OTHER:
                    board_error_msg = "Other Error";
                    break;
                default:
                    board_error_msg = "Unknown Error";
                    break;
            }

            if (!board_error_msg.empty())
            {
                if (!error_msg.empty())
                {
                    error_msg += "  ";
                }

                // error of the board with board index to the error message
                // string
                error_msg +=
                    "[Board " + std::to_string(i) + "] " + board_error_msg;
            }
        }
    }

    // check if position is within the limits
    Vector position = this->joint_modules_.get_measured_angles();
    if (!config_.is_within_hard_position_limits(position))
    {
        if (!error_msg.empty())
        {
            error_msg += " | ";
        }

        error_msg += "Position limits exceeded.";
    }

    return error_msg;
}

TPL_NJBRD
void NJBRD::shutdown()
{
    // Move on the shutdown trajectory step by step.  If no shutdown trajectory
    // is configured, the list of steps will be empty, so nothing will happen.
    bool success = true;
    for (const auto &step : config_.shutdown_trajectory)
    {
        success &=
            this->move_to_position(step.target_position_rad,
                                   this->config_.move_to_position_tolerance_rad,
                                   step.move_steps);

        // do not continue if one step failed
        if (!success)
        {
            break;
        }
    }

    pause_motors();

    if (!success)
    {
        // TODO: report this somehow as this probably means that someone
        // needs to disentangle the robot manually.
        std::cerr << "Failed to reach rest position.  Robot may be blocked."
                  << std::endl;
    }

    // write number of actions to the run duration logs
    int timestamp =
        static_cast<int>(real_time_tools::Timer::get_current_time_sec());
    for (const std::string &logfile_name : config_.run_duration_logfiles)
    {
        std::cout << "Write run duration log " << logfile_name << std::endl;
        std::ofstream file(logfile_name, std::ios_base::app);
        if (!file)
        {
            std::cerr << "Failed to open file " << logfile_name
                      << " for writing." << std::endl;
        }
        else
        {
            file << timestamp << "\t" << action_counter_ << std::endl;
        }
    }
}

TPL_NJBRD
auto NJBRD::process_desired_action(const Action &desired_action,
                                   const Observation &latest_observation,
                                   const double max_torque_Nm,
                                   const Vector &safety_kd,
                                   const Vector &default_position_control_kp,
                                   const Vector &default_position_control_kd,
                                   const Vector &lower_position_limits,
                                   const Vector &upper_position_limits)
    -> Action
{
    Action processed_action = desired_action;

    // Position Limits
    // ---------------
    // If a joint exceeds the soft position limit, replace the command for that
    // joint with a position command to the limit
    for (std::size_t i = 0; i < N_JOINTS; i++)
    {
        // Clamp position commands to the allowed range (note that if position
        // is NaN both conditions are false, so the NaN is preserved).
        if (processed_action.position[i] < lower_position_limits[i])
        {
            processed_action.position[i] = lower_position_limits[i];
        }
        else if (processed_action.position[i] > upper_position_limits[i])
        {
            processed_action.position[i] = upper_position_limits[i];
        }

        auto set_limit_action = [&](double sign, double limit) {
            // Discard torque command if it pushes further out of the valid
            // range.
            if (processed_action.torque[i] * sign > 0)
            {
                processed_action.torque[i] = 0;
            }

            // If no position is set, set it to the limit value (otherwise it
            // will already be clamped to the limit range, so it will be fine).
            if (std::isnan(processed_action.position[i]))
            {
                processed_action.position[i] = limit;
            }

            // do not allow custom gains
            processed_action.position_kp[i] = default_position_control_kp[i];
            processed_action.position_kd[i] = default_position_control_kd[i];
        };

        if (latest_observation.position[i] < lower_position_limits[i])
        {
            set_limit_action(-1, lower_position_limits[i]);
        }
        else if (latest_observation.position[i] > upper_position_limits[i])
        {
            set_limit_action(+1, upper_position_limits[i]);
        }
    }

    // Position Controller
    // -------------------

    // Run the position controller only if a target position is set for at
    // least one joint.
    if (!processed_action.position.array().isNaN().all())
    {
        // Replace NaN-values with default gains
        processed_action.position_kp =
            processed_action.position_kp.array().isNaN().select(
                default_position_control_kp, processed_action.position_kp);
        processed_action.position_kd =
            processed_action.position_kd.array().isNaN().select(
                default_position_control_kd, processed_action.position_kd);

        Vector position_error =
            processed_action.position - latest_observation.position;

        // simple PD controller
        Vector position_control_torque =
            processed_action.position_kp.cwiseProduct(position_error) -
            processed_action.position_kd.cwiseProduct(
                latest_observation.velocity);

        // position_control_torque contains NaN for joints where target
        // position is set to NaN!  Filter those out and set the torque to
        // zero instead.
        position_control_torque =
            position_control_torque.array().isNaN().select(
                0, position_control_torque);

        // Add result of position controller to the torque command
        processed_action.torque += position_control_torque;
    }

    // Safety Checks
    // -------------
    // limit to configured maximum torque
    processed_action.torque =
        clamp(processed_action.torque, -max_torque_Nm, max_torque_Nm);
    // velocity damping to prevent too fast movements
    processed_action.torque -=
        safety_kd.cwiseProduct(latest_observation.velocity);
    // after applying checks, make sure we are still below the max. torque
    processed_action.torque =
        clamp(processed_action.torque, -max_torque_Nm, max_torque_Nm);

    return processed_action;
}

TPL_NJBRD
bool NJBRD::is_within_hard_position_limits(const Observation &observation) const
{
    return config_.is_within_hard_position_limits(observation.position);
}

TPL_NJBRD
auto NJBRD::apply_action_uninitialized(const NJBRD::Action &desired_action)
    -> Action
{
    double start_time_sec = real_time_tools::Timer::get_current_time_sec();

    Observation observation = get_latest_observation();

    // Only enable soft position limits once initialization is done (i.e. no
    // limits during homing).
    Vector lower_limits =
        is_initialized_
            ? config_.soft_position_limits_lower
            : Vector::Constant(-std::numeric_limits<double>::infinity());
    Vector upper_limits =
        is_initialized_
            ? config_.soft_position_limits_upper
            : Vector::Constant(std::numeric_limits<double>::infinity());

    Action applied_action =
        process_desired_action(desired_action,
                               observation,
                               max_torque_Nm_,
                               config_.safety_kd,
                               config_.position_control_gains.kp,
                               config_.position_control_gains.kd,
                               lower_limits,
                               upper_limits);

    joint_modules_.set_torques(applied_action.torque);
    joint_modules_.send_torques();

    action_counter_++;

    real_time_tools::Timer::sleep_until_sec(start_time_sec + 0.001);

    return applied_action;
}

TPL_NJBRD
void NJBRD::_initialize()
{
    joint_modules_.set_position_control_gains(
        config_.position_control_gains.kp, config_.position_control_gains.kd);

    bool homing_succeeded = homing();
    pause_motors();

    // NOTE: do not set is_initialized_ yet as we want to allow move_to_position
    // below to move without position limits (as it might be that after homing
    // it is out of the limits).
    if (homing_succeeded)
    {
        Vector waypoint = get_latest_observation().position;

        bool reached_goal = false;
        for (size_t i = 0; i < N_JOINTS; i++)
        {
            waypoint[i] = config_.initial_position_rad[i];

            reached_goal =
                move_to_position(waypoint,
                                 config_.move_to_position_tolerance_rad,
                                 config_.calibration.move_steps);
        }
        if (!reached_goal)
        {
            rt_printf("Failed to reach initial position, timeout exceeded.\n");
        }
    }

    pause_motors();

    is_initialized_ = homing_succeeded;
}

TPL_NJBRD
void NJBRD::move_until_blocking(NJBRD::Vector torques_Nm)
{
    //! Min. number of steps when moving to the end stop.
    constexpr uint32_t MIN_STEPS_MOVE_TO_END_STOP = 1000;
    //! Size of the window when computing average velocity.
    constexpr uint32_t SIZE_VELOCITY_WINDOW = 100;
    //! Velocity limit at which the joints are considered to be stopped.
    constexpr double STOP_VELOCITY = 0.01;

    static_assert(MIN_STEPS_MOVE_TO_END_STOP > SIZE_VELOCITY_WINDOW,
                  "MIN_STEPS_MOVE_TO_END_STOP has to be bigger than"
                  " SIZE_VELOCITY_WINDOW to ensure correct computation"
                  " of average velocity.");

    // Move until velocity drops to almost zero (= joints hit the end
    // stops) but at least for MIN_STEPS_MOVE_TO_END_STOP time steps.
    // TODO: add timeout to this loop?
    std::vector<Vector> running_velocities(SIZE_VELOCITY_WINDOW);
    Vector summed_velocities = Vector::Zero();
    uint32_t step_count = 0;
    while (
        step_count < MIN_STEPS_MOVE_TO_END_STOP ||
        (summed_velocities.maxCoeff() / SIZE_VELOCITY_WINDOW > STOP_VELOCITY))
    {
        apply_action_uninitialized(torques_Nm);
        Vector abs_velocities = get_latest_observation().velocity.cwiseAbs();

        uint32_t running_index = step_count % SIZE_VELOCITY_WINDOW;
        if (step_count >= SIZE_VELOCITY_WINDOW)
        {
            summed_velocities -= running_velocities[running_index];
        }
        running_velocities[running_index] = abs_velocities;
        summed_velocities += abs_velocities;
        step_count++;
    }
}

TPL_NJBRD
bool NJBRD::homing()
{
    rt_printf("Start homing.\n");

    // First move to end-stop if this is required by the selected homing method.
    switch (config_.homing_method)
    {
        case Config::HomingMethod::ENDSTOP:
        case Config::HomingMethod::ENDSTOP_INDEX:
        case Config::HomingMethod::ENDSTOP_RELEASE:
        {
            if (!has_endstop_)
            {
                rt_printf(
                    "Invalid config: Selected homing method needs endstop but "
                    "'has_endstop' is false.");
                return false;
            }

            if (config_.calibration.endstop_search_torques_Nm.isZero())
            {
                rt_printf(
                    "Invalid config: A homing method with end-stop search is "
                    "selected but 'endstop_search_torques_Nm' is zero.");
                return false;
            }

            move_until_blocking(config_.calibration.endstop_search_torques_Nm);
            rt_printf("Reached end stop.\n");

            break;
        }
        default:
            // other homing methods do nothing here
            break;
    }

    blmc_drivers::HomingReturnCode homing_status =
        blmc_drivers::HomingReturnCode::NOT_INITIALIZED;

    // Now do the actual homing
    switch (config_.homing_method)
    {
        case Config::HomingMethod::NONE:
            homing_status = blmc_drivers::HomingReturnCode::SUCCEEDED;
            break;  // nothing to do here

        case Config::HomingMethod::NEXT_INDEX:
        case Config::HomingMethod::ENDSTOP_INDEX:
        {
            // Home on encoder index

            //! Distance after which encoder index search is aborted.
            //! Computed based on gear ratio to be 1.5 motor revolutions.
            const double INDEX_SEARCH_DISTANCE_LIMIT_RAD =
                (1.5 / motor_parameters_.gear_ratio) * 2 * M_PI;
            //! Absolute step size when moving for encoder index search.
            constexpr double INDEX_SEARCH_STEP_SIZE_RAD = 0.0003;

            if (config_.calibration.endstop_search_torques_Nm.isZero())
            {
                rt_printf(
                    "Invalid config: A homing method with index search is "
                    "selected but 'endstop_search_torques_Nm' is zero.  The "
                    "sign of 'endstop_search_torques_Nm' is used to determine "
                    "the index search direction (opposite direction to end "
                    "stop search).");
                return false;
            }

            // Set the search direction for each joint opposite to the end-stop
            // search direction.
            Vector index_search_step_sizes;
            for (unsigned int i = 0; i < N_JOINTS; i++)
            {
                index_search_step_sizes[i] = INDEX_SEARCH_STEP_SIZE_RAD;
                if (config_.calibration.endstop_search_torques_Nm[i] > 0)
                {
                    index_search_step_sizes[i] *= -1;
                }
            }

            homing_status =
                joint_modules_.execute_homing(INDEX_SEARCH_DISTANCE_LIMIT_RAD,
                                              config_.home_offset_rad,
                                              index_search_step_sizes);

            break;
        }

        case Config::HomingMethod::CURRENT_POSITION:
        case Config::HomingMethod::ENDSTOP:
        {
            // Home at current position

            homing_status = joint_modules_.execute_homing_at_current_position(
                config_.home_offset_rad);

            break;
        }

        case Config::HomingMethod::ENDSTOP_RELEASE:
        {
            // First set motors to zero torque (so they are not actively pushing
            // anymore), then home at current position.

            constexpr uint32_t NUM_ZERO_TORQUE_STEPS = 1000;

            // release motors (set torque = 0) for a moment, so it is not
            // actively pressing against the end-stop anymore.
            Vector zero = Vector::Zero();
            for (uint32_t i = 0; i < NUM_ZERO_TORQUE_STEPS; i++)
            {
                apply_action_uninitialized(zero);
            }

            // home at the current position (which should be at the end-stop)
            homing_status = joint_modules_.execute_homing_at_current_position(
                config_.home_offset_rad);

            break;
        }
    }

    rt_printf("Finished homing.");
    return homing_status == blmc_drivers::HomingReturnCode::SUCCEEDED;
}

TPL_NJBRD
bool NJBRD::move_to_position(const NJBRD::Vector &goal_pos,
                             const double tolerance,
                             const uint32_t time_steps)
{
    // move to the goal position on a minium jerk trajectory, see
    // https://web.archive.org/web/20200715015252/https://mika-s.github.io/python/control-theory/trajectory-generation/2017/12/06/trajectory-generation-with-a-minimum-jerk-trajectory.html

    const auto initial_position = get_latest_observation().position;

    for (uint32_t t = 0; t < time_steps; t++)
    {
        double alpha = (double)t / (double)time_steps;
        auto step_goal = initial_position + (goal_pos - initial_position) *
                                                (10.0 * std::pow(alpha, 3) -
                                                 15.0 * std::pow(alpha, 4) +
                                                 6.0 * std::pow(alpha, 5));

        apply_action_uninitialized(Action::Position(step_goal));
    }

    // check if the goal was really reached
    const Vector position_error = goal_pos - get_latest_observation().position;
    bool reached_goal = (position_error.array().abs() < tolerance).all();

    return reached_goal;
}

template <size_t N_JOINTS, size_t N_MOTOR_BOARDS>
auto SimpleNJointBlmcRobotDriver<N_JOINTS,
                                 N_MOTOR_BOARDS>::get_latest_observation()
    -> Observation
{
    Observation observation;

    observation.position = this->joint_modules_.get_measured_angles();
    observation.velocity = this->joint_modules_.get_measured_velocities();
    observation.torque = this->joint_modules_.get_measured_torques();

    return observation;
}

}  // namespace robot_fingers
