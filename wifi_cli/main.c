/*
 *  Copyright 2026 NXP
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

///////////////////////////////////////////////////////////////////////////////
//  Includes
///////////////////////////////////////////////////////////////////////////////
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "wlan_bt_fw.h"
#include "wlan.h"
#include "wifi.h"
#include "wm_net.h"
#include <osa.h>
#if CONFIG_NXP_WIFI_SOFTAP_SUPPORT
#include "dhcp-server.h"
#endif
#include "cli.h"
#include "wifi_ping.h"
#include "iperf.h"
#include "app.h"
#ifndef RW610
#include "wifi_bt_config.h"
#else
#include "fsl_power.h"
#include "fsl_ocotp.h"
#endif
#include "cli_utils.h"
#if CONFIG_HOST_SLEEP
#include "host_sleep.h"
#endif
#include "fsl_common.h"

/* ---- ENET_1G / PHY / bridge includes ---- */
#include "fsl_iomuxc.h"
#include "fsl_enet.h"           /* mcuxsdk/drivers/enet/fsl_enet.h        */
#include "fsl_phyrtl8211f.h"    /* components/phy/device/phyrtl8211f/     */
#include "fsl_silicon_id.h"     /* components/silicon_id/                 */
#include "lwip/netifapi.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "netif/ethernet.h"
#include "ethernetif.h"
#include "netif/bridgeif.h"
#include "lwip/etharp.h"
#include "fsl_silicon_id.h"     /* MAC from silicon ID */

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* ENET_1G + RTL8211F */
#define EXAMPLE_ENET            ENET_1G
#define EXAMPLE_PHY_ADDRESS     BOARD_ENET1_PHY_ADDRESS   /* 0x01 on EVKB  */
#define EXAMPLE_PHY_OPS         (&phyrtl8211f_ops)
#define EXAMPLE_PHY_RESOURCE    (&g_phy_resource)
#define EXAMPLE_NETIF_INIT_FN   ethernetif1_init
#define EXAMPLE_CLOCK_FREQ      CLOCK_GetRootClockFreq(kCLOCK_Root_Bus)

/* Thread config */
#define INIT_THREAD_STACKSIZE   1024
#define INIT_THREAD_PRIO        DEFAULT_THREAD_PRIO

#if CONFIG_WPS2
#define MAIN_TASK_STACK_SIZE    6000
#else
#define MAIN_TASK_STACK_SIZE    4096
#endif

/*******************************************************************************
 * Variables
 ******************************************************************************/
phy_rtl8211f_resource_t g_phy_resource;

static struct netif eth_netif;  /* eth0 — ENET_1G Ethernet port */
static struct netif br_netif;   /* br0  — lwIP bridge interface */
static phy_handle_t phyHandle;

/* Forward declaration for DHCP monitor thread */
static void print_dhcp_state(void *arg);

/*******************************************************************************
 * ENET_1G MDIO / clock helpers
 ******************************************************************************/
void BOARD_InitModuleClock(void)
{
    const clock_sys_pll1_config_t sysPll1Config = { .pllDiv2En = true };
    CLOCK_InitSysPll1(&sysPll1Config);

    /* 125 MHz for ENET_1G RGMII */
    clock_root_config_t rootCfg = { .mux = 4, .div = 4 };
    CLOCK_SetRootClock(kCLOCK_Root_Enet2, &rootCfg);

    /* 198 MHz bus clock (MDIO source) — syspll2pfd3 = 396 MHz / 2 */
    CLOCK_InitPfd(kCLOCK_PllSys2, kCLOCK_Pfd3, 24);
    rootCfg.mux = 7;
    rootCfg.div = 2;
    CLOCK_SetRootClock(kCLOCK_Root_Bus, &rootCfg);
}

void IOMUXC_SelectENETClock(void)
{
    /* Enable RGMII TX clock output on ENET_1G */
    IOMUXC_GPR->GPR5 |= IOMUXC_GPR_GPR5_ENET1G_RGMII_EN_MASK;
}

