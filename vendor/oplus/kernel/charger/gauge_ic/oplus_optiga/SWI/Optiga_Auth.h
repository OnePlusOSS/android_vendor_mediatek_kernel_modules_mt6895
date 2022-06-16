#ifndef OPTIGA_AUTH_H
#define OPTIGA_AUTH_H

#include "../oplus_optiga.h"

BOOL Ecc_ReadODC (uint32_t * gf2n_ODC, uint32_t * gf2n_PublicKey);
BOOL Ecc_SendChallengeAndGetResponse( uint16_t * gf2n_Challenge, uint32_t * gf2n_XResponse, uint32_t * gf2n_ZResponse, BOOL bPolling, uint8_t bEccMode );
uint16_t Ecc_ReadODC_Burst(uint32_t *gf2n_ODC, uint32_t *gf2n_PublicKey) ;
BOOL Swi_ReadRegisterSpaceBurst(uint16_t uw_Address, uint8_t *ubp_Data);

#define ODC_BYTE_SIZE    (48u)
#define PUK_BYTE_SIZE    (24u)
#endif

