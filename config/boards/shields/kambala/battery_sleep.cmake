# aroum/zmk blocks deep sleep while BLE is connected. Build an adapted copy of
# activity.c so the normal ZMK inactivity/wakeup handling also works when paired.
# Keep the USB power guard and leave the dependency checkout untouched.
function(kambala_enable_battery_sleep)
  set(activity_source "${APPLICATION_SOURCE_DIR}/src/activity.c")
  file(READ "${activity_source}" activity_code)

  set(connection_guard [=[    bool isConnectedToHost = false;
#if IS_ENABLED(CONFIG_ZMK_BLE)
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    isConnectedToHost = zmk_ble_active_profile_is_connected();
#elif IS_ENABLED(CONFIG_ZMK_SPLIT)
    isConnectedToHost = zmk_split_bt_peripheral_is_connected();
#endif
#endif

    if (!isConnectedToHost && inactive_time > MAX_SLEEP_MS && !is_usb_power_present()) {]=])
  string(FIND "${activity_code}" "${connection_guard}" guard_offset)
  if(guard_offset EQUAL -1)
    message(FATAL_ERROR "Kambala: activity.c changed; review the battery sleep adaptation")
  endif()
  string(REPLACE "${connection_guard}"
    "    if (inactive_time >= MAX_SLEEP_MS && !is_usb_power_present()) {"
    activity_code "${activity_code}")

  set(generated_source "${CMAKE_CURRENT_BINARY_DIR}/kambala_activity.c")
  file(WRITE "${generated_source}" "${activity_code}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${activity_source}")

  get_target_property(app_sources app SOURCES)
  set(replaced_sources)
  set(activity_count 0)
  foreach(source IN LISTS app_sources)
    if(source STREQUAL "src/activity.c" OR source STREQUAL "${activity_source}")
      list(APPEND replaced_sources "${generated_source}")
      math(EXPR activity_count "${activity_count} + 1")
    else()
      list(APPEND replaced_sources "${source}")
    endif()
  endforeach()
  if(NOT activity_count EQUAL 1)
    message(FATAL_ERROR "Kambala: expected exactly one ZMK activity.c source")
  endif()
  set_property(TARGET app PROPERTY SOURCES "${replaced_sources}")
  message(STATUS "Kambala: battery deep sleep ignores BLE connection; USB prevents sleep")
endfunction()

# Shield CMake runs before app/CMakeLists.txt adds activity.c to the app target.
cmake_language(DEFER DIRECTORY "${APPLICATION_SOURCE_DIR}"
  CALL kambala_enable_battery_sleep)
