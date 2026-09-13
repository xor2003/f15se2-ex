set pagination off
set confirm off
set debuginfod enabled off
set $placed = 0
set $steps = 0
break initMissionStrings
commands
silent
printf "MISSION_BEGIN\n"
continue
end
break updateFrame
commands
silent
if g_initPhase >= 2
if $placed == 0
set var g_ViewX = 557056
set var g_ViewY = 588800
set var g_viewZ = 1500
set var g_altitude = 1500
set var g_velocity = 21600
set var g_setThrust = 100
set var g_thrust = 100
set var g_ourHead = 0
set var g_ourRoll = 0
set var g_ourPitch = 0
set var g_orientationDirty = 1
set var g_autoLandingActive = 0
set var g_autopilotEngaged = 0
set var gameData->unk4 = 3
set var g_viewMode = 0
set $placed = 1
printf "FLIGHT_START x=%d y=%d z=%d\n", g_ViewX, g_ViewY, g_viewZ
end
set var g_savedPosVisible = 0
set $steps = $steps + 1
if $steps % 20 == 0
printf "PATH step=%d x=%d y=%d z=%d heading=%d pitch=%d speed=%d\n", $steps, g_ViewX, g_ViewY, g_viewZ, g_ourHead, g_ourPitch, g_knots
end
end
continue
end
break finalizeMission
commands
silent
printf "FLIGHT_END code=%d steps=%d x=%d y=%d z=%d\n", (int)$rdi, $steps, g_ViewX, g_ViewY, g_viewZ
if (int)$rdi != 2 || $steps < 2
quit 1
end
quit 0
end
run
