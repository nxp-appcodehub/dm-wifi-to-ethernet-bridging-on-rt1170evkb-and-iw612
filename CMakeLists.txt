
# Copyright 2026 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.22.0)

cmake_path(APPEND conf_file_path ${CMAKE_CURRENT_LIST_DIR} ${board}_${core_id} prj.conf)
set(CONF_FILE ${conf_file_path} ${CONF_FILE})
include(${SdkRootDirPath}/cmake/extension/mcux.cmake)

project(wifi_ethernet_bridge_rt1170evkb_iw612 LANGUAGES C CXX ASM PROJECT_BOARD_PORT_PATH ${board_root}/${board}/wifi_examples/wifi_cli)

include(${SdkRootDirPath}/CMakeLists.txt)


include("${board}_${core_id}/board_files.cmake")

mcux_add_source(
    SOURCES wifi_cli/main.c
)

mcux_add_include(
    INCLUDES wifi_cli
)
mcux_add_iar_configuration(
	CC "--dlib_config full\
		--no_inline"
	CX "--no_clustering"
	LD "--semihosting"
)


mcux_add_source(
    SOURCES cm7/app.h
            cm7/hardware_init.c
            common/host_sleep.c
            common/host_sleep.h
            common/pin_mux.c
            common/pin_mux.h
            evkbmimxrt1170/FreeRTOSConfigBoard.h
            evkbmimxrt1170/sdmmc_config.c
            evkbmimxrt1170/sdmmc_config.h
            evkbmimxrt1170/wifi_bt_config.c
            evkbmimxrt1170/wifi_bt_config.h
            freertos_config/FreeRTOSConfig.h
            lwip_config/lwiphooks.h
            lwip_config/lwipopts.h
            lwip_config/lwippools.h
            wifi_config/wifi_config.h
)

mcux_add_include(
    INCLUDES cm7
             common
             freertos_config
             lwip_config
             wifi_config
             wifi_examples_wifi_cli
)

target_include_directories(${MCUX_SDK_PROJECT_NAME} PRIVATE
    ${SdkRootDirPath}/components/phy/device/phyrtl8211f
    ${SdkRootDirPath}/components/phy/mdio/enet
    ${SdkRootDirPath}/drivers/enet
    ${SdkRootDirPath}/components/phy
    ${SdkRootDirPath}/middleware/lwip/port/enet
)

target_sources(${MCUX_SDK_PROJECT_NAME} PRIVATE
    ${SdkRootDirPath}/drivers/enet/fsl_enet.c
    ${SdkRootDirPath}/components/phy/device/phyrtl8211f/fsl_phyrtl8211f.c
    ${SdkRootDirPath}/middleware/lwip/port/ethernetif.c
    ${SdkRootDirPath}/middleware/lwip/port/enet_ethernetif.c
    ${SdkRootDirPath}/middleware/lwip/port/enet_ethernetif_kinetis.c
    ${SdkRootDirPath}/middleware/lwip/src/netif/bridgeif.c
    ${SdkRootDirPath}/middleware/lwip/src/netif/bridgeif_fdb.c
    ${SdkRootDirPath}/middleware/lwip/port/ethernetif_mmac.c
)

mcux_add_mdk_configuration(
    CC "-mfloat-abi=hard"
)

mcux_add_iar_configuration(
	CC "--dlib_config full\
		--no_inline"
	CX "--no_clustering"
	LD "--semihosting"
)

mcux_add_macro(
    CC "-DFSL_SDK_ENABLE_DRIVER_CACHE_CONTROL=1\
       -DBOARD_NETWORK_USE_100M_ENET_PORT=0\
       -DUSE_RTOS=1\
       -DPRINTF_ADVANCED_ENABLE=1\
       -DSDMMCHOST_ENABLE_CACHE_LINE_ALIGN_TRANSFER=1\
       -DFSL_USDHC_ENABLE_SCATTER_GATHER_TRANSFER=1\
       -DCONFIG_TX_RX_ZERO_COPY=1\
       -DLWIP_NUM_NETIF_CLIENT_DATA=8"
)

mcux_add_macro(
    TOOLCHAINS armgcc
    TARGETS flexspi_nor_release flexspi_nor_debug
    AS "-D__STARTUP_INITIALIZE_RAMFUNCTION"
)

mcux_remove_armgcc_linker_script(
    BASE_PATH ${SdkRootDirPath}
    TARGETS flexspi_nor_debug flexspi_nor_release
    LINKER ${device_root}/RT/RT1170/MIMXRT1176/gcc/MIMXRT1176xxxxx_cm7_flexspi_nor.ld
)

mcux_remove_iar_linker_script(
    BASE_PATH ${SdkRootDirPath}
    TARGETS flexspi_nor_debug flexspi_nor_release
    LINKER ${device_root}/RT/RT1170/MIMXRT1176/iar/MIMXRT1176xxxxx_cm7_flexspi_nor.icf
)

mcux_remove_mdk_linker_script(
    BASE_PATH ${SdkRootDirPath}
    TARGETS flexspi_nor_debug flexspi_nor_release
    LINKER ${device_root}/RT/RT1170/MIMXRT1176/arm/MIMXRT1176xxxxx_cm7_flexspi_nor.scf
)

mcux_add_armgcc_linker_script(
    BASE_PATH ${SdkRootDirPath}
    TARGETS flexspi_nor_debug flexspi_nor_release
    LINKER ${board_root}/${board}/wifi_examples/common/linker/MIMXRT1176xxxxx_cm7_flexspi_nor.ld
)

mcux_add_iar_linker_script(
    BASE_PATH ${SdkRootDirPath}
    TARGETS flexspi_nor_debug flexspi_nor_release
    LINKER ${board_root}/${board}/wifi_examples/common/linker/MIMXRT1176xxxxx_cm7_flexspi_nor.icf
)

mcux_add_mdk_linker_script(
    BASE_PATH ${SdkRootDirPath}
    TARGETS flexspi_nor_debug flexspi_nor_release
    LINKER ${board_root}/${board}/wifi_examples/common/linker/MIMXRT1176xxxxx_cm7_flexspi_nor.scf
)

mcux_add_iar_configuration(
    LD "--config_def=__stack_size__=0x400\
        --config_def=__heap_size__=0x400"
)

mcux_add_mdk_configuration(
    LD "--predefine=\"-D__stack_size__=0x400\"\
        --predefine=\"-D__heap_size__=0x400\""
)

mcux_add_armgcc_configuration(
    LD "-Xlinker --defsym=__stack_size__=0x400\
        -Xlinker --defsym=__heap_size__=0x400"
)

mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)

mcux_add_include(INCLUDES evkbmimxrt1170)
