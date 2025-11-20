# File: CMakeLists.txt
# Location: app/modules/uart_control/CMakeLists.txt

# This file tells the Zephyr build system to compile our C source file.
zephyr_library()

# List of source files to compile for this module
zephyr_library_sources(src/uart_control.c)