void BOARD_ENETFlexibleConfigure(enet_config_t *config)
{
    config->miiMode = kENET_RgmiiMode;
}

static void MDIO_Init(void)
{
    (void)CLOCK_EnableClock(s_enetClock[ENET_GetInstance(EXAMPLE_ENET)]);
    ENET_SetSMI(EXAMPLE_ENET, EXAMPLE_CLOCK_FREQ, false);
}

static status_t MDIO_Write(uint8_t phyAddr, uint8_t regAddr, uint16_t data)
{
    return ENET_MDIOWrite(EXAMPLE_ENET, phyAddr, regAddr, data);
}

static status_t MDIO_Read(uint8_t phyAddr, uint8_t regAddr, uint16_t *pData)
{
    return ENET_MDIORead(EXAMPLE_ENET, phyAddr, regAddr, pData);
}

/*******************************************************************************
 * Bridge helpers
 ******************************************************************************/
static void dump_netif(const char *tag, struct netif *n)
{
    PRINTF("\r\n[%s] flags=0x%02x up=%d link=%d mac=%02x:%02x:%02x:%02x:%02x:%02x\r\n",
           tag, n->flags, netif_is_up(n),
           !!(n->flags & NETIF_FLAG_LINK_UP),
           n->hwaddr[0], n->hwaddr[1], n->hwaddr[2],
           n->hwaddr[3], n->hwaddr[4], n->hwaddr[5]);
}

/**
 * @brief Create lwIP L2 bridge: br0 = eth0 (ENET_1G) + uap0 (Wi-Fi AP)
 *        Run DHCP client on br0 to get IP from upstream router.
 */
static err_t setup_bridge(struct netif *enet_port, struct netif *wifi_port)
{
    bridgeif_initdata_t br_init = {
        .ethaddr = {{ enet_port->hwaddr[0], enet_port->hwaddr[1], enet_port->hwaddr[2],
                enet_port->hwaddr[3], enet_port->hwaddr[4], enet_port->hwaddr[5] }},
        .max_ports = 2,
        .max_fdb_dynamic_entries = 64,
        .max_fdb_static_entries = 8
    };
    ip4_addr_t ipaddr, netmask, gw;
    ip4_addr_set_zero(&ipaddr);
    ip4_addr_set_zero(&netmask);
    ip4_addr_set_zero(&gw);

    /* Add bridge netif — this is the one that gets an IP */
    err_t err = netifapi_netif_add(&br_netif, &ipaddr, &netmask, &gw,
                                   &br_init, bridgeif_init, ethernet_input);
    if (err != ERR_OK)
    {
        PRINTF("[BRIDGE] netifapi_netif_add failed (%d)\r\n", err);
        return err;
    }
    netifapi_netif_set_default(&br_netif);
    netifapi_netif_set_up(&br_netif);

    netif_set_flags(enet_port, NETIF_FLAG_ETHERNET);
    netif_set_flags(wifi_port, NETIF_FLAG_ETHERNET);

    /* Add Ethernet port */
    if (bridgeif_add_port(&br_netif, enet_port) != ERR_OK)
    {
        PRINTF("[BRIDGE] add eth0 port failed\r\n");
        return ERR_IF;
    }
    netifapi_netif_set_up(enet_port);

    /* Set ENET_1G MAC to promiscuous mode so it accepts frames for all MACs */
    EXAMPLE_ENET->RCR |= ENET_RCR_PROM_MASK;
    

    /* Add Wi-Fi uAP port */
    if (bridgeif_add_port(&br_netif, wifi_port) != ERR_OK)
    {
        PRINTF("[BRIDGE] add uap0 port failed\r\n");
        return ERR_IF;
    }

    // struct netif *n;
    // for (n = netif_list; n != NULL; n = n->next) {
    //     PRINTF("[NETIF] %c%c%d mac=%02x:%02x:%02x:%02x:%02x:%02x\r\n",
    //         n->name[0], n->name[1], netif_get_index(n),
    //         n->hwaddr[0], n->hwaddr[1], n->hwaddr[2],
    //         n->hwaddr[3], n->hwaddr[4], n->hwaddr[5]);
    // }

    // PRINTF("[BRIDGE] eth0 linkoutput=%p input=%p\r\n",
    //    (void*)enet_port->linkoutput, (void*)enet_port->input);
    // PRINTF("[BRIDGE] uap0 linkoutput=%p input=%p\r\n",
    //    (void*)wifi_port->linkoutput, (void*)wifi_port->input);

    /* ------------------------------------------------------------------ */
    /* Start DHCP on bridge interface — br0 gets IP from upstream router   */
    /* ------------------------------------------------------------------ */
    err = netifapi_dhcp_start(&br_netif);
    if (err != ERR_OK)
    {
        PRINTF("[BRIDGE] dhcp_start failed (%d)\r\n", err);
        return err;
    }

    PRINTF("\r\n================================================\r\n");
    PRINTF(" Bridge br0: eth0 (ENET_1G) + uap0 (Wi-Fi AP)\r\n");
    PRINTF(" DHCP started on br0 - waiting for IP from router...\r\n");
    PRINTF("================================================\r\n");

    dump_netif("ETH0", enet_port);
    dump_netif("UAP0", wifi_port);
    dump_netif("BR0",  &br_netif);

    /* Spawn DHCP state monitor — prints IP once BOUND */
    if (sys_thread_new("dhcp_mon", print_dhcp_state, &br_netif,
                       2048, INIT_THREAD_PRIO) == NULL)
    {
        LWIP_ASSERT("setup_bridge: dhcp_mon task creation failed", 0);
    }

    return ERR_OK;
}

