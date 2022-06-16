/**
 * @file   Optiga_Auth.c
 * @date   Mar, 2016
 * @brief  Implementation of authentication flow
 *
 */

#include "Optiga_Auth.h"
#include "Optiga_Swi.h"
#include "Optiga_Nvm.h"
#include "../ECC/Optiga_Ecc.h"
#include "../Platform/board.h"
#include <linux/delay.h>


static BOOL Optiga_SendChallenge(uint16_t* challenge, BOOL bPolling, uint8_t cmd);
static BOOL Optiga_ReadResponse_burst(uint32_t *Z_resp, uint32_t *X_resp, uint8_t bEccMode);

/* ****************************************************************************
   name:      Ecc_ReadODC()

   function:  Read out ODC and public key from OPTIGA

   input:     OUT: gf2n_ODC
                gf2n_t array holding the ODC.
              OUT: gf2n_PublicKey
                gf2n_t array holding the public key.

   output:    bool

   return:    true, if all was ok.
              false, if errors detected.

   date:
 ************************************************************************* */
BOOL Ecc_ReadODC (uint32_t * gf2n_ODC, uint32_t * gf2n_PublicKey)
{
	if(Nvm_ReadData( 0x0100 + (8 * 23), 48, (uint8_t*)gf2n_ODC ) == FALSE)
		return FALSE;

	if(Nvm_ReadData( 0x0100 + (8 * 29), 18, (uint8_t*)gf2n_PublicKey ) == FALSE)
		return FALSE;

	return TRUE;
}

/* ****************************************************************************
   name:      Ecc_SendChallengeAnGetResponse()

   function:  send a calculated challenge to OPTIGA and start ECC engine of
              the OPTIGA IC.
              if OPTIGA indicates calculation finished, the results are read
              from OPTIGA memory space into the arrays gf2n_XResponse and
              gf2n_ZResponse.

   input:     IN: gf2n_Challenge
                gf2n_t array holding the challenge to be issued.
              OUT: gf2n_XResponse
                gf2n_t array holding the x part of the OPTIGA response.
              OUT: gf2n_ZResponse
                gf2n_t array holding the z part of the OPTIGA response.
              IN: bPolling
                polling mode to check OPTIGA engine for calculation finished
                state.
                if 'true', then a wait of 200ms is done before the data is
                read back, else the host waits for SWI IRQ signal.
                NOTE: please use FALSE, as that is the most efficient way
                      to handle the ECC engine.
   output:    bool

   return:    true, if all was ok.
              false, if errors detected.

   date:
 ************************************************************************* */
BOOL Ecc_SendChallengeAndGetResponse(uint16_t * gf2n_Challenge, uint32_t * gf2n_XResponse, uint32_t * gf2n_ZResponse, BOOL bPolling, uint8_t bEccMode )
{
	BOOL ret = FALSE;

	ret = Optiga_SendChallenge(gf2n_Challenge, bPolling, bEccMode);
	if (ret == FALSE) {
		printk(" Ecc_SendChallengeAndGetResponse: Optiga_SendChallenge failed.\n");
		return FALSE;
	}

	// fixed time wait of at least 50ms
#ifdef BURST_READ_INTERVAL
	usleep_range(34000, 34000);
	//printk(" Ecc_SendChallengeAndGetResponse after wait 34ms \n");
#endif
	/*ret = Optiga_ReadResponse(gf2n_ZResponse, gf2n_XResponse, bEccMode);*/
	ret = Optiga_ReadResponse_burst(gf2n_ZResponse, gf2n_XResponse, bEccMode);
	return ret;
}

