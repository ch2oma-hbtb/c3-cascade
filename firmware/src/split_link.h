/**
 * @file split_link.h
 * @brief C3-Cascade — ESP-NOW split keyboard communication
 *
 * Handles wireless communication between master and slave halves
 * using ESP-NOW protocol.
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
};

// ============================================================================
// Packet structure (must fit in 250 bytes ESP-NOW limit)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint8_t             type;                   // split_packet_type_t
    uint8_t             slave_id;               // Slave identifier (0, 1, ...)
    uint8_t             matrix[MATRIX_ROWS];    // Row bitfields
    uint32_t            timestamp;              // Sender's millis()
    uint8_t             battery_level;          // Battery % (0–100, or 0xFF if unknown)
} split_packet_t;

// ============================================================================
// Slave info (tracked by master)
// ============================================================================

typedef struct {
    uint8_t             mac[6];                 // Slave MAC address
    matrix_state_t      matrix;                 // Last received matrix state
    uint32_t            last_seen;              // millis() of last packet
    uint8_t             battery_level;          // Last reported battery
    bool                connected;              // Is this slave active?
} slave_info_t;

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Initialize ESP-NOW for the configured role (master or slave)
 */
void split_link_init();

/**
 * @brief Process any pending received data (call in loop)
 * Master: checks for incoming slave reports
 * Slave: sends matrix report if state changed
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
 * @brief Get the number of configured slaves
 */
uint8_t split_link_slave_count();

/**
 * @brief Shut down ESP-NOW (before deep sleep)
 */
void split_link_shutdown();

#endif // C3CASCADE_SPLIT_LINK_H
