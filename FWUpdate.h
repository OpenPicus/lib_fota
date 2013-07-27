#ifndef _FWUPDATE_H_
#define _FWUPDATE_H_

#define	UPDATE_SUCCEDED				0
#define ERR_NOT_CONNECTED			1
#define	ERR_CONNECTING_SERVER		2
#define ERR_FW_FILE					3
#define ERR_MD5_WRONG_FORMAT		4
#define ERR_MD5_FILE				5
#define ERR_MD5_NO_MATCH			6
#define ERR_TRANSFER_INTERRPUTED 	7
#define ERR_CONFIG_SERVER			8

#define STREAM_CHUNK_DIM			536
#define _DBG_FW
typedef struct
{
	BYTE event;
	int subEvent;
}updtRep;

BOOL FWNewEnable();
BYTE FWUpdateFTP(char *ServerIP, char *ServerPort, char *user, char *pwd, char *binfile, char *md5file);
void FWUpdateInit();
void FLASHErase();
int FWUpdateEvent();
#endif
