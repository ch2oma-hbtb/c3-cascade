/**
 * @file split_link.h
 * @brief C3-Cascade — Split keyboard wireless communication
 *
 * Handles wireless communication between master and slave halves
 * using either ESP-NOW or WiFi UDP transport (auto-selected per board).
 *
 * Features:
 *   - Auto-discovery: slaves broadcast DISCOVER, master responds with ACK
 *   - No hardcoded MAC addresses required
 *   - Transport-agnostic API
 *
 * Master: receives matrix reports from slaves, sends heartbeats
 * Slave:  sends matrix reports to master, receives heartbeats
 */

#ifndef C3CASCADE_SPLIT_LINK_H
#define C3CASCADE_SPLIT_LINK_H

#include "config.h"
#include "matrix.h"
#include <stdint.h>

// ============================================================================
// Packet types
// ============================================================================

enum split_packet_type_t : uint8_t {
    SPLIT_PKT_MATRIX_REPORT = 0x01,   // Slave → Master: key matrix state
    SPLIT_PKT_HEARTBEAT     = 0x02,   // Master → Slave: alive signal
    SPLIT_PKT_SYNC          = 0x03,   // Master → Slave: sync command
    SPLIT_PKT_ACK           = 0x04,   // Acknowledgment
    SPLIT_PKT_DISCOVER      = 0x10,   // Slave → Broadcast: "I'm a slave, pair me"
    SPLIT_PKT_DISCOVER_ACK  = 0x11,   // Master → Slave: "Paired, your ID is X"
};

// ============================================================================
// Packet structure
// Fits in ESP-NOW (250 byte limit) and UDP payload
// ============================================================================

typedef struct __attribute__((packed)) {
    uint8_t             type;                   // split_packet_type_t
    uint8_t             slave_id;               // Slave identifier (0, 1, ...)
    uint8_t             matrix[MATRIX_ROWS];    // Row bitfields
    uint32_t            timestamp;              // Sender's millis()
    uint8_t             battery_level;          // Battery % (0–100, or 0xFF if unknown)
    uint8_t             mac[6];                 // Sender's MAC (used during discovery)
} split_packet_t;

// ============================================================================
// Slave info (tracked by master)
// ============================================================================

enum split_transport_t : uint8_t {
    TRANSPORT_NONE = 0,
    TRANSPORT_ESPNOW = 1,
    TRANSPORT_WIFI_UDP = 2
};

typedef struct {
    uint8_t             mac[6];                 // Slave MAC address
    matrix_state_t      matrix;                 // Last received matrix state
    uint32_t            last_seen;              // millis() of last packet
    uint8_t             battery_level;          // Last reported battery
    bool                connected;              // Is this slave active?
    bool                discovered;             // Has this slave been discovered?
    split_transport_t   transport;              // Which transport is this slave using
    uint32_t            ip_address;             // (WiFi UDP only) IP address of slave
} slave_info_t;

// ============================================================================
// Public API (transport-agnostic)
// ============================================================================

/**
 * @brief Initialize the split link for the configured role and transport
 * Master: starts listening for discovery broadcasts and matrix reports
 * Slave:  begins discovery broadcast to find the master
 */
void split_link_init();

/**
 * @brief Process any pending received data (call in loop)
 * Master: checks for incoming slave reports and discovery requests
 * Slave:  handles discovery responses and heartbeats
 */
void split_link_update();

/**
 * @brief Send the local matrix state to the master (slave only)
 * @param state  Pointer to the current matrix state
 */
void split_link_send_matrix(const matrix_state_t* state);

/**
 * @brief Get the matrix state of a connected slave (master only)
 * @param slave_id  Slave index (0, 1, ...)
 * @return Pointer to the slave's matrix state, or nullptr if not connected
 */
const matrix_state_t* split_link_get_slave_matrix(uint8_t slave_id);

/**
 * @brief Check if a slave is connected (master only)
 * @param slave_id  Slave index
 */
bool split_link_slave_connected(uint8_t slave_id);

/**
 * @brief Get the number of discovered/configured slaves
 */
uint8_t split_link_slave_count();

/**
 * @brief Check if discovery is complete
 * @return true if at least one slave has been discovered (master)
 *         or if the master has been found (slave)
 */
bool split_link_is_paired();

/**
 * @brief Shut down the split link (before deep sleep)
 */
void split_link_shutdown();

/**
 * @brief Reset slave pairing state — clear all known slaves and
 *        re-enable discovery. Master only.
 */
void split_link_reset_pairing();

#endif // C3CASCADE_SPLIT_LINK_H