/**
 * @brief Polls DHCP state on br0 and prints IP when BOUND.
 */
static void print_dhcp_state(void *arg)
{
    struct netif *netif = (struct netif *)arg;
    struct dhcp  *dhcp;
    u8_t          last_state = DHCP_STATE_OFF;

    while (netif_is_up(netif))
    {
        dhcp = netif_dhcp_data(netif);
        if (dhcp != NULL && dhcp->state != last_state)
        {
            last_state = dhcp->state;
            PRINTF("[DHCP br0] state: ");
            switch (last_state)
            {
                case DHCP_STATE_OFF:         PRINTF("OFF");         break;
                case DHCP_STATE_REQUESTING:  PRINTF("REQUESTING");  break;
                case DHCP_STATE_INIT:        PRINTF("INIT");        break;
                case DHCP_STATE_REBOOTING:   PRINTF("REBOOTING");   break;
                case DHCP_STATE_REBINDING:   PRINTF("REBINDING");   break;
                case DHCP_STATE_RENEWING:    PRINTF("RENEWING");    break;
                case DHCP_STATE_SELECTING:   PRINTF("SELECTING");   break;
                case DHCP_STATE_INFORMING:   PRINTF("INFORMING");   break;
                case DHCP_STATE_CHECKING:    PRINTF("CHECKING");    break;
                case DHCP_STATE_BOUND:       PRINTF("BOUND");       break;
                case DHCP_STATE_BACKING_OFF: PRINTF("BACKING_OFF"); break;
                default:                     PRINTF("%u", last_state); break;
            }
            PRINTF("\r\n");

            if (last_state == DHCP_STATE_BOUND)
            {
                PRINTF("\r\n================================================\r\n");
                PRINTF(" Bridge IP  : %s\r\n", ipaddr_ntoa(&netif->ip_addr));
                PRINTF(" Subnet mask: %s\r\n", ipaddr_ntoa(&netif->netmask));
                PRINTF(" Gateway    : %s\r\n", ipaddr_ntoa(&netif->gw));
                PRINTF("================================================\r\n");
            }
        }
        sys_msleep(20U);
    }
    vTaskDelete(NULL);
}
/**
 * @brief lwIP + ENET_1G init thread.
 *        Adds eth0 netif, waits for PHY link-up.
 *        Runs once at startup, then deletes itself.
 */
