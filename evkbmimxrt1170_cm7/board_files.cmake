
# Copyright 2026 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

mcux_add_configuration(
    CC "-DSDK_DEBUGCONSOLE=1"
    CX "-DSDK_DEBUGCONSOLE=1"
)


mcux_add_source(
    SOURCES evkbmimxrt1170/board.c
            evkbmimxrt1170/board.h
)

mcux_add_include(
    INCLUDES evkbmimxrt1170
)

mcux_add_source(
    SOURCES clock/clock_config.c
            clock/clock_config.h
)

mcux_add_include(
    INCLUDES clock
)