static BOOL Optiga_SendChallenge(uint16_t * challenge, BOOL bPolling, uint8_t bEccMode)
{
	uint8_t ubCap7Value;
	uint8_t ubInt0Value;
	uint8_t ubIndex;
	uint8_t ubWordIndex;
	uint8_t ubData;

	struct oplus_optiga_chip * optiga_chip_info = oplus_get_optiga_info();
	unsigned long flags;

	if (bPolling)
	{
		ubCap7Value = 0x00u;
		ubInt0Value = 0x00u;
	}
	else
	{
		ubCap7Value = 0x80u;
		ubInt0Value = 0x01u;
	}

	spin_lock_irqsave(&optiga_chip_info->slock, flags);
	if( Swi_WriteConfigSpace( SWI_CAP7, ubCap7Value, 0x80u ) == FALSE )
	{
		spin_unlock_irqrestore(&optiga_chip_info->slock, flags);
		return FALSE;
	}
	spin_unlock_irqrestore(&optiga_chip_info->slock, flags);
	msleep(10);

	spin_lock_irqsave(&optiga_chip_info->slock, flags);
	if (Swi_WriteConfigSpace( SWI_INT0, ubInt0Value, 0x01u ) == FALSE )
	{
		spin_unlock_irqrestore(&optiga_chip_info->slock, flags);
		return FALSE;
	}
	spin_unlock_irqrestore(&optiga_chip_info->slock, flags);
	msleep(10);

	spin_lock_irqsave(&optiga_chip_info->slock, flags);
	/*select device register set */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_EREG);

	/* write 16 bytes of challenge */
	for( ubIndex = 0u; ubIndex < 16u; ubIndex++ )
	{
		/* set start each aligned 8 byte addresses */
		if ((ubIndex & 0x07u) == 0u )
		{
			spin_unlock_irqrestore(&optiga_chip_info->slock, flags);
			msleep(10);

			spin_lock_irqsave(&optiga_chip_info->slock, flags);
			Swi_SendRawWordNoIrq(SWI_ERA, (((0x0040u + ubIndex) >> 8u) & 0xFFu));
			Swi_SendRawWordNoIrq(SWI_WRA, ( (0x0040u + ubIndex)        & 0xFFu));
		}

		ubWordIndex = (ubIndex >> 1u);
		if( (ubIndex & 1u) == 1u )
		{
			ubData = (uint8_t)((challenge[ubWordIndex] >> 8u) & 0x00FFu);
		}
		else
		{
			ubData = (uint8_t)(challenge[ubWordIndex] & 0xFFu);
		}

		/* write data w/o any interruption */
		Swi_SendRawWordNoIrq( SWI_WD, ubData);
	}
	spin_unlock_irqrestore(&optiga_chip_info->slock, flags);
	msleep(10);

	spin_lock_irqsave(&optiga_chip_info->slock, flags);
	/* write remaining last bytes */
	Swi_SendRawWordNoIrq(SWI_ERA, ((0x0340u >> 8u) & 0xFFu));
	Swi_SendRawWordNoIrq(SWI_WRA, ( 0x0340u        & 0xFFu));
	Swi_SendRawWordNoIrq( SWI_WD, (uint8_t)(challenge[8] & 0xFFu));

	/* start ECC calculation */
	Swi_SendRawWordNoIrq(SWI_BC, SWI_OPTIGA_ECCSTART|bEccMode);
	spin_unlock_irqrestore(&optiga_chip_info->slock, flags);

	return TRUE;
}



/*Burst read ecc response */
#define ECC_RX_RESPONSE_X_REG (0x0010u) /*!< Base Address of WIP2. Same as ECC RX ResponseX */
#define ECC_RX_RESPONSE_X_SIZE (8u)     /*!< ECC RX ResponseX is 8 bytes */
#define ECC_RX_RESPONSE_Z_REG (0x0030u) /*!< ECC RX ResponseZ */
#define ECC_RX_RESPONSE_Z_SIZE (0xFu)   /*!< ECC RX ResponseZ is 16 + 1 bytes */
#define ECC_RX_RESPONSE_Z_MSB (0x0330u) /*!< ECC RX ResponseZ MSB*/

#define ZRESP_SIZE 16
#define XRESP_SIZE 16
#define XRESP_READ_SIZE 8
#define LOAD_SIZE_4_BYTES (4)

/**
* @brief Read data from EREG space register in burst mode.
* @param uw_Address EREG register address
* @param ubp_Data Data from the address to be returned.
*/
BOOL Swi_ReadRegisterSpaceBurst(uint16_t uw_Address, uint8_t *ubp_Data) {
	BOOL ret = FALSE;
	unsigned long flags;
	uint8_t data = 0;
	uint8_t index = LOAD_SIZE_4_BYTES;
	uint8_t len = 8;
	uint8_t loop = 0;
	uint8_t j = 0;
	struct oplus_optiga_chip * optiga_chip_info = oplus_get_optiga_info();

	/*Read 4 bytes every times*/
	for (loop = 0; loop < len/LOAD_SIZE_4_BYTES; loop++) {
		index = LOAD_SIZE_4_BYTES;
		spin_lock_irqsave(&optiga_chip_info->slock, flags);
		Swi_SendRawWordNoIrq(SWI_BC, SWI_RBL2);                    /*!< set burst length to 4 byte */
		Swi_SendRawWordNoIrq(SWI_BC, SWI_EREG);                    /*!< select EREG register set */
		Swi_SendRawWordNoIrq(SWI_ERA, HIGH_BYTE_ADDR(uw_Address)); /*!< send high byte of address */
		spin_unlock_irqrestore(&optiga_chip_info->slock, flags);

		msleep(10);
		spin_lock_irqsave(&optiga_chip_info->slock, flags);
		Swi_SendRawWordNoIrq(SWI_RRA, LOW_BYTE_ADDR(uw_Address)); /*!< send low byte of address */
		while (index) {
			ret = Swi_ReceiveRawWord(&data);
			if (ret != TRUE) {
				break;
			}
			ubp_Data[j++] = data;
			index--;
		}
		spin_unlock_irqrestore(&optiga_chip_info->slock, flags);
		uw_Address += LOAD_SIZE_4_BYTES;
		msleep(10);
	}
	return ret;
}


