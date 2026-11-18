# NXP Application Code Hub
[<img src="https://mcuxpresso.nxp.com/static/icon/nxp-logo-color.svg" width="100"/>](https://www.nxp.com)

## Wi-Fi to Ethernet Bridging on RT1170EVKB and IW612

This demo implements a transparent L2 (Layer 2) bridge on the MIMXRT1170-EVKB evaluation board using the IW612 Wi-Fi module (Murata 2EL M.2). The RT1170 acts as a wireless access point (UAP) and bridges Wi-Fi clients directly to the upstream Ethernet network.

Wi-Fi clients that associate with the UAP are placed on the same L2 network segment as Ethernet-connected devices. The bridge interface obtains its IP address via DHCP from the upstream router, enabling full bidirectional connectivity between Wi-Fi and Ethernet hosts without any routing or NAT.

#### Boards: MIMXRT1170-EVKB
#### Categories: Bridge, Wireless Connectivity
#### Peripherals: ETHERNET, Wi-Fi
#### Toolchains: VS Code

---

## Table of Contents
1. [Software](#step1)
2. [Hardware](#step2)
3. [Setup](#step3)
4. [Results](#step4)
5. [FAQs](#step5) 
6. [Support](#step6)
7. [Release Notes](#step7)

---

## 1. Software<a name="step1"></a>

- [MCUXpresso for VScode 1.5.61 or newer](https://www.nxp.com/products/processors-and-microcontrollers/arm-microcontrollers/general-purpose-mcus/lpc800-arm-cortex-m0-plus-/mcuxpresso-for-visual-studio-code:MCUXPRESSO-VSC?cid=wechat_iot_303216)
- [SDK v26.06.00 for MIMXRT1170EVKB](https://mcuxpresso.nxp.com/en/select)

---
## 2. Hardware<a name="step2"></a>
- MIMXRT1170-EVKB
- IW612 Murata 2EL M.2 module
- Ethernet Cable
- Wi-Fi Client (e.g. laptop)
- Ethernet Client (e.g. PC)

---

## 3. Setup<a name="step3"></a>

### 3.1 Install VS Code and MCUXpresso Extension

1. Download and install VS Code from https://code.visualstudio.com/download
2. Open VS Code, go to Extensions (Ctrl+Shift+X)
3. Search for **MCUXpresso for VS Code** and click Install
4. Install **Serial Monitor** extension (search "Serial Monitor") for UART output

### 3.2 Install ARM GCC Toolchain

Download and install ARM GCC 14.3.rel1 from:
https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads

Select the installer for your OS (Windows: `.exe`, Linux: `.tar.xz`, Mac: `.pkg`).

Note the installation path — you will need it in Step 3.4.

### 3.3 Install LinkServer (Flash/Debug Probe)

Download and install LinkServer from:
https://www.nxp.com/design/design-center/software/development-software/mcuxpresso-software-and-tools-/linkserver-for-microcontrollers:LINKERSERVER

LinkServer provides the debug probe driver for the MIMXRT1170-EVKB onboard debugger.

### 3.4 Install NXP SDK via MCUXpresso Extension

1. In VS Code, click the **MCUXpresso icon** in the left sidebar
2. Under **Installed SDKs** click **+** (Add SDK from Repository)
3. Select **MIMXRT1170-EVKB** from the board list
4. Select SDK version **v26.06.00**
5. Click **Install** — the extension downloads and installs the SDK automatically

Alternatively, download the SDK zip manually from https://mcuxpresso.nxp.com/en/select
(select board: MIMXRT1170-EVKB, version: v26.06.00), then use
**Install SDK from archive** in the MCUXpresso panel.

### 3.5 Import Project into VS Code

1. In VS Code MCUXpresso panel → **Projects** → click **Import Project**
2. Select **Import local project**
3. Browse to the cloned folder `wifi_ethernet_bridge_rt1170evkb_rw612/`
4. Click **Open** — VS Code detects `CMakeLists.txt` automatically
5. Select:
   - **SDK:** `v26.06.00`
   - **Board:** `evkbmimxrt1170`
   - **Core:** `cm7`
   - **Toolchain:** `GCC`
   - **Toolchain path:** path to your ARM GCC installation from Step 3.2
6. Click **Import**

### 3.6 Build the Project

- In the MCUXpresso panel → right-click the project → **Build**

### 3.7 Flash the Board

1. Connect MIMXRT1170-EVKB via the **USB debug port (J11)**
2. In MCUXpresso panel → right-click the project → **Flash**

   Or to flash and start a debug session simultaneously:
   - Select **LinkServer** as the debug probe when prompted 

### 3.7 Configure and Start the UAP

Open a serial terminal at 115200 baud, 8N1 on the RT1170 debug port (J11).

Add and start the UAP network profile:

```
wlan-add mybridge ssid <YOUR_SSID> role uap channel 6 wpa2 psk <YOUR_PASSWORD>
wlan-start-network mybridge
```

The bridge initializes automatically. Serial output:

```
# wlan-add mybridge ssid wifi_eth_bridge role uap channel 6 wpa2 psk 123456789
No IP address assigned for uAP. This is only for bridge mode.
Added "mybridge"

# wlan-start-network mybridge
[wlcm] Warn: NOTE: uAP will automatically switch to the channel that station is on.

# app_cb: WLAN: UAP Started
========================================
Soft AP "wifi_eth_bridge" started successfully
========================================
[dhcp] Warn: server not dhcpd_running.

================================================
 Bridge br0: eth0 (ENET_1G) + uap0 (Wi-Fi AP)
 DHCP started on br0 - waiting for IP from router...
================================================

[ETH0] flags=0x57 up=1 link=1 mac=54:27:8d:6a:7d:96

[UAP0] flags=0x57 up=1 link=1 mac=52:26:ef:a2:f2:26

[BR0] flags=0x7f up=1 link=1 mac=54:27:8d:6a:7d:96
========================================
[DHCP br0] state: REQUESTING
[DHCP br0] state: BOUND

================================================
 Bridge IP  : 192.168.1.144
 Subnet mask: 255.255.255.0
 Gateway    : 192.168.1.1
================================================
```

### 3.8 Connect a Wi-Fi Client

Connect a laptop to the UAP SSID. The client receives a DHCP lease from the upstream router (not from the RT1170) and is placed on the same subnet as Ethernet-connected devices.

---

## 4. Results<a name="step4"></a>

### Bridge Initialization

```
[ETH] Link up
[ETH0] flags=0x4e up=0 link=1 mac=54:27:8d:6a:7d:96
Soft AP "wifi_eth_bridge" started successfully
DHCP started on br0 - waiting for IP from router...
[DHCP br0] state: REQUESTING
[DHCP br0] state: BOUND
Bridge IP  : 192.168.1.144
Subnet mask: 255.255.255.0
Gateway    : 192.168.1.1
```

### iPerf test (bidirectional)

Run bidirectional iPerf test:

```bash
# On Windows (server)
iperf3.exe -s

# On Ubuntu (client)
iperf3 -c <windows_ip> -t 30 -i 1
```

### Ping Latency

```
# Windows to Ubuntu via bridge
ping 192.168.1.193
Pinging 192.168.1.193 with 32 bytes of data:
Reply from 192.168.1.193: bytes=32 time=23ms TTL=64
Reply from 192.168.1.193: bytes=32 time=56ms TTL=64
Reply from 192.168.1.193: bytes=32 time=14ms TTL=64
Reply from 192.168.1.193: bytes=32 time=6ms TTL=64

Ping statistics for 192.168.1.193:
    Packets: Sent = 4, Received = 4, Lost = 0 (0% loss),
Approximate round trip times in milli-seconds:
    Minimum = 6ms, Maximum = 56ms, Average = 24ms
```

## 5. FAQs<a name="step5"></a>

## 6. Support<a name="step6"></a>

#### Project Metadata

<!----- Boards ----->
[![Board badge](https://img.shields.io/badge/Board-MIMXRT1170&ndash;EVKB-blue)]()

<!----- Categories ----->
[![Category badge](https://img.shields.io/badge/Category-BRIDGE-yellowgreen)](https://mcuxpresso.nxp.com/appcodehub?category=bridge)
[![Category badge](https://img.shields.io/badge/Category-WIRELESS%20CONNECTIVITY-yellowgreen)](https://mcuxpresso.nxp.com/appcodehub?category=wireless_connectivity)

<!----- Peripherals ----->
[![Peripheral badge](https://img.shields.io/badge/Peripheral-ETHERNET-yellow)](https://mcuxpresso.nxp.com/appcodehub?peripheral=ethernet)
[![Peripheral badge](https://img.shields.io/badge/Peripheral-WI&ndash;FI-yellow)](https://mcuxpresso.nxp.com/appcodehub?peripheral=wifi)

<!----- Toolchains ----->
[![Toolchain badge](https://img.shields.io/badge/Toolchain-VS%20CODE-orange)](https://mcuxpresso.nxp.com/appcodehub?toolchain=vscode)

Questions regarding the content/correctness of this example can be entered as Issues within this GitHub repository.

>**Note**: For more general technical questions regarding NXP Microcontrollers and the difference in expected functionality, enter your questions on the [NXP Community Forum](https://community.nxp.com/)

[![Follow us on Youtube](https://img.shields.io/badge/Youtube-Follow%20us%20on%20Youtube-red.svg)](https://www.youtube.com/NXP_Semiconductors)
[![Follow us on LinkedIn](https://img.shields.io/badge/LinkedIn-Follow%20us%20on%20LinkedIn-blue.svg)](https://www.linkedin.com/company/nxp-semiconductors)
[![Follow us on Facebook](https://img.shields.io/badge/Facebook-Follow%20us%20on%20Facebook-blue.svg)](https://www.facebook.com/nxpsemi/)
[![Follow us on Twitter](https://img.shields.io/badge/X-Follow%20us%20on%20X-black.svg)](https://x.com/NXP)

## 7. Release Notes<a name="step7"></a>
| Version | Description / Update                           | Date                        |
|:-------:|------------------------------------------------|----------------------------:|
| 1.0     | Initial release on Application Code Hub        | November 18<sup>th</sup> 2026 |
