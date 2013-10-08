#include "taskFlyport.h"
#include "FWUpdate.h"
#include "libpic30.h"


updtRep report;

static unsigned long int FlashLoc = 0x1C0000;
TCP_SOCKET ftpSock;
static void FWDebug(char *dbgstr);

/**********************************************************************************************
 * 	void FWUpdateInit(): 
 *	The function simply initializes the external SPI flash memory.
 *						 
 * 	Parameters:
 *	-
 *
 * 	Returns:
 *	-
************************************************************************************************/
void FWUpdateInit()
{
	SPIFlashInit();
}

static char buffer[STREAM_CHUNK_DIM + 1];
/**********************************************************************************************
 * 	BYTE FWUpdateFTP(char *ServerIP, char *ServerPort, char *user, char *pwd, char *binfile, char *md5file ): 
 *	The function downloads the bin file from the specified FTP server and writes it inside the 
 *	external flash memory. Once the memory is written, it's possible to validate the operation 
 *	specifying an md5 key file on the server. An md5 key is generated starting from the content
 *	of the memory and compared with the one downloaded from the file. If they match, the operation
 *	has accomplished succesffully, otherwise, some data has been corrupted during download. In this
 *	case is possible to start a new download, without problem for the Flyport, since the program 
 *	memory has not been written.
 *						 
 * 	Parameters:
 *	char *ServerIP - IP address of the FTP server
 *	char *ServerPort - Port of the FTP server
 *	char *user - Username
 *	char *pwd - Password
 *	char *binfile - Bin file containing the new firmware
 *	char *md5file - Md5 file containing the md5 key, use NO_MD5 to avoid md5 check
 *	char *buffer - The buffer used for the bin file
 *
 * 	Returns:
 *	The report for the operation.  
************************************************************************************************/
BYTE FWUpdateFTP(char *ServerIP, char *ServerPort, char *user, char *pwd, char *binfile, char *md5file)
{
	int opReport, counter = 0;
	unsigned long len, totLen = 0;

	
	//	Check on network connection, if not present, error is returned
	#if defined (FLYPORT_WF)
	if (WFGetStat() != CONNECTED)
	{
		FWDebug("ERROR: Flyport not connected to a network!\n");
		report.event = ERR_NOT_CONNECTED;
		report.subEvent = FTP_ERR_NOT_CREATED;
		return report.event;
	}
	#elif defined(FLYPORT_ETH)
	if (!MACLinked)
	{
		FWDebug("ERROR: Flyport not connected to a network!\n");
		report.event = ERR_NOT_CONNECTED;
		report.subEvent = FTP_ERR_NOT_CREATED;
		return report.event;
	}
	#endif
	//	----- CONNECTION WITH FTP SERVER -----
	opReport = FTPConnect(&ftpSock, ServerIP, ServerPort, user, pwd);
	if (opReport != FTP_CONNECTED)
	{
		report.event = ERR_CONNECTING_SERVER;
		report.subEvent = opReport;	
		FTPClose(ftpSock);
		return report.event;
	}
	
	while (1)
	{	
		opReport = FTPSendCmd(ftpSock, "TYPE I", NULL, 0);
		if ((opReport != FTP_ERR_SERV_TIMEOUT) && ((opReport/100) == 2))
			break;
		else
			counter++;
		if (counter >= 5)
		{
			report.event = ERR_CONFIG_SERVER;
			report.subEvent = FTP_ERR_SERV_TIMEOUT;	
			FTPClose(ftpSock);
			return report.event;
		}
	}
	//	----- DOWNLOADING THE NEW FIRMWARE FILE	-----
	opReport = FTPStreamOpen(ftpSock, binfile, RETR);
	if (opReport == FTP_CONNECTED)
	{
		totLen = 0;
		//	Initializing Flash writing at address 0x1C0000
		SPIFlashBeginWrite(FlashLoc);
		#ifdef _DBG_FW
		int blink = 0;
		#endif
		//	The file is downloaded and written in flash
		//	NOTE: at the moment no check is performed on file integrity,
		//	md5 will be used in the following
		while (FTPStreamStat() == FTP_STREAM_READING)
		{
			len = FTPStreamRead(buffer, STREAM_CHUNK_DIM, 4);
			totLen += len;
			#ifdef _DBG_FW
			if (len > 0)
				blink++;
			if (blink>=10)
			{
				char data[33];
				sprintf(data, "Data:%lu\n", totLen);
				FWDebug(data);
				blink = 0;
			}
			#endif
			SPIFlashWriteArray((BYTE*)buffer, len);
		}
		//	Check on completion of download (not about integrity!)
		if (FTPStreamStat() != FTP_STREAM_EOF)
		{
			report.event = ERR_FW_FILE;
			report.subEvent = FTP_ERR_SERV_DISCONNECTED;	
			sprintf(buffer, "\nDISCONNECTED - Firmware length: %lu\n", totLen);
			FWDebug(buffer);
			FTPClose(ftpSock);
			return report.event;
		}
		sprintf(buffer, "\nOK - Firmware length: %lu\n", totLen);
		FWDebug(buffer);
		FTPStreamClose();
	}
	else
	{
		//	An error occured entering in stream mode, error is reported
		FWDebug("Error downloading file\n");
		FTPStreamClose();
		report.event = ERR_FW_FILE;
		report.subEvent = opReport;	
		FTPClose(ftpSock);
		return report.event;
	}
	
	//	----- MD5 FILE DOWNLOAD (IF REQUIRED) AND COMPARISON WITH MEMORY MD5 -----
	if (md5file != NULL)
	{
		//	Downloading the md5 file
		BOOL fileComplete = FALSE;
		len = 0;		
		BYTE cnt = 0;
		//	The md5 file is downloaded up to 3 times, to avoid possible errors in connection
		while (len < 16)
		{
			FWDebug("MD5 file download...\n\n");
			if (FTPisConn(ftpSock))
				FWDebug("Connected\n");
			else
				FWDebug("Disconnected\n");
			opReport = FTPStreamOpen(ftpSock, md5file, RETR);
			if (opReport == FTP_CONNECTED)
			{	
				len = FTPStreamRead(buffer, 16, 3);
			}
			if (FTPStreamStat() == FTP_STREAM_EOF)
				fileComplete = TRUE;
			else
				fileComplete = FALSE;
			FTPStreamClose();

			cnt++;
			if (cnt >= 3)
				break;
		}
		
		//	Exception handling
		if (len < 16) 
		{
			if (fileComplete)
				report.event = ERR_MD5_WRONG_FORMAT;
			else
				report.event = ERR_MD5_FILE;
			if (opReport != FTP_CONNECTED)
				report.subEvent = opReport;
			else
				report.subEvent = FTP_UNKNOWN_ERROR;
			FTPClose(ftpSock);
			return report.event;
		}

		BYTE rep;
		char dbgmd5[50];
		FWDebug("\nMD5: ");
		for (rep = 0; rep < 16; rep++)
		{
			sprintf(dbgmd5, "%.2X ", (BYTE)buffer[rep]);
			FWDebug(dbgmd5);
		}
		FWDebug("\n");

		//	MD5 INTEGRITY CHECK ON MEMORY
		BYTE resmd[16];
		HASH_SUM Hash;
		FWDebug("\r\nCalculating md5 from memory...\r\n");
		MD5Initialize (&Hash);
		long unsigned int f_ind = 0;
		BYTE b_read[2];
		for (f_ind = 0; f_ind < 256512; f_ind++)
		{
			SPIFlashReadArray(0x1c0000+f_ind, b_read, 1);
			HashAddData (&Hash, b_read, 1);
		}
		MD5Calculate(&Hash, resmd);
		BYTE i;
		char rr[3];
		FWDebug("MD5:\r\n");
		for (i=0; i<16; i++)
		{
			sprintf(rr,"%X ",resmd[i]);
			FWDebug(rr);
			if (resmd[i] != (BYTE) buffer[i])
			{
				report.event = ERR_MD5_NO_MATCH;
				report.subEvent = FTP_UNKNOWN_ERROR;	
				FTPClose(ftpSock);
				return report.event;
			}
		}	
	}
	report.event = UPDATE_SUCCEDED;
	report.subEvent = 0;	
	FTPClose(ftpSock);
	return report.event;	
}

