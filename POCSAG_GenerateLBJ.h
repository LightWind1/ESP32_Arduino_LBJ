/*-----------------------------------------------------------------------
*@file     POCSAG_GenerateLBJ.h
*@brief    POCSAG寻呼码列车接近报警信息生成程序
*@author   谢英男(xieyingnan1994@163.com）
*@version  1.0
*@date     2020/07/27
-----------------------------------------------------------------------*/
#ifndef POCSAG_GENERATELBJ_H
#define POCSAG_GENERATELBJ_H

#include <Arduino.h>

extern uint32_t POCSAG_Batch1[];	//数组：储存码组1的数据
extern uint32_t POCSAG_Batch2[];	//数组：储存码组2的数据

//定义2个功能码表示上下行，按照标准TB/T3504-2018
#define FUNC_SHANGXING		0x3	//功能码11:上行
#define FUNC_XIAXING		0x1	//功能码01:下行
#define FUNC_TIMESYNC		0	//功能码00：时钟同步
//当码字个数<=16时（仅使用1个码组时），码组2的处置方法
#define BATCH2_TRUNCATE		0	//截断，只保留码组1
#define BATCH2_COPY_BATCH1 	1	//将码组1复制到码组2
#define BATCH2_LEAVE_IDLE 	2	//保持码组2为空闲码不变
//定义若干错误码
#define POCSAG_ERR_NONE					0
#define POCSAG_ERR_INVALID_ADDRESS		-1
#define POCSAG_ERR_INVALID_FUNCCODE		-2
#define POCSAG_ERR_INVALID_BATCH2OPT	-3

int8_t POCSAG_MakeCodeWordsLBJ(uint32_t Address, int8_t FuncCode, char* Text,
							   int8_t Batch2Opt, bool InvertOpt);

#ifdef POCSAG_DEBUG_MSG_ON
	    #define POCSAG_DEBUG_MSG(...) printf(__VA_ARGS__)
#else
		#define POCSAG_DEBUG_MSG(...)
#endif
#define POCSAG_ASSERT(STATEVAR) \
	    {if((STATEVAR) != POCSAG_ERR_NONE) { return(STATEVAR);}}

#endif