static void stack_init(void *arg)
{
    ethernetif_config_t enet_config = {
        .phyHandle   = &phyHandle,
        .phyAddr     = EXAMPLE_PHY_ADDRESS,
        .phyOps      = EXAMPLE_PHY_OPS,
        .phyResource = EXAMPLE_PHY_RESOURCE,
        .srcClockHz  = EXAMPLE_CLOCK_FREQ,
    };

    LWIP_UNUSED_ARG(arg);

    /* Derive MAC from RT1170 silicon ID */
    (void)SILICONID_ConvertToMacAddr(&enet_config.macAddress);

    net_ipv4stack_init();

    netifapi_netif_add(&eth_netif, NULL, NULL, NULL,
                       &enet_config, EXAMPLE_NETIF_INIT_FN, tcpip_input);
    // netifapi_netif_set_up(&eth_netif);

    /* Wait for Ethernet link-up (retry indefinitely) */
    while (ethernetif_wait_linkup(&eth_netif, 5000) != ERR_OK)
    {
        PRINTF("[ETH] PHY auto-negotiation failed - check cable\r\n");
    }

    PRINTF("[ETH] Link up\r\n");
    dump_netif("ETH0", &eth_netif);

    vTaskDelete(NULL);
}

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
int wlan_driver_init(void);
int wlan_driver_deinit(void);
int wlan_driver_reset(void);
int wlan_reset_cli_init(void);
#ifndef RW610
#if CONFIG_HOST_SLEEP
int wlan_hs_cli_init(void);
#endif
#endif

static void task_main(osa_task_param_t arg);

static OSA_TASK_DEFINE(task_main, WLAN_TASK_PRI_LOW, 1, MAIN_TASK_STACK_SIZE, 0);

OSA_TASK_HANDLE_DEFINE(task_main_Handle);

static void printSeparator(void)
{
    (void)PRINTF("========================================\r\n");
}

/* Callback Function passed to WLAN Connection Manager. The callback function
 * gets called when there are WLAN Events that need to be handled by the
 * application.
 */
