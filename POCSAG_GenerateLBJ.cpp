/*-----------------------------------------------------------------------
*@file     POCSAG_GenerateLBJ.c
*@brief    POCSAG寻呼码列车接近报警信息生成程序
*@author   谢英男(xieyingnan1994@163.com）
*@version  1.1
*@date     2020/08/02
-----------------------------------------------------------------------*/

#include "POCSAG_GenerateLBJ.h"

uint32_t POCSAG_Batch1[16] = {0};	//码组1，包含16个码字
uint32_t POCSAG_Batch2[16] = {0};	//码组2，包含16个码字
//定义各个位数对应的掩码表
const uint8_t SizeToMask[7] = {0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f};
//用于大小端判断的共用体和宏
static union{uint8_t c[4];uint32_t l;}endian_test = {{ 'L', '?', '?', 'B' }};
#define ENDIAN_TEST() ((char)endian_test.l)//大端时为B,小端时为L

/*-----------------------------------------------------------------------
*@brief		检查输入参数的合法性
*@param		与上层函数POCSAG_MakeCodeWords()相同
*@retval	错误码，在头文件中定义，无错误时返回POCSAG_ERR_NONE(0)
-----------------------------------------------------------------------*/
static int8_t CheckParamSanity(uint32_t Address, int8_t FuncCode, int8_t Batch2Opt)
{
	if((Address > 0x1FFFFF)||(Address == 0))
		return(POCSAG_ERR_INVALID_ADDRESS);	//地址为21位，且不能为0
	if((FuncCode != FUNC_SHANGXING)&&(FuncCode != FUNC_XIAXING)&&
		(FuncCode != FUNC_TIMESYNC))
		return(POCSAG_ERR_INVALID_FUNCCODE); //如果不是这两个功能码
	if((Batch2Opt < BATCH2_TRUNCATE)||(Batch2Opt > BATCH2_LEAVE_IDLE))
		return(POCSAG_ERR_INVALID_BATCH2OPT);	//码组2选项为0-2
	return(POCSAG_ERR_NONE);//参数无错误时返回POCSAG_ERR_NONE(0)
}
/*-----------------------------------------------------------------------
*@brief		将已生成的码字填入码字数组POCSAG_Batch1/2
*@param		codeword_index - 码字在码组中的编号，0-15表示位于第一码组
*           POCSAG_Batch1[]，16-31表示位于第二码组POCSAG_Batch2[]
*           in_codeword - 要填入的码字
*@retval	无
-----------------------------------------------------------------------*/
void StuffCodeWordItem(uint8_t codeword_index, uint32_t in_codeword)
{
	if(codeword_index > 31)
		return;	//范围检查：0-31
	POCSAG_DEBUG_MSG("Stuffed POCSAG_Batch");
	if(codeword_index < 16)
		POCSAG_Batch1[codeword_index] = in_codeword;//0-15表示位于第一码组
	else
		POCSAG_Batch2[codeword_index - 16] = in_codeword;
													//16-31表示位于第二码组
	POCSAG_DEBUG_MSG("%hhu[%hhu] ",(codeword_index<16)?1:2,
					  codeword_index);
	POCSAG_DEBUG_MSG("with %08Xh (",in_codeword);
#ifdef POCSAG_DEBUG_MSG_ON
	for(uint8_t bit=1;bit<=32;bit++,in_codeword<<=1)
	{
		if(in_codeword & 0x80000000)
			POCSAG_DEBUG_MSG("1");
		else
			POCSAG_DEBUG_MSG("0");
		if((bit%8==0) && (bit!=32))
			POCSAG_DEBUG_MSG(" ");
		if(bit == 32)
			POCSAG_DEBUG_MSG("b)\r\n");
	}
#endif
}
/*-----------------------------------------------------------------------
*@brief		计算BCH(31,21),计算偶校验位
*@param		带待计算的码字（低11位已清空为0）
*@retval	计算结果，作为最终填入码字数组中的值
-----------------------------------------------------------------------*/
static uint32_t CreateBCHandParity(uint32_t in)
{
    uint32_t work_cw = in;	//计算用
    uint32_t local_cw = in;	//储存结果用
    uint8_t parity = 0;	//奇偶校验用计数变量

    //1.计算BCH(31,21)
    for (uint8_t bit = 1; bit <= 21; bit++, work_cw <<= 1)
    {
        if (work_cw & 0x80000000)
            work_cw ^= 0xED200000;
    }
    local_cw |= (work_cw >> 21);	//保存计算结果

    //2.计算奇偶校验
    work_cw = local_cw;
    for (int bit = 1; bit <= 32; bit++, work_cw <<= 1)
    {
        if (work_cw & 0x80000000)
            parity++;
    }
    if (parity % 2)	//偶校验：如果1-31位有奇数个1
        local_cw++;//则将末尾32位设为1，以保证整个码字中有偶数个1

    return (local_cw);	//返回结果
}
/*-----------------------------------------------------------------------
*@brief		翻转BCD位顺序
*@param		bcd - 要转换的BCD码
*@retval	转换结果
-----------------------------------------------------------------------*/
static uint8_t FlipBCDbitOrder(uint8_t bcd)
{
	uint8_t result = 0;

	for(uint8_t i = 4;i > 0;i--)//i=4->1,因此i-1=3->0
	{
		if(bcd & (1<<(i-1)))	//bit3->bit0遍历
			result |= 1<<3-(i-1);//bit0-bit3判断是否写1
	}
	return result;
}
/*-----------------------------------------------------------------------
*@brief		纯数字类型消息，将ASCII数字字符和部分字母符号转换为非压缩BCD码
*@param		ch - 待转换的字符
*@retval	转换结果，非压缩的BCD码
-----------------------------------------------------------------------*/
static uint8_t CharToBCD(uint8_t ch)
{
	uint8_t retval;

	if(ch >= '0' && ch <= '9')
		retval = ch - '0';
	else
	{
		switch(ch)
		{
			case '*': retval = 0x0A; break;
			case 'U': retval = 0x0B; break;
			case '\x20':retval = 0x0C; break;
			case '-': retval = 0x0D; break;
			case '(': retval = 0x0E; break;
			case ')': retval = 0x0F; break;
			default: retval = 0x0A; break;
		}
	}
	retval = FlipBCDbitOrder(retval);	//翻转位顺序
	return retval;
}
/*-----------------------------------------------------------------------
*@brief		生成包含列车预警信息的POCSAG码字，消息采用BCD方式传送
*@detail 	1.该子程序只生成码字（包含地址码字和数据码字）。不生成前导码和同步码。
*			2.生成的1组或2组码字保存在数组POCSAG_Batch1/2中。
*			3.未使用的码字按照POCSAG标准，用空闲码字填充。
*			4.输入字符串的长度：纯数字ASCII字符为40个
*@param		Address - 地址码，其中低3位表示地址码字开始位置在码组中所在的帧
*           FuncCode - 功能码，表明列车上下行类别
*           Text* - 指向文本的指针，最长40个字符
*           Batch2Opt - 当码字个数<=16时（仅使用1个码组时）码组2的处置方法
*           （截断、重复码组1内容、还是保留并填为空闲码字）          
*           InvertOpt - 数据位是否翻转
*@retval	<=0时为错误码。>0时为生成的码字数据占用的码组个数（1或2）
-----------------------------------------------------------------------*/
int8_t POCSAG_MakeCodeWordsLBJ(uint32_t Address, int8_t FuncCode, char* Text,
							   int8_t Batch2Opt, bool InvertOpt)
{
	uint8_t txt_len;	//文本长度
	uint8_t current_cw_index;	//指示当前码字，范围0-31
	//==========Part0:准备工作==========
	//=====Part0.0:检查参数合法性
	POCSAG_ASSERT(CheckParamSanity(Address,FuncCode,Batch2Opt));
	//=====Part0.1:计算文本长度
	if((txt_len = strlen(Text)) > 40)
		txt_len = 40;	//文本限制在最长40个字符
	//=====Part0.2:将两个码组内的所有码字都填为空闲码字0x7a89c197
	for(uint8_t i = 0;i < 16;i++)
	{
		POCSAG_Batch1[i] = 0x7A89C197;	//将码组1的16个码字填为空闲码字
		POCSAG_Batch2[i] = 0x7A89C197;	//将码组2的16个码字填为空闲码字
	}
	//==========Part1:生成地址码字==========
	//关于地址码字，其格式如下所示：
	//0aaaaaaa aaaaaaaa aaaffccc cccccccp
	//"0" = 固定，表示当前码字包含地址信息
	//a[18] = 地址的高18位
	//f[2] = 两位功能码，表示功能0-3，也表示4个不同的“地址空间”
	//c[10] = 10位BCH(31,21)校验码
	//p = 偶校验位，前面31位中的1位偶数时该位为0，为奇数时该位为1
	//注意：地址的低三位并未在码字中编码。这三位用来表示本地址码字在码组中的
	//的起始位置（帧编号，范围0-7）
	uint32_t address_cw = 0;	//地址码字
	current_cw_index = (Address&0x7)<<1;//由原始地址低3位算出起始码字位置
	address_cw = Address >> 3;	//地址码字为原始地址的高18位
	address_cw <<= 2;	//为功能码空出位置
	address_cw |= FuncCode&0x03;	//在地址码字的后面附加2位功能码
	StuffCodeWordItem(current_cw_index,CreateBCHandParity(address_cw<<11));
							//计算BCH和校验位，填入码字数组
	//==========Part2:转换POCSAG格式编码==========
	//关于消息码字，其格式如下所示：
	//1ttttttt tttttttt tttttccc cccccccp
	//"1" = 固定值，表示当前码字包含已编码的信息
	//t[20] = 20位ASCII文本/BCD格式的数字/汉字区位码
	//c[10] = 10位BCH(31,21)校验码
	//p = 偶校验位，前面31位中的1位偶数时该位为0，为奇数时该位为1
	//=====Part2.1:纯数字类型，将数字字符转换为BCD码，每个码字存储5个BCD码
	if((FuncCode == FUNC_SHANGXING)||(FuncCode == FUNC_XIAXING)||
	   (FuncCode == FUNC_TIMESYNC))
	{
		uint8_t group_cnt = txt_len / 5;//5个数字一组，求出分组数
		uint8_t residual_cnt = txt_len % 5;//剩下落单的数字数
		uint32_t message_bcd_cw;	//保存纯数字BCD码的消息码字
		for(uint8_t group = 0;group < group_cnt;group++)//先解决成组的数字
		{
			message_bcd_cw = 0;//每5个数字为一组，生成5个BCD占用一个码字
			for(uint8_t i = 0;i < 5;i++)
			{
				message_bcd_cw <<= 4;//左移4位让出位置
				message_bcd_cw |= CharToBCD(Text[group*5+i])&0x0F;
					//数字字符转换为BCD码，按位追加到码字后面，以便左对齐
			}
			message_bcd_cw <<= 11;//填入5个BCD码后，给BCH和偶校验位让出位置
			message_bcd_cw |= 0x80000000;//最高位置1表示此码字为消息码字
			StuffCodeWordItem(++current_cw_index,
							  CreateBCHandParity(message_bcd_cw));	
		}	//生成BCH和偶校验位后将码字填入码组数组中，总码字计数+1
		if(residual_cnt !=0)//如果有落单的数字，再解决落单的数字
		{
			message_bcd_cw = 0;//这些数字的BCD靠左对齐填入码字，后部空位用0补齐
			for(uint8_t i = 0;i < residual_cnt;i++)
			{
				message_bcd_cw <<= 4;
				message_bcd_cw |= CharToBCD(Text[group_cnt*5+i])&0x0F;
			}
			message_bcd_cw <<= (5 - residual_cnt)*4;//将后部空位用0补齐
			message_bcd_cw <<= 11;//填入5个BCD码后，给BCH和偶校验位让出位置
			message_bcd_cw |= 0x80000000;//最高位置1表示此码字为消息码字
			StuffCodeWordItem(++current_cw_index,
							  CreateBCHandParity(message_bcd_cw));	
		}	//生成BCH和偶校验位后将码字填入码组数组中，总码字计数+1
	}
	//==========Part3:后处理==========
	//=====Part3.1:根据选项，决定是否对码组数据进行相位反向
	if(InvertOpt)//如果需要反向，则将每个码字与0xFFFFFFFF异或进行反向
	{
		for(uint8_t i = 0;i < 16;i++)
		{
			POCSAG_Batch1[i] ^= 0xFFFFFFFF;
			POCSAG_Batch2[i] ^= 0xFFFFFFFF;
		}
		POCSAG_DEBUG_MSG("Bit phase is inverted!\r\n");
	}
	//=====Part3.2:POCSAG_Batch1/2的数组元素仅为容器，需转为大端格式
	if(ENDIAN_TEST() == 'L')//如果本机是小端存储结构
	{
		uint32_t temp1;
		union{uint32_t i; uint8_t bytes[4];}temp2;
		for(uint8_t i = 0;i < 16;i++)
		{
			temp1 = POCSAG_Batch1[i];//从数组抽取
			temp2.bytes[0] = (temp1>>24)&0xFF;//颠倒大小端
			temp2.bytes[1] = (temp1>>16)&0xFF;
			temp2.bytes[2] = (temp1>>8)&0xFF;
			temp2.bytes[3] = temp1&0xFF;
			POCSAG_Batch1[i] = temp2.i;//存回数组

			temp1 = POCSAG_Batch2[i];//从数组抽取
			temp2.bytes[0] = (temp1>>24)&0xFF;//颠倒大小端
			temp2.bytes[1] = (temp1>>16)&0xFF;
			temp2.bytes[2] = (temp1>>8)&0xFF;
			temp2.bytes[3] = temp1&0xFF;
			POCSAG_Batch2[i] = temp2.i;//存回数组
		}
		POCSAG_DEBUG_MSG("Little endian is changed to "
						 "big endian!\r\n");
	}
	//=====Part3.3:恢复结尾字符，返回码组个数，程序执行结束
	int8_t retval;
	if(current_cw_index < 16)	//如果码字占用小于一个码组
	{
		if(Batch2Opt == BATCH2_TRUNCATE)
			retval = 1;	//返回“占用一个码组”，程序结束
		else if(Batch2Opt == BATCH2_COPY_BATCH1)
		{
			memcpy(POCSAG_Batch2,POCSAG_Batch1,
					16);//将码组1拷贝到码组2
			retval = 2;	//返回“占用两个码组”，程序结束
		}
	}
	else
	{
		retval = 2;	//返回“占用两个码组”，程序结束
	}
	return retval;
}
