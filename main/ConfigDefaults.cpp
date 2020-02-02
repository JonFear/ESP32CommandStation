/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2019-2020 Mike Dunston

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see http://www.gnu.org/licenses
**********************************************************************/

#include <utils/constants.hxx>

// GCC pre-compiler trick to expand the value from a #define constant
#define DEFAULT_CONST_EXPAND_VALUE(var, value) DEFAULT_CONST(var, value)

// GCC pre-compiler trick to expand the value from a #define constant
#define OVERRIDE_CONST_EXPAND_VALUE(var, value) OVERRIDE_CONST(var, value)

#include "sdkconfig.h"

#include "DefaultConfigs.h"

///////////////////////////////////////////////////////////////////////////////
// This is the priority at which the app_main will increase to prior to handing
// over to the LCC Executor.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST(cs_main_task_priority, 1);

///////////////////////////////////////////////////////////////////////////////
// This flag will clear the stored configuration data causing the command
// station to regenerate the configuration from scratch. This is usually not
// necessary.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST_FALSE(cs_force_factory_reset);

///////////////////////////////////////////////////////////////////////////////
// This flag will force a factory reset by removing the LCC_CDI_FILE and
// LCC_CONFIG_FILE before starting the OpenMRN stack. This should not normally
// be required.
///////////////////////////////////////////////////////////////////////////////
#if CONFIG_LCC_FACTORY_RESET
DEFAULT_CONST_TRUE(lcc_force_factory_reset);
#else
DEFAULT_CONST_FALSE(lcc_force_factory_reset);
#endif

///////////////////////////////////////////////////////////////////////////////
// This flag controls the fsync call inteval for the LCC node config file when
// using the SD card as the storage device. When using SPIFFS as the storage
// device this setting will not be used.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST(lcc_sd_sync_interval_sec, 10);

///////////////////////////////////////////////////////////////////////////////
// This flag controls the printing of all LCC GridConnect packets.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST_FALSE(lcc_print_all_packets);

///////////////////////////////////////////////////////////////////////////////
// This flag controls automatic creation of Locomotive roster entries based on
// the request from the LCC FindProtocolServer -> AllTrainNodes::allocate_node
// call.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST_TRUE(cs_train_db_auto_create_entries);

///////////////////////////////////////////////////////////////////////////////
// This flag controls the automatic persistence of the locomotive roster list.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST(cs_train_db_auto_persist_sec, 30);

///////////////////////////////////////////////////////////////////////////////
// This flag controls the automatic persistence of the turnouts list.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST(cs_turnouts_auto_persist_sec, 30);

///////////////////////////////////////////////////////////////////////////////
// This flag will print a list of FreeRTOS tasks every ~5min. This is not
// recommended to be enabled except during debugging sessions as it will cause
// the FreeRTOS scheduler to remain in a "locked" state for an extended period.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST_FALSE(cs_task_list_report);
DEFAULT_CONST(cs_task_list_list_interval_sec, 300);

///////////////////////////////////////////////////////////////////////////////
// This flag controls how often the CS task stats will be reported.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST(cs_task_stats_report_interval_sec, 45);

///////////////////////////////////////////////////////////////////////////////
// This is the number of pending dcc::Packet objects that the LocalTrackIf will
// use in it's FixedPool.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST(cs_track_pool_size, 5);

///////////////////////////////////////////////////////////////////////////////
// Enabling this will print all RailCom packet data as it arrives at the hub.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST_FALSE(enable_railcom_packet_dump);

///////////////////////////////////////////////////////////////////////////////
// This controls the ability to enable RailCom via configuration parameters.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST_FALSE(cs_railcom_enabled);

///////////////////////////////////////////////////////////////////////////////
// This is the number of pending dcc::Packet objects that the RMT driver will
// allow to be queued for outbound delivery.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST(rmt_packet_queue_ops, 10);
DEFAULT_CONST(rmt_packet_queue_prog, 5);