int wlan_event_callback(enum wlan_event_reason reason, void *data)
{
    int ret;
    struct wlan_ip_config addr;
    char ssid[IEEEtypes_SSID_SIZE + 1] = {0};
    char ip[16];
    static int auth_fail = 0;
#if CONFIG_NXP_WIFI_SOFTAP_SUPPORT
    wlan_uap_client_disassoc_t *disassoc_resp = data;
#endif
    int i;

    switch (reason)
    {
        case WLAN_REASON_INITIALIZED:
            (void)PRINTF("app_cb: WLAN initialized\r\n");
            printSeparator();

            ret = wlan_basic_cli_init();
            if (ret != WM_SUCCESS)
            {
                (void)PRINTF("Failed to initialize BASIC WLAN CLIs\r\n");
                return 0;
            }

            ret = wlan_cli_init();
            if (ret != WM_SUCCESS)
            {
                (void)PRINTF("Failed to initialize WLAN CLIs\r\n");
                return 0;
            }
            (void)PRINTF("WLAN CLIs are initialized\r\n");
            printSeparator();

            ret = wlan_enhanced_cli_init();
            if (ret != WM_SUCCESS)
            {
                (void)PRINTF("Failed to initialize WLAN CLIs\r\n");
                return 0;
            }
            (void)PRINTF("ENHANCED WLAN CLIs are initialized\r\n");
            printSeparator();
#ifdef RW610
#if CONFIG_HOST_SLEEP
            ret = host_sleep_cli_init();
            if (ret != WM_SUCCESS)
            {
                (void)PRINTF("Failed to initialize WLAN CLIs\r\n");
                return 0;
            }
            (void)PRINTF("HOST SLEEP CLIs are initialized\r\n");
            printSeparator();
#endif
#endif
            ret = ping_cli_init();
            if (ret != WM_SUCCESS)
            {
                (void)PRINTF("Failed to initialize PING CLI\r\n");
                return 0;
            }

            ret = iperf_cli_init();
            if (ret != WM_SUCCESS)
            {
                (void)PRINTF("Failed to initialize IPERF CLI\r\n");
                return 0;
            }

#if CONFIG_NXP_WIFI_SOFTAP_SUPPORT
            ret = dhcpd_cli_init();
            if (ret != WM_SUCCESS)
            {
                (void)PRINTF("Failed to initialize DHCP Server CLI\r\n");
                return 0;
            }
#endif

            (void)PRINTF("CLIs Available:\r\n");
            printSeparator();
            help_command(0, NULL);
            printSeparator();
            break;
        case WLAN_REASON_INITIALIZATION_FAILED:
            (void)PRINTF("app_cb: WLAN: initialization failed\r\n");
            break;
        case WLAN_REASON_AUTH_SUCCESS:
            (void)PRINTF("app_cb: WLAN: authenticated to network\r\n");
            break;
        case WLAN_REASON_SUCCESS:
            (void)PRINTF("app_cb: WLAN: connected to network\r\n");
            ret = wlan_get_address(&addr);
            if (ret != WM_SUCCESS)
            {
                (void)PRINTF("failed to get IP address\r\n");
                return 0;
            }

            net_inet_ntoa(addr.ipv4.address, ip);

            ret = wlan_get_current_network_ssid(ssid);
            if (ret != WM_SUCCESS)
            {
                (void)PRINTF("Failed to get External AP network\r\n");
                return 0;
            }

            (void)PRINTF("Connected to following BSS:\r\n");
            (void)PRINTF("SSID = [%s]\r\n", ssid);
            if (addr.ipv4.address != 0U)
            {
                (void)PRINTF("IPv4 Address: [%s]\r\n", ip);
            }
#if CONFIG_IPV6
            for (i = 0; i < CONFIG_MAX_IPV6_ADDRESSES; i++)
            {
                if (ip6_addr_isvalid(((int8_t)(addr.ipv6[i].addr_state))) != 0)
                {
                    (void)PRINTF("IPv6 Address: %-13s:\t%s (%s)\r\n",
                                 ipv6_addr_type_to_desc((struct net_ipv6_config *)&addr.ipv6[i]),
                                 inet6_ntoa(addr.ipv6[i].address), ipv6_addr_state_to_desc(addr.ipv6[i].addr_state));
                }
            }
            (void)PRINTF("\r\n");
#endif
            auth_fail = 0;
            break;
        case WLAN_REASON_CONNECT_FAILED:
            (void)PRINTF("app_cb: WLAN: connect failed\r\n");
            break;
        case WLAN_REASON_NETWORK_NOT_FOUND:
            (void)PRINTF("app_cb: WLAN: network not found\r\n");
            break;
        case WLAN_REASON_NETWORK_AUTH_FAILED:
            (void)PRINTF("app_cb: WLAN: network authentication failed\r\n");
            auth_fail++;
            if (auth_fail >= 3)
            {
                (void)PRINTF("Authentication Failed. Disconnecting ... \r\n");
                (void)wlan_disconnect();
                auth_fail = 0;
            }
            break;
        case WLAN_REASON_ADDRESS_SUCCESS:
            (void)PRINTF("network mgr: DHCP new lease\r\n");
            break;
        case WLAN_REASON_ADDRESS_FAILED:
            (void)PRINTF("app_cb: failed to obtain an IP address\r\n");
            break;
        case WLAN_REASON_USER_DISCONNECT:
            (void)PRINTF("app_cb: disconnected\r\n");
            auth_fail = 0;
            break;
        case WLAN_REASON_LINK_LOST:
            (void)PRINTF("app_cb: WLAN: link lost\r\n");
            break;
        case WLAN_REASON_CHAN_SWITCH:
            (void)PRINTF("app_cb: WLAN: channel switch\r\n");
            break;
#if CONFIG_NXP_WIFI_SOFTAP_SUPPORT
        case WLAN_REASON_UAP_SUCCESS:
            (void)PRINTF("app_cb: WLAN: UAP Started\r\n");
            ret = wlan_get_current_uap_network_ssid(ssid);

            if (ret != WM_SUCCESS)
            {
                (void)PRINTF("Failed to get Soft AP network\r\n");
                return 0;
            }

            printSeparator();
            (void)PRINTF("Soft AP \"%s\" started successfully\r\n", ssid);
            printSeparator();
            /*ret = dhcp_server_start(net_get_uap_handle());
            if (ret != 0)
            {
                PRINTF("%s\r\n", dhcp_server_err_str(ret));
            }
            else
            {
                (void)PRINTF("DHCP Server started successfully\r\n");
            }*/
			dhcp_server_stop();

            struct netif *uap_if = net_get_uap_handle();
           if (uap_if == NULL) {
                PRINTF("[BRIDGE] ERROR: uap_if is NULL\r\n");
                return 0;
            }
            // PRINTF("[BRIDGE] uap_if ptr=%p linkoutput=%p\r\n",
            //     (void*)uap_if, (void*)uap_if->linkoutput);
            // struct netif *uap_if = net_get_uap_interface();
            if (uap_if != NULL)
            {
                ip4_addr_t zero;
                ip4_addr_set_zero(&zero);
                netifapi_netif_set_addr(uap_if, &zero, &zero, &zero);
                netifapi_dhcp_stop(uap_if);
                netifapi_netif_set_up(uap_if);  /* keep link up for forwarding */
            }

            // PRINTF("[BRIDGE] uap_if ptr=%p linkoutput=%p\r\n", (void*)uap_if, (void*)uap_if->linkoutput);

            if (setup_bridge(&eth_netif, uap_if) != ERR_OK)
            {
                PRINTF("[BRIDGE] setup_bridge() failed!\r\n");
            }
            printSeparator();
            break;
        case WLAN_REASON_UAP_CLIENT_ASSOC:
            (void)PRINTF("app_cb: WLAN: UAP a Client Associated\r\n");
            printSeparator();
            (void)PRINTF("Client => ");
            print_mac((const char *)data);
            (void)PRINTF("Associated with Soft AP\r\n");
            printSeparator();
            break;
        case WLAN_REASON_UAP_CLIENT_CONN:
            (void)PRINTF("app_cb: WLAN: UAP a Client Connected\r\n");
            printSeparator();
            (void)PRINTF("Client => ");
            print_mac((const char *)data);
            (void)PRINTF("Connected with Soft AP\r\n");
            printSeparator();
            break;
        case WLAN_REASON_UAP_CLIENT_DISSOC:
            printSeparator();
            (void)PRINTF("app_cb: WLAN: UAP a Client Dissociated:");
            (void)PRINTF(" Client MAC => ");
            print_mac((const char *)(disassoc_resp->sta_addr));
            (void)PRINTF(" Reason code => ");
            (void)PRINTF("%d\r\n", disassoc_resp->reason_code);
            printSeparator();
            break;
        case WLAN_REASON_UAP_STOPPED:
            (void)PRINTF("app_cb: WLAN: UAP Stopped\r\n");
            printSeparator();
            (void)PRINTF("Soft AP stopped successfully\r\n");
			/* Stop DHCP on bridge when UAP goes down */
            netifapi_dhcp_stop(&br_netif);
            PRINTF("DHCP on br0 stopped\r\n");
            printSeparator();

            /*
			dhcp_server_stop();

            (void)PRINTF("DHCP Server stopped successfully\r\n");
            printSeparator();
			*/
            break;
#endif /* CONFIG_NXP_WIFI_SOFTAP_SUPPORT */
        case WLAN_REASON_PS_ENTER:
            break;
        case WLAN_REASON_PS_EXIT:
            break;
        case WLAN_REASON_RSSI_LOW:
#if CONFIG_SUBSCRIBE_EVENT_SUPPORT
        case WLAN_REASON_RSSI_HIGH:
        case WLAN_REASON_SNR_LOW:
        case WLAN_REASON_SNR_HIGH:
        case WLAN_REASON_MAX_FAIL:
        case WLAN_REASON_BEACON_MISSED:
        case WLAN_REASON_DATA_RSSI_LOW:
        case WLAN_REASON_DATA_RSSI_HIGH:
        case WLAN_REASON_DATA_SNR_LOW:
        case WLAN_REASON_DATA_SNR_HIGH:
        case WLAN_REASON_LINK_QUALITY:
        case WLAN_REASON_PRE_BEACON_LOST:
#endif
            break;
        case WLAN_REASON_FW_HANG:
        case WLAN_REASON_FW_RESET:
#ifdef RW610
            (void)PRINTF("app_cb: WLAN: FW hang Event: %d\r\n", reason);
#endif
            break;
        default:
            (void)PRINTF("app_cb: WLAN: Unknown Event: %d\r\n", reason);
            break;
    }
    return 0;
}

