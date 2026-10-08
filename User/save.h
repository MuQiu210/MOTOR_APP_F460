/// @file  save.h
/// @brief NVStore persistence and firmware metadata interface.

#ifndef __APP_USER_SAVE_H__
#define __APP_USER_SAVE_H__

#include <stdint.h>

/// @brief Initialize NVStore and load persisted data and firmware metadata.
void save_init(void);

/// @brief Request that the current save cache be persisted later from the main loop.
void save_request(void);

/// @brief Register the periodic save task with the scheduler.
void save_cycle_register(void);

/// @brief Read the stored firmware length and MD5 metadata.
/// @param length Optional destination for firmware length.
/// @param md5 Optional destination for the 16-byte firmware MD5.
void firmware_info_recv(uint32_t *length, uint8_t md5[16]);

#endif /* __APP_USER_SAVE_H__ */
