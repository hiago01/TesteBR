#!/bin/sh
cd "$(dirname "$0")" && g++ -std=c++17 -Wall -Wextra -O1 -x c++ -o /tmp/test_cam test_cam.test && /tmp/test_cam