int wlan_driver_init(void)
{
    int result = 0;

#if CONFIG_FW_DNLD_ASYNC
    result = wlan_init_nb(wlan_fw_bin, wlan_fw_bin_len, wlan_event_callback);
#else
    /* Initialize WIFI Driver */
    result = wlan_init(wlan_fw_bin, wlan_fw_bin_len);

    assert(0 == result);

    result = wlan_start(wlan_event_callback);

    assert(0 == result);
#endif
    return result;
}

int wlan_driver_deinit(void)
{
    int result = wlan_stop();
    assert(0 == result);
    wlan_deinit(0);
    return result;
}

static void wlan_hw_reset(void)
{
    BOARD_WIFI_BT_Enable(false);
    OSA_TimeDelay(10);
    BOARD_WIFI_BT_Enable(true);
}

int wlan_driver_reset(void)
{
    int result = wlan_driver_deinit();
    assert(0 == result);
    wlan_hw_reset();
    result = wlan_driver_init();
    assert(0 == result);
    return result;
}

static void test_wlan_reset(int argc, char **argv)
{
    (void)wlan_driver_reset();
}

#ifndef RW610
#if CONFIG_HOST_SLEEP
static void test_mcu_suspend(int argc, char **argv)
{
    (void)mcu_suspend();
}

