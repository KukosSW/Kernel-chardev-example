# Kernel_Module
Example of kernel module

My Char Device has simple functionality:

    - read data from user e.g echo "Kukos" > /dev/my_char_device

    - write the last read data e.g cat /dev/my_char_device:  "Kukos"

    - support at most 16 users in one time

    - SYSFS (/sys/class/my_cdev/my_cdev_sysfs)

        - write to stdout max supported users

        - changed max supported users (1 - 16)

    - DEBUGFS (/sys/kernel/debug/my_cdev_debugfs)

        - write to stdout current users