///////////////////////////////////////////////////////////////////////////////
// This is the number of pending dcc::Packet objects that the RMT driver will
// allow to be queued for outbound delivery.
///////////////////////////////////////////////////////////////////////////////
#if CONFIG_OPS_ENERGIZE_ON_STARTUP
DEFAULT_CONST_TRUE(cs_energize_ops_on_boot);
#else
DEFAULT_CONST_FALSE(cs_energize_ops_on_boot);
#endif

///////////////////////////////////////////////////////////////////////////////
// This controls how many DCC e-stop packets will be generated before the
// e-stop handler will discontinue sending packets.
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST(cs_estop_packet_count, 200);

///////////////////////////////////////////////////////////////////////////////
// Status LED configuration settings
///////////////////////////////////////////////////////////////////////////////
#if CONFIG_STATUS_LED
DEFAULT_CONST_TRUE(status_led_enabled);
#else
DEFAULT_CONST_FALSE(status_led_enabled);
#endif
DEFAULT_CONST_EXPAND_VALUE(status_led_pin, CONFIG_STATUS_LED_DATA_PIN);
DEFAULT_CONST_EXPAND_VALUE(status_led_brightness, CONFIG_STATUS_LED_BRIGHTNESS);
DEFAULT_CONST(status_led_update_interval_msec, 450);

///////////////////////////////////////////////////////////////////////////////
// Increase the number of memory spaces available at runtime to account for the
// Traction protocol CDI/FDI needs.
///////////////////////////////////////////////////////////////////////////////
OVERRIDE_CONST(num_memory_spaces, 10);

///////////////////////////////////////////////////////////////////////////////
// Increase the GridConnect buffer size to improve performance by bundling more
// than one GridConnect packet into the same send() call to the socket.
///////////////////////////////////////////////////////////////////////////////
OVERRIDE_CONST_EXPAND_VALUE(gridconnect_buffer_size, CONFIG_TCP_MSS);

///////////////////////////////////////////////////////////////////////////////
// This will allow up to 1000 usec for the buffer to fill up before sending it
// out over the socket connection.
///////////////////////////////////////////////////////////////////////////////
OVERRIDE_CONST(gridconnect_buffer_delay_usec, 500);

///////////////////////////////////////////////////////////////////////////////
// This limites the number of outbound GridConnect packets which limits the
// memory used by the BufferPort.
///////////////////////////////////////////////////////////////////////////////
OVERRIDE_CONST(gridconnect_bridge_max_outgoing_packets, 2);

///////////////////////////////////////////////////////////////////////////////
// This increases number of state flows to invoke before checking for any FDs
// that have pending data.
///////////////////////////////////////////////////////////////////////////////
OVERRIDE_CONST(executor_select_prescaler, 60);

///////////////////////////////////////////////////////////////////////////////
// This increases the number of local nodes and aliases available for the LCC
// stack. This is needed to allow for virtual train nodes.
///////////////////////////////////////////////////////////////////////////////
OVERRIDE_CONST(local_nodes_count, 30);
OVERRIDE_CONST(local_alias_cache_size, 30);

///////////////////////////////////////////////////////////////////////////////
// HC12 configuration settings
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST(hc12_buffer_size, 256);
DEFAULT_CONST(hc12_uart_speed, 19200);

///////////////////////////////////////////////////////////////////////////////
// Nextion configuration settings
///////////////////////////////////////////////////////////////////////////////
DEFAULT_CONST_EXPAND_VALUE(nextion_uart_num, NEXTION_UART_NUM);
DEFAULT_CONST_EXPAND_VALUE(nextion_uart_speed, NEXTION_UART_BAUD);
DEFAULT_CONST(nextion_buffer_size, 512);
DEFAULT_CONST_EXPAND_VALUE(nextion_rx_pin, NEXTION_RX_PIN);
DEFAULT_CONST_EXPAND_VALUE(nextion_tx_pin, NEXTION_TX_PIN);