static struct cli_command hs_commands[] = {
    {"mcu-suspend", NULL, test_mcu_suspend},
};

int wlan_hs_cli_init(void)
{
    unsigned int i;

    for (i = 0; i < sizeof(hs_commands) / sizeof(struct cli_command); i++)
    {
        if (cli_register_command(&hs_commands[i]) != 0)
        {
            return -1;
        }
    }

    return 0;
}
#endif
#endif

static struct cli_command reset_commands[] = {
    {"wlan-reset", NULL, test_wlan_reset},
#if CONFIG_HOST_SLEEP
    {"mcu-suspend", NULL, test_mcu_suspend},
#endif
};

int wlan_reset_cli_init(void)
{
    unsigned int i;
    for (i = 0; i < sizeof(reset_commands) / sizeof(struct cli_command); i++)
    {
        if (cli_register_command(&reset_commands[i]) != 0)
            return -1;
    }
    return 0;
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    PRINTF("\r\n*** STACK OVERFLOW in task: %s ***\r\n", pcTaskName);
    taskDISABLE_INTERRUPTS();
    for (;;);
}

void vApplicationMallocFailedHook(void)
{
    PRINTF("\r\n*** MALLOC FAILED — heap exhausted! Free: %u bytes ***\r\n",
           (unsigned)xPortGetFreeHeapSize());
    taskDISABLE_INTERRUPTS();
    for (;;);
}