static BOOL Optiga_ReadResponse_burst(uint32_t *Z_resp, uint32_t *X_resp, uint8_t bEccMode) {
	uint8_t ubIndex = 0;
	uint8_t ubData[16] = {0};
	unsigned long flags = 0;
	int i = 0;
	int temp = 0;

	BOOL ret = FALSE;
	struct oplus_optiga_chip * optiga_chip_info = oplus_get_optiga_info();
	memset(ubData, 0, sizeof(ubData));

	if (NULL == optiga_chip_info) {
		return FALSE;
	}

	ret = Swi_ReadRegisterSpaceBurst(ECC_RX_RESPONSE_Z_REG, ubData);
	if (ret == FALSE) {
        	printk("Optiga_ReadResponse_burst read first 8 bytes of ECC_RX_RESPONSE_Z_REG failed.\n");
        	return FALSE;
	}
	
	ret = Swi_ReadRegisterSpaceBurst(ECC_RX_RESPONSE_Z_REG + 8, ubData + 8);
	if ( ret == FALSE) {
		printk("Optiga_ReadResponse_burst read  second 8 bytes of ECC_RX_RESPONSE_Z_REG failed.\n");
		return FALSE;
    	}
	convert8_to_32(Z_resp, ubData, ZRESP_SIZE);
	memset(X_resp, 0, XRESP_SIZE);

	/*Burst Read*/
	ret = Swi_ReadRegisterSpaceBurst(ECC_RX_RESPONSE_X_REG, ubData);
	if (ret == FALSE) {
            printk("Optiga_ReadResponse_burst read ECC_RX_RESPONSE_X_REG failed.\n");
            return FALSE;
	}
	
	convert8_to_32(X_resp, ubData, XRESP_READ_SIZE);

	spin_lock_irqsave(&optiga_chip_info->slock, flags);
	/*!< read remaining ZResponse bits */
	if (Swi_ReadRegisterSpace(ECC_RX_RESPONSE_Z_MSB, ubData) != TRUE) {
		spin_unlock_irqrestore(&optiga_chip_info->slock, flags);
		printk("Optiga_ReadResponse_burst read ECC_RX_RESPONSE_Z_MSB failed.\n");
		return FALSE;
	}
	Z_resp[4] = (uint32_t)ubData[0];
	spin_unlock_irqrestore(&optiga_chip_info->slock, flags);

	return TRUE;
}

/**
* @brief Read out ODC and ECC public key from the device.
* @param gf2n_ODC Returns the ODC 
* @param gf2n_PublicKey Returns the ECC Public Key
*/
uint16_t Ecc_ReadODC_Burst(uint32_t *gf2n_ODC, uint32_t *gf2n_PublicKey) 
{
	uint8_t ubODC[ODC_BYTE_SIZE];
	uint8_t ubPUBKEY[PUK_BYTE_SIZE];
	uint16_t ret = APP_ECC_INIT;

	memset(ubODC, 0, sizeof(ubODC));
	memset(ubPUBKEY, 0, sizeof(ubPUBKEY));
	ret = Nvm_ReadODC(ubODC, ubPUBKEY, NVM_BURST_BYTE_READ);
	if (ret != INF_SWI_SUCCESS) {
		ret = APP_ECC_E_READ_ODC;
	}

	convert8_to_32(gf2n_ODC, ubODC, ODC_BYTE_SIZE);
	convert8_to_32(gf2n_PublicKey, ubPUBKEY, PUK_BYTE_SIZE);
	return ret;
}

