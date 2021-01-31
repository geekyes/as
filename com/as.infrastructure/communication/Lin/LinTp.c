/**
 * AS - the open source Automotive Software on https://github.com/parai
 *
 * Copyright (C) 2021  AS <parai@foxmail.com>
 *
 * This source code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation; See <http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */
/* ============================ [ INCLUDES  ] ====================================================== */
#include "LinIf.h"
#ifdef USE_BSWM
#include "BswM.h"
#endif
#include "asdebug.h"
/* ============================ [ MACROS    ] ====================================================== */
#define AS_LOG_LINTP 0

/* see ISO 15765-2 2004 */
#define N_PCI_MASK  0x30
#define N_PCI_SF    0x00
#define N_PCI_FF    0x10
#define N_PCI_CF    0x20
#define N_PCI_SF_DL 0x07

#define N_PCI_SN   0x0F
/* ============================ [ TYPES     ] ====================================================== */
/* ============================ [ DECLARES  ] ====================================================== */
/* ============================ [ DATAS     ] ====================================================== */
/* ============================ [ LOCALS    ] ====================================================== */
/* ============================ [ FUNCTIONS ] ====================================================== */
void LinTp_Init(const LinTp_ConfigType* ConfigPtr)
{
	(void*) ConfigPtr;
	asAssert(LinTp_Config.txPduNum == LinTp_Config.rxPduNum);
}

Std_ReturnType LinTp_Transmit(PduIdType TxPduId, const PduInfoType* PduInfoPtr)
{
	Std_ReturnType ercd = E_OK;
	NetworkHandleType network;
	LinTp_ContextType* context;

	if(TxPduId < LinTp_Config.txPduNum) {
		context = &LinTp_Config.context[TxPduId];
		network = LinTp_Config.txPduConfigs[TxPduId].network;
		#ifdef USE_BSWM
		BswM_LinTp_RequestMode(network, LINTP_DIAG_REQUEST);
		#else
		ercd = LinIf_ScheduleRequest(network, LinTp_Config.txPduConfigs[TxPduId].schTbl);
		#endif
		context->PduInfo = *PduInfoPtr;
		context->index = 0;
		context->SN = 1;
		context->timer = 0;
		ASLOG(LINTP, ("LinTp %d request transmit %d bytes\n", TxPduId, PduInfoPtr->SduLength));
	} else {
		ercd = E_NOT_OK;
	}

	return ercd;
}

Std_ReturnType LinTp_TriggerTransmit(PduIdType TxPduId, const PduInfoType* PduInfoPtr)
{
	Std_ReturnType ercd = E_NOT_OK;
	PduLengthType doSize, pos=0;
	LinTp_ContextType* context;
	uint8_t* data;

	if(TxPduId < LinTp_Config.txPduNum) {
		data = PduInfoPtr->SduDataPtr;
		context = &LinTp_Config.context[TxPduId];
		if(context->index < context->PduInfo.SduLength) {
			doSize = context->PduInfo.SduLength - context->index;
			data[pos++] = LinTp_Config.txPduConfigs[TxPduId].NA;
			if(0 == context->index) {
				if(doSize <= 6) {
					data[pos++] = N_PCI_SF|doSize;
				} else {
					doSize = 5;
					data[pos++] = N_PCI_FF|((doSize>>8)&0xF);
					data[pos++] = doSize&0xFF;
				}
			} else {
				if(doSize > 6) {
					doSize = 6;
				}
				data[pos++] = N_PCI_CF|context->SN;
				context->SN ++;
				if(context->SN > 15) {
					context->SN = 0;
				}
			}
			memcpy(&data[pos], context->PduInfo.SduDataPtr+context->index, doSize);
			pos += doSize;
			while(pos < 8) {
				data[pos++] = 0x55;
			}
			context->index += doSize;
			ASLOG(LINTP, ("LinTp %d provide %d bytes, left %d bytes\n", TxPduId, doSize, context->PduInfo.SduLength - context->index));
			ercd = E_OK;
		}
	}
	return ercd;
}

PduLengthType LinTp_TransmitLeft(PduIdType TxPduId)
{
	PduLengthType left = 0;
	LinTp_ContextType* context;

	if(TxPduId < LinTp_Config.txPduNum) {
		context = &LinTp_Config.context[TxPduId];
		left = context->PduInfo.SduLength - context->index;
	}

	return left;
}

Std_ReturnType LinTp_StartReception(PduIdType RxPduId, const PduInfoType* PduInfoPtr)
{
	Std_ReturnType ercd = E_OK;
	NetworkHandleType network;
	LinTp_ContextType* context;

	if(RxPduId < LinTp_Config.rxPduNum) {
		context = &LinTp_Config.context[RxPduId];
		network = LinTp_Config.rxPduConfigs[RxPduId].network;
		#ifdef USE_BSWM
		BswM_LinTp_RequestMode(network, LINTP_DIAG_RESPONSE);
		#else
		ercd = LinIf_ScheduleRequest(network, LinTp_Config.rxPduConfigs[RxPduId].schTbl);
		#endif
		context->PduInfo = *PduInfoPtr;
		context->index = 0;
		context->SN = 1;
		context->timer = LinTp_Config.rxPduConfigs[RxPduId].timeout;
		ASLOG(LINTP, ("LinTp %d start reception\n", RxPduId));
	} else {
		ercd = E_NOT_OK;
	}

	return ercd;
}

void LinTp_RxIndication(PduIdType RxPduId, const PduInfoType *PduInfo)
{
	LinTp_ContextType* context;

	if(RxPduId < LinTp_Config.rxPduNum) {
		context = &LinTp_Config.context[RxPduId];
		if(PduInfo->SduDataPtr[0] != LinTp_Config.rxPduConfigs[RxPduId].NA) {
			ASLOG(LINTP, ("ignore as NA R %X != E %X\n", PduInfo->SduDataPtr[0], LinTp_Config.rxPduConfigs[RxPduId].NA));
		} else {
			switch(PduInfo->SduDataPtr[1]&N_PCI_MASK) {
				case N_PCI_SF:
					break;
				case N_PCI_FF:
					break;
				case N_PCI_CF:
					break;
				default:
					ASLOG(LINTP, ("invalid frame"));
					break;
			}
			context->timer = LinTp_Config.rxPduConfigs[RxPduId].timeout;
		}
	}
}

void LinTp_MainFunction(void) {

	int i;
	LinTp_ContextType* context;
	NetworkHandleType network;
	for(i=0; i<LinTp_Config.rxPduNum; i++) {
		context = &LinTp_Config.context[i];
		if(context->timer > 0) {
			context->timer --;
			if (context->timer == 0) {
				#ifdef USE_LINTPGW
				LinTpGw_RxIndication(i, E_NOT_OK);
				#else
				#error TODO
				#endif
			}
		}
	}
}

void LinTp_Shutdown(void)
{

}