int FWUpdateEvent()
{
	return report.subEvent;
}

/**********************************************************************************************
 * 	BOOL FWNewEnable(): 
 *	The function enables the new firmware update erasing the internal PIC memory page starting
 *	at 0x29800. In this way the bootloader, at the next startup, loads the new firmware from the 
 *	externalflash memory.
 *	Bugfix for bootladers previous to v.1.2.2: before enabling the new firmware, the function performs a 
 *	check on the first six bytes of the flash (from location 0x1C0000, the starting point of the new 
 *	firmware). If this is uninitialized, the process is blocked, since it would corrupt the new 
 *	reset vector in bootloaders (fixed from v.1.2.2). This problem doesn't exist with following bootloaders, 
 *	but the check is still valid, since a firmware containing 0xFF in the first six bytes 
 *	(the low and the high words of the reset vector) is incorrect. 
 *						 
 * 	Parameters:
 *	-
 *
 * 	Returns:
 *	The report for the operation. TRUE if the operation was completed, FALSE if the flash memory wasn't
 *	written correctly
 *	
************************************************************************************************/
BOOL FWNewEnable()
{
	BOOL enable_low = FALSE, enable_high = FALSE;
	BYTE i,fread[1];
	//	Check on low word of reset vector
	for (i = 0; i < 3;i++)
	{
		SPIFlashReadArray(0x1C0000 + i, fread, 1);
		if (fread[0] != 0xFF)
			enable_low = TRUE;
	}
	//	Check on high word of reset vector
	for (i = 3; i < 6;i++)
	{
		SPIFlashReadArray(0x1C0000 + i, fread, 1);
		if (fread[0] != 0xFF)
			enable_high = TRUE;
	}
	if ( (enable_low == TRUE) && (enable_high == TRUE) )
	{
		_erase_flash(0x29800);
		return TRUE;
	}
	else
		return FALSE;
}



/********************************************************************
 * 	void FLASHErase(): 	
 * 	this function erases the flash memory part dedicated to the 
 * 	firmware. The rest of the flash memory is not modified.
 *						 
 * 	Parameters:
 *	-
 *	
 * 	Returns:
 *	-
********************************************************************/
void FLASHErase()
{				
	unsigned long int mem_ind;
	FWDebug("Erasing flash... ");
	for (mem_ind = 0x1C0000; mem_ind < 0x1FFFFF; mem_ind += 0x400)
		SPIFlashEraseSector(mem_ind);
	FWDebug("Done!\r\n");
}

static void FWDebug(char *dbgstr)
{
	#ifdef _DBG_FW
	UARTWrite(1, dbgstr);
	#endif
}
