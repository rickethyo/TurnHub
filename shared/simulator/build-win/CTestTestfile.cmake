# CMake generated Testfile for 
# Source directory: D:/TurnHub/Include/shared/simulator
# Build directory: D:/TurnHub/Include/shared/simulator/build-win
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[atlas_ui]=] "D:/TurnHub/Include/shared/simulator/build-win/bin/turnhub_sim.exe" "--device" "atlas" "--self-test")
set_tests_properties([=[atlas_ui]=] PROPERTIES  ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT "20" _BACKTRACE_TRIPLES "D:/TurnHub/Include/shared/simulator/CMakeLists.txt;80;add_test;D:/TurnHub/Include/shared/simulator/CMakeLists.txt;0;")
add_test([=[sigil_ui]=] "D:/TurnHub/Include/shared/simulator/build-win/bin/turnhub_sim.exe" "--device" "sigil" "--self-test")
set_tests_properties([=[sigil_ui]=] PROPERTIES  ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT "20" _BACKTRACE_TRIPLES "D:/TurnHub/Include/shared/simulator/CMakeLists.txt;81;add_test;D:/TurnHub/Include/shared/simulator/CMakeLists.txt;0;")
subdirs("_deps/sdl2-build")
subdirs("_deps/lvgl-build")
