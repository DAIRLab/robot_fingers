#!/usr/bin/env python3
import numpy as np
import copy
import sys
from os import path

import progressbar
from ament_index_python.packages import get_package_share_directory

from robot_interfaces import one_joint
import robot_fingers


N_JOINTS = 1

Action = one_joint.Action

# Configuration
# ========================================

# Limit of the range in which the joint can move (i.e. should be a little
# bit before hitting the end stop).
POSITION_LIMIT = 2.7

# Number of times the motor hits the endstop in each "hit end stop" phase
NUM_ENDSTOP_HITS = 10

# Number of back and forth movements during each "fixed velocity" phase
NUM_FIXED_VELOCITY_MOVEMENT_STEPS = 150


# Number of times the complete scenario is repeated
# NUM_ITERATIONS = 20
NUM_ITERATIONS = 1


# ========================================


def zero_torque_ctrl(robot, duration, print_position=False):
    """Send zero-torque commands for the specified duration."""
    action = Action()
    step = 0

    while step < duration:
        step += 1
        t = robot.append_desired_action(action)
        robot.wait_until_timeindex(t)
        if print_position:
            print(
                "\rPosition: %10.4f" % robot.get_observation(t).position[0],
                end="",
            )


def go_to(robot, goal_position, steps, hold):
    """Go to the goal position with linear profile and hold there.

    :param robot: Robot frontend used to control the robot.
    :param goal_position: Position to which the robot shall move.
    :param steps: Number of steps for the movement.  The velocity of the robot
        depends on the number of steps and the actual distance it has to move.
    :param hold: Number of time steps to hold the motor at the goal position
        once it is reached.
    """
    t = robot.append_desired_action(Action())
    desired_step_position = copy.copy(robot.get_observation(t).position)

    stepsize = (goal_position - desired_step_position) / steps

    for _ in range(steps):
        desired_step_position += stepsize
        t = robot.append_desired_action(Action(position=desired_step_position))
        robot.wait_until_timeindex(t)

    action = Action(position=np.ones(N_JOINTS) * goal_position)
    for _ in range(hold):
        t = robot.append_desired_action(action)
        robot.wait_until_timeindex(t)


def go_to_zero(robot, steps, hold):
    """Go to zero position.  See go_to for description of parameters."""
    go_to(robot, np.zeros(N_JOINTS), steps, hold)


def hit_endstop(robot, desired_torque, hold=0, timeout=5000):
    """Hit the end stop with the given torque.

    Applies a constant torque on the joints until velocity drops to near-zero
    (in which case it is assumed that the end stop is reached).

    :param robot: Robot frontend used to control the robot.
    :param desired_torque: Torque that is applied on the joints.
    :param hold: Duration for which the torque is held up after hitting the end
        stop.
    :param timeout: Stop if joint is still moving after this time.
    """
    zero_velocity = 0.001
    step = 0
    action = Action(torque=desired_torque)
    t = robot.append_desired_action(action)

    while (
        np.any(np.abs(robot.get_observation(t).velocity) > zero_velocity)
        or step < 100
    ) and step < timeout:
        t = robot.append_desired_action(action)

        step += 1

    for _ in range(hold):
        t = robot.append_desired_action(action)
        robot.wait_until_timeindex(t)


def test_if_moves(robot, desired_torque, timeout):
    for _ in range(timeout):
        t = robot.append_desired_action(Action(torque=desired_torque))
        # This is a bit hacky: It is assumed that the joints move if they reach
        # a position > 0 within the given time.  Note that this assumes that
        # they start somewhere in the negative range!
        if np.all(robot.get_observation(t).position > 0):
            return True
    return False


def determine_start_torque(robot):
    """Determine minimum torque to make the joints move.

    Moves the joint to negative position limit and applies a constant torque.
    The motor is considered to be moving if it reaches the positive range
    within a given time frame.  If not, the whole procedure is repeated with a
    increasing torque until the joint moves.
    """
    robot.append_desired_action(Action())

    max_torque = 0.4
    stepsize = 0.025
    for trq in np.arange(max_torque, step=stepsize):
        print("test %f Nm" % trq)
        go_to(robot, -POSITION_LIMIT, 1000, 100)
        desired_torque = np.ones(N_JOINTS) * trq
        if test_if_moves(robot, desired_torque, 3000):
            break


def validate_position(robot):
    """Check if measured position is correct.

    Hit the end stop from both sites to check if expected and actual
    position match.
    """
    tolerance = 0.1
    desired_torque = np.ones(N_JOINTS) * 0.22

    position = [None, None]

    for i, sign in enumerate((+1, -1)):
        hit_endstop(robot, sign * desired_torque)
        t = robot.get_current_timeindex()
        position[i] = robot.get_observation(t).position

    center = (position[0] + position[1]) / 2

    if np.abs(center) > tolerance:
        raise RuntimeError(
            "Unexpected center position." "Expected 0.0, actual is %f" % center
        )
    else:
        print("Position is okay.")