static void task_main(osa_task_param_t arg)
{
    int32_t result = 0;
    (void)result;

    (void)PRINTF("Initialize CLI\r\n");
    printSeparator();

    result = cli_init();
    assert(WM_SUCCESS == result);

    result = wlan_reset_cli_init();
    assert(WM_SUCCESS == result);

#if CONFIG_HOST_SLEEP
#ifndef RW610
    hostsleep_init(wlan_hs_pre_cfg, wlan_hs_post_cfg);
#else
    (void)hostsleep_init();
#endif
#endif

    (void)PRINTF("Initialize WLAN Driver\r\n");
    printSeparator();

    /* Initialize WIFI Driver */
    result = wlan_driver_init();

    assert(WM_SUCCESS == result);

#ifndef RW610
#if CONFIG_HOST_SLEEP
    result = wlan_hs_cli_init();

    assert(WM_SUCCESS == result);
#endif
#endif

#if CONFIG_FW_DNLD_ASYNC
    wlan_nb_task_cleanup();
#endif

    while (true)
    {
        /* wait for interface up */
        OSA_TimeDelay(5000);
    }
}

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
int main(void)
{
    OSA_Init();

    /*BOARD_InitHardware();*/
    BOARD_ConfigMPU();
    BOARD_InitBootPins();
    BOARD_InitPinsM2();          /* USDHC1 SDIO pins for IW612 M.2 */
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    /* ---- ENET_1G hardware init ---- */
    BOARD_InitModuleClock();
    IOMUXC_SelectENETClock();
    BOARD_InitEnet1GPins();

    /* RTL8211F PHY reset: >=10ms low, then high, 30ms settle */
    gpio_pin_config_t gpio_config = { kGPIO_DigitalOutput, 0, kGPIO_NoIntmode };
    GPIO_PinInit(GPIO11, 14U, &gpio_config);
    SDK_DelayAtLeastUs(10000U, CLOCK_GetFreq(kCLOCK_CpuClk));
    GPIO_WritePinOutput(GPIO11, 14U, 1U);
    SDK_DelayAtLeastUs(30000U, CLOCK_GetFreq(kCLOCK_CpuClk));

    EnableIRQ(ENET_1G_MAC0_Tx_Rx_1_IRQn);
    EnableIRQ(ENET_1G_MAC0_Tx_Rx_2_IRQn);

    MDIO_Init();
    g_phy_resource.read  = MDIO_Read;
    g_phy_resource.write = MDIO_Write;
    /* ---- end ENET_1G init ---- */
#ifdef RW610
    POWER_PowerOffBle();
#endif

    printSeparator();
    PRINTF("Wi-Fi to Ethernet L2 Bridge Demo\r\n");
    PRINTF("Board: MIMXRT1170-EVKB  Module: IW612 (Murata 2EL M.2)\r\n");
    printSeparator();

    /* Spawn lwIP + ENET_1G init thread */
    if (sys_thread_new("stack_init", stack_init, NULL, INIT_THREAD_STACKSIZE, INIT_THREAD_PRIO) == NULL)
    {
        LWIP_ASSERT("main: stack_init task creation failed", 0);
    }

    /* Spawn WLAN + CLI task */
    (void)OSA_TaskCreate((osa_task_handle_t)task_main_Handle, OSA_TASK(task_main), NULL);
/*
    printSeparator();
    (void)PRINTF("wifi cli demo\r\n");
    printSeparator();

    (void)OSA_TaskCreate((osa_task_handle_t)task_main_Handle, OSA_TASK(task_main), NULL);
*/

    PRINTF("Free heap before start: %u bytes\r\n", (unsigned)xPortGetFreeHeapSize());
    OSA_Start();

    return 0;
}