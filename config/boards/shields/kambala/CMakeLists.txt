target_sources(app PRIVATE status_led.c)
target_sources_ifdef(CONFIG_ZMK_PM_SOFT_OFF app PRIVATE behavior_soft_off_led.c)