def hard_direction_change(robot, num_repetitions, torque):
    """Move back and forth by toggling sign of the torque command."""
    # set position limit far enough from the end stop to ensure we don't hit it
    # even when overshooting (we don't want to break the end stop).
    position_limit = 0.6

    desired_torque = np.ones(N_JOINTS) * torque

    t = robot.append_desired_action(Action())

    progress = progressbar.ProgressBar()
    for _ in progress(range(num_repetitions)):
        step = 0
        while np.all(robot.get_observation(t).position < position_limit):
            t = robot.append_desired_action(Action(desired_torque))
            step += 1
            if step > 2000:
                raise RuntimeError("timeout hard_direction_change")

        step = 0
        while np.all(robot.get_observation(t).position > -position_limit):
            t = robot.append_desired_action(Action(-desired_torque))
            step += 1
            if step > 2000:
                raise RuntimeError("timeout -hard_direction_change")

    # dampen movement to not hit end stop
    go_to(robot, -position_limit, 10, 100)


def main():
    if len(sys.argv) >= 2:
        log_directory = sys.argv[1]
    else:
        log_directory = "/tmp"

    def log_path(filename):
        return path.join(log_directory, filename)

    # load the default config file
    config_file_path = path.join(
        get_package_share_directory("robot_fingers"),
        "config",
        "onejoint_high_load.yaml",
    )

    robot_data = one_joint.SingleProcessData()
    finger_backend = robot_fingers.create_one_joint_backend(
        robot_data, config_file_path
    )
    robot = one_joint.Frontend(robot_data)

    logger = one_joint.Logger(robot_data, 100)

    # rotate without end stop
    # goal_position = 60
    # # move to goal position within 2000 ms and wait there for 100 ms
    # go_to(robot, goal_position, 20000, 100)

    finger_backend.initialize()
    print("initialization finished")
    go_to_zero(robot, 1000, 2000)

    # zero_torque_ctrl(robot, 99999999, print_position=True)

    print("initial position validation")
    validate_position(robot)

    go_to_zero(robot, 1000, 2000)

    # [(0, 0.0),
    #  (1, 0.18),
    #  (2, 0.36),
    #  (3, 0.54),
    #  (4, 0.72),
    #  (5, 0.8999999999999999),
    #  (6, 1.08),
    #  (7, 1.26),
    #  (8, 1.44),
    #  (9, 1.6199999999999999),
    #  (10, 1.7999999999999998)]

    # Careful, dangerous!
    # hit_torque = np.ones(N_JOINTS) * 1.26
    # print("Start to push")
    # hit_endstop(robot, hit_torque, hold=100)
    # hit_endstop(robot, -hit_torque, hold=100)

    for iteration in range(NUM_ITERATIONS):
        print("START TEST ITERATION %d" % iteration)

        # print("Determine torque to start movement.")
        # determine_start_torque(robot)

        print("Switch directions with high torque")
        low_trq = 0.2
        currents = range(5, 15)
        logger.start(log_path("one_joint_test_data.csv"))
        for current in currents:
            trq = current * (0.02 * 9)
            print("A = %d (trq = %f)" % (current, trq))
            go_to(robot, -POSITION_LIMIT, 500, 10)
            hard_direction_change(robot, 2, trq)

            t = robot.get_current_timeindex()
            if np.any(
                np.abs(robot.get_observation(t).position) > POSITION_LIMIT
            ):
                print("ERROR: Position limit exceeded!")
                return

            hard_direction_change(robot, 10, low_trq)

            # Determine torque to start movement
            # determine_start_torque(robot)

        logger.stop()

        print("position validation after switch directions")
        validate_position(robot)

        # skip the following tests
        continue

        print("Hit the end stop...")

        trq = 1.8
        hit_torque = np.ones(N_JOINTS) * trq
        progress = progressbar.ProgressBar()
        for _ in progress(range(NUM_ENDSTOP_HITS)):
            hit_torque *= -1
            hit_endstop(robot, hit_torque, hold=10)
            # hit_endstop(robot, hit_torque)
            # zero_torque_ctrl(robot, 10)

        # hit_torque = np.ones(N_JOINTS) * 0.2
        # for i in range(NUM_ENDSTOP_HITS):
        #    hit_torque *= -1
        #    hit_endstop(robot, hit_torque)
        #    zero_torque_ctrl(robot, 10)

        # hit_torque = np.ones(N_JOINTS) * 0.4
        # for i in range(NUM_ENDSTOP_HITS):
        #    hit_torque *= -1
        #    hit_endstop(robot, hit_torque)
        #    zero_torque_ctrl(robot, 10)

        print("position validation after hitting")
        validate_position(robot)

        print("Move with fixed velocity...")

        goal_position = POSITION_LIMIT

        progress = progressbar.ProgressBar()
        for _ in progress(range(NUM_FIXED_VELOCITY_MOVEMENT_STEPS)):
            goal_position *= -1
            # move to goal position within 2000 ms and wait there for 100 ms
            go_to(robot, goal_position, 2000, 100)

        # print("validate position")
        # validate_position(robot)

        # for i in range(NUM_FIXED_VELOCITY_MOVEMENT_STEPS):
        #    goal_position *= -1
        #    go_to(robot, goal_position, 1000, 100)

        # print("validate position")
        # validate_position(robot)

        # for i in range(NUM_FIXED_VELOCITY_MOVEMENT_STEPS):
        #    goal_position *= -1
        #    go_to(robot, goal_position, 500, 100)

        print("final position validation")
        validate_position(robot)

        go_to_zero(robot, 1000, 3000)


if __name__ == "__main__":
    main()